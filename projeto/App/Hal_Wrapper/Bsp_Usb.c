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

extern uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];
extern uint8_t recvDone;
extern uint32_t recvSize;

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

bspUsbReturn_t BspUsb_Transmit(uint8_t* buffer, uint16_t bufferSize);
bspUsbReturn_t BspUsb_Received(uint8_t* buffer, uint16_t bufferSize);


/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

bspUsbReturn_t BspUsb_Init(void)
{

	Usb_Init((void *)BspUsb_Transmit,(void *)BspUsb_Received);

	return eBSP_USB_RETURN_OK;
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

bspUsbReturn_t BspUsb_Transmit(uint8_t* buffer, uint16_t bufferSize)
{
	uint8_t ret = 0;

	ret = CDC_Transmit_FS(buffer, bufferSize);

	if (ret == 0)
	{
		return eBSP_USB_RETURN_OK;
	}
	return eBSP_USB_RETURN_ERROR;
}

bspUsbReturn_t BspUsb_Received(uint8_t* buffer, uint16_t bufferSize)
{

	if (recvDone == 1)
	{
		buffer = UserRxBufferFS;
		bufferSize = recvSize;
	}

	return eBSP_USB_RETURN_OK;
}


/** @} DOXYGEN GROUP TAG END OF FILE */


