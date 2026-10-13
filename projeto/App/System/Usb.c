/***********************************************************************************************************************
 *   @file         Bsp_Usb.c
 *   @addtogroup   App
 *   @brief        Arquivo que implementa a abstração da USB
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
 *   @version      <b>1.0.0 - 07/10/2026</b> \n Luiz Neto \n Primeira versao
 *   @version      <b>1.1.0 - 13/10/2026</b> \n Luiz Neto \n Usb_NewSession: avisa quando um programa abre a
 *                 porta no PC (sessao nova)
 *
 *   @copyright
 *   @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include "Usb.h"
#include <stddef.h>

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

// Timeout em ms
#define dTIMEOUT_UART (10U)

/***********************************************************************************************************************
 * TIPOS LOCAIS
 **********************************************************************************************************************/

static struct usb
{
    /// @brief Estrutura com os ponteiros de funções necessárias para a lib
    struct func
    {
        usbWriteFunc_t      write;
        usbReadFunc_t       read;
        usbNewSessionFunc_t newSession;
    } func;

} usb;


/***********************************************************************************************************************
 * VARIAVEIS LOCAIS
 **********************************************************************************************************************/


/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/


/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

usbReturn_t Usb_Init(usbWriteFunc_t writeFunc, usbReadFunc_t readFunc, usbNewSessionFunc_t newSessionFunc)
{
	if ((writeFunc == NULL) || (readFunc == NULL) || (newSessionFunc == NULL))
	{
		return eUSB_RETURN_INVALID_ARGUMENT;
	}

	usb.func.write      = writeFunc;
	usb.func.read       = readFunc;
	usb.func.newSession = newSessionFunc;

	return eUSB_RETURN_OK;
}

usbReturn_t Usb_Write(uint8_t* buffer, uint16_t bufferSize)
{
	if ((usb.func.write == NULL) || (buffer == NULL))
	{
		return eUSB_RETURN_INVALID_ARGUMENT;
	}

	return usb.func.write(buffer, bufferSize);
}

usbReturn_t Usb_Read(uint8_t* buffer, uint32_t bufferSize, uint32_t* readSize, uint32_t timeoutMs)
{
	if ((usb.func.read == NULL) || (buffer == NULL) || (readSize == NULL))
	{
		return eUSB_RETURN_INVALID_ARGUMENT;
	}

	return usb.func.read(buffer, bufferSize, readSize, timeoutMs);
}

/***********************************************************************************************************************
 * @brief      Informa se um programa abriu a porta no PC desde a ultima consulta (sessao nova).
 * @details    Cada abertura e informada uma unica vez. Quem chama usa isso para esquecer o estado da sessao
 *             anterior (ex.: o SEQ da ultima resposta, ja que o PC recomeca a contagem).
 * @return     1 se houve sessao nova, 0 caso contrario (ou se o modulo nao foi iniciado)
 **********************************************************************************************************************/
uint8_t Usb_NewSession(void)
{
	if (usb.func.newSession == NULL)
	{
		return 0U;
	}

	return usb.func.newSession();
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/



/** @} DOXYGEN GROUP TAG END OF FILE */


