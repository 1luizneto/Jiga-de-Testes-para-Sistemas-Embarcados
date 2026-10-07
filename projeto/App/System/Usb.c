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
        usbWriteFunc_t write;
        usbReadFunc_t  read;
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

usbReturn_t Usb_Init(usbWriteFunc_t writeFunc, usbReadFunc_t readFunc)
{
	if ((writeFunc == NULL) || (readFunc == NULL))
	{
		return eUSB_RETURN_INVALID_ARGUMENT;
	}

	usb.func.write = writeFunc;
	usb.func.read  = readFunc;

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

usbReturn_t Usb_Read(uint8_t* buffer, uint32_t bufferSize, uint32_t* readSize)
{
	if ((usb.func.read == NULL) || (buffer == NULL) || (readSize == NULL))
	{
		return eUSB_RETURN_INVALID_ARGUMENT;
	}

	return usb.func.read(buffer, bufferSize, readSize);
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/



/** @} DOXYGEN GROUP TAG END OF FILE */


