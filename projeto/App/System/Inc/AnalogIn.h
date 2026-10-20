/***********************************************************************************************************************
 *   @file       AnalogIn.h
 *   @addtogroup App
 *   @{
 **********************************************************************************************************************/

#ifndef SYSTEM_INC_ANALOGIN_H_
#define SYSTEM_INC_ANALOGIN_H_

/***********************************************************************************************************************
 * INCLUDES NECESSARIOS
 **********************************************************************************************************************/

#include "stdint.h"

/***********************************************************************************************************************
 * DEFINES PUBLICOS
 **********************************************************************************************************************/

// Canais da jiga: 0 = A0 (ADC1, PA3), 1 = A1 (ADC2, PC0), 2 = A2 (ADC3, PC3)
#define dANALOGIN_CANAL_MAX (2U)

// Resolucao de 12 bits
#define dANALOGIN_VALOR_MAX (4095U)

/***********************************************************************************************************************
 * TIPOS DE DADOS PUBLICOS
 **********************************************************************************************************************/

/// @brief Retornos publicos da lib
typedef enum analogInReturn
{
	eANALOGIN_RETURN_OK,
	eANALOGIN_RETURN_INVALID_ARGUMENT,
	eANALOGIN_RETURN_ERROR,

	eANALOGIN_RETURN_END_ENUM
} analogInReturn_t;

/// @brief Faz uma conversao no canal
typedef analogInReturn_t (*analogInReadFunc_t)(uint8_t canal, uint16_t *valor);

/***********************************************************************************************************************
 * PROTOTIPOS PUBLICOS
 **********************************************************************************************************************/

analogInReturn_t AnalogIn_Init(analogInReadFunc_t readFunc);
analogInReturn_t AnalogIn_Read(uint8_t canal, uint16_t *valor);

#endif /* SYSTEM_INC_ANALOGIN_H_ */

/** @} DOXYGEN GROUP TAG END OF FILE */
