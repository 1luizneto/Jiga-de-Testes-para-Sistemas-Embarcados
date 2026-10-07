/***********************************************************************************************************************
 *   @file       Usb.h
 *   @addtogroup App
 *   @{
 **********************************************************************************************************************/
#ifndef SYSTEM_INC_USB_H_
#define SYSTEM_INC_USB_H_


/***********************************************************************************************************************
 * INCLUDES NECESSARIOS
 **********************************************************************************************************************/

#include "stdint.h"

/***********************************************************************************************************************
 * TIPOS DE DADOS PUBLICOS
 **********************************************************************************************************************/

/// @brief Retornos publicos da lib
typedef enum usbReturn
{
    eUSB_RETURN_OK,
    eUSB_RETURN_INVALID_ARGUMENT,
    eUSB_RETURN_ERROR,

    eUSB_RETURN_END_ENUM
} usbReturn_t;

/***********************************************************************************************************************
 * PROTOTIPOS PUBLICOS
 **********************************************************************************************************************/


usbReturn_t Usb_Init(void (*writeFunc)(uint8_t* buffer, uint16_t bufferSize),
					 void (*readFunc)(uint8_t* buffer, uint32_t bufferSize));
usbReturn_t Usb_Write(uint8_t* buffer, uint16_t bufferSize);
usbReturn_t Usb_Read(uint8_t* buffer, uint32_t bufferSize);

#endif /* SYSTEM_INC_USB_H_ */

/** @} DOXYGEN GROUP TAG END OF FILE */







