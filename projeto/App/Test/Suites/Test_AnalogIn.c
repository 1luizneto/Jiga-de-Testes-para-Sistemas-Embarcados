/*
 * Test_AnalogIn.c
 *
 * Testes do AnalogIn com um ADC falso: validacao do canal (sem acionar o ADC), repasse do valor lido e
 * tratamento de falha do periferico.
 */

#include <string.h>
#include "unity.h"
#include "AnalogIn.h"

/// Registro das chamadas ao ADC falso
static struct adcFalso
{
	uint8_t          chamadas;
	uint8_t          canal;     ///< canal da ultima chamada
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

// O AnalogIn passa a usar o ADC falso. Na build Test nada mais usa o ADC, entao nao precisa restaurar.
static void IniciaAnalogIn(void)
{
	memset(&adcFalso, 0, sizeof(adcFalso));
	adcFalso.retorno = eANALOGIN_RETURN_OK;
	AnalogIn_Init(AdcFalso_Read);
}

// 1. Leitura valida: o canal chega ao ADC e o valor volta para quem chamou
void Test_AnalogIn_ReadValido(void)
{
	uint16_t valor = 0;
	IniciaAnalogIn();
	adcFalso.valor = 2047;

	TEST_ASSERT_EQUAL(eANALOGIN_RETURN_OK, AnalogIn_Read(1, &valor));
	TEST_ASSERT_EQUAL_UINT8(1, adcFalso.chamadas);
	TEST_ASSERT_EQUAL_UINT8(1, adcFalso.canal);
	TEST_ASSERT_EQUAL_UINT16(2047, valor);
}

// 2. Limites aceitos: canais 0 e 2, valores 0 e 4095
void Test_AnalogIn_ReadLimites(void)
{
	uint16_t valor = 0xFFFF;
	IniciaAnalogIn();

	adcFalso.valor = 0;
	TEST_ASSERT_EQUAL(eANALOGIN_RETURN_OK, AnalogIn_Read(0, &valor));
	TEST_ASSERT_EQUAL_UINT8(0, adcFalso.canal);
	TEST_ASSERT_EQUAL_UINT16(0, valor);

	adcFalso.valor = 4095;
	TEST_ASSERT_EQUAL(eANALOGIN_RETURN_OK, AnalogIn_Read(2, &valor));
	TEST_ASSERT_EQUAL_UINT8(2, adcFalso.canal);
	TEST_ASSERT_EQUAL_UINT16(4095, valor);
}

// 3. Canal inexistente: recusado sem acionar o ADC e sem alterar o valor
void Test_AnalogIn_ReadCanalInvalido(void)
{
	uint16_t valor = 0xAAAA;
	IniciaAnalogIn();

	TEST_ASSERT_EQUAL(eANALOGIN_RETURN_INVALID_ARGUMENT, AnalogIn_Read(3, &valor));
	TEST_ASSERT_EQUAL_UINT8(0, adcFalso.chamadas);
	TEST_ASSERT_EQUAL_HEX16(0xAAAA, valor);
}

// 4. Falha do periferico (HAL): erro e o valor nao e alterado
void Test_AnalogIn_ReadFalhaPeriferico(void)
{
	uint16_t valor = 0xAAAA;
	IniciaAnalogIn();
	adcFalso.retorno = eANALOGIN_RETURN_ERROR;
	adcFalso.valor   = 1000;

	TEST_ASSERT_EQUAL(eANALOGIN_RETURN_ERROR, AnalogIn_Read(0, &valor));
	TEST_ASSERT_EQUAL_HEX16(0xAAAA, valor);
}

// 5. BSP devolve mais que 12 bits: tratado como falha, o valor nao e alterado
void Test_AnalogIn_ReadValorForaDaFaixa(void)
{
	uint16_t valor = 0xAAAA;
	IniciaAnalogIn();
	adcFalso.valor = 4096;

	TEST_ASSERT_EQUAL(eANALOGIN_RETURN_ERROR, AnalogIn_Read(0, &valor));
	TEST_ASSERT_EQUAL_HEX16(0xAAAA, valor);
}

// 6. Argumentos nulos
void Test_AnalogIn_ArgumentosInvalidos(void)
{
	IniciaAnalogIn();

	TEST_ASSERT_EQUAL(eANALOGIN_RETURN_INVALID_ARGUMENT, AnalogIn_Init(NULL));
	TEST_ASSERT_EQUAL(eANALOGIN_RETURN_INVALID_ARGUMENT, AnalogIn_Read(0, NULL));
	TEST_ASSERT_EQUAL_UINT8(0, adcFalso.chamadas);
}
