/***********************************************************************************************************************
 *   @file       DigitalIo.h
 *   @addtogroup App
 *   @{
 **********************************************************************************************************************/

#ifndef SYSTEM_INC_DIGITALIO_H_
#define SYSTEM_INC_DIGITALIO_H_


/***********************************************************************************************************************
 * INCLUDES NECESSARIOS
 **********************************************************************************************************************/


#include "stdint.h"

/***********************************************************************************************************************
 * DEFINES PUBLICOS
 **********************************************************************************************************************/

// Saidas da jiga: GPIO1 a GPIO4 (PC8 a PC11)
#define dDIGITALIO_SAIDA_MIN (1U)
#define dDIGITALIO_SAIDA_MAX (4U)

// Entradas da jiga: GPIO5 a GPIO8 (PC12, PD2, PG2, PG3)
#define dDIGITALIO_ENTRADA_MIN (1U)
#define dDIGITALIO_ENTRADA_MAX (4U)

// Niveis logicos
#define dDIGITALIO_NIVEL_BAIXO (0U)
#define dDIGITALIO_NIVEL_ALTO  (1U)

/***********************************************************************************************************************
 * TIPOS DE DADOS PUBLICOS
 **********************************************************************************************************************/

/// @brief Retornos publicos da lib
typedef enum digitalIoReturn
{
	eDIGITALIO_RETURN_OK,
	eDIGITALIO_RETURN_INVALID_ARGUMENT,
	eDIGITALIO_RETURN_ERROR,

	eDIGITALIO_RETURN_END_ENUM
} digitalIoReturn_t;

/// @brief Aplica o nível em uma saida
typedef digitalIoReturn_t (*digitalIoWriteFunc_t)(uint8_t saida, uint8_t nivel);

/// @brief Aplica o nível em uma entrada
typedef digitalIoReturn_t (*digitalIoReadFunc_t)(uint8_t entrada, uint8_t *nivel);

/***********************************************************************************************************************
 * PROTOTIPOS PUBLICOS
 **********************************************************************************************************************/

digitalIoReturn_t DigitalIO_Init(digitalIoWriteFunc_t writeFunc, digitalIoReadFunc_t readFunc);
digitalIoReturn_t DigitalIO_Set(uint8_t saída, uint8_t nivel);
digitalIoReturn_t DigitalIO_Read(uint8_t *mascara);
digitalIoReturn_t DigitalIO_SafeState(void);

#endif /* SYSTEM_INC_DIGITALIO_H_ */

/** @} DOXYGEN GROUP TAG END OF FILE */
