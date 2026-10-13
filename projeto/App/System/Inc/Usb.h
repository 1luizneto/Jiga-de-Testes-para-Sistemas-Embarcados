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
    eUSB_RETURN_BUSY,
    eUSB_RETURN_EMPTY,

    eUSB_RETURN_END_ENUM
} usbReturn_t;

typedef usbReturn_t (*usbWriteFunc_t)(uint8_t* buffer, uint16_t bufferSize);

typedef usbReturn_t (*usbReadFunc_t)(uint8_t* buffer, uint32_t bufferSize, uint32_t* readSize, uint32_t timeoutMs);

/// @brief Retorna 1 (uma unica vez) se um programa abriu a porta no PC desde a ultima consulta, senao 0
typedef uint8_t (*usbNewSessionFunc_t)(void);

/***********************************************************************************************************************
 * PROTOTIPOS PUBLICOS
 **********************************************************************************************************************/


usbReturn_t Usb_Init(usbWriteFunc_t writeFunc, usbReadFunc_t readFunc, usbNewSessionFunc_t newSessionFunc);
usbReturn_t Usb_Write(uint8_t* buffer, uint16_t bufferSize);
usbReturn_t Usb_Read(uint8_t* buffer, uint32_t bufferSize, uint32_t* readSize, uint32_t timeoutMs);
uint8_t Usb_NewSession(void);

#endif /* SYSTEM_INC_USB_H_ */

/** @} DOXYGEN GROUP TAG END OF FILE */







