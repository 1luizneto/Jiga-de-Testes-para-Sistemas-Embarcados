/***********************************************************************************************************************
 *   @file         I2cBus.c
 *   @addtogroup   App
 *   @brief        Barramento I2C da jiga: escravo com mapa de 256 registradores e transferencias como mestre
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Ferramentas:</b>
 *   - Generic.
 *
 *   \n <b>Dependencias:</b>
 *   - Funcoes de configuracao, desligamento, transferencia e lock fornecidas pela BSP (I2cBus_Init).
 *
 *   \n <b>Observacoes:</b>
 *   - Nao conhece o HAL: os testes Unity registram um barramento falso no I2cBus_Init.
 *   - Escravo no padrao de uma EEPROM 24C02: na escrita do DUT, o 1o byte e o ponteiro de registrador e os
 *     seguintes sao gravados com auto-incremento; a leitura sai a partir do ponteiro. Depois de 0xFF, volta a 0x00.
 *   - Os eventos do barramento rodam na ISR; o Load/Dump (task) copia com a IRQ de evento travada pela BSP.
 *   - Load/Dump funcionam sem I2C_CONFIG: o PC carrega o mapa antes de ligar o escravo.
 *
 *   Changelog
 *   @version      <b>1.0.0 - 27/10/2026</b> \n Luiz Neto \n Primeira versao
 *
 *   @copyright
 *   @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include "I2cBus.h"
#include <stddef.h>
#include <string.h>

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

// Barramento desconfigurado (boot, RESET_ALL ou falha ao configurar)
#define dI2CBUS_PAPEL_NENHUM (0xFFU)

/***********************************************************************************************************************
 * TIPOS LOCAIS
 **********************************************************************************************************************/

static struct i2cBus
{
	i2cBusFunc_t func;

	uint8_t mapa[dI2CBUS_TAMANHO_MAPA];
	uint8_t ponteiro;         ///< Proximo registrador lido ou escrito pelo DUT (uint8_t: volta a 0 sozinho)
	uint8_t esperaPonteiro;   ///< 1: o proximo byte escrito pelo DUT e o ponteiro
	uint8_t papel;            ///< dI2CBUS_PAPEL_ESCRAVO, dI2CBUS_PAPEL_MESTRE ou dI2CBUS_PAPEL_NENHUM
} i2cBus;

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Registra as funcoes da BSP e comeca desconfigurado, com o mapa zerado.
 * @details    Nao mexe no hardware: quem leva o barramento ao estado seguro e a BSP (I2cBus_SafeState).
 * @param[in]  func  Funcoes da BSP (todas obrigatorias)
 **********************************************************************************************************************/
i2cBusReturn_t I2cBus_Init(const i2cBusFunc_t *func)
{
	if ((func == NULL) || (func->config == NULL) || (func->off == NULL) || (func->xfer == NULL) ||
	    (func->lock == NULL) || (func->unlock == NULL))
	{
		return eI2CBUS_RETURN_INVALID_ARGUMENT;
	}

	i2cBus.func = *func;
	memset(i2cBus.mapa, 0, sizeof(i2cBus.mapa));
	i2cBus.ponteiro       = 0U;
	i2cBus.esperaPonteiro = 0U;
	i2cBus.papel          = dI2CBUS_PAPEL_NENHUM;

	return eI2CBUS_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Configura o papel da jiga no barramento.
 * @details    Durante a troca e em falha da BSP o barramento fica desconfigurado. O mapa nao e alterado.
 * @param[in]  papel       dI2CBUS_PAPEL_ESCRAVO ou dI2CBUS_PAPEL_MESTRE
 * @param[in]  endereco    Endereco de 7 bits da jiga como escrava (ignorado como mestre)
 * @param[in]  velocidade  0 = 100 kHz, 1 = 400 kHz
 * @return     eI2CBUS_RETURN_OK, eI2CBUS_RETURN_INVALID_ARGUMENT ou eI2CBUS_RETURN_ERROR (falha da BSP)
 **********************************************************************************************************************/
i2cBusReturn_t I2cBus_Config(uint8_t papel, uint8_t endereco, uint8_t velocidade)
{
	if (i2cBus.func.config == NULL)
	{
		return eI2CBUS_RETURN_ERROR;
	}

	if ((papel > dI2CBUS_PAPEL_MESTRE) || (velocidade > dI2CBUS_VELOCIDADE_MAX))
	{
		return eI2CBUS_RETURN_INVALID_ARGUMENT;
	}

	if ((papel == dI2CBUS_PAPEL_ESCRAVO) && ((endereco < dI2CBUS_ENDERECO_MIN) || (endereco > dI2CBUS_ENDERECO_MAX)))
	{
		return eI2CBUS_RETURN_INVALID_ARGUMENT;
	}

	i2cBus.papel          = dI2CBUS_PAPEL_NENHUM;
	i2cBus.esperaPonteiro = 0U;

	if (i2cBus.func.config(papel, endereco, velocidade) != eI2CBUS_RETURN_OK)
	{
		return eI2CBUS_RETURN_ERROR;
	}

	i2cBus.papel = papel;

	return eI2CBUS_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Carrega bytes no mapa de registradores (I2C_SLAVE_LOAD).
 * @param[in]  offset      Primeiro registrador
 * @param[in]  quantidade  1 a 256 - offset
 * @param[in]  dados       Bytes a gravar
 **********************************************************************************************************************/
i2cBusReturn_t I2cBus_Load(uint8_t offset, uint8_t quantidade, const uint8_t *dados)
{
	if (i2cBus.func.lock == NULL)
	{
		return eI2CBUS_RETURN_ERROR;
	}

	if ((dados == NULL) || (quantidade == 0U) || (((uint16_t)offset + quantidade) > dI2CBUS_TAMANHO_MAPA))
	{
		return eI2CBUS_RETURN_INVALID_ARGUMENT;
	}

	i2cBus.func.lock();
	memcpy(&i2cBus.mapa[offset], dados, quantidade);
	i2cBus.func.unlock();

	return eI2CBUS_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Le bytes do mapa de registradores (I2C_SLAVE_DUMP): o que o PC carregou ou o DUT escreveu.
 * @param[in]  offset      Primeiro registrador
 * @param[in]  quantidade  1 a 256 - offset
 * @param[out] dados       Bytes lidos
 **********************************************************************************************************************/
i2cBusReturn_t I2cBus_Dump(uint8_t offset, uint8_t quantidade, uint8_t *dados)
{
	if (i2cBus.func.lock == NULL)
	{
		return eI2CBUS_RETURN_ERROR;
	}

	if ((dados == NULL) || (quantidade == 0U) || (((uint16_t)offset + quantidade) > dI2CBUS_TAMANHO_MAPA))
	{
		return eI2CBUS_RETURN_INVALID_ARGUMENT;
	}

	i2cBus.func.lock();
	memcpy(dados, &i2cBus.mapa[offset], quantidade);
	i2cBus.func.unlock();

	return eI2CBUS_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Transferencia como mestre (I2C_MASTER_XFER): escreve nw bytes e depois le nr bytes.
 * @param[in]  endereco  Endereco de 7 bits do dispositivo
 * @param[in]  escrita   Bytes a escrever (pode ser NULL se nw = 0)
 * @param[in]  nw        Quantidade a escrever
 * @param[out] leitura   Bytes lidos (pode ser NULL se nr = 0)
 * @param[in]  nr        Quantidade a ler (nw + nr >= 1)
 * @return     eI2CBUS_RETURN_OK, _INVALID_ARGUMENT, _NOT_CONFIGURED (sem papel de mestre) ou o retorno da BSP
 **********************************************************************************************************************/
i2cBusReturn_t I2cBus_MasterXfer(uint8_t endereco, const uint8_t *escrita, uint8_t nw, uint8_t *leitura, uint8_t nr)
{
	if (i2cBus.func.xfer == NULL)
	{
		return eI2CBUS_RETURN_ERROR;
	}

	if ((endereco < dI2CBUS_ENDERECO_MIN) || (endereco > dI2CBUS_ENDERECO_MAX) || ((nw == 0U) && (nr == 0U)) ||
	    ((nw > 0U) && (escrita == NULL)) || ((nr > 0U) && (leitura == NULL)))
	{
		return eI2CBUS_RETURN_INVALID_ARGUMENT;
	}

	if (i2cBus.papel != dI2CBUS_PAPEL_MESTRE)
	{
		return eI2CBUS_RETURN_NOT_CONFIGURED;
	}

	return i2cBus.func.xfer(endereco, escrita, nw, leitura, nr);
}

/***********************************************************************************************************************
 * @brief      Estado seguro: barramento desligado e desconfigurado, mapa e ponteiro zerados.
 * @details    Zera o mapa mesmo se a BSP falhar ao desligar, para o proximo teste nao herdar dados.
 **********************************************************************************************************************/
i2cBusReturn_t I2cBus_SafeState(void)
{
	if (i2cBus.func.off == NULL)
	{
		return eI2CBUS_RETURN_ERROR;
	}

	i2cBusReturn_t ret = i2cBus.func.off();

	i2cBus.func.lock();
	memset(i2cBus.mapa, 0, sizeof(i2cBus.mapa));
	i2cBus.ponteiro       = 0U;
	i2cBus.esperaPonteiro = 0U;
	i2cBus.papel          = dI2CBUS_PAPEL_NENHUM;
	i2cBus.func.unlock();

	return (ret == eI2CBUS_RETURN_OK) ? eI2CBUS_RETURN_OK : eI2CBUS_RETURN_ERROR;
}

/***********************************************************************************************************************
 * @brief      O DUT enderecou a jiga para escrita: o proximo byte que ele mandar e o ponteiro de registrador.
 **********************************************************************************************************************/
void I2cBus_InicioEscrita(void)
{
	i2cBus.esperaPonteiro = 1U;
}

/***********************************************************************************************************************
 * @brief      Byte escrito pelo DUT: o 1o da escrita e o ponteiro; os demais vao para o mapa, com auto-incremento.
 * @param[in]  byte  Byte recebido
 **********************************************************************************************************************/
void I2cBus_ByteRecebido(uint8_t byte)
{
	if (i2cBus.esperaPonteiro != 0U)
	{
		i2cBus.ponteiro       = byte;
		i2cBus.esperaPonteiro = 0U;
	}
	else
	{
		i2cBus.mapa[i2cBus.ponteiro] = byte;
		i2cBus.ponteiro++;
	}
}

/***********************************************************************************************************************
 * @brief      Byte que o DUT vai ler: o registrador apontado, com auto-incremento.
 * @return     mapa[ponteiro]
 **********************************************************************************************************************/
uint8_t I2cBus_ProximoByte(void)
{
	uint8_t byte = i2cBus.mapa[i2cBus.ponteiro];

	i2cBus.ponteiro++;

	return byte;
}

/***********************************************************************************************************************
 * @brief      Fim de uma leitura do DUT: devolve o byte que o periferico buscou a mais e o DUT nao leu.
 * @details    O I2C do F7 recarrega o TXDR assim que o byte anterior comeca a sair; quando o DUT responde NACK no
 *             ultimo byte, o byte recarregado e descartado. Sem isso, a leitura seguinte pularia um registrador.
 **********************************************************************************************************************/
void I2cBus_FimLeitura(void)
{
	i2cBus.ponteiro--;
}

/** @} DOXYGEN GROUP TAG END OF FILE */
