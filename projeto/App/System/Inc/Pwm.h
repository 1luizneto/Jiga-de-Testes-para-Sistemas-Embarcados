/***********************************************************************************************************************
 *   @file       Pwm.h
 *   @addtogroup App
 *   @{
 **********************************************************************************************************************/

#ifndef SYSTEM_INC_PWM_H_
#define SYSTEM_INC_PWM_H_

/***********************************************************************************************************************
 * INCLUDES NECESSARIOS
 **********************************************************************************************************************/

#include "stdint.h"

/***********************************************************************************************************************
 * DEFINES PUBLICOS
 **********************************************************************************************************************/

// Canais da jiga: 1 = PWM1 (PE9, TIM1_CH1), 2 = PWM2 (PA0, TIM2_CH1), 3 = PWM3 (PA6, TIM3_CH1)
#define dPWM_CANAL_MIN (1U)
#define dPWM_CANAL_MAX (3U)

// Faixa de frequencia aceita, em Hz: ate 100 khz o periodo tem >= 1080 contagens nos tres timers
#define dPWM_FREQUENCIA_MIN (1U)
#define dPWM_FREQUENCIA_MAX (100000U)

// Razao ciclica em centesimos de por cento (10000 = 100,00 %)
#define dPWM_RAZAO_MAX (10000U)

// Frequencia dos canais parados no boot (com razao ciclica 0)
#define dPWM_FREQUENCIA_PADRAO (1000U)

/***********************************************************************************************************************
 * TIPOS DE DADOS PUBLICOS
 **********************************************************************************************************************/

/// @brief Retornos publicos da lib
typedef enum pwmReturn
{
	ePWM_RETURN_OK,
	ePWM_RETURN_INVALID_ARGUMENT,
	ePWM_RETURN_ERROR,

	ePWM_RETURN_END_ENUM
} pwmReturn_t;

/// @brief Timer de um canal
typedef struct pwmTimer
{
	uint32_t clockHz;
	uint32_t periodoMax;
} pwmTimer_t;

/// @brief Valores dos registradores de um canal
typedef struct pwmConfig
{
	uint32_t prescaler;
	uint32_t periodo;
	uint32_t comparacao;
} pwmConfig_t;

/// @brief Aplica a configuracao no timer do canal (fornecida pela BSP)
typedef pwmReturn_t (*pwmApplyFunc_t)(uint8_t canal, const pwmConfig_t *config);

/***********************************************************************************************************************
 * PROTOTIPOS PUBLICOS
 **********************************************************************************************************************/

pwmReturn_t Pwm_Init(pwmApplyFunc_t applyFunc, const pwmTimer_t timers[dPWM_CANAL_MAX]);
pwmReturn_t Pwm_Calcula(const pwmTimer_t *timer, uint32_t frequencia, uint16_t razao, pwmConfig_t *config);
pwmReturn_t Pwm_Set(uint8_t canal, uint32_t frequencia, uint16_t razao);
pwmReturn_t Pwm_Stop(uint8_t canal);
pwmReturn_t Pwm_SafeState(void);

#endif /* SYSTEM_INC_PWM_H_ */

/** @} DOXYGEN GROUP TAG END OF FILE */
