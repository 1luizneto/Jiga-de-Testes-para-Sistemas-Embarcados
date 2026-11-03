/*
 * Test_Exec.c
 *
 * Testes do Exec com DAC, GPIO, ADC, timer, barramentos I2C e SPI e UART falsos: conferem o resultado (ACK/NACK) e, principalmente, se o
 * periferico foi ou nao acionado ("nenhuma saida alterada" do CT09), sem multimetro.
 */

#include <string.h>
#include "unity.h"
#include "Exec.h"
#include "AnalogOut.h"
#include "AnalogIn.h"
#include "DigitalIo.h"
#include "Pwm.h"
#include "I2cBus.h"
#include "SpiBus.h"
#include "UartBus.h"

/// Payload do PWM_SET: canal + frequencia (u32) + razao ciclica (u16)
#define dEXEC_TESTE_PWM_SET_SIZE (7U)

/// Clock do SPI2 na placa (PCLK1)
#define dEXEC_TESTE_SPI_CLOCK_HZ (54000000U)

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

/// Registro das chamadas ao barramento I2C falso
static struct i2cFalso
{
	uint8_t        configChamadas;
	uint8_t        offChamadas;
	uint8_t        xferChamadas;
	uint8_t        papel;
	uint8_t        endereco;
	uint8_t        velocidade;
	uint8_t        xferEndereco;
	uint8_t        xferEscrita;   ///< 1o byte escrito
	uint8_t        xferNw;
	uint8_t        xferNr;
	i2cBusReturn_t retornoXfer;   ///< simula falta de ACK do dispositivo
} i2cFalso;

static i2cBusReturn_t I2cFalso_Config(uint8_t papel, uint8_t endereco, uint8_t velocidade)
{
	i2cFalso.configChamadas++;
	i2cFalso.papel      = papel;
	i2cFalso.endereco   = endereco;
	i2cFalso.velocidade = velocidade;

	return eI2CBUS_RETURN_OK;
}

static i2cBusReturn_t I2cFalso_Off(void)
{
	i2cFalso.offChamadas++;

	return eI2CBUS_RETURN_OK;
}

// Le 0xA0, 0xA1, ... do dispositivo
static i2cBusReturn_t I2cFalso_Xfer(uint8_t endereco, const uint8_t *escrita, uint8_t nw, uint8_t *leitura, uint8_t nr)
{
	i2cFalso.xferChamadas++;
	i2cFalso.xferEndereco = endereco;
	i2cFalso.xferEscrita  = (nw > 0U) ? escrita[0] : 0U;
	i2cFalso.xferNw       = nw;
	i2cFalso.xferNr       = nr;

	for (uint8_t i = 0U; i < nr; i++)
	{
		leitura[i] = (uint8_t)(0xA0U + i);
	}

	return i2cFalso.retornoXfer;
}

static void I2cFalso_Lock(void)
{
}

static void I2cFalso_Unlock(void)
{
}

static const i2cBusFunc_t i2cFalsoFunc =
{
	I2cFalso_Config, I2cFalso_Off, I2cFalso_Xfer, I2cFalso_Lock, I2cFalso_Unlock,
};

/// Registro das chamadas ao barramento SPI falso
static struct spiFalso
{
	uint8_t        configChamadas;
	uint8_t        offChamadas;
	uint8_t        xferChamadas;
	uint8_t        papel;
	uint8_t        modo;
	uint16_t       divisor;
	uint8_t        xferEscrita;   ///< 1o byte escrito
	uint8_t        xferQuantidade;
	spiBusReturn_t retornoXfer;   ///< simula falha do HAL
} spiFalso;

static spiBusReturn_t SpiFalso_Config(uint8_t papel, uint8_t modo, uint16_t divisor)
{
	spiFalso.configChamadas++;
	spiFalso.papel   = papel;
	spiFalso.modo    = modo;
	spiFalso.divisor = divisor;

	return eSPIBUS_RETURN_OK;
}

static spiBusReturn_t SpiFalso_Off(void)
{
	spiFalso.offChamadas++;

	return eSPIBUS_RETURN_OK;
}

// O DUT responde 0xB0, 0xB1, ...
static spiBusReturn_t SpiFalso_Xfer(const uint8_t *escrita, uint8_t *leitura, uint8_t quantidade)
{
	spiFalso.xferChamadas++;
	spiFalso.xferEscrita    = escrita[0];
	spiFalso.xferQuantidade = quantidade;

	for (uint8_t i = 0U; i < quantidade; i++)
	{
		leitura[i] = (uint8_t)(0xB0U + i);
	}

	return spiFalso.retornoXfer;
}

static void SpiFalso_Lock(void)
{
}

static void SpiFalso_Unlock(void)
{
}

static const spiBusFunc_t spiFalsoFunc =
{
	SpiFalso_Config, SpiFalso_Off, SpiFalso_Xfer, SpiFalso_Lock, SpiFalso_Unlock,
};

/// Registro das chamadas a UART falsa
static struct uartFalsa
{
	uint8_t         configChamadas;
	uint8_t         offChamadas;
	uint8_t         writeChamadas;
	uint32_t        baud;
	uint8_t         writeEscrita;   ///< 1o byte escrito
	uint8_t         writeQuantidade;
	uartBusReturn_t retornoWrite;   ///< simula falha ou timeout do HAL
} uartFalsa;

static uartBusReturn_t UartFalsa_Config(uint32_t baud)
{
	uartFalsa.configChamadas++;
	uartFalsa.baud = baud;

	return eUARTBUS_RETURN_OK;
}

static uartBusReturn_t UartFalsa_Off(void)
{
	uartFalsa.offChamadas++;

	return eUARTBUS_RETURN_OK;
}

static uartBusReturn_t UartFalsa_Write(const uint8_t *dados, uint8_t quantidade)
{
	uartFalsa.writeChamadas++;
	uartFalsa.writeEscrita    = dados[0];
	uartFalsa.writeQuantidade = quantidade;

	return uartFalsa.retornoWrite;
}

static void UartFalsa_Lock(void)
{
}

static void UartFalsa_Unlock(void)
{
}

static const uartBusFunc_t uartFalsaFunc =
{
	UartFalsa_Config, UartFalsa_Off, UartFalsa_Write, UartFalsa_Lock, UartFalsa_Unlock,
};

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

	memset(&i2cFalso, 0, sizeof(i2cFalso));
	i2cFalso.retornoXfer = eI2CBUS_RETURN_OK;
	I2cBus_Init(&i2cFalsoFunc);

	memset(&spiFalso, 0, sizeof(spiFalso));
	spiFalso.retornoXfer = eSPIBUS_RETURN_OK;
	SpiBus_Init(&spiFalsoFunc, dEXEC_TESTE_SPI_CLOCK_HZ);

	memset(&uartFalsa, 0, sizeof(uartFalsa));
	uartFalsa.retornoWrite = eUARTBUS_RETURN_OK;
	UartBus_Init(&uartFalsaFunc);
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

// Configura a UART em 115200 baud (0x0001C200)
static void ConfiguraUart(void)
{
	const uint8_t config[] = {0x00, 0xC2, 0x01, 0x00};
	commFrame_t frame;
	execResult_t result;
	MontaComando(&frame, 0x70, dCOMM_CMD_UART_CONFIG, config, sizeof(config));
	Exec_Process(&frame, &result);
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

// 6. RESET_ALL: ACK, DAC em 0, GPIO1-4 em nivel baixo, os tres PWM parados e o I2C, o SPI e a UART desligados
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

	TEST_ASSERT_EQUAL_UINT8(1, i2cFalso.offChamadas);
	TEST_ASSERT_EQUAL_UINT8(1, spiFalso.offChamadas);
	TEST_ASSERT_EQUAL_UINT8(1, uartFalsa.offChamadas);
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
	TEST_ASSERT_EQUAL_UINT8(0, i2cFalso.offChamadas);
	TEST_ASSERT_EQUAL_UINT8(0, spiFalso.offChamadas);
	TEST_ASSERT_EQUAL_UINT8(0, uartFalsa.offChamadas);
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

// 27. I2C_CONFIG como escravo no 0x50 a 100 kHz: ACK e o barramento recebe os parametros
void Test_Exec_I2cConfigValido(void)
{
	const uint8_t payload[] = {0x00, 0x50, 0x00};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x30, dCOMM_CMD_I2C_CONFIG, payload, sizeof(payload));

	TEST_ASSERT_EQUAL(eEXEC_RETURN_OK, Exec_Process(&frame, &result));
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, result.size);
	TEST_ASSERT_EQUAL_UINT8(1, i2cFalso.configChamadas);
	TEST_ASSERT_EQUAL_UINT8(dI2CBUS_PAPEL_ESCRAVO, i2cFalso.papel);
	TEST_ASSERT_EQUAL_HEX8(0x50, i2cFalso.endereco);
	TEST_ASSERT_EQUAL_UINT8(0, i2cFalso.velocidade);
}

// 28. I2C_CONFIG com endereco reservado ou tamanho errado: NACK PARAMETRO_INVALIDO e o I2C nao e tocado
void Test_Exec_I2cConfigInvalido(void)
{
	const uint8_t reservado[] = {0x00, 0x78, 0x00};
	const uint8_t curto[]     = {0x00, 0x50};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();

	MontaComando(&frame, 0x31, dCOMM_CMD_I2C_CONFIG, reservado, sizeof(reservado));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	MontaComando(&frame, 0x32, dCOMM_CMD_I2C_CONFIG, curto, sizeof(curto));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	TEST_ASSERT_EQUAL_UINT8(0, i2cFalso.configChamadas);
}

// 29. I2C_SLAVE_LOAD de 3 bytes em 0x10 e I2C_SLAVE_DUMP de volta: ACK com os mesmos bytes
void Test_Exec_I2cLoadDump(void)
{
	const uint8_t load[]     = {0x10, 0x03, 0x11, 0x22, 0x33};
	const uint8_t dump[]     = {0x10, 0x03};
	const uint8_t esperado[] = {0x11, 0x22, 0x33};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();

	MontaComando(&frame, 0x33, dCOMM_CMD_I2C_SLAVE_LOAD, load, sizeof(load));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, result.size);

	MontaComando(&frame, 0x34, dCOMM_CMD_I2C_SLAVE_DUMP, dump, sizeof(dump));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(3, result.size);
	TEST_ASSERT_EQUAL_UINT8_ARRAY(esperado, result.dados, 3);
}

// 30. I2C_SLAVE_LOAD com quantidade que nao bate com o payload, sem quantidade ou alem do mapa: NACK, mapa intacto
void Test_Exec_I2cLoadInvalido(void)
{
	const uint8_t faltaByte[] = {0x00, 0x03, 0xAA, 0xBB};   // diz 3, manda 2
	const uint8_t semQtd[]    = {0x00};
	const uint8_t alemMapa[]  = {0xFF, 0x02, 0xAA, 0xBB};   // 0xFF + 2 passa do fim do mapa
	const uint8_t dump[]      = {0x00, 0x02};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();

	MontaComando(&frame, 0x35, dCOMM_CMD_I2C_SLAVE_LOAD, faltaByte, sizeof(faltaByte));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	MontaComando(&frame, 0x36, dCOMM_CMD_I2C_SLAVE_LOAD, semQtd, sizeof(semQtd));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	MontaComando(&frame, 0x37, dCOMM_CMD_I2C_SLAVE_LOAD, alemMapa, sizeof(alemMapa));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	MontaComando(&frame, 0x38, dCOMM_CMD_I2C_SLAVE_DUMP, dump, sizeof(dump));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(0, result.dados[0]);
	TEST_ASSERT_EQUAL_UINT8(0, result.dados[1]);
}

// 31. I2C_SLAVE_DUMP de 241 bytes (nao cabe na resposta), alem do fim do mapa ou sem quantidade: NACK sem dados
void Test_Exec_I2cDumpInvalido(void)
{
	const uint8_t grande[]   = {0x00, 241};
	const uint8_t alemMapa[] = {0xFF, 0x02};
	const uint8_t semQtd[]   = {0x00};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();

	MontaComando(&frame, 0x39, dCOMM_CMD_I2C_SLAVE_DUMP, grande, sizeof(grande));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, result.size);

	MontaComando(&frame, 0x3A, dCOMM_CMD_I2C_SLAVE_DUMP, alemMapa, sizeof(alemMapa));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, result.size);

	MontaComando(&frame, 0x3B, dCOMM_CMD_I2C_SLAVE_DUMP, semQtd, sizeof(semQtd));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);
}

// 32. I2C_MASTER_XFER sem I2C_CONFIG de mestre: NACK NAO_CONFIGURADO e o barramento nao e tocado
void Test_Exec_I2cXferNaoConfigurado(void)
{
	const uint8_t payload[] = {0x68, 0x01, 0x02, 0x0F};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x3C, dCOMM_CMD_I2C_MASTER_XFER, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_NAO_CONFIGURADO, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, result.size);
	TEST_ASSERT_EQUAL_UINT8(0, i2cFalso.xferChamadas);
}

// 33. Como mestre, escreve o registrador 0x0F do 0x68 e le 2 bytes: ACK com os bytes lidos
void Test_Exec_I2cXferValido(void)
{
	const uint8_t config[]  = {0x01, 0x00, 0x01};
	const uint8_t payload[] = {0x68, 0x01, 0x02, 0x0F};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();

	MontaComando(&frame, 0x3D, dCOMM_CMD_I2C_CONFIG, config, sizeof(config));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);

	MontaComando(&frame, 0x3E, dCOMM_CMD_I2C_MASTER_XFER, payload, sizeof(payload));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(2, result.size);
	TEST_ASSERT_EQUAL_HEX8(0xA0, result.dados[0]);
	TEST_ASSERT_EQUAL_HEX8(0xA1, result.dados[1]);
	TEST_ASSERT_EQUAL_HEX8(0x68, i2cFalso.xferEndereco);
	TEST_ASSERT_EQUAL_HEX8(0x0F, i2cFalso.xferEscrita);
	TEST_ASSERT_EQUAL_UINT8(1, i2cFalso.xferNw);
	TEST_ASSERT_EQUAL_UINT8(2, i2cFalso.xferNr);
}

// 34. I2C_MASTER_XFER com nw que nao bate com o payload ou nr de 241: NACK e o barramento nao e tocado
void Test_Exec_I2cXferInvalido(void)
{
	const uint8_t config[]    = {0x01, 0x00, 0x00};
	const uint8_t faltaByte[] = {0x68, 0x02, 0x01, 0x0F};   // diz nw = 2, manda 1
	const uint8_t nrGrande[]  = {0x68, 0x00, 241};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x3F, dCOMM_CMD_I2C_CONFIG, config, sizeof(config));
	Exec_Process(&frame, &result);

	MontaComando(&frame, 0x40, dCOMM_CMD_I2C_MASTER_XFER, faltaByte, sizeof(faltaByte));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	MontaComando(&frame, 0x41, dCOMM_CMD_I2C_MASTER_XFER, nrGrande, sizeof(nrGrande));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	TEST_ASSERT_EQUAL_UINT8(0, i2cFalso.xferChamadas);
}

// 35. Dispositivo que nao responde (falta de ACK): NACK ERRO_PERIFERICO e sem dados
void Test_Exec_I2cXferSemAck(void)
{
	const uint8_t config[]  = {0x01, 0x00, 0x00};
	const uint8_t payload[] = {0x68, 0x00, 0x01};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x42, dCOMM_CMD_I2C_CONFIG, config, sizeof(config));
	Exec_Process(&frame, &result);
	i2cFalso.retornoXfer = eI2CBUS_RETURN_NACK;

	MontaComando(&frame, 0x43, dCOMM_CMD_I2C_MASTER_XFER, payload, sizeof(payload));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PERIFERICO, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, result.size);
}

// 36. RESET_ALL zera o mapa do escravo: um teste nao herda os registradores do anterior
void Test_Exec_ResetAllZeraMapaI2c(void)
{
	const uint8_t load[] = {0x00, 0x01, 0x5A};
	const uint8_t dump[] = {0x00, 0x01};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();

	MontaComando(&frame, 0x44, dCOMM_CMD_I2C_SLAVE_LOAD, load, sizeof(load));
	Exec_Process(&frame, &result);
	MontaComando(&frame, 0x45, dCOMM_CMD_RESET_ALL, NULL, 0);
	Exec_Process(&frame, &result);
	MontaComando(&frame, 0x46, dCOMM_CMD_I2C_SLAVE_DUMP, dump, sizeof(dump));
	Exec_Process(&frame, &result);

	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_HEX8(0x00, result.dados[0]);
}

// 37. SPI_CONFIG como mestre no modo 3 a 1 MHz: ACK e o SPI recebe o divisor 64 (54 MHz / 64 = 843 750 Hz <= 1 MHz)
void Test_Exec_SpiConfigMestre(void)
{
	const uint8_t payload[] = {0x01, 0x03, 0x40, 0x42, 0x0F, 0x00};   // 0x000F4240 = 1 000 000
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x47, dCOMM_CMD_SPI_CONFIG, payload, sizeof(payload));

	TEST_ASSERT_EQUAL(eEXEC_RETURN_OK, Exec_Process(&frame, &result));
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, result.size);
	TEST_ASSERT_EQUAL_UINT8(1, spiFalso.configChamadas);
	TEST_ASSERT_EQUAL_UINT8(dSPIBUS_PAPEL_MESTRE, spiFalso.papel);
	TEST_ASSERT_EQUAL_UINT8(3, spiFalso.modo);
	TEST_ASSERT_EQUAL_UINT16(64, spiFalso.divisor);
}

// 38. SPI_CONFIG com modo 4, mestre abaixo de 210 938 Hz ou tamanho errado: NACK PARAMETRO_INVALIDO e o SPI nao e tocado
void Test_Exec_SpiConfigInvalido(void)
{
	const uint8_t modo4[]  = {0x00, 0x04, 0x00, 0x00, 0x00, 0x00};
	const uint8_t lento[]  = {0x01, 0x00, 0xA0, 0x86, 0x01, 0x00};   // 100 000 Hz
	const uint8_t curto[]  = {0x00, 0x00, 0x00, 0x00, 0x00};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();

	MontaComando(&frame, 0x48, dCOMM_CMD_SPI_CONFIG, modo4, sizeof(modo4));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	MontaComando(&frame, 0x49, dCOMM_CMD_SPI_CONFIG, lento, sizeof(lento));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	MontaComando(&frame, 0x4A, dCOMM_CMD_SPI_CONFIG, curto, sizeof(curto));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	TEST_ASSERT_EQUAL_UINT8(0, spiFalso.configChamadas);
}

// 39. SPI_SLAVE_LOAD de 3 bytes (sem SPI_CONFIG): ACK e a proxima transacao envia os 3 bytes seguidos de 0xFF
void Test_Exec_SpiLoadValido(void)
{
	const uint8_t payload[]  = {0x03, 0x11, 0x22, 0x33};
	const uint8_t esperado[] = {0x11, 0x22, 0x33, 0xFF};
	uint8_t tx[dSPIBUS_TAMANHO_BUFFER];
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x4B, dCOMM_CMD_SPI_SLAVE_LOAD, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, result.size);

	SpiBus_PreparaTx(tx);
	TEST_ASSERT_EQUAL_UINT8_ARRAY(esperado, tx, sizeof(esperado));
	TEST_ASSERT_EQUAL_HEX8(0xFF, tx[dSPIBUS_TAMANHO_BUFFER - 1U]);
}

// 40. SPI_SLAVE_LOAD com quantidade que nao bate com o payload ou sem quantidade: NACK e o buffer fica intacto
void Test_Exec_SpiLoadInvalido(void)
{
	const uint8_t carga[]     = {0x01, 0x5A};
	const uint8_t faltaByte[] = {0x03, 0xAA, 0xBB};   // diz 3, manda 2
	uint8_t tx[dSPIBUS_TAMANHO_BUFFER];
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x4C, dCOMM_CMD_SPI_SLAVE_LOAD, carga, sizeof(carga));
	Exec_Process(&frame, &result);

	MontaComando(&frame, 0x4D, dCOMM_CMD_SPI_SLAVE_LOAD, faltaByte, sizeof(faltaByte));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	MontaComando(&frame, 0x4E, dCOMM_CMD_SPI_SLAVE_LOAD, NULL, 0);
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	SpiBus_PreparaTx(tx);
	TEST_ASSERT_EQUAL_HEX8(0x5A, tx[0]);
	TEST_ASSERT_EQUAL_HEX8(0xFF, tx[1]);
}

// 41. SPI_SLAVE_READ depois de duas transacoes do DUT: ACK com [2][dados]; a leitura seguinte vem vazia ([0])
void Test_Exec_SpiReadValido(void)
{
	const uint8_t rx1[]      = {0x5A};
	const uint8_t rx2[]      = {0xA5};
	const uint8_t esperado[] = {0x02, 0x5A, 0xA5};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	SpiBus_FimTransacao(rx1, sizeof(rx1));
	SpiBus_FimTransacao(rx2, sizeof(rx2));

	MontaComando(&frame, 0x4F, dCOMM_CMD_SPI_SLAVE_READ, NULL, 0);
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(3, result.size);
	TEST_ASSERT_EQUAL_UINT8_ARRAY(esperado, result.dados, sizeof(esperado));

	MontaComando(&frame, 0x50, dCOMM_CMD_SPI_SLAVE_READ, NULL, 0);
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(1, result.size);
	TEST_ASSERT_EQUAL_UINT8(0, result.dados[0]);
}

// 42. SPI_SLAVE_READ com buffer cheio (239 bytes): a resposta [239][dados] ocupa os 240 bytes do payload
void Test_Exec_SpiReadCheio(void)
{
	uint8_t rx[dSPIBUS_TAMANHO_BUFFER];
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	memset(rx, 0x3C, sizeof(rx));
	SpiBus_FimTransacao(rx, sizeof(rx));

	MontaComando(&frame, 0x51, dCOMM_CMD_SPI_SLAVE_READ, NULL, 0);
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(dCOMM_PAYLOAD_MAX_SIZE, result.size);
	TEST_ASSERT_EQUAL_UINT8(dSPIBUS_TAMANHO_BUFFER, result.dados[0]);
	TEST_ASSERT_EQUAL_HEX8(0x3C, result.dados[dCOMM_PAYLOAD_MAX_SIZE - 1U]);
}

// 43. SPI_SLAVE_READ com payload: NACK PARAMETRO_INVALIDO e o buffer nao e esvaziado
void Test_Exec_SpiReadComPayload(void)
{
	const uint8_t rx[]      = {0x77};
	const uint8_t payload[] = {0x00};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	SpiBus_FimTransacao(rx, sizeof(rx));

	MontaComando(&frame, 0x52, dCOMM_CMD_SPI_SLAVE_READ, payload, sizeof(payload));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, result.size);

	MontaComando(&frame, 0x53, dCOMM_CMD_SPI_SLAVE_READ, NULL, 0);
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(1, result.dados[0]);
	TEST_ASSERT_EQUAL_HEX8(0x77, result.dados[1]);
}

// 44. SPI_MASTER_XFER sem SPI_CONFIG de mestre: NACK NAO_CONFIGURADO e o barramento nao e tocado
void Test_Exec_SpiXferNaoConfigurado(void)
{
	const uint8_t payload[] = {0x01, 0x9F};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x54, dCOMM_CMD_SPI_MASTER_XFER, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_NAO_CONFIGURADO, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, result.size);
	TEST_ASSERT_EQUAL_UINT8(0, spiFalso.xferChamadas);
}

// 45. Como mestre, envia 0x9F 0x00 0x00: ACK com os 3 bytes que o DUT devolveu
void Test_Exec_SpiXferValido(void)
{
	const uint8_t config[]  = {0x01, 0x00, 0x40, 0x42, 0x0F, 0x00};
	const uint8_t payload[] = {0x03, 0x9F, 0x00, 0x00};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();

	MontaComando(&frame, 0x55, dCOMM_CMD_SPI_CONFIG, config, sizeof(config));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);

	MontaComando(&frame, 0x56, dCOMM_CMD_SPI_MASTER_XFER, payload, sizeof(payload));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(3, result.size);
	TEST_ASSERT_EQUAL_HEX8(0xB0, result.dados[0]);
	TEST_ASSERT_EQUAL_HEX8(0xB2, result.dados[2]);
	TEST_ASSERT_EQUAL_HEX8(0x9F, spiFalso.xferEscrita);
	TEST_ASSERT_EQUAL_UINT8(3, spiFalso.xferQuantidade);
}

// 46. SPI_MASTER_XFER com quantidade que nao bate com o payload, quantidade 0 ou sem payload: NACK, barramento intocado
void Test_Exec_SpiXferInvalido(void)
{
	const uint8_t config[]    = {0x01, 0x00, 0x40, 0x42, 0x0F, 0x00};
	const uint8_t faltaByte[] = {0x02, 0x9F};   // diz 2, manda 1
	const uint8_t zero[]      = {0x00};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x57, dCOMM_CMD_SPI_CONFIG, config, sizeof(config));
	Exec_Process(&frame, &result);

	MontaComando(&frame, 0x58, dCOMM_CMD_SPI_MASTER_XFER, faltaByte, sizeof(faltaByte));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	MontaComando(&frame, 0x59, dCOMM_CMD_SPI_MASTER_XFER, zero, sizeof(zero));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	MontaComando(&frame, 0x5A, dCOMM_CMD_SPI_MASTER_XFER, NULL, 0);
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	TEST_ASSERT_EQUAL_UINT8(0, spiFalso.xferChamadas);
}

// 47. Falha do HAL na transferencia: NACK ERRO_PERIFERICO e sem dados
void Test_Exec_SpiXferFalhaPeriferico(void)
{
	const uint8_t config[]  = {0x01, 0x00, 0x40, 0x42, 0x0F, 0x00};
	const uint8_t payload[] = {0x01, 0x9F};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x5B, dCOMM_CMD_SPI_CONFIG, config, sizeof(config));
	Exec_Process(&frame, &result);
	spiFalso.retornoXfer = eSPIBUS_RETURN_ERROR;

	MontaComando(&frame, 0x5C, dCOMM_CMD_SPI_MASTER_XFER, payload, sizeof(payload));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PERIFERICO, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, result.size);
}

// 48. RESET_ALL esvazia os buffers do escravo e tira o papel de mestre: um teste nao herda o SPI do anterior
void Test_Exec_ResetAllEsvaziaSpi(void)
{
	const uint8_t config[] = {0x01, 0x00, 0x40, 0x42, 0x0F, 0x00};
	const uint8_t load[]   = {0x01, 0x5A};
	const uint8_t xfer[]   = {0x01, 0x9F};
	const uint8_t rx[]     = {0x77};
	uint8_t tx[dSPIBUS_TAMANHO_BUFFER];
	commFrame_t frame;
	execResult_t result;
	IniciaExec();

	MontaComando(&frame, 0x5D, dCOMM_CMD_SPI_CONFIG, config, sizeof(config));
	Exec_Process(&frame, &result);
	MontaComando(&frame, 0x5E, dCOMM_CMD_SPI_SLAVE_LOAD, load, sizeof(load));
	Exec_Process(&frame, &result);
	SpiBus_FimTransacao(rx, sizeof(rx));
	MontaComando(&frame, 0x5F, dCOMM_CMD_RESET_ALL, NULL, 0);
	Exec_Process(&frame, &result);

	SpiBus_PreparaTx(tx);
	TEST_ASSERT_EQUAL_HEX8(0xFF, tx[0]);

	MontaComando(&frame, 0x60, dCOMM_CMD_SPI_SLAVE_READ, NULL, 0);
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, result.dados[0]);

	MontaComando(&frame, 0x61, dCOMM_CMD_SPI_MASTER_XFER, xfer, sizeof(xfer));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_NAO_CONFIGURADO, result.erro);
}

// 49. UART_CONFIG em 115200 baud: ACK e a UART recebe o baud (u32 little-endian)
void Test_Exec_UartConfigValido(void)
{
	const uint8_t payload[] = {0x00, 0xC2, 0x01, 0x00};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x62, dCOMM_CMD_UART_CONFIG, payload, sizeof(payload));

	TEST_ASSERT_EQUAL(eEXEC_RETURN_OK, Exec_Process(&frame, &result));
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, result.size);
	TEST_ASSERT_EQUAL_UINT8(1, uartFalsa.configChamadas);
	TEST_ASSERT_EQUAL_UINT32(115200, uartFalsa.baud);
}

// 50. UART_CONFIG com 1199 baud, 1 000 001 baud ou tamanho errado: NACK PARAMETRO_INVALIDO e a UART nao e tocada
void Test_Exec_UartConfigInvalido(void)
{
	const uint8_t lento[]  = {0xAF, 0x04, 0x00, 0x00};   // 1199
	const uint8_t rapido[] = {0x41, 0x42, 0x0F, 0x00};   // 1 000 001
	const uint8_t curto[]  = {0x00, 0xC2, 0x01};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();

	MontaComando(&frame, 0x63, dCOMM_CMD_UART_CONFIG, lento, sizeof(lento));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	MontaComando(&frame, 0x64, dCOMM_CMD_UART_CONFIG, rapido, sizeof(rapido));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	MontaComando(&frame, 0x65, dCOMM_CMD_UART_CONFIG, curto, sizeof(curto));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	TEST_ASSERT_EQUAL_UINT8(0, uartFalsa.configChamadas);
}

// 51. UART_WRITE sem UART_CONFIG: NACK NAO_CONFIGURADO e nada e enviado
void Test_Exec_UartWriteNaoConfigurado(void)
{
	const uint8_t payload[] = {0x02, 'O', 'K'};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x66, dCOMM_CMD_UART_WRITE, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_NAO_CONFIGURADO, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, uartFalsa.writeChamadas);
}

// 52. UART_WRITE de 3 bytes: ACK sem dados e a UART envia os 3 bytes
void Test_Exec_UartWriteValido(void)
{
	const uint8_t payload[] = {0x03, 'O', 'K', '\n'};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	ConfiguraUart();
	MontaComando(&frame, 0x67, dCOMM_CMD_UART_WRITE, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, result.size);
	TEST_ASSERT_EQUAL_UINT8(1, uartFalsa.writeChamadas);
	TEST_ASSERT_EQUAL_HEX8('O', uartFalsa.writeEscrita);
	TEST_ASSERT_EQUAL_UINT8(3, uartFalsa.writeQuantidade);
}

// 53. UART_WRITE com quantidade que nao bate com o payload, quantidade 0 ou sem payload: NACK e nada e enviado
void Test_Exec_UartWriteInvalido(void)
{
	const uint8_t faltaByte[] = {0x03, 'O', 'K'};   // diz 3, manda 2
	const uint8_t zero[]      = {0x00};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	ConfiguraUart();

	MontaComando(&frame, 0x68, dCOMM_CMD_UART_WRITE, faltaByte, sizeof(faltaByte));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	MontaComando(&frame, 0x69, dCOMM_CMD_UART_WRITE, zero, sizeof(zero));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	MontaComando(&frame, 0x6A, dCOMM_CMD_UART_WRITE, NULL, 0);
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	TEST_ASSERT_EQUAL_UINT8(0, uartFalsa.writeChamadas);
}

// 54. Falha ou timeout do HAL na escrita: NACK ERRO_PERIFERICO
void Test_Exec_UartWriteFalhaPeriferico(void)
{
	const uint8_t payload[] = {0x01, 'X'};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	ConfiguraUart();
	uartFalsa.retornoWrite = eUARTBUS_RETURN_ERROR;
	MontaComando(&frame, 0x6B, dCOMM_CMD_UART_WRITE, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PERIFERICO, result.erro);
}

// 55. UART_READ sem UART_CONFIG: NACK NAO_CONFIGURADO e sem dados
void Test_Exec_UartReadNaoConfigurado(void)
{
	const uint8_t payload[] = {0x10};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x6C, dCOMM_CMD_UART_READ, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_NAO_CONFIGURADO, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, result.size);
}

// 56. UART_READ com maximo 2 de 3 bytes recebidos: [2][A B]; depois [1][C] e, sem mais nada, [0]
void Test_Exec_UartReadValido(void)
{
	const uint8_t max2[]      = {0x02};
	const uint8_t max10[]     = {0x0A};
	const uint8_t esperado1[] = {0x02, 'A', 'B'};
	const uint8_t esperado2[] = {0x01, 'C'};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	ConfiguraUart();
	UartBus_ByteRecebido('A');
	UartBus_ByteRecebido('B');
	UartBus_ByteRecebido('C');

	MontaComando(&frame, 0x6D, dCOMM_CMD_UART_READ, max2, sizeof(max2));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(3, result.size);
	TEST_ASSERT_EQUAL_UINT8_ARRAY(esperado1, result.dados, sizeof(esperado1));

	MontaComando(&frame, 0x6E, dCOMM_CMD_UART_READ, max10, sizeof(max10));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(2, result.size);
	TEST_ASSERT_EQUAL_UINT8_ARRAY(esperado2, result.dados, sizeof(esperado2));

	MontaComando(&frame, 0x6F, dCOMM_CMD_UART_READ, max10, sizeof(max10));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(1, result.size);
	TEST_ASSERT_EQUAL_UINT8(0, result.dados[0]);
}

// 57. UART_READ com maximo 255 e 250 bytes recebidos: devolve 239 (a resposta ocupa os 240 bytes) e depois os 11 restantes
void Test_Exec_UartReadMaximoGrande(void)
{
	const uint8_t max255[] = {0xFF};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	ConfiguraUart();
	for (uint16_t i = 0U; i < 250U; i++)
	{
		UartBus_ByteRecebido((uint8_t)i);
	}

	MontaComando(&frame, 0x71, dCOMM_CMD_UART_READ, max255, sizeof(max255));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(dCOMM_PAYLOAD_MAX_SIZE, result.size);
	TEST_ASSERT_EQUAL_UINT8(dUARTBUS_TAMANHO_MAX, result.dados[0]);
	TEST_ASSERT_EQUAL_HEX8(0x00, result.dados[1]);
	TEST_ASSERT_EQUAL_HEX8(238, result.dados[dCOMM_PAYLOAD_MAX_SIZE - 1U]);

	MontaComando(&frame, 0x72, dCOMM_CMD_UART_READ, max255, sizeof(max255));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(11, result.dados[0]);
	TEST_ASSERT_EQUAL_HEX8(239, result.dados[1]);
}

// 58. UART_READ sem payload ou com 2 bytes: NACK PARAMETRO_INVALIDO e o buffer fica intacto
void Test_Exec_UartReadTamanhoErrado(void)
{
	const uint8_t longo[] = {0x01, 0x00};
	const uint8_t max1[]  = {0x01};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	ConfiguraUart();
	UartBus_ByteRecebido('Z');

	MontaComando(&frame, 0x73, dCOMM_CMD_UART_READ, NULL, 0);
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, result.size);

	MontaComando(&frame, 0x74, dCOMM_CMD_UART_READ, longo, sizeof(longo));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);

	MontaComando(&frame, 0x75, dCOMM_CMD_UART_READ, max1, sizeof(max1));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(1, result.dados[0]);
	TEST_ASSERT_EQUAL_HEX8('Z', result.dados[1]);
}

// 59. RESET_ALL desliga a UART e esvazia o buffer: a leitura pede UART_CONFIG de novo e, depois dele, vem vazia
void Test_Exec_ResetAllDesligaUart(void)
{
	const uint8_t max10[] = {0x0A};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	ConfiguraUart();
	UartBus_ByteRecebido('A');

	MontaComando(&frame, 0x76, dCOMM_CMD_RESET_ALL, NULL, 0);
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(1, uartFalsa.offChamadas);

	MontaComando(&frame, 0x77, dCOMM_CMD_UART_READ, max10, sizeof(max10));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_NAO_CONFIGURADO, result.erro);

	ConfiguraUart();
	MontaComando(&frame, 0x78, dCOMM_CMD_UART_READ, max10, sizeof(max10));
	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, result.dados[0]);
}
