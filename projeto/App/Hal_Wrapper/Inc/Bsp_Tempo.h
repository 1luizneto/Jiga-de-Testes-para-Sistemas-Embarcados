/***********************************************************************************************************************
 *   @file       Bsp_Tempo.h
 *   @addtogroup App
 *   @{
 **********************************************************************************************************************/

#ifndef HAL_WRAPPER_INC_BSP_TEMPO_H_
#define HAL_WRAPPER_INC_BSP_TEMPO_H_

/***********************************************************************************************************************
 * INCLUDES NECESSARIOS
 **********************************************************************************************************************/

#include "stdint.h"

/***********************************************************************************************************************
 * TIPOS DE DADOS PUBLICOS
 **********************************************************************************************************************/

/// @brief Retornos publicos da lib
typedef enum bspTempoReturn
{
	eBSP_TEMPO_RETURN_OK,
	eBSP_TEMPO_RETURN_ERROR,

	eBSP_TEMPO_RETURN_END_ENUM
} bspTempoReturn_t;

/***********************************************************************************************************************
 * PROTOTIPOS PUBLICOS
 **********************************************************************************************************************/

bspTempoReturn_t BspTempo_Init(void);

// Borda em uma entrada GPIO5 a GPIO8: chamado pelo HAL_GPIO_EXTI_Callback (Bsp_Spi), dentro da ISR
void BspTempo_Borda(uint16_t pino);

#endif /* HAL_WRAPPER_INC_BSP_TEMPO_H_ */

/** @} DOXYGEN GROUP TAG END OF FILE */
