/***********************************************************************************************************************
 *   @file         Bsp_Pwm.c
 *   @addtogroup   App
 *   @brief        Abstracao dos timers do PWM (HAL) para o modulo Pwm
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Ferramentas:</b>
 *   - Generic.
 *
 *   \n <b>Dependencias:</b>
 *   - htim1, htim2 e htim3 configurados pelo CubeMX (MX_TIMx_Init): PWM mode 1 no CH1, contagem crescente,
 *     preload do ARR e do CCR ligados.
 *
 *   \n <b>Observacoes:</b>
 *   - O clock de cada timer vem da configuracao do RCC (nao fica fixo no codigo).
 *   - Entre o MX_TIMx_Init e o BspPwm_Init (alguns ms no boot) os canais ainda nao foram iniciados.
 *
 *   Changelog
 *   @version      <b>1.0.0 - 21/10/2026</b> \n Luiz Neto \n Primeira versao
 *
 *   @copyright
 *   @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include "Bsp_Pwm.h"
#include "Pwm.h"
#include "DebugLog.h"
#include "tim.h"

/***********************************************************************************************************************
 * VARIAVEIS LOCAIS
 **********************************************************************************************************************/

/// Timer de cada canal (indice = canal - 1)
static TIM_HandleTypeDef * const bspPwmHandles[dPWM_CANAL_MAX] =
{
		&htim1,
		&htim2,
		&htim3,
};

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static uint32_t BspPwm_ClockTimer(uint8_t apb2);
static pwmReturn_t BspPwm_Apply(uint8_t canal, const pwmConfig_t *config);

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Entrega os timers ao modulo Pwm e leva os tres canais ao estado seguro.
 * @details    O estado seguro (razao ciclica 0) ja inicia os timers: a jiga liga com as saidas PWM conduzidas em
 *             nivel baixo. Chamado no Bsp_Init, depois do MX_TIMx_Init e do BspUart_Init (usa o DebugLog).
 **********************************************************************************************************************/
bspPwmReturn_t BspPwm_Init(void)
{
	const pwmTimer_t timers[dPWM_CANAL_MAX] =
	{
		{BspPwm_ClockTimer(1U), 0xFFFFU},       // TIM1: 16 bits
		{BspPwm_ClockTimer(0U), 0xFFFFFFFFU},   // TIM2: 32 bits
		{BspPwm_ClockTimer(0U), 0xFFFFU},       // TIM3: 16 bits
	};

	DebugLog_SendTerminal("[BSP -> PWM] Clock dos timers: TIM1 %lu Hz, TIM2 %lu Hz, TIM3 %lu Hz\r\n",
	                      timers[0].clockHz, timers[1].clockHz, timers[2].clockHz);

	if (Pwm_Init(BspPwm_Apply, timers) != ePWM_RETURN_OK)
	{
		return eBSP_PWM_RETURN_ERROR;
	}

	if (Pwm_SafeState() != ePWM_RETURN_OK)
	{
		return eBSP_PWM_RETURN_ERROR;
	}

	return eBSP_PWM_RETURN_OK;
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Clock na entrada dos timers de um barramento APB.
 * @details    Com TIMPRE = 0 (padrao): divisor do APB igual a 1 -> PCLK; senao -> 2 x PCLK (RM0410, RCC). Com
 *             TIMPRE = 1 a regra e outra: devolve 0 e o Pwm_Init recusa, em vez de gerar a frequencia errada.
 * @param[in]  apb2  1 = APB2 (TIM1), 0 = APB1 (TIM2, TIM3)
 * @return     Clock em Hz, ou 0 se a configuracao nao for suportada
 **********************************************************************************************************************/
static uint32_t BspPwm_ClockTimer(uint8_t apb2)
{
	if ((RCC->DCKCFGR1 & RCC_DCKCFGR1_TIMPRE) != 0U)
	{
		return 0U;
	}

	uint32_t pclk     = (apb2 != 0U) ? HAL_RCC_GetPCLK2Freq() : HAL_RCC_GetPCLK1Freq();
	uint32_t divisor  = (apb2 != 0U) ? (RCC->CFGR & RCC_CFGR_PPRE2) : (RCC->CFGR & RCC_CFGR_PPRE1);

	// Campo PPREx = 0 significa APB sem divisao
	return (divisor == 0U) ? pclk : (2U * pclk);
}

/***********************************************************************************************************************
 * @brief      Escreve PSC, ARR e CCR no timer do canal e aplica os tres juntos.
 * @details    Com o preload ligado, os valores escritos so valem no evento de update; o UG forca esse evento na
 *             hora (o contador volta a 0: o periodo em andamento e truncado). Na primeira vez, inicia o canal.
 *             Canal e valores ja foram validados e calculados pelo Pwm.
 * @param[in]  canal   1 a 3
 * @param[in]  config  Valores dos registradores
 * @return     ePWM_RETURN_OK ou ePWM_RETURN_ERROR (falha do HAL ao iniciar o canal)
 **********************************************************************************************************************/
static pwmReturn_t BspPwm_Apply(uint8_t canal, const pwmConfig_t *config)
{
	TIM_HandleTypeDef *htim = bspPwmHandles[canal - 1U];

	__HAL_TIM_SET_PRESCALER(htim, config->prescaler);
	__HAL_TIM_SET_AUTORELOAD(htim, config->periodo);
	__HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_1, config->comparacao);
	htim->Instance->EGR = TIM_EGR_UG;

	if (TIM_CHANNEL_STATE_GET(htim, TIM_CHANNEL_1) == HAL_TIM_CHANNEL_STATE_READY)
	{
		// No TIM1 (timer avancado) o HAL tambem liga a saida principal (MOE)
		if (HAL_TIM_PWM_Start(htim, TIM_CHANNEL_1) != HAL_OK)
		{
			return ePWM_RETURN_ERROR;
		}
	}

	return ePWM_RETURN_OK;
}

/** @} DOXYGEN GROUP TAG END OF FILE */



