/***********************************************************************************************************************
 *   @file         Bsp_Uart.c
 *   @addtogroup   App
 *   @brief        Arquivo que implementa a abstração da Uart
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Ferramentas:</b>
 *   - Generic.
 *
 *   \n <b>Dependencias:</b>
 *   - None.
 *
 *   \n <b>Observacoes:</b>
 *   - None.
 *
 *   Changelog
 *   @version      <b>1.0.0 - 17/09/2026</b> \n Luiz Neto \n Primeira versao
 *
 *   @copyright
 *   @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include "Bsp_Uart.h"
#include "main.h"
#include "usart.h"
#include "DebugLog.h"
#include "cmsis_os.h"

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

// Timeout do envio em ms (256 B a 1 Mbaud levam ~2,6 ms; folga para mensagens longas)
#define dTIMEOUT_UART (50U)

// Tempo maximo esperando outra task terminar de usar a UART, em ms
#define dTIMEOUT_MUTEX (50U)

/***********************************************************************************************************************
 * TIPOS LOCAIS
 **********************************************************************************************************************/


/***********************************************************************************************************************
 * VARIAVEIS LOCAIS
 **********************************************************************************************************************/

extern osMutexId_t uartDebugMutexHandle;

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

bspUartReturn_t BspUart_Send(uint8_t *txBuffer, uint16_t bufferSize);
bspUartReturn_t BspUart_Received(void);


/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

bspUartReturn_t BspUart_Init(void)
{
	DebugLog_Init((void *)BspUart_Send);

	return eBSP_UART_RETURN_OK;
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

bspUartReturn_t BspUart_Send(uint8_t *txBuffer, uint16_t bufferSize)
{
	bspUartReturn_t ret;

	// Antes do scheduler (ex.: log do main) so existe um contexto, entao nao ha disputa pela UART
	uint8_t usaMutex = (osKernelGetState() == osKernelRunning) ? 1U : 0U;

	// Sem o mutex nao transmite: outra task esta usando a UART
	if ((usaMutex == 1U) && (osMutexAcquire(uartDebugMutexHandle, dTIMEOUT_MUTEX) != osOK))
	{
		return eBSP_UART_RETURN_ERROR;
	}

	if (HAL_UART_Transmit(&huart3, txBuffer, bufferSize, dTIMEOUT_UART) == HAL_OK)
	{
		ret = eBSP_UART_RETURN_OK;
	}
	else
	{
		ret = eBSP_UART_RETURN_ERROR;
	}

	if (usaMutex == 1U)
	{
		osMutexRelease(uartDebugMutexHandle);
	}

	return ret;
}

bspUartReturn_t BspUart_Received(void)
{
	return eBSP_UART_RETURN_OK;
}

/** @} DOXYGEN GROUP TAG END OF FILE */

