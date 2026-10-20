/*
 * Test_DigitalIo.c
 *
 * Testes do DigitalIo com um GPIO falso: validacao dos parametros (sem tocar no pino), montagem da mascara
 * do GPIO_READ e estado seguro.
 */

#include <string.h>
#include "unity.h"
#include "DigitalIo.h"

/// Registro das chamadas ao GPIO falso
static struct gpioFalso
{
	uint8_t           escritas;
	uint8_t           saida[8];
	uint8_t           nivel[8];
	uint8_t           entradas[dDIGITALIO_ENTRADA_MAX + 1U];   ///< nivel que cada entrada (1 a 4) devolve
	digitalIoReturn_t retornoWrite;                            ///< simula falha na escrita
	digitalIoReturn_t retornoRead;                             ///< simula falha na leitura
} gpioFalso;

static digitalIoReturn_t GpioFalso_Write(uint8_t saida, uint8_t nivel)
{
	if (gpioFalso.escritas < 8U)
	{
		gpioFalso.saida[gpioFalso.escritas] = saida;
		gpioFalso.nivel[gpioFalso.escritas] = nivel;
	}
	gpioFalso.escritas++;

	return gpioFalso.retornoWrite;
}

static digitalIoReturn_t GpioFalso_Read(uint8_t entrada, uint8_t *nivel)
{
	*nivel = gpioFalso.entradas[entrada];

	return gpioFalso.retornoRead;
}

// O DigitalIo passa a usar o GPIO falso. Na build Test nada mais usa os GPIOs, entao nao precisa restaurar.
static void IniciaDigitalIo(void)
{
	memset(&gpioFalso, 0, sizeof(gpioFalso));
	gpioFalso.retornoWrite = eDIGITALIO_RETURN_OK;
	gpioFalso.retornoRead  = eDIGITALIO_RETURN_OK;
	DigitalIO_Init(GpioFalso_Write, GpioFalso_Read);
}

// 1. Set valido: a saida e o nivel chegam ao pino
void Test_DigitalIo_SetValido(void)
{
	IniciaDigitalIo();

	TEST_ASSERT_EQUAL(eDIGITALIO_RETURN_OK, DigitalIO_Set(1, 1));
	TEST_ASSERT_EQUAL_UINT8(1, gpioFalso.escritas);
	TEST_ASSERT_EQUAL_UINT8(1, gpioFalso.saida[0]);
	TEST_ASSERT_EQUAL_UINT8(1, gpioFalso.nivel[0]);
}

// 2. Limites aceitos: saida 4, nivel 0
void Test_DigitalIo_SetLimites(void)
{
	IniciaDigitalIo();

	TEST_ASSERT_EQUAL(eDIGITALIO_RETURN_OK, DigitalIO_Set(4, 0));
	TEST_ASSERT_EQUAL_UINT8(4, gpioFalso.saida[0]);
	TEST_ASSERT_EQUAL_UINT8(0, gpioFalso.nivel[0]);
}

// 3. Saida 0 e 5: recusadas sem tocar no pino
void Test_DigitalIo_SetSaidaInvalida(void)
{
	IniciaDigitalIo();

	TEST_ASSERT_EQUAL(eDIGITALIO_RETURN_INVALID_ARGUMENT, DigitalIO_Set(0, 1));
	TEST_ASSERT_EQUAL(eDIGITALIO_RETURN_INVALID_ARGUMENT, DigitalIO_Set(5, 1));
	TEST_ASSERT_EQUAL_UINT8(0, gpioFalso.escritas);
}

// 4. Nivel diferente de 0/1: recusado sem tocar no pino
void Test_DigitalIo_SetNivelInvalido(void)
{
	IniciaDigitalIo();

	TEST_ASSERT_EQUAL(eDIGITALIO_RETURN_INVALID_ARGUMENT, DigitalIO_Set(1, 2));
	TEST_ASSERT_EQUAL_UINT8(0, gpioFalso.escritas);
}

// 5. Mascara: entradas 1 e 3 (GPIO5 e GPIO7) em alto -> 0x05, bits 4 a 7 em 0
void Test_DigitalIo_ReadMascara(void)
{
	uint8_t mascara = 0xFF;
	IniciaDigitalIo();
	gpioFalso.entradas[1] = 1;
	gpioFalso.entradas[3] = 1;

	TEST_ASSERT_EQUAL(eDIGITALIO_RETURN_OK, DigitalIO_Read(&mascara));
	TEST_ASSERT_EQUAL_HEX8(0x05, mascara);
}

// 6. Todas em alto -> 0x0F; todas em baixo -> 0x00
void Test_DigitalIo_ReadExtremos(void)
{
	uint8_t mascara = 0xFF;
	IniciaDigitalIo();

	TEST_ASSERT_EQUAL(eDIGITALIO_RETURN_OK, DigitalIO_Read(&mascara));
	TEST_ASSERT_EQUAL_HEX8(0x00, mascara);

	memset(gpioFalso.entradas, 1, sizeof(gpioFalso.entradas));
	TEST_ASSERT_EQUAL(eDIGITALIO_RETURN_OK, DigitalIO_Read(&mascara));
	TEST_ASSERT_EQUAL_HEX8(0x0F, mascara);
}

// 7. Falha na leitura: erro e a mascara nao e alterada
void Test_DigitalIo_ReadFalha(void)
{
	uint8_t mascara = 0xAA;
	IniciaDigitalIo();
	gpioFalso.retornoRead = eDIGITALIO_RETURN_ERROR;

	TEST_ASSERT_EQUAL(eDIGITALIO_RETURN_ERROR, DigitalIO_Read(&mascara));
	TEST_ASSERT_EQUAL_HEX8(0xAA, mascara);
}

// 8. Estado seguro: as 4 saidas em nivel baixo
void Test_DigitalIo_SafeState(void)
{
	IniciaDigitalIo();

	TEST_ASSERT_EQUAL(eDIGITALIO_RETURN_OK, DigitalIO_SafeState());
	TEST_ASSERT_EQUAL_UINT8(4, gpioFalso.escritas);
	for (uint8_t i = 0; i < 4U; i++)
	{
		TEST_ASSERT_EQUAL_UINT8(i + 1U, gpioFalso.saida[i]);
		TEST_ASSERT_EQUAL_UINT8(0, gpioFalso.nivel[i]);
	}
}

// 9. Estado seguro com falha: continua nas outras saidas e informa o erro
void Test_DigitalIo_SafeStateComFalha(void)
{
	IniciaDigitalIo();
	gpioFalso.retornoWrite = eDIGITALIO_RETURN_ERROR;

	TEST_ASSERT_EQUAL(eDIGITALIO_RETURN_ERROR, DigitalIO_SafeState());
	TEST_ASSERT_EQUAL_UINT8(4, gpioFalso.escritas);
}

// 10. Argumentos nulos
void Test_DigitalIo_ArgumentosInvalidos(void)
{
	IniciaDigitalIo();

	TEST_ASSERT_EQUAL(eDIGITALIO_RETURN_INVALID_ARGUMENT, DigitalIO_Init(NULL, GpioFalso_Read));
	TEST_ASSERT_EQUAL(eDIGITALIO_RETURN_INVALID_ARGUMENT, DigitalIO_Init(GpioFalso_Write, NULL));
	TEST_ASSERT_EQUAL(eDIGITALIO_RETURN_INVALID_ARGUMENT, DigitalIO_Read(NULL));
}
