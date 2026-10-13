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
 *   @version      <b>1.0.1 - 12/10/2026</b> \n Luiz Neto \n BspUart_Send libera o mutex tambem quando o envio da
 *                                                           certo, nao transmite sem o mutex e dispensa o mutex
 *                                                           antes do scheduler iniciar
 *   @version      <b>1.1.0 - 13/10/2026</b> \n Luiz Neto \n O mutex sai do envio e vira o lock/unlock do DebugLog;
 *                                                          assinaturas iguais aos tipos do DebugLog
 *                                                          (sem cast para void *)
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

static void BspUart_Send(uint8_t *txBuffer, uint16_t bufferSize);
static uint8_t BspUart_Lock(void);
static void BspUart_Unlock(void);
bspUartReturn_t BspUart_Received(void);


/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

bspUartReturn_t BspUart_Init(void)
{
	if (DebugLog_Init(BspUart_Send, BspUart_Lock, BspUart_Unlock) != eDEBUGLOG_RETURN_OK)
	{
		return eBSP_UART_RETURN_ERROR;
	}

	return eBSP_UART_RETURN_OK;
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Envia pela USART3 (VCP do ST-LINK).
 * @details    Quem chama (DebugLog) ja esta com o lock.
 * @param[in]  txBuffer    Bytes a enviar
 * @param[in]  bufferSize  Quantidade de bytes
 **********************************************************************************************************************/
static void BspUart_Send(uint8_t *txBuffer, uint16_t bufferSize)
{
	(void)HAL_UART_Transmit(&huart3, txBuffer, bufferSize, dTIMEOUT_UART);
}

/***********************************************************************************************************************
 * @brief      Reserva a UART de debug para uma mensagem.
 * @details    Antes do scheduler (ex.: log do main) so existe um contexto, entao nao ha disputa e o mutex e dispensado.
 * @return     1 se conseguiu, 0 se outra task nao liberou em dTIMEOUT_MUTEX
 **********************************************************************************************************************/
static uint8_t BspUart_Lock(void)
{
	// Antes do scheduler
	if (osKernelGetState() != osKernelRunning)
	{
		return 1U;
	}

	return (osMutexAcquire(uartDebugMutexHandle, dTIMEOUT_MUTEX) == osOK) ? 1U : 0U;
}

/***********************************************************************************************************************
 * @brief      Libera a UART de debug reservada pelo BspUart_Lock.
 **********************************************************************************************************************/
static void BspUart_Unlock(void)
{
	if (osKernelGetState() == osKernelRunning)
	{
		osMutexRelease(uartDebugMutexHandle);
	}
}
bspUartReturn_t BspUart_Received(void)
{
	return eBSP_UART_RETURN_OK;
}

/** @} DOXYGEN GROUP TAG END OF FILE */

