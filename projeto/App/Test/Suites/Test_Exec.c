/*
 * Test_Exec.c
 *
 * Testes do Exec com DAC, GPIO, ADC e timer falsos: conferem o resultado (ACK/NACK) e, principalmente, se o
 * periferico foi ou nao acionado ("nenhuma saida alterada" do CT09), sem multimetro.
 */

#include <string.h>
#include "unity.h"
#include "Exec.h"
#include "AnalogOut.h"
#include "AnalogIn.h"
#include "DigitalIo.h"
#include "Pwm.h"

/// Payload do PWM_SET: canal + frequencia (u32) + razao ciclica (u16)
#define dEXEC_TESTE_PWM_SET_SIZE (7U)

/// Registro das chamadas ao DAC falso
static struct dacFalso
{
	uint8_t           chamadas;
	uint8_t           canal[4];
	uint16_t          valor[4];
	analogOutReturn_t retorno;   ///< o que o DAC falso devolve (simula falha do HAL)
} dacFalso;

static analogOutReturn_t DacFalso_Set(uint8_t canal, uint16_t valor)
{
	if (dacFalso.chamadas < 4U)
	{
		dacFalso.canal[dacFalso.chamadas] = canal;
		dacFalso.valor[dacFalso.chamadas] = valor;
	}
	dacFalso.chamadas++;

	return dacFalso.retorno;
}

/// Registro das chamadas ao GPIO falso
static struct gpioFalso
{
	uint8_t           escritas;
	uint8_t           saida[8];
	uint8_t           nivel[8];
	uint8_t           leituras;
	uint8_t           entradas[dDIGITALIO_ENTRADA_MAX + 1U];
} gpioFalso;

static digitalIoReturn_t GpioFalso_Write(uint8_t saida, uint8_t nivel)
{
	if (gpioFalso.escritas < 8U)
	{
		gpioFalso.saida[gpioFalso.escritas] = saida;
		gpioFalso.nivel[gpioFalso.escritas] = nivel;
	}
	gpioFalso.escritas++;

	return eDIGITALIO_RETURN_OK;
}

static digitalIoReturn_t GpioFalso_Read(uint8_t entrada, uint8_t *nivel)
{
	gpioFalso.leituras++;
	*nivel = gpioFalso.entradas[entrada];

	return eDIGITALIO_RETURN_OK;
}

/// Registro das chamadas ao ADC falso
static struct adcFalso
{
	uint8_t          chamadas;
	uint8_t          canal;
	uint16_t         valor;     ///< o que o ADC falso "converte"
	analogInReturn_t retorno;   ///< simula falha do HAL
} adcFalso;

static analogInReturn_t AdcFalso_Read(uint8_t canal, uint16_t *valor)
{
	adcFalso.chamadas++;
	adcFalso.canal = canal;
	*valor = adcFalso.valor;

	return adcFalso.retorno;
}

/// Timers da placa (os mesmos do Test_Pwm.c): TIM1 216 MHz 16 bits, TIM2 108 MHz 32 bits, TIM3 108 MHz 16 bits
static const pwmTimer_t timersExec[dPWM_CANAL_MAX] =
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

// Os modulos de periferico passam a usar os falsos. Na build Test nada mais usa o hardware, entao nao precisa restaurar.
static void IniciaExec(void)
{
	memset(&dacFalso, 0, sizeof(dacFalso));
	memset(&gpioFalso, 0, sizeof(gpioFalso));
	memset(&adcFalso, 0, sizeof(adcFalso));
	dacFalso.retorno = eANALOGOUT_RETURN_OK;
	adcFalso.retorno = eANALOGIN_RETURN_OK;
	AnalogOut_Init(DacFalso_Set);
	DigitalIO_Init(GpioFalso_Write, GpioFalso_Read);
	AnalogIn_Init(AdcFalso_Read);

	memset(&timerFalso, 0, sizeof(timerFalso));
	timerFalso.retorno = ePWM_RETURN_OK;
	Pwm_Init(TimerFalso_Apply, timersExec);
}

static void MontaComando(commFrame_t *frame, uint8_t seq, uint8_t cmd, const uint8_t *payload, uint8_t size)
{
	memset(frame, 0, sizeof(commFrame_t));
	frame->seq  = seq;
	frame->cmd  = cmd;
	frame->size = size;
	if (size > 0U)
	{
		memcpy(frame->payload, payload, size);
	}
}

// 1. DAC_SET valido: ACK e o DAC recebe canal e valor (u16 little-endian)
void Test_Exec_DacSetValido(void)
{
	const uint8_t payload[] = {0x01, 0x00, 0x08};   // canal 1, valor 0x0800 = 2048
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x05, dCOMM_CMD_DAC_SET, payload, sizeof(payload));

	TEST_ASSERT_EQUAL(eEXEC_RETURN_OK, Exec_Process(&frame, &result));
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0x05, result.seq);
	TEST_ASSERT_EQUAL_HEX8(dCOMM_CMD_DAC_SET, result.cmd);
	TEST_ASSERT_EQUAL_UINT8(0, result.size);
	TEST_ASSERT_EQUAL_UINT8(1, dacFalso.chamadas);
	TEST_ASSERT_EQUAL_UINT8(1, dacFalso.canal[0]);
	TEST_ASSERT_EQUAL_UINT16(2048, dacFalso.valor[0]);
}

// 2. Limites aceitos: canal 2 e valor 4095
void Test_Exec_DacSetLimites(void)
{
	const uint8_t payload[] = {0x02, 0xFF, 0x0F};   // canal 2, valor 0x0FFF = 4095
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x06, dCOMM_CMD_DAC_SET, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(2, dacFalso.canal[0]);
	TEST_ASSERT_EQUAL_UINT16(4095, dacFalso.valor[0]);
}

// 3. Canal inexistente: NACK PARAMETRO_INVALIDO e o DAC nao e acionado
void Test_Exec_DacSetCanalInvalido(void)
{
	const uint8_t payload[] = {0x03, 0x00, 0x08};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x07, dCOMM_CMD_DAC_SET, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, dacFalso.chamadas);
}

// 4. Valor acima de 4095: NACK PARAMETRO_INVALIDO e o DAC nao e acionado
void Test_Exec_DacSetValorInvalido(void)
{
	const uint8_t payload[] = {0x01, 0x00, 0x10};   // 0x1000 = 4096
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x08, dCOMM_CMD_DAC_SET, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, dacFalso.chamadas);
}

// 5. Payload com tamanho errado: NACK PARAMETRO_INVALIDO e o DAC nao e acionado
void Test_Exec_DacSetTamanhoErrado(void)
{
	const uint8_t payload[] = {0x01, 0x00};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x09, dCOMM_CMD_DAC_SET, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, dacFalso.chamadas);
}

// 6. RESET_ALL: ACK, DAC em 0, GPIO1-4 em nivel baixo e os tres PWM parados
void Test_Exec_ResetAll(void)
{
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x0A, dCOMM_CMD_RESET_ALL, NULL, 0);

	TEST_ASSERT_EQUAL(eEXEC_RETURN_OK, Exec_Process(&frame, &result));
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(2, dacFalso.chamadas);
	TEST_ASSERT_EQUAL_UINT8(1, dacFalso.canal[0]);
	TEST_ASSERT_EQUAL_UINT16(0, dacFalso.valor[0]);
	TEST_ASSERT_EQUAL_UINT8(2, dacFalso.canal[1]);
	TEST_ASSERT_EQUAL_UINT16(0, dacFalso.valor[1]);

	TEST_ASSERT_EQUAL_UINT8(4, gpioFalso.escritas);
	for (uint8_t i = 0; i < 4U; i++)
	{
		TEST_ASSERT_EQUAL_UINT8(i + 1U, gpioFalso.saida[i]);
		TEST_ASSERT_EQUAL_UINT8(0, gpioFalso.nivel[i]);
	}

	TEST_ASSERT_EQUAL_UINT8(3, timerFalso.chamadas);
	for (uint8_t i = 0; i < 3U; i++)
	{
		TEST_ASSERT_EQUAL_UINT8(i + 1U, timerFalso.canal[i]);
		TEST_ASSERT_EQUAL_UINT32(0, timerFalso.config[i].comparacao);
	}
}

// 7. RESET_ALL com payload: NACK PARAMETRO_INVALIDO e nada e acionado
void Test_Exec_ResetAllComPayload(void)
{
	const uint8_t payload[] = {0x00};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x0B, dCOMM_CMD_RESET_ALL, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, dacFalso.chamadas);
	TEST_ASSERT_EQUAL_UINT8(0, gpioFalso.escritas);
	TEST_ASSERT_EQUAL_UINT8(0, timerFalso.chamadas);
}

// 8. Comando desconhecido: NACK CMD_DESCONHECIDO e nada e acionado
void Test_Exec_CmdDesconhecido(void)
{
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x0C, 0x55, NULL, 0);

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_CMD_DESCONHECIDO, result.erro);
	TEST_ASSERT_EQUAL_HEX8(0x55, result.cmd);
	TEST_ASSERT_EQUAL_UINT8(0, dacFalso.chamadas);
}

// 9. Falha do periferico (HAL): NACK ERRO_PERIFERICO
void Test_Exec_DacSetFalhaPeriferico(void)
{
	const uint8_t payload[] = {0x01, 0x00, 0x08};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	dacFalso.retorno = eANALOGOUT_RETURN_ERROR;
	MontaComando(&frame, 0x0D, dCOMM_CMD_DAC_SET, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PERIFERICO, result.erro);
}

// 10. Argumentos nulos
void Test_Exec_ArgumentosInvalidos(void)
{
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x0E, dCOMM_CMD_RESET_ALL, NULL, 0);

	TEST_ASSERT_EQUAL(eEXEC_RETURN_INVALID_ARGUMENT, Exec_Process(NULL, &result));
	TEST_ASSERT_EQUAL(eEXEC_RETURN_INVALID_ARGUMENT, Exec_Process(&frame, NULL));
}

// 11. GPIO_SET valido: ACK sem dados e o pino recebe saida e nivel
void Test_Exec_GpioSetValido(void)
{
	const uint8_t payload[] = {0x02, 0x01};   // GPIO2 em alto
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x10, dCOMM_CMD_GPIO_SET, payload, sizeof(payload));

	TEST_ASSERT_EQUAL(eEXEC_RETURN_OK, Exec_Process(&frame, &result));
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_HEX8(dCOMM_CMD_GPIO_SET, result.cmd);
	TEST_ASSERT_EQUAL_UINT8(0, result.size);
	TEST_ASSERT_EQUAL_UINT8(1, gpioFalso.escritas);
	TEST_ASSERT_EQUAL_UINT8(2, gpioFalso.saida[0]);
	TEST_ASSERT_EQUAL_UINT8(1, gpioFalso.nivel[0]);
}

// 12. GPIO_SET com pino ou nivel fora da faixa: NACK PARAMETRO_INVALIDO e nenhum pino e tocado
void Test_Exec_GpioSetParametroInvalido(void)
{
	const uint8_t pinoInvalido[]  = {0x05, 0x01};
	const uint8_t nivelInvalido[] = {0x01, 0x02};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();

	MontaComando(&frame, 0x11, dCOMM_CMD_GPIO_SET, pinoInvalido, sizeof(pinoInvalido));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	MontaComando(&frame, 0x12, dCOMM_CMD_GPIO_SET, nivelInvalido, sizeof(nivelInvalido));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	TEST_ASSERT_EQUAL_UINT8(0, gpioFalso.escritas);
}

// 13. GPIO_SET com tamanho errado: NACK PARAMETRO_INVALIDO e nenhum pino e tocado
void Test_Exec_GpioSetTamanhoErrado(void)
{
	const uint8_t payload[] = {0x01};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x13, dCOMM_CMD_GPIO_SET, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, gpioFalso.escritas);
}

// 14. GPIO_READ: ACK com 1 byte; GPIO5 e GPIO8 em alto -> mascara 0x09
void Test_Exec_GpioReadValido(void)
{
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	gpioFalso.entradas[1] = 1;
	gpioFalso.entradas[4] = 1;
	MontaComando(&frame, 0x14, dCOMM_CMD_GPIO_READ, NULL, 0);

	TEST_ASSERT_EQUAL(eEXEC_RETURN_OK, Exec_Process(&frame, &result));
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(1, result.size);
	TEST_ASSERT_EQUAL_HEX8(0x09, result.dados[0]);
}

// 15. GPIO_READ com payload: NACK PARAMETRO_INVALIDO, sem dados e sem ler os pinos
void Test_Exec_GpioReadComPayload(void)
{
	const uint8_t payload[] = {0x00};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x15, dCOMM_CMD_GPIO_READ, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, result.size);
	TEST_ASSERT_EQUAL_UINT8(0, gpioFalso.leituras);
}

// 16. ADC_READ valido: ACK [canal][valor LE]; canal 0 e 2047 = 00 FF 07 (exemplo do Apendice A)
void Test_Exec_AdcReadValido(void)
{
	const uint8_t payload[]  = {0x00};
	const uint8_t esperado[] = {0x00, 0xFF, 0x07};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	adcFalso.valor = 2047;
	MontaComando(&frame, 0x16, dCOMM_CMD_ADC_READ, payload, sizeof(payload));

	TEST_ASSERT_EQUAL(eEXEC_RETURN_OK, Exec_Process(&frame, &result));
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(3, result.size);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(esperado, result.dados, sizeof(esperado));
	TEST_ASSERT_EQUAL_UINT8(0, adcFalso.canal);
}

// 17. ADC_READ com canal inexistente: NACK PARAMETRO_INVALIDO, sem dados e sem acionar o ADC
void Test_Exec_AdcReadCanalInvalido(void)
{
	const uint8_t payload[] = {0x03};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x17, dCOMM_CMD_ADC_READ, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, result.size);
	TEST_ASSERT_EQUAL_UINT8(0, adcFalso.chamadas);
}

// 18. ADC_READ com tamanho errado: NACK PARAMETRO_INVALIDO e sem acionar o ADC
void Test_Exec_AdcReadTamanhoErrado(void)
{
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x18, dCOMM_CMD_ADC_READ, NULL, 0);

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, adcFalso.chamadas);
}

// 19. ADC_READ com falha do periferico: NACK ERRO_PERIFERICO e sem dados
void Test_Exec_AdcReadFalhaPeriferico(void)
{
	const uint8_t payload[] = {0x01};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	adcFalso.retorno = eANALOGIN_RETURN_ERROR;
	MontaComando(&frame, 0x19, dCOMM_CMD_ADC_READ, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PERIFERICO, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, result.size);
}

// 20. PWM_SET com o exemplo do Apendice A: canal 1, 1 kHz (u32 LE), 50 % (u16 LE) -> PSC 3, ARR 53999, CCR 27000
void Test_Exec_PwmSetValido(void)
{
	const uint8_t payload[] = {0x01, 0xE8, 0x03, 0x00, 0x00, 0x88, 0x13};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x20, dCOMM_CMD_PWM_SET, payload, sizeof(payload));

	TEST_ASSERT_EQUAL(eEXEC_RETURN_OK, Exec_Process(&frame, &result));
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, result.size);
	TEST_ASSERT_EQUAL_UINT8(1, timerFalso.chamadas);
	TEST_ASSERT_EQUAL_UINT8(1, timerFalso.canal[0]);
	TEST_ASSERT_EQUAL_UINT32(3, timerFalso.config[0].prescaler);
	TEST_ASSERT_EQUAL_UINT32(53999, timerFalso.config[0].periodo);
	TEST_ASSERT_EQUAL_UINT32(27000, timerFalso.config[0].comparacao);
}

// 21. Frequencia usando 3 bytes do u32 (100 kHz = A0 86 01 00) no canal 2 (32 bits), 25 % (C4 09)
void Test_Exec_PwmSetFrequenciaU32(void)
{
	const uint8_t payload[] = {0x02, 0xA0, 0x86, 0x01, 0x00, 0xC4, 0x09};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x21, dCOMM_CMD_PWM_SET, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(2, timerFalso.canal[0]);
	TEST_ASSERT_EQUAL_UINT32(0, timerFalso.config[0].prescaler);
	TEST_ASSERT_EQUAL_UINT32(1079, timerFalso.config[0].periodo);
	TEST_ASSERT_EQUAL_UINT32(270, timerFalso.config[0].comparacao);
}

// 22. PWM_SET fora da faixa (canal 4, frequencia 0 e 100001, razao 10001): NACK e nenhum timer tocado
void Test_Exec_PwmSetParametroInvalido(void)
{
	const uint8_t canal4[]        = {0x04, 0xE8, 0x03, 0x00, 0x00, 0x88, 0x13};
	const uint8_t frequencia0[]   = {0x01, 0x00, 0x00, 0x00, 0x00, 0x88, 0x13};
	const uint8_t frequenciaMax[] = {0x01, 0xA1, 0x86, 0x01, 0x00, 0x88, 0x13};   // 100001 Hz
	const uint8_t razaoMax[]      = {0x01, 0xE8, 0x03, 0x00, 0x00, 0x11, 0x27};   // 10001
	const uint8_t *payloads[]     = {canal4, frequencia0, frequenciaMax, razaoMax};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();

	for (uint8_t i = 0; i < 4U; i++)
	{
		MontaComando(&frame, (uint8_t)(0x22 + i), dCOMM_CMD_PWM_SET, payloads[i], dEXEC_TESTE_PWM_SET_SIZE);
		Exec_Process(&frame, &result);
		TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);
	}
	TEST_ASSERT_EQUAL_UINT8(0, timerFalso.chamadas);
}

// 23. PWM_SET com tamanho errado (6 e 3 bytes): NACK PARAMETRO_INVALIDO e nenhum timer tocado
void Test_Exec_PwmSetTamanhoErrado(void)
{
	const uint8_t payload[] = {0x01, 0xE8, 0x03, 0x00, 0x00, 0x88};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();

	MontaComando(&frame, 0x26, dCOMM_CMD_PWM_SET, payload, sizeof(payload));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	MontaComando(&frame, 0x2B, dCOMM_CMD_PWM_SET, payload, 3U);   // tamanho do DAC_SET
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	TEST_ASSERT_EQUAL_UINT8(0, timerFalso.chamadas);
}

// 24. PWM_STOP: ACK e o canal fica com razao ciclica 0 (mantendo a frequencia)
void Test_Exec_PwmStopValido(void)
{
	const uint8_t payload[] = {0x03};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x27, dCOMM_CMD_PWM_STOP, payload, sizeof(payload));

	TEST_ASSERT_EQUAL(eEXEC_RETURN_OK, Exec_Process(&frame, &result));
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(1, timerFalso.chamadas);
	TEST_ASSERT_EQUAL_UINT8(3, timerFalso.canal[0]);
	TEST_ASSERT_EQUAL_UINT32(0, timerFalso.config[0].comparacao);
}

// 25. PWM_STOP com canal 0 ou sem payload: NACK PARAMETRO_INVALIDO e nenhum timer tocado
void Test_Exec_PwmStopInvalido(void)
{
	const uint8_t canal0[] = {0x00};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();

	MontaComando(&frame, 0x28, dCOMM_CMD_PWM_STOP, canal0, sizeof(canal0));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	MontaComando(&frame, 0x29, dCOMM_CMD_PWM_STOP, NULL, 0);
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	TEST_ASSERT_EQUAL_UINT8(0, timerFalso.chamadas);
}

// 26. PWM_SET com falha do periferico: NACK ERRO_PERIFERICO
void Test_Exec_PwmSetFalhaPeriferico(void)
{
	const uint8_t payload[] = {0x01, 0xE8, 0x03, 0x00, 0x00, 0x88, 0x13};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	timerFalso.retorno = ePWM_RETURN_ERROR;
	MontaComando(&frame, 0x2A, dCOMM_CMD_PWM_SET, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PERIFERICO, result.erro);
}
