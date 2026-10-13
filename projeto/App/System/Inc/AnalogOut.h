/***********************************************************************************************************************
 *   @file       AnalogOut.h
 *   @addtogroup App
 *   @{
 **********************************************************************************************************************/

#ifndef SYSTEM_INC_ANALOGOUT_H_
#define SYSTEM_INC_ANALOGOUT_H_

/***********************************************************************************************************************
 * INCLUDES NECESSARIOS
 **********************************************************************************************************************/

#include "stdint.h"

/***********************************************************************************************************************
 * DEFINES PUBLICOS
 **********************************************************************************************************************/

// Canais da jiga (DAC1 PA4 e DAC2 PA5)
#define dANALOGOUT_CANAL_MIN (1U)
#define dANALOGOUT_CANAL_MAX (2U)

// Resolucao de 12 bits
#define dANALOGOUT_VALOR_MAX (4095U)

/***********************************************************************************************************************
 * TIPOS DE DADOS PUBLICOS
 **********************************************************************************************************************/

/// @brief Retornos publicos da lib
typedef enum analogOutReturn
{
	eANALOGOUT_RETURN_OK,
	eANALOGOUT_RETURN_INVALID_ARGUMENT,
	eANALOGOUT_RETURN_ERROR,

	eANALOGOUT_RETURN_END_ENUM
} analogOutReturn_t;

/// @brief Aplica o valor no canal (fornecida pela BSP)
typedef analogOutReturn_t (*analogOutSetFunc_t)(uint8_t canal, uint16_t valor);

/***********************************************************************************************************************
 * PROTOTIPOS PUBLICOS
 **********************************************************************************************************************/

analogOutReturn_t AnalogOut_Init(analogOutSetFunc_t setFunc);
analogOutReturn_t AnalogOut_Set(uint8_t canal, uint16_t valor);
analogOutReturn_t AnalogOut_SafeState(void);

#endif /* SYSTEM_INC_ANALOGOUT_H_ */

/** @} DOXYGEN GROUP TAG END OF FILE */
