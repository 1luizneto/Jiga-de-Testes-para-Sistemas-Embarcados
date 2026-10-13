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
 *   @version      <b>1.1.0 - 13/10/2026</b> \n Luiz Neto \n Lock/Unlock fornecidos pela Bsp cobrem a formatacao
 *                                                           e o envio: duas taks logando juntas nao misturam as
 *                                                           mensagens no printfBuffer
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
		debugLogSendFunc_t   uartSend;
		debugLogLockFunc_t   lock;
		debugLogUnlockFunc_t unlock;
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

debuglogReturn_t DebugLog_Init(debugLogSendFunc_t sendFunc, debugLogLockFunc_t lockFunc,
                               debugLogUnlockFunc_t unlockFunc)
{
	if ((sendFunc == NULL) || (lockFunc == NULL) || (unlockFunc == NULL))
	{
		return eDEBUGLOG_RETURN_INVALID_ARGUMENT;
	}

	debugLog.func.uartSend = sendFunc;
	debugLog.func.lock     = lockFunc;
	debugLog.func.unlock   = unlockFunc;

	return eDEBUGLOG_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Formata e envia uma mensagem para o terminal de debug (estilo printf).
 * @details    O printfBuffer e unico: a trava cobre a formatacao E o envio, senao uma task de prioridade maior
 *             poderia formatar a mensagem dela no buffer enquanto outra ainda nao terminou de enviar a sua.
 *             Nao chamar de ISR (o lock usa mutex).
 **********************************************************************************************************************/
debuglogReturn_t DebugLog_SendTerminal(const char *string, ...)
{
	va_list args;
	int size;
	debuglogReturn_t ret = eDEBUGLOG_RETURN_ERROR;

	if ((string == NULL) || (debugLog.func.uartSend == NULL))
	{
		return eDEBUGLOG_RETURN_INVALID_ARGUMENT;
	}

	// Outra task segurou o terminal por tempo demais: perde esta mensagem em vez de travar quem chamou
	if (debugLog.func.lock() == 0U)
	{
		return eDEBUGLOG_RETURN_ERROR;
	}

	va_start(args, string);
	size = vsnprintf(debugLog.printfBuffer, dDEBUGLOG_MAX_BUFFER_SIZE, string, args);
	va_end(args);

	if ((size > 0) && (size < (int)dDEBUGLOG_MAX_BUFFER_SIZE))
	{
		debugLog.func.uartSend((uint8_t *)debugLog.printfBuffer, (uint16_t)size);
		ret = eDEBUGLOG_RETURN_OK;
	}

	// Libera em todos os caminhos, inclusive quando a formatacao falha
	debugLog.func.unlock();

	return ret;
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

/** @} DOXYGEN GROUP TAG END OF FILE */
