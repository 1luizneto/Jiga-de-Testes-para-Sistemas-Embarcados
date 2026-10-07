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
    eUSB_RETURN_BUSY,       ///< Transmissao anterior ainda em andamento
    eUSB_RETURN_EMPTY,      ///< Nenhum pacote recebido

    eUSB_RETURN_END_ENUM
} usbReturn_t;

typedef usbReturn_t (*usbWriteFunc_t)(uint8_t* buffer, uint16_t bufferSize);

typedef usbReturn_t (*usbReadFunc_t)(uint8_t* buffer, uint32_t bufferSize, uint32_t* readSize);

/***********************************************************************************************************************
 * PROTOTIPOS PUBLICOS
 **********************************************************************************************************************/


usbReturn_t Usb_Init(usbWriteFunc_t writeFunc, usbReadFunc_t readFunc);
usbReturn_t Usb_Write(uint8_t* buffer, uint16_t bufferSize);
usbReturn_t Usb_Read(uint8_t* buffer, uint32_t bufferSize, uint32_t* readSize);

#endif /* SYSTEM_INC_USB_H_ */

/** @} DOXYGEN GROUP TAG END OF FILE */







