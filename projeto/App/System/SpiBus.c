/***********************************************************************************************************************
 *   @file         SpiBus.c
 *   @addtogroup   App
 *   @brief        Barramento SPI da jiga: escravo com buffers de transmissao e recepcao e transferencias como mestre
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Ferramentas:</b>
 *   - Generic.
 *
 *   \n <b>Dependencias:</b>
 *   - Clock do SPI e funcoes de configuracao, desligamento, transferencia e lock fornecidos pela BSP (SpiBus_Init).
 *
 *   \n <b>Observacoes:</b>
 *   - Nao conhece o HAL: os testes Unity registram um barramento falso no SpiBus_Init.
 *   - Escravo: cada transacao do DUT (CS em nivel baixo) recebe o buffer de transmissao a partir do byte 0 e,
 *     depois do fim dele, dSPIBUS_BYTE_VAZIO. O que o DUT envia se acumula ate o SPI_SLAVE_READ, que devolve e
 *     esvazia; o excedente e descartado.
 *   - A BSP mantem o escravo armado com uma copia do buffer de transmissao (SpiBus_PreparaTx): um SPI_SLAVE_LOAD
 *     vale a partir da transacao seguinte.
 *   - Load/Read funcionam sem SPI_CONFIG: o PC carrega o buffer antes de ligar o escravo.
 *
 *   Changelog
 *   @version      <b>1.0.0 - 28/10/2026</b> \n Luiz Neto \n Primeira versao
 *
 *   @copyright
 *   @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include "SpiBus.h"
#include <stddef.h>
#include <string.h>

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

// Barramento desconfigurado (boot, RESET_ALL ou falha ao configurar)
#define dSPIBUS_PAPEL_NENHUM (0xFFU)

/***********************************************************************************************************************
 * TIPOS LOCAIS
 **********************************************************************************************************************/

static struct spiBus
{
	spiBusFunc_t func;
	uint32_t     clockHz;

	uint8_t tx[dSPIBUS_TAMANHO_BUFFER];   ///< O que o escravo envia em cada transacao
	uint8_t txTamanho;
	uint8_t rx[dSPIBUS_TAMANHO_BUFFER];   ///< O que o DUT enviou desde o ultimo SPI_SLAVE_READ
	uint8_t rxTamanho;
	uint8_t papel;                        ///< dSPIBUS_PAPEL_ESCRAVO, dSPIBUS_PAPEL_MESTRE ou dSPIBUS_PAPEL_NENHUM
} spiBus;

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static void SpiBus_LimpaBuffers(void);

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Registra as funcoes da BSP e o clock do SPI e comeca desconfigurado, com os buffers vazios.
 * @details    Nao mexe no hardware: quem leva o barramento ao estado seguro e a BSP (SpiBus_SafeState).
 * @param[in]  func     Funcoes da BSP (todas obrigatorias)
 * @param[in]  clockHz  Clock na entrada do SPI (o divisor do mestre e calculado sobre ele)
 **********************************************************************************************************************/
spiBusReturn_t SpiBus_Init(const spiBusFunc_t *func, uint32_t clockHz)
{
	if ((func == NULL) || (func->config == NULL) || (func->off == NULL) || (func->xfer == NULL) ||
	    (func->lock == NULL) || (func->unlock == NULL) || (clockHz == 0U))
	{
		return eSPIBUS_RETURN_INVALID_ARGUMENT;
	}

	spiBus.func    = *func;
	spiBus.clockHz = clockHz;
	spiBus.papel   = dSPIBUS_PAPEL_NENHUM;
	SpiBus_LimpaBuffers();

	return eSPIBUS_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Menor divisor (potencia de 2, de 2 a 256) cuja frequencia nao passa da pedida.
 * @details    O mestre nunca fica mais rapido que o pedido: 1 MHz com 54 MHz de clock -> 64 (843,75 kHz).
 * @param[in]  clockHz     Clock do SPI
 * @param[in]  frequencia  Frequencia pedida, em Hz
 * @param[out] divisor     Divisor escolhido
 * @return     eSPIBUS_RETURN_OK ou eSPIBUS_RETURN_INVALID_ARGUMENT (abaixo de clockHz / 256)
 **********************************************************************************************************************/
spiBusReturn_t SpiBus_Divisor(uint32_t clockHz, uint32_t frequencia, uint16_t *divisor)
{
	if ((divisor == NULL) || (clockHz == 0U) || (frequencia == 0U))
	{
		return eSPIBUS_RETURN_INVALID_ARGUMENT;
	}

	for (uint32_t candidato = dSPIBUS_DIVISOR_MIN; candidato <= dSPIBUS_DIVISOR_MAX; candidato *= 2U)
	{
		// clock / candidato <= frequencia, sem divisao (nao perde a parte fracionaria)
		if (clockHz <= ((uint64_t)frequencia * candidato))
		{
			*divisor = (uint16_t)candidato;
			return eSPIBUS_RETURN_OK;
		}
	}

	return eSPIBUS_RETURN_INVALID_ARGUMENT;
}

/***********************************************************************************************************************
 * @brief      Configura o papel da jiga no barramento (SPI_CONFIG).
 * @details    Durante a troca e em falha da BSP o barramento fica desconfigurado. Os buffers nao sao alterados.
 * @param[in]  papel       dSPIBUS_PAPEL_ESCRAVO ou dSPIBUS_PAPEL_MESTRE
 * @param[in]  modo        0 a 3 (CPOL e CPHA)
 * @param[in]  frequencia  Em Hz, so como mestre (ignorada como escravo: quem gera o clock e o DUT)
 * @return     eSPIBUS_RETURN_OK, eSPIBUS_RETURN_INVALID_ARGUMENT ou eSPIBUS_RETURN_ERROR (falha da BSP)
 **********************************************************************************************************************/
spiBusReturn_t SpiBus_Config(uint8_t papel, uint8_t modo, uint32_t frequencia)
{
	uint16_t divisor = 0U;

	if (spiBus.func.config == NULL)
	{
		return eSPIBUS_RETURN_ERROR;
	}

	if ((papel > dSPIBUS_PAPEL_MESTRE) || (modo > dSPIBUS_MODO_MAX))
	{
		return eSPIBUS_RETURN_INVALID_ARGUMENT;
	}

	if ((papel == dSPIBUS_PAPEL_MESTRE) && (SpiBus_Divisor(spiBus.clockHz, frequencia, &divisor) != eSPIBUS_RETURN_OK))
	{
		return eSPIBUS_RETURN_INVALID_ARGUMENT;
	}

	spiBus.papel = dSPIBUS_PAPEL_NENHUM;

	if (spiBus.func.config(papel, modo, divisor) != eSPIBUS_RETURN_OK)
	{
		return eSPIBUS_RETURN_ERROR;
	}

	spiBus.papel = papel;

	return eSPIBUS_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Carrega o buffer de transmissao do escravo (SPI_SLAVE_LOAD). Vale a partir da proxima transacao.
 * @param[in]  dados       Bytes a enviar (pode ser NULL se quantidade = 0)
 * @param[in]  quantidade  0 a dSPIBUS_TAMANHO_BUFFER (0: o escravo envia so dSPIBUS_BYTE_VAZIO)
 **********************************************************************************************************************/
spiBusReturn_t SpiBus_Load(const uint8_t *dados, uint8_t quantidade)
{
	if (spiBus.func.lock == NULL)
	{
		return eSPIBUS_RETURN_ERROR;
	}

	if ((quantidade > dSPIBUS_TAMANHO_BUFFER) || ((quantidade > 0U) && (dados == NULL)))
	{
		return eSPIBUS_RETURN_INVALID_ARGUMENT;
	}

	spiBus.func.lock();
	if (quantidade > 0U)
	{
		memcpy(spiBus.tx, dados, quantidade);
	}
	spiBus.txTamanho = quantidade;
	spiBus.func.unlock();

	return eSPIBUS_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Devolve o que o DUT enviou desde a ultima leitura e esvazia o buffer (SPI_SLAVE_READ).
 * @param[out] dados       Ate dSPIBUS_TAMANHO_BUFFER bytes
 * @param[out] quantidade  Bytes devolvidos (0 se o DUT nao enviou nada)
 **********************************************************************************************************************/
spiBusReturn_t SpiBus_Read(uint8_t *dados, uint8_t *quantidade)
{
	if (spiBus.func.lock == NULL)
	{
		return eSPIBUS_RETURN_ERROR;
	}

	if ((dados == NULL) || (quantidade == NULL))
	{
		return eSPIBUS_RETURN_INVALID_ARGUMENT;
	}

	spiBus.func.lock();
	memcpy(dados, spiBus.rx, spiBus.rxTamanho);
	*quantidade      = spiBus.rxTamanho;
	spiBus.rxTamanho = 0U;
	spiBus.func.unlock();

	return eSPIBUS_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Transferencia como mestre (SPI_MASTER_XFER): envia e recebe a mesma quantidade, com o CS baixo.
 * @param[in]  escrita     Bytes a enviar
 * @param[out] leitura     Bytes recebidos
 * @param[in]  quantidade  1 a dSPIBUS_TAMANHO_BUFFER
 * @return     eSPIBUS_RETURN_OK, _INVALID_ARGUMENT, _NOT_CONFIGURED (sem papel de mestre) ou o retorno da BSP
 **********************************************************************************************************************/
spiBusReturn_t SpiBus_MasterXfer(const uint8_t *escrita, uint8_t *leitura, uint8_t quantidade)
{
	if (spiBus.func.xfer == NULL)
	{
		return eSPIBUS_RETURN_ERROR;
	}

	if ((escrita == NULL) || (leitura == NULL) || (quantidade == 0U) || (quantidade > dSPIBUS_TAMANHO_BUFFER))
	{
		return eSPIBUS_RETURN_INVALID_ARGUMENT;
	}

	if (spiBus.papel != dSPIBUS_PAPEL_MESTRE)
	{
		return eSPIBUS_RETURN_NOT_CONFIGURED;
	}

	return spiBus.func.xfer(escrita, leitura, quantidade);
}

/***********************************************************************************************************************
 * @brief      Estado seguro: barramento desligado e desconfigurado, buffers vazios.
 * @details    Esvazia os buffers mesmo se a BSP falhar ao desligar, para o proximo teste nao herdar dados.
 **********************************************************************************************************************/
spiBusReturn_t SpiBus_SafeState(void)
{
	if (spiBus.func.off == NULL)
	{
		return eSPIBUS_RETURN_ERROR;
	}

	spiBusReturn_t ret = spiBus.func.off();

	spiBus.func.lock();
	SpiBus_LimpaBuffers();
	spiBus.papel = dSPIBUS_PAPEL_NENHUM;
	spiBus.func.unlock();

	return (ret == eSPIBUS_RETURN_OK) ? eSPIBUS_RETURN_OK : eSPIBUS_RETURN_ERROR;
}

/***********************************************************************************************************************
 * @brief      Preenche o que o escravo vai enviar na proxima transacao: o buffer de transmissao e, depois,
 *             dSPIBUS_BYTE_VAZIO ate completar dSPIBUS_TAMANHO_BUFFER bytes.
 * @param[out] tx  Buffer de transmissao da BSP
 **********************************************************************************************************************/
void SpiBus_PreparaTx(uint8_t tx[dSPIBUS_TAMANHO_BUFFER])
{
	memcpy(tx, spiBus.tx, spiBus.txTamanho);
	memset(&tx[spiBus.txTamanho], dSPIBUS_BYTE_VAZIO, dSPIBUS_TAMANHO_BUFFER - spiBus.txTamanho);
}

/***********************************************************************************************************************
 * @brief      Fim de uma transacao do DUT (subida do CS): acumula o que ele enviou, descartando o excedente.
 * @param[in]  rx          Bytes recebidos na transacao
 * @param[in]  quantidade  Quantidade recebida
 **********************************************************************************************************************/
void SpiBus_FimTransacao(const uint8_t *rx, uint16_t quantidade)
{
	uint16_t livre = (uint16_t)(dSPIBUS_TAMANHO_BUFFER - spiBus.rxTamanho);
	uint16_t copia = (quantidade < livre) ? quantidade : livre;

	memcpy(&spiBus.rx[spiBus.rxTamanho], rx, copia);
	spiBus.rxTamanho = (uint8_t)(spiBus.rxTamanho + copia);
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Esvazia os buffers de transmissao e recepcao.
 **********************************************************************************************************************/
static void SpiBus_LimpaBuffers(void)
{
	memset(spiBus.tx, 0, sizeof(spiBus.tx));
	memset(spiBus.rx, 0, sizeof(spiBus.rx));
	spiBus.txTamanho = 0U;
	spiBus.rxTamanho = 0U;
}

/** @} DOXYGEN GROUP TAG END OF FILE */
