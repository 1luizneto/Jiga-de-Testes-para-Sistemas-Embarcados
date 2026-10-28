/***********************************************************************************************************************
 *   @file         Bsp_Uart4.c
 *   @addtogroup   App
 *   @brief        Abstracao da UART4 (HAL) para o modulo UartBus: recepcao por interrupcao e escrita por polling
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Ferramentas:</b>
 *   - Generic.
 *
 *   \n <b>Dependencias:</b>
 *   - huart4 configurada pelo CubeMX (MX_UART4_Init): PD1 = TX, PD0 = RX, 8N1, IRQ na prioridade 5.
 *
 *   \n <b>Observacoes:</b>
 *   - Recepcao byte a byte (HAL_UART_Receive_IT de 1 byte, rearmada no callback): cada byte vai para o buffer
 *     circular do UartBus. Os callbacks do HAL rodam na ISR da UART4: nada de DebugLog nem de FreeRTOS aqui.
 *   - Erro que para a recepcao (overrun) rearma; erros de quadro, ruido e paridade so descartam o byte.
 *   - E a UART do DUT; a do DebugLog (USART3) fica no Bsp_Uart.
 *   - Desligada (boot e RESET_ALL) = HAL_UART_DeInit: periferico parado e PD0/PD1 soltos.
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

#include "Bsp_Uart4.h"
#include "UartBus.h"
#include "DebugLog.h"
#include "usart.h"

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

// Folga do timeout da escrita, alem do tempo dos bytes no baud configurado
#define dBSP_UART4_FOLGA_MS (10U)

/***********************************************************************************************************************
 * VARIAVEIS LOCAIS
 **********************************************************************************************************************/

static struct bspUart4
{
	volatile uint8_t ativa;   ///< 1: configurada e recebendo (o callback repassa ao UartBus)
	uint8_t  rxByte;          ///< Byte recebido
	uint32_t baud;            ///< Baud configurado (para o timeout da escrita)
	uint8_t  irqLigada;       ///< Estado da IRQ antes do lock
} bspUart4;

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static uartBusReturn_t BspUart4_Config(uint32_t baud);
static uartBusReturn_t BspUart4_Off(void);
static uartBusReturn_t BspUart4_Write(const uint8_t *dados, uint8_t quantidade);
static void BspUart4_Lock(void);
static void BspUart4_Unlock(void);

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Entrega a UART4 ao modulo UartBus e a leva ao estado seguro (desligada).
 * @details    Chamado no Bsp_Init, depois do MX_UART4_Init e do BspUart_Init (usa o DebugLog).
 **********************************************************************************************************************/
bspUart4Return_t BspUart4_Init(void)
{
	static const uartBusFunc_t func =
	{
		BspUart4_Config,
		BspUart4_Off,
		BspUart4_Write,
		BspUart4_Lock,
		BspUart4_Unlock,
	};

	if (UartBus_Init(&func) != eUARTBUS_RETURN_OK)
	{
		return eBSP_UART4_RETURN_ERROR;
	}

	if (UartBus_SafeState() != eUARTBUS_RETURN_OK)
	{
		return eBSP_UART4_RETURN_ERROR;
	}

	DebugLog_SendTerminal("[BSP -> UART] UART4 (PD1 TX, PD0 RX) desligada ate o UART_CONFIG\r\n");

	return eBSP_UART4_RETURN_OK;
}

/***********************************************************************************************************************
 * CALLBACKS DO HAL (ISR da UART4)
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Byte recebido do DUT: vai para o UartBus e a recepcao do proximo e armada.
 **********************************************************************************************************************/
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	if ((huart->Instance != UART4) || (bspUart4.ativa == 0U))
	{
		return;
	}

	UartBus_ByteRecebido(bspUart4.rxByte);
	(void)HAL_UART_Receive_IT(huart, &bspUart4.rxByte, 1U);
}

/***********************************************************************************************************************
 * @brief      Erro na recepcao. O overrun para a recepcao: rearma. Nos outros erros a recepcao continua sozinha.
 **********************************************************************************************************************/
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
	if ((huart->Instance != UART4) || (bspUart4.ativa == 0U))
	{
		return;
	}

	if (huart->RxState == HAL_UART_STATE_READY)
	{
		(void)HAL_UART_Receive_IT(huart, &bspUart4.rxByte, 1U);
	}
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Reinicia a UART4 no baud pedido (8N1) e liga a recepcao.
 * @param[in]  baud  dUARTBUS_BAUD_MIN a dUARTBUS_BAUD_MAX (ja validado)
 * @return     eUARTBUS_RETURN_OK ou eUARTBUS_RETURN_ERROR (falha do HAL; a UART4 fica desligada)
 **********************************************************************************************************************/
static uartBusReturn_t BspUart4_Config(uint32_t baud)
{
	if (BspUart4_Off() != eUARTBUS_RETURN_OK)
	{
		return eUARTBUS_RETURN_ERROR;
	}

	huart4.Init.BaudRate     = baud;
	huart4.Init.WordLength   = UART_WORDLENGTH_8B;
	huart4.Init.StopBits     = UART_STOPBITS_1;
	huart4.Init.Parity       = UART_PARITY_NONE;
	huart4.Init.Mode         = UART_MODE_TX_RX;
	huart4.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
	huart4.Init.OverSampling = UART_OVERSAMPLING_16;

	if (HAL_UART_Init(&huart4) != HAL_OK)
	{
		(void)BspUart4_Off();
		return eUARTBUS_RETURN_ERROR;
	}

	bspUart4.baud  = baud;
	bspUart4.ativa = 1U;

	if (HAL_UART_Receive_IT(&huart4, &bspUart4.rxByte, 1U) != HAL_OK)
	{
		(void)BspUart4_Off();
		return eUARTBUS_RETURN_ERROR;
	}

	return eUARTBUS_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Desliga a UART4: sem recepcao, periferico parado e pinos soltos.
 **********************************************************************************************************************/
static uartBusReturn_t BspUart4_Off(void)
{
	bspUart4.ativa = 0U;

	if (HAL_UART_DeInit(&huart4) != HAL_OK)
	{
		return eUARTBUS_RETURN_ERROR;
	}

	return eUARTBUS_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Envia os bytes por polling (chamada pela TestExecTask).
 * @details    Timeout = tempo dos bytes no baud configurado (10 bits por byte) + dBSP_UART4_FOLGA_MS.
 * @return     eUARTBUS_RETURN_OK ou eUARTBUS_RETURN_ERROR (falha ou timeout do HAL)
 **********************************************************************************************************************/
static uartBusReturn_t BspUart4_Write(const uint8_t *dados, uint8_t quantidade)
{
	uint32_t timeoutMs = ((10000U * (uint32_t)quantidade) / bspUart4.baud) + dBSP_UART4_FOLGA_MS;

	if (HAL_UART_Transmit(&huart4, dados, quantidade, timeoutMs) != HAL_OK)
	{
		return eUARTBUS_RETURN_ERROR;
	}

	return eUARTBUS_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Impede a recepcao (IRQ da UART4) enquanto a task tira bytes do buffer do UartBus.
 **********************************************************************************************************************/
static void BspUart4_Lock(void)
{
	bspUart4.irqLigada = (NVIC_GetEnableIRQ(UART4_IRQn) != 0U) ? 1U : 0U;
	HAL_NVIC_DisableIRQ(UART4_IRQn);
}

/***********************************************************************************************************************
 * @brief      Libera a recepcao (so religa a IRQ se ela estava ligada antes do lock).
 **********************************************************************************************************************/
static void BspUart4_Unlock(void)
{
	if (bspUart4.irqLigada != 0U)
	{
		HAL_NVIC_EnableIRQ(UART4_IRQn);
	}
}

/** @} DOXYGEN GROUP TAG END OF FILE */
