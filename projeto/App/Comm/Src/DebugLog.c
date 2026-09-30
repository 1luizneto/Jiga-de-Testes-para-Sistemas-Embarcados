/***********************************************************************************************************************
 *   @file         DebugLog.c
 *   @addtogroup   App
 *   @brief        Lib que implementa a função que vai servir como debug do projeto inteiro
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

#include "DebugLog.h"
#include "stddef.h"
#include "stdarg.h"
#include "stdio.h"

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * TIPOS LOCAIS
 **********************************************************************************************************************/

static struct debugLog
{
	struct func
	{
		void (*uartSend)(uint8_t *txBuffer, uint16_t txBufferSize);
	}func;

	char printfBuffer[dDEBUGLOG_MAX_BUFFER_SIZE];

} debugLog;

/***********************************************************************************************************************
 * VARIAVEIS LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

debuglogReturn_t DebugLog_Init(void (*uartSendFunc)(uint8_t *txBuffer, uint16_t txBufferSize))
{
	if (uartSendFunc == NULL)
	{
		return eDEBUGLOG_RETURN_INVALID_ARGUMENT;
	}

	debugLog.func.uartSend = uartSendFunc;

	return eDEBUGLOG_RETURN_OK;
}

debuglogReturn_t DebugLog_SendTerminal(const char *string, ...)
{
	va_list args;
	int size;

	if (string == NULL || debugLog.func.uartSend == NULL)
	{
		return eDEBUGLOG_RETURN_INVALID_ARGUMENT;
	}

	va_start(args, string);
	size = vsnprintf(debugLog.printfBuffer, dDEBUGLOG_MAX_BUFFER_SIZE, string, args);
	va_end(args);

	if (size <= 0 || size >= dDEBUGLOG_MAX_BUFFER_SIZE)
	{
		return eDEBUGLOG_RETURN_ERROR;
	}

	debugLog.func.uartSend((uint8_t *)debugLog.printfBuffer, (uint16_t)size);

	return eDEBUGLOG_RETURN_OK;

}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

/** @} DOXYGEN GROUP TAG END OF FILE */
