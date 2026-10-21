/***********************************************************************************************************************
 *   @file         Pwm.c
 *   @addtogroup   App
 *   @brief        Saidas PWM da jiga (PWM1 a PWM3): validacao, calculo de PSC/ARR/CCR e estado seguro
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Ferramentas:</b>
 *   - Generic.
 *
 *   \n <b>Dependencias:</b>
 *   - Clock e ARR maximo de cada timer e a funcao que aplica a configuracao, fornecidos pela BSP (Pwm_Init).
 *
 *   \n <b>Observacoes:</b>
 *   - Nao conhece o HAL: os testes Unity registram um timer falso no Pwm_Init.
 *   - Canal parado = razao ciclica 0 com o timer rodando: a saida fica conduzida em nivel baixo, nunca solta.
 *   - O calculo e espelhado no Software (protocolo.py), com os mesmos vetores de teste.
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

#include "Pwm.h"
#include <stddef.h>

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

// Maior divisor do clock
#define dPWM_DIVISOR_MAX (65536U)

/***********************************************************************************************************************
 * TIPOS LOCAIS
 **********************************************************************************************************************/

static struct pwm
{
	struct func
	{
		pwmApplyFunc_t apply;
	} func;

	pwmTimer_t timers[dPWM_CANAL_MAX];
	pwmConfig_t configs[dPWM_CANAL_MAX];
} pwm;

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static pwmReturn_t Pwm_Aplica(uint8_t canal, const pwmConfig_t *config);

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Registra os timers e a funcao da BSP e prepara cada canal parado em dPWM_FREQUENCIA_PADRAO.
 * @details    Nao aplica nada no hardware: quem leva os canais ao estado seguro e a BSP (Pwm_SafeState).
 * @param[in]  applyFunc  Aplica a configuracao no timer do canal
 * @param[in]  timers     Clock e ARR maximo de cada canal (indice = canal - 1)
 **********************************************************************************************************************/
pwmReturn_t Pwm_Init(pwmApplyFunc_t applyFunc, const pwmTimer_t timers[dPWM_CANAL_MAX])
{
	if ((applyFunc == NULL) || (timers == NULL))
	{
		return ePWM_RETURN_INVALID_ARGUMENT;
	}

	for (uint8_t i = 0U; i < dPWM_CANAL_MAX; i++)
	{
		if (Pwm_Calcula(&timers[i], dPWM_FREQUENCIA_PADRAO, 0U, &pwm.configs[i]) != ePWM_RETURN_OK)
		{
			return ePWM_RETURN_INVALID_ARGUMENT;
		}

		pwm.timers[i] = timers[i];
	}

	pwm.func.apply = applyFunc;

	return ePWM_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Calcula PSC, ARR e CCR para uma frequencia e uma razao ciclica.
 *
 * @details    Usa o menor divisor que faz o periodo caber no contador: o ARR fica o maior possivel e a razao ciclica
 *             tem a melhor resolucao. A frequencia gerada e a mais proxima possivel da pedida (erro de
 *             arredondamento < 0,05 % ate dPWM_FREQUENCIA_MAX). Em erro, a config nao e alterada.
 *
 * @param[in]  timer       Clock e ARR maximo do timer
 * @param[in]  frequencia  dPWM_FREQUENCIA_MIN a dPWM_FREQUENCIA_MAX, em Hz
 * @param[in]  razao       0 a dPWM_RAZAO_MAX, em centesimos de por cento
 * @param[out] config      Valores dos registradores
 *
 * @return     ePWM_RETURN_OK ou ePWM_RETURN_INVALID_ARGUMENT (fora da faixa ou timer incapaz da frequencia)
 **********************************************************************************************************************/
pwmReturn_t Pwm_Calcula(const pwmTimer_t *timer, uint32_t frequencia, uint16_t razao, pwmConfig_t *config)
{
	if ((timer == NULL) || (config == NULL) || (timer->clockHz == 0U) || (timer->periodoMax == 0U))
	{
		return ePWM_RETURN_INVALID_ARGUMENT;
	}

	if ((frequencia < dPWM_FREQUENCIA_MIN) || (frequencia > dPWM_FREQUENCIA_MAX) || (razao > dPWM_RAZAO_MAX))
	{
		return ePWM_RETURN_INVALID_ARGUMENT;
	}

	uint64_t contagens = ((uint64_t)timer->clockHz + (frequencia / 2U)) / frequencia;
	uint64_t periodoMax = (uint64_t)timer->periodoMax + 1U;

	uint64_t divisor = (contagens + periodoMax - 1U) / periodoMax;
	if (divisor == 0U)
	{
		divisor = 1U;
	}
	if (divisor > dPWM_DIVISOR_MAX)
	{
		return ePWM_RETURN_INVALID_ARGUMENT;
	}

	uint64_t passo = divisor * frequencia;
	uint64_t periodo = ((uint64_t)timer->clockHz + (passo / 2U)) / passo;
	if (periodo > periodoMax)
	{
		periodo = periodoMax;
	}
	if (periodo < 2U)
	{
		ePWM_RETURN_INVALID_ARGUMENT;
	}

	config->prescaler  = (uint32_t)(divisor - 1U);
	config->periodo    = (uint32_t)(periodo - 1U);
	config->comparacao = (uint32_t)((((uint64_t)razao * periodo) + (dPWM_RAZAO_MAX / 2U)) / dPWM_RAZAO_MAX);

	return ePWM_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Gera PWM em um canal.
 *
 * @details    Parametro fora da faixa e recusado sem tocar no timer ("nenhuma saida alterada", CT09).
 *
 * @param[in]  canal       1 a 3
 * @param[in]  frequencia  dPWM_FREQUENCIA_MIN a dPWM_FREQUENCIA_MAX, em Hz
 * @param[in]  razao       0 a dPWM_RAZAO_MAX, em centesimos de por cento
 **********************************************************************************************************************/
pwmReturn_t Pwm_Set(uint8_t canal, uint32_t frequencia, uint16_t razao)
{
	pwmConfig_t config;

	if (pwm.func.apply == NULL)
	{
		return ePWM_RETURN_ERROR;
	}

	if ((canal < dPWM_CANAL_MIN) || (canal > dPWM_CANAL_MAX))
	{
		return ePWM_RETURN_INVALID_ARGUMENT;
	}

	if (Pwm_Calcula(&pwm.timers[canal - 1U], frequencia, razao, &config) != ePWM_RETURN_OK)
	{
		return ePWM_RETURN_INVALID_ARGUMENT;
	}

	return Pwm_Aplica(canal, &config);

}

/***********************************************************************************************************************
 * @brief      Para o PWM de um canal: mesma frequencia, razao ciclica 0 (saida em nivel baixo, conduzida).
 * @param[in]  canal  1 a 3
 **********************************************************************************************************************/
pwmReturn_t Pwm_Stop(uint8_t canal)
{
	if (pwm.func.apply == NULL)
	{
		return ePWM_RETURN_ERROR;
	}

	if ((canal < dPWM_CANAL_MIN) || (canal > dPWM_CANAL_MAX))
	{
		return ePWM_RETURN_INVALID_ARGUMENT;
	}

	pwmConfig_t config = pwm.configs[canal - 1U];
	config.comparacao  = 0U;

	return Pwm_Aplica(canal, &config);
}

/***********************************************************************************************************************
 * @brief      Leva os tres canais ao estado seguro: PWM parado (razao ciclica 0).
 * @details    Tenta todos os canais mesmo que um falhe, para nao deixar um estimulo aplicado.
 **********************************************************************************************************************/
pwmReturn_t Pwm_SafeState(void)
{
	pwmReturn_t ret = ePWM_RETURN_OK;

	for (uint8_t canal = dPWM_CANAL_MIN; canal <= dPWM_CANAL_MAX; canal++)
	{
		if (Pwm_Stop(canal) != ePWM_RETURN_OK)
		{
			ret = ePWM_RETURN_ERROR;
		}
	}
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Aplica a configuracao pela BSP e guarda como a atual do canal.
 * @details    So guarda se a BSP aplicou: em falha, o Pwm_Stop seguinte usa a ultima configuracao que valeu.
 * @param[in]  canal   1 a 3 (ja validado)
 * @param[in]  config  Valores dos registradores
 * @return     O retorno da BSP
 **********************************************************************************************************************/
static pwmReturn_t Pwm_Aplica(uint8_t canal, const pwmConfig_t *config)
{
	pwmReturn_t ret = pwm.func.apply(canal, config);

	if (ret == ePWM_RETURN_OK)
	{
		pwm.configs[canal - 1U] = *config;
	}

	return ret;
}

/** @} DOXYGEN GROUP TAG END OF FILE */
