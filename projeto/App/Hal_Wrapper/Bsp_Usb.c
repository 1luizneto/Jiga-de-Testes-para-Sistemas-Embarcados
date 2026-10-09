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
#include "FreeRTOS.h"
#include "stream_buffer.h"

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

/// Capacidade do stream buffer de recepcao: 2 quadros maximos (2 x 246 B)
#define dRX_STREAM_SIZE (512U)

#define dRX_STREAM_TRIGGER (1U)

/***********************************************************************************************************************
 * TIPOS LOCAIS
 **********************************************************************************************************************/

static struct bspUSb
{

	uint8_t rxStreamStorage[dRX_STREAM_SIZE + 1U];

	StaticStreamBuffer_t rxStreamControl;

	StreamBufferHandle_t rxStream;

	volatile uint32_t rxDroppedBytes;

} bspUsb;

/***********************************************************************************************************************
 * VARIAVEIS LOCAIS
 **********************************************************************************************************************/


/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static usbReturn_t BspUsb_Transmit(uint8_t* buffer, uint16_t bufferSize);
static usbReturn_t BspUsb_Received(uint8_t* buffer, uint32_t bufferSize, uint32_t* readSize, uint32_t timeoutMs);
static void BspUsb_RxFromIsr(uint8_t *buffer, uint32_t bufferSize);


/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

bspUsbReturn_t BspUsb_Init(void)
{

	bspUsb.rxStream = xStreamBufferCreateStatic(dRX_STREAM_SIZE,
												dRX_STREAM_TRIGGER,
												bspUsb.rxStreamStorage,
												&bspUsb.rxStreamControl);
	bspUsb.rxDroppedBytes = 0;

	if (bspUsb.rxStream == NULL)
	{
		return eBSP_USB_RETURN_ERROR;
	}

	CDC_RegisterRxCallback(BspUsb_RxFromIsr);

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

static usbReturn_t BspUsb_Received(uint8_t* buffer, uint32_t bufferSize, uint32_t* readSize, uint32_t timeoutMs)
{
	*readSize = xStreamBufferReceive(bspUsb.rxStream, buffer, bufferSize, pdMS_TO_TICKS(timeoutMs));

	if (*readSize == 0)
	{
		return eUSB_RETURN_EMPTY;
	}

	return eUSB_RETURN_OK;
}

static void BspUsb_RxFromIsr(uint8_t *buffer, uint32_t bufferSize)
{
	BaseType_t higherPriorityTaskWoken = pdFALSE;

	size_t written = xStreamBufferSendFromISR(bspUsb.rxStream, buffer, bufferSize, &higherPriorityTaskWoken);

	if (written < bufferSize)
	{
		bspUsb.rxDroppedBytes += (bufferSize - written);
	}

	portYIELD_FROM_ISR(higherPriorityTaskWoken);
}


/** @} DOXYGEN GROUP TAG END OF FILE */


