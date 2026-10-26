/*
 * Test_Pwm.c
 *
 * Testes do Pwm com um timer falso: calculo de PSC/ARR/CCR (com os clocks da placa), validacao dos parametros
 * (sem tocar no timer), parada e estado seguro. Os vetores de calculo sao os mesmos do protocolo.py.
 */

#include <string.h>
#include "unity.h"
#include "Pwm.h"

/// Timers da placa: TIM1 a 216 MHz (16 bits), TIM2 a 108 MHz (32 bits), TIM3 a 108 MHz (16 bits)
static const pwmTimer_t timersPlaca[dPWM_CANAL_MAX] =
{
	{216000000U, 0xFFFFU},
	{108000000U, 0xFFFFFFFFU},
	{108000000U, 0xFFFFU},
};

/// Registro das chamadas ao timer falso
static struct timerFalso
{
	uint8_t     chamadas;
	uint8_t     canal[4];
	pwmConfig_t config[4];
	pwmReturn_t retorno;   ///< simula falha do HAL
} timerFalso;

static pwmReturn_t TimerFalso_Apply(uint8_t canal, const pwmConfig_t *config)
{
	if (timerFalso.chamadas < 4U)
	{
		timerFalso.canal[timerFalso.chamadas]  = canal;
		timerFalso.config[timerFalso.chamadas] = *config;
	}
	timerFalso.chamadas++;

	return timerFalso.retorno;
}

// O Pwm passa a usar o timer falso. Na build Test nada mais usa os timers, entao nao precisa restaurar.
static void IniciaPwm(void)
{
	memset(&timerFalso, 0, sizeof(timerFalso));
	timerFalso.retorno = ePWM_RETURN_OK;
	Pwm_Init(TimerFalso_Apply, timersPlaca);
}

static void ConfereConfig(uint32_t prescaler, uint32_t periodo, uint32_t comparacao, const pwmConfig_t *config)
{
	TEST_ASSERT_EQUAL_UINT32(prescaler, config->prescaler);
	TEST_ASSERT_EQUAL_UINT32(periodo, config->periodo);
	TEST_ASSERT_EQUAL_UINT32(comparacao, config->comparacao);
}

// 1. 1 kHz, 50 % no TIM1: 216000 contagens nao cabem em 16 bits -> PSC 3, ARR 53999 (exato)
void Test_Pwm_Calcula1kHzTim1(void)
{
	pwmConfig_t config;

	TEST_ASSERT_EQUAL(ePWM_RETURN_OK, Pwm_Calcula(&timersPlaca[0], 1000U, 5000U, &config));
	ConfereConfig(3U, 53999U, 27000U, &config);
}

// 2. 100 kHz no TIM3 (25 %) e no TIM1 (90 %): PSC 0, periodo de 1080 e 2160 contagens
void Test_Pwm_Calcula100kHz(void)
{
	pwmConfig_t config;

	TEST_ASSERT_EQUAL(ePWM_RETURN_OK, Pwm_Calcula(&timersPlaca[2], 100000U, 2500U, &config));
	ConfereConfig(0U, 1079U, 270U, &config);

	TEST_ASSERT_EQUAL(ePWM_RETURN_OK, Pwm_Calcula(&timersPlaca[0], 100000U, 9000U, &config));
	ConfereConfig(0U, 2159U, 1944U, &config);
}

// 3. 1 Hz: nos timers de 16 bits precisa de PSC grande; no TIM2 (32 bits) o PSC fica 0
void Test_Pwm_Calcula1Hz(void)
{
	pwmConfig_t config;

	TEST_ASSERT_EQUAL(ePWM_RETURN_OK, Pwm_Calcula(&timersPlaca[0], 1U, 5000U, &config));
	ConfereConfig(3295U, 65533U, 32767U, &config);

	TEST_ASSERT_EQUAL(ePWM_RETURN_OK, Pwm_Calcula(&timersPlaca[1], 1U, 5000U, &config));
	ConfereConfig(0U, 107999999U, 54000000U, &config);

	TEST_ASSERT_EQUAL(ePWM_RETURN_OK, Pwm_Calcula(&timersPlaca[2], 1U, 5000U, &config));
	ConfereConfig(1647U, 65533U, 32767U, &config);
}

// 4. 50 Hz no TIM3 (servo) e 20 kHz no TIM2 (motor): frequencia nao exata usa a mais proxima
void Test_Pwm_CalculaFrequenciasTipicas(void)
{
	pwmConfig_t config;

	TEST_ASSERT_EQUAL(ePWM_RETURN_OK, Pwm_Calcula(&timersPlaca[2], 50U, 5000U, &config));
	ConfereConfig(32U, 65454U, 32728U, &config);   // 49,9997 Hz

	TEST_ASSERT_EQUAL(ePWM_RETURN_OK, Pwm_Calcula(&timersPlaca[1], 20000U, 1234U, &config));
	ConfereConfig(0U, 5399U, 666U, &config);
}

// 5. Razao ciclica 0 (sempre baixo), 10000 (CCR = ARR + 1, sempre alto) e 1 (0,01 %)
void Test_Pwm_CalculaRazaoLimites(void)
{
	pwmConfig_t config;

	TEST_ASSERT_EQUAL(ePWM_RETURN_OK, Pwm_Calcula(&timersPlaca[0], 1000U, 0U, &config));
	TEST_ASSERT_EQUAL_UINT32(0U, config.comparacao);

	TEST_ASSERT_EQUAL(ePWM_RETURN_OK, Pwm_Calcula(&timersPlaca[0], 1000U, 10000U, &config));
	TEST_ASSERT_EQUAL_UINT32(config.periodo + 1U, config.comparacao);

	TEST_ASSERT_EQUAL(ePWM_RETURN_OK, Pwm_Calcula(&timersPlaca[0], 1000U, 1U, &config));
	TEST_ASSERT_EQUAL_UINT32(5U, config.comparacao);
}

// 6. Fora da faixa: frequencia 0 e 100001, razao 10001 -> recusado e a config nao muda
void Test_Pwm_CalculaForaDaFaixa(void)
{
	pwmConfig_t config = {111U, 222U, 333U};

	TEST_ASSERT_EQUAL(ePWM_RETURN_INVALID_ARGUMENT, Pwm_Calcula(&timersPlaca[0], 0U, 5000U, &config));
	TEST_ASSERT_EQUAL(ePWM_RETURN_INVALID_ARGUMENT, Pwm_Calcula(&timersPlaca[0], 100001U, 5000U, &config));
	TEST_ASSERT_EQUAL(ePWM_RETURN_INVALID_ARGUMENT, Pwm_Calcula(&timersPlaca[0], 1000U, 10001U, &config));
	ConfereConfig(111U, 222U, 333U, &config);
}

// 7. Set valido: o timer do canal recebe a configuracao calculada
void Test_Pwm_SetValido(void)
{
	IniciaPwm();

	TEST_ASSERT_EQUAL(ePWM_RETURN_OK, Pwm_Set(1U, 1000U, 5000U));
	TEST_ASSERT_EQUAL_UINT8(1, timerFalso.chamadas);
	TEST_ASSERT_EQUAL_UINT8(1, timerFalso.canal[0]);
	ConfereConfig(3U, 53999U, 27000U, &timerFalso.config[0]);
}

// 8. Canal 0 e 4, frequencia e razao invalidas: recusados sem tocar no timer
void Test_Pwm_SetInvalido(void)
{
	IniciaPwm();

	TEST_ASSERT_EQUAL(ePWM_RETURN_INVALID_ARGUMENT, Pwm_Set(0U, 1000U, 5000U));
	TEST_ASSERT_EQUAL(ePWM_RETURN_INVALID_ARGUMENT, Pwm_Set(4U, 1000U, 5000U));
	TEST_ASSERT_EQUAL(ePWM_RETURN_INVALID_ARGUMENT, Pwm_Set(1U, 0U, 5000U));
	TEST_ASSERT_EQUAL(ePWM_RETURN_INVALID_ARGUMENT, Pwm_Set(1U, 1000U, 10001U));
	TEST_ASSERT_EQUAL_UINT8(0, timerFalso.chamadas);
}

// 9. Stop: mantem a frequencia do ultimo Set e zera a razao ciclica
void Test_Pwm_StopMantemFrequencia(void)
{
	IniciaPwm();
	Pwm_Set(3U, 50U, 7500U);

	TEST_ASSERT_EQUAL(ePWM_RETURN_OK, Pwm_Stop(3U));
	TEST_ASSERT_EQUAL_UINT8(2, timerFalso.chamadas);
	TEST_ASSERT_EQUAL_UINT8(3, timerFalso.canal[1]);
	ConfereConfig(32U, 65454U, 0U, &timerFalso.config[1]);
	TEST_ASSERT_EQUAL(ePWM_RETURN_INVALID_ARGUMENT, Pwm_Stop(4U));
}

// 10. Estado seguro: os tres canais em 1 kHz (padrao do boot) com razao 0
void Test_Pwm_SafeState(void)
{
	IniciaPwm();

	TEST_ASSERT_EQUAL(ePWM_RETURN_OK, Pwm_SafeState());
	TEST_ASSERT_EQUAL_UINT8(3, timerFalso.chamadas);
	ConfereConfig(3U, 53999U, 0U, &timerFalso.config[0]);    // TIM1: 216 MHz
	ConfereConfig(0U, 107999U, 0U, &timerFalso.config[1]);   // TIM2: 108 MHz, 32 bits
	ConfereConfig(1U, 53999U, 0U, &timerFalso.config[2]);    // TIM3: 108 MHz, 16 bits
}

// 11. Estado seguro com falha: continua nos outros canais e informa o erro
void Test_Pwm_SafeStateComFalha(void)
{
	IniciaPwm();
	timerFalso.retorno = ePWM_RETURN_ERROR;

	TEST_ASSERT_EQUAL(ePWM_RETURN_ERROR, Pwm_SafeState());
	TEST_ASSERT_EQUAL_UINT8(3, timerFalso.chamadas);
}

// 12. Falha do HAL no Set: a configuracao nao vira a atual (o Stop seguinte usa a anterior)
void Test_Pwm_FalhaNaoGuardaConfig(void)
{
	IniciaPwm();
	timerFalso.retorno = ePWM_RETURN_ERROR;
	TEST_ASSERT_EQUAL(ePWM_RETURN_ERROR, Pwm_Set(1U, 100000U, 5000U));

	timerFalso.retorno = ePWM_RETURN_OK;
	Pwm_Stop(1U);
	ConfereConfig(3U, 53999U, 0U, &timerFalso.config[1]);   // continua no 1 kHz do boot
}

// 13. Argumentos nulos e timer sem clock
void Test_Pwm_ArgumentosInvalidos(void)
{
	const pwmTimer_t semClock[dPWM_CANAL_MAX] = {{0U, 0xFFFFU}, {108000000U, 0xFFFFU}, {108000000U, 0xFFFFU}};
	pwmConfig_t config;

	TEST_ASSERT_EQUAL(ePWM_RETURN_INVALID_ARGUMENT, Pwm_Init(NULL, timersPlaca));
	TEST_ASSERT_EQUAL(ePWM_RETURN_INVALID_ARGUMENT, Pwm_Init(TimerFalso_Apply, NULL));
	TEST_ASSERT_EQUAL(ePWM_RETURN_INVALID_ARGUMENT, Pwm_Init(TimerFalso_Apply, semClock));
	TEST_ASSERT_EQUAL(ePWM_RETURN_INVALID_ARGUMENT, Pwm_Calcula(NULL, 1000U, 5000U, &config));
	TEST_ASSERT_EQUAL(ePWM_RETURN_INVALID_ARGUMENT, Pwm_Calcula(&timersPlaca[0], 1000U, 5000U, NULL));
}
