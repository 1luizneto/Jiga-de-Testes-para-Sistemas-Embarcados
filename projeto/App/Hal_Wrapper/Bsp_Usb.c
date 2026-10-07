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

#include "Bsp_Usb.h"
#include "Usb.h"
#include "main.h"
#include "usbd_cdc_if.h"
#include "DebugLog.h"
#include "cmsis_os.h"

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

// Timeout em ms
#define dTIMEOUT_UART (10U)

/***********************************************************************************************************************
 * TIPOS LOCAIS
 **********************************************************************************************************************/


/***********************************************************************************************************************
 * VARIAVEIS LOCAIS
 **********************************************************************************************************************/


/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static usbReturn_t BspUsb_Transmit(uint8_t* buffer, uint16_t bufferSize);
static usbReturn_t BspUsb_Received(uint8_t* buffer, uint32_t bufferSize, uint32_t* readSize);


/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

bspUsbReturn_t BspUsb_Init(void)
{
	if (Usb_Init(BspUsb_Transmit, BspUsb_Received) != eUSB_RETURN_OK)
	{
		return eBSP_USB_RETURN_ERROR;
	}

	return eBSP_USB_RETURN_OK;
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

static usbReturn_t BspUsb_Transmit(uint8_t* buffer, uint16_t bufferSize)
{
	uint8_t ret = CDC_Transmit_FS(buffer, bufferSize);

	if (ret == USBD_OK)
	{
		return eUSB_RETURN_OK;
	}
	if (ret == USBD_BUSY)
	{
		return eUSB_RETURN_BUSY;
	}

	return eUSB_RETURN_ERROR;
}

static usbReturn_t BspUsb_Received(uint8_t* buffer, uint32_t bufferSize, uint32_t* readSize)
{
	*readSize = CDC_Read_FS(buffer, bufferSize);

	if (*readSize == 0)
	{
		return eUSB_RETURN_EMPTY;
	}

	return eUSB_RETURN_OK;
}


/** @} DOXYGEN GROUP TAG END OF FILE */


