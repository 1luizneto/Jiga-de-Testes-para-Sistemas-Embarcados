/***********************************************************************************************************************
 *   @file         UartBus.c
 *   @addtogroup   App
 *   @brief        UART da jiga para o DUT: configuracao do baud, escrita e recepcao continua em buffer circular
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Ferramentas:</b>
 *   - Generic.
 *
 *   \n <b>Dependencias:</b>
 *   - Funcoes de configuracao, desligamento, escrita e lock fornecidas pela BSP (UartBus_Init).
 *
 *   \n <b>Observacoes:</b>
 *   - Nao conhece o HAL: os testes Unity registram uma UART falsa no UartBus_Init.
 *   - Formato fixo 8N1 (o Apendice A so define o baud).
 *   - A recepcao roda na ISR: cada byte vai para um buffer circular de dUARTBUS_TAMANHO_RX bytes; com o buffer
 *     cheio, os que chegam sao descartados. O UART_READ tira do buffer os mais antigos primeiro.
 *   - Escrita e leitura exigem UART_CONFIG (NAO_CONFIGURADO); o UART_CONFIG esvazia o buffer.
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

#include "UartBus.h"
#include <stddef.h>

/***********************************************************************************************************************
 * TIPOS LOCAIS
 **********************************************************************************************************************/

static struct uartBus
{
	uartBusFunc_t func;

	uint8_t  rx[dUARTBUS_TAMANHO_RX];
	uint8_t  inicio;        ///< Byte mais antigo (uint8_t: volta a 0 sozinho)
	uint16_t quantidade;    ///< Bytes no buffer (0 a dUARTBUS_TAMANHO_RX)
	uint8_t  configurada;   ///< 1: UART_CONFIG aplicado
} uartBus;

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Registra as funcoes da BSP e comeca desconfigurada, com o buffer vazio.
 * @details    Nao mexe no hardware: quem leva a UART ao estado seguro e a BSP (UartBus_SafeState).
 * @param[in]  func  Funcoes da BSP (todas obrigatorias)
 **********************************************************************************************************************/
uartBusReturn_t UartBus_Init(const uartBusFunc_t *func)
{
	if ((func == NULL) || (func->config == NULL) || (func->off == NULL) || (func->write == NULL) ||
	    (func->lock == NULL) || (func->unlock == NULL))
	{
		return eUARTBUS_RETURN_INVALID_ARGUMENT;
	}

	uartBus.func        = *func;
	uartBus.inicio      = 0U;
	uartBus.quantidade  = 0U;
	uartBus.configurada = 0U;

	return eUARTBUS_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Configura o baud (UART_CONFIG) e esvazia o buffer de recepcao.
 * @details    Durante a troca e em falha da BSP a UART fica desconfigurada.
 * @param[in]  baud  dUARTBUS_BAUD_MIN a dUARTBUS_BAUD_MAX
 * @return     eUARTBUS_RETURN_OK, eUARTBUS_RETURN_INVALID_ARGUMENT ou eUARTBUS_RETURN_ERROR (falha da BSP)
 **********************************************************************************************************************/
uartBusReturn_t UartBus_Config(uint32_t baud)
{
	if (uartBus.func.config == NULL)
	{
		return eUARTBUS_RETURN_ERROR;
	}

	if ((baud < dUARTBUS_BAUD_MIN) || (baud > dUARTBUS_BAUD_MAX))
	{
		return eUARTBUS_RETURN_INVALID_ARGUMENT;
	}

	uartBus.configurada = 0U;

	if (uartBus.func.config(baud) != eUARTBUS_RETURN_OK)
	{
		return eUARTBUS_RETURN_ERROR;
	}

	uartBus.func.lock();
	uartBus.inicio     = 0U;
	uartBus.quantidade = 0U;
	uartBus.func.unlock();

	uartBus.configurada = 1U;

	return eUARTBUS_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Envia bytes ao DUT (UART_WRITE).
 * @param[in]  dados       Bytes a enviar
 * @param[in]  quantidade  1 a dUARTBUS_TAMANHO_MAX
 * @return     eUARTBUS_RETURN_OK, _INVALID_ARGUMENT, _NOT_CONFIGURED ou o retorno da BSP
 **********************************************************************************************************************/
uartBusReturn_t UartBus_Write(const uint8_t *dados, uint8_t quantidade)
{
	if (uartBus.func.write == NULL)
	{
		return eUARTBUS_RETURN_ERROR;
	}

	if ((dados == NULL) || (quantidade == 0U) || (quantidade > dUARTBUS_TAMANHO_MAX))
	{
		return eUARTBUS_RETURN_INVALID_ARGUMENT;
	}

	if (uartBus.configurada == 0U)
	{
		return eUARTBUS_RETURN_NOT_CONFIGURED;
	}

	return uartBus.func.write(dados, quantidade);
}

/***********************************************************************************************************************
 * @brief      Tira do buffer os bytes recebidos do DUT, os mais antigos primeiro (UART_READ).
 * @param[in]  maximo      Quantidade maxima pedida (acima de dUARTBUS_TAMANHO_MAX, vale dUARTBUS_TAMANHO_MAX)
 * @param[out] dados       Bytes lidos
 * @param[out] quantidade  Bytes devolvidos (0 se nada chegou)
 * @return     eUARTBUS_RETURN_OK, _INVALID_ARGUMENT ou _NOT_CONFIGURED
 **********************************************************************************************************************/
uartBusReturn_t UartBus_Read(uint8_t maximo, uint8_t *dados, uint8_t *quantidade)
{
	if (uartBus.func.lock == NULL)
	{
		return eUARTBUS_RETURN_ERROR;
	}

	if ((dados == NULL) || (quantidade == NULL))
	{
		return eUARTBUS_RETURN_INVALID_ARGUMENT;
	}

	if (uartBus.configurada == 0U)
	{
		return eUARTBUS_RETURN_NOT_CONFIGURED;
	}

	uint8_t limite = (maximo < dUARTBUS_TAMANHO_MAX) ? maximo : (uint8_t)dUARTBUS_TAMANHO_MAX;

	uartBus.func.lock();
	uint8_t lidos = (uartBus.quantidade < limite) ? (uint8_t)uartBus.quantidade : limite;
	for (uint8_t i = 0U; i < lidos; i++)
	{
		dados[i] = uartBus.rx[uartBus.inicio];
		uartBus.inicio++;
	}
	uartBus.quantidade = (uint16_t)(uartBus.quantidade - lidos);
	uartBus.func.unlock();

	*quantidade = lidos;

	return eUARTBUS_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Estado seguro: UART desligada e desconfigurada, buffer vazio.
 * @details    Esvazia o buffer mesmo se a BSP falhar ao desligar, para o proximo teste nao herdar dados.
 **********************************************************************************************************************/
uartBusReturn_t UartBus_SafeState(void)
{
	if (uartBus.func.off == NULL)
	{
		return eUARTBUS_RETURN_ERROR;
	}

	uartBusReturn_t ret = uartBus.func.off();

	uartBus.func.lock();
	uartBus.inicio      = 0U;
	uartBus.quantidade  = 0U;
	uartBus.configurada = 0U;
	uartBus.func.unlock();

	return (ret == eUARTBUS_RETURN_OK) ? eUARTBUS_RETURN_OK : eUARTBUS_RETURN_ERROR;
}

/***********************************************************************************************************************
 * @brief      Byte recebido do DUT: entra no fim do buffer circular; com o buffer cheio, e descartado.
 * @param[in]  byte  Byte recebido
 **********************************************************************************************************************/
void UartBus_ByteRecebido(uint8_t byte)
{
	if (uartBus.quantidade < dUARTBUS_TAMANHO_RX)
	{
		uartBus.rx[(uint8_t)(uartBus.inicio + uartBus.quantidade)] = byte;
		uartBus.quantidade++;
	}
}

/** @} DOXYGEN GROUP TAG END OF FILE */
