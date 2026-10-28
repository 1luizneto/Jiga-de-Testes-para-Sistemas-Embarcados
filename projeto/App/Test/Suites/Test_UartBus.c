/*
 * Test_UartBus.c
 *
 * Testes do UartBus com uma UART falsa: faixa do baud (sem tocar na BSP quando invalido), escrita, recepcao no
 * buffer circular como o DUT a faria (ordem, leitura parcial, volta do indice, buffer cheio) e estado seguro.
 */

#include <string.h>
#include "unity.h"
#include "UartBus.h"

/// Registro das chamadas a UART falsa
static struct uartFalsa
{
	uint8_t  configChamadas;
	uint8_t  offChamadas;
	uint8_t  writeChamadas;
	uint8_t  lockChamadas;
	uint8_t  unlockChamadas;
	uint32_t baud;
	uint8_t  escrita[4];
	uint8_t  quantidade;

	uartBusReturn_t retornoConfig;   ///< simula falha do HAL
	uartBusReturn_t retornoOff;
	uartBusReturn_t retornoWrite;
} uartFalsa;

static uartBusReturn_t UartFalsa_Config(uint32_t baud)
{
	uartFalsa.configChamadas++;
	uartFalsa.baud = baud;

	return uartFalsa.retornoConfig;
}

static uartBusReturn_t UartFalsa_Off(void)
{
	uartFalsa.offChamadas++;

	return uartFalsa.retornoOff;
}

static uartBusReturn_t UartFalsa_Write(const uint8_t *dados, uint8_t quantidade)
{
	uartFalsa.writeChamadas++;
	uartFalsa.quantidade = quantidade;
	memcpy(uartFalsa.escrita, dados, (quantidade < 4U) ? quantidade : 4U);

	return uartFalsa.retornoWrite;
}

static void UartFalsa_Lock(void)
{
	uartFalsa.lockChamadas++;
}

static void UartFalsa_Unlock(void)
{
	uartFalsa.unlockChamadas++;
}

static const uartBusFunc_t funcFalsas =
{
	UartFalsa_Config, UartFalsa_Off, UartFalsa_Write, UartFalsa_Lock, UartFalsa_Unlock,
};

// O UartBus passa a usar a UART falsa, ja configurada a 115200. Na build Test nada mais usa a UART4.
static void IniciaUartBus(void)
{
	memset(&uartFalsa, 0, sizeof(uartFalsa));
	uartFalsa.retornoConfig = eUARTBUS_RETURN_OK;
	uartFalsa.retornoOff    = eUARTBUS_RETURN_OK;
	uartFalsa.retornoWrite  = eUARTBUS_RETURN_OK;
	UartBus_Init(&funcFalsas);
	UartBus_Config(115200U);
}

// O DUT envia 'quantidade' bytes: inicio, inicio + 1, ...
static void DutEnvia(uint8_t inicio, uint16_t quantidade)
{
	for (uint16_t i = 0U; i < quantidade; i++)
	{
		UartBus_ByteRecebido((uint8_t)(inicio + i));
	}
}

// 1. Init sem as funcoes da BSP e recusado
void Test_UartBus_InitInvalido(void)
{
	uartBusFunc_t semWrite = funcFalsas;
	semWrite.write = NULL;

	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_INVALID_ARGUMENT, UartBus_Init(NULL));
	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_INVALID_ARGUMENT, UartBus_Init(&semWrite));
}

// 2. Baud na faixa (inclusive os limites): chega na BSP
void Test_UartBus_ConfigValido(void)
{
	IniciaUartBus();
	TEST_ASSERT_EQUAL_UINT32(115200U, uartFalsa.baud);

	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_OK, UartBus_Config(dUARTBUS_BAUD_MIN));
	TEST_ASSERT_EQUAL_UINT32(dUARTBUS_BAUD_MIN, uartFalsa.baud);
	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_OK, UartBus_Config(dUARTBUS_BAUD_MAX));
	TEST_ASSERT_EQUAL_UINT32(dUARTBUS_BAUD_MAX, uartFalsa.baud);
}

// 3. Baud fora da faixa: recusado sem tocar na BSP
void Test_UartBus_ConfigInvalido(void)
{
	IniciaUartBus();
	uartFalsa.configChamadas = 0U;

	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_INVALID_ARGUMENT, UartBus_Config(dUARTBUS_BAUD_MIN - 1U));
	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_INVALID_ARGUMENT, UartBus_Config(dUARTBUS_BAUD_MAX + 1U));
	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_INVALID_ARGUMENT, UartBus_Config(0U));
	TEST_ASSERT_EQUAL_UINT8(0, uartFalsa.configChamadas);
}

// 4. Falha da BSP ao configurar: a UART fica desconfigurada
void Test_UartBus_ConfigFalhaBsp(void)
{
	const uint8_t dados[1] = {0x55U};

	IniciaUartBus();
	uartFalsa.retornoConfig = eUARTBUS_RETURN_ERROR;

	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_ERROR, UartBus_Config(9600U));
	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_NOT_CONFIGURED, UartBus_Write(dados, 1U));
}

// 5. Escrita e leitura sem UART_CONFIG: NAO_CONFIGURADO, sem tocar na BSP
void Test_UartBus_SemConfig(void)
{
	const uint8_t dados[1] = {0x55U};
	uint8_t lidos[4];
	uint8_t quantidade = 0U;

	IniciaUartBus();
	UartBus_Init(&funcFalsas);   // volta a desconfigurada

	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_NOT_CONFIGURED, UartBus_Write(dados, 1U));
	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_NOT_CONFIGURED, UartBus_Read(4U, lidos, &quantidade));
	TEST_ASSERT_EQUAL_UINT8(0, uartFalsa.writeChamadas);
}

// 6. Escrita: os bytes chegam na BSP; quantidade 0, 240 ou sem dados e recusado; falha da BSP
void Test_UartBus_Write(void)
{
	const uint8_t dados[3] = {'o', 'l', 'a'};
	uint8_t grande[dUARTBUS_TAMANHO_MAX + 1U] = {0};

	IniciaUartBus();

	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_OK, UartBus_Write(dados, 3U));
	TEST_ASSERT_EQUAL_UINT8(3U, uartFalsa.quantidade);
	TEST_ASSERT_EQUAL_UINT8_ARRAY(dados, uartFalsa.escrita, 3);

	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_INVALID_ARGUMENT, UartBus_Write(dados, 0U));
	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_INVALID_ARGUMENT, UartBus_Write(grande, dUARTBUS_TAMANHO_MAX + 1U));
	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_INVALID_ARGUMENT, UartBus_Write(NULL, 1U));
	TEST_ASSERT_EQUAL_UINT8(1, uartFalsa.writeChamadas);

	uartFalsa.retornoWrite = eUARTBUS_RETURN_ERROR;
	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_ERROR, UartBus_Write(dados, 3U));
}

// 7. O DUT envia 5 bytes: a leitura devolve os 5 na ordem e esvazia
void Test_UartBus_RecebeERead(void)
{
	const uint8_t esperado[5] = {0x10U, 0x11U, 0x12U, 0x13U, 0x14U};
	uint8_t lidos[dUARTBUS_TAMANHO_MAX];
	uint8_t quantidade = 0U;

	IniciaUartBus();
	DutEnvia(0x10U, 5U);

	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_OK, UartBus_Read(10U, lidos, &quantidade));
	TEST_ASSERT_EQUAL_UINT8(5U, quantidade);
	TEST_ASSERT_EQUAL_UINT8_ARRAY(esperado, lidos, 5);

	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_OK, UartBus_Read(10U, lidos, &quantidade));
	TEST_ASSERT_EQUAL_UINT8(0U, quantidade);
}

// 8. Leitura parcial: o maximo pedido limita; o resto fica para a proxima
void Test_UartBus_ReadParcial(void)
{
	uint8_t lidos[dUARTBUS_TAMANHO_MAX];
	uint8_t quantidade = 0U;

	IniciaUartBus();
	DutEnvia(0x00U, 10U);

	UartBus_Read(4U, lidos, &quantidade);
	TEST_ASSERT_EQUAL_UINT8(4U, quantidade);
	TEST_ASSERT_EQUAL_HEX8(0x03U, lidos[3]);

	UartBus_Read(0U, lidos, &quantidade);
	TEST_ASSERT_EQUAL_UINT8(0U, quantidade);

	UartBus_Read(255U, lidos, &quantidade);
	TEST_ASSERT_EQUAL_UINT8(6U, quantidade);
	TEST_ASSERT_EQUAL_HEX8(0x04U, lidos[0]);
}

// 9. Uma leitura devolve no maximo 239 bytes (o que cabe na resposta), mesmo pedindo 255
void Test_UartBus_ReadLimitado(void)
{
	uint8_t lidos[dUARTBUS_TAMANHO_MAX];
	uint8_t quantidade = 0U;

	IniciaUartBus();
	DutEnvia(0x00U, 250U);

	UartBus_Read(255U, lidos, &quantidade);
	TEST_ASSERT_EQUAL_UINT8(dUARTBUS_TAMANHO_MAX, quantidade);
	UartBus_Read(255U, lidos, &quantidade);
	TEST_ASSERT_EQUAL_UINT8(11U, quantidade);
	TEST_ASSERT_EQUAL_HEX8(239U, lidos[0]);
}

// 10. O indice volta ao inicio do buffer circular sem perder a ordem
void Test_UartBus_BufferCircularVolta(void)
{
	uint8_t lidos[dUARTBUS_TAMANHO_MAX];
	uint8_t quantidade = 0U;

	IniciaUartBus();
	DutEnvia(0x00U, 200U);
	UartBus_Read(200U, lidos, &quantidade);   // o inicio fica em 200

	DutEnvia(0x80U, 100U);                    // ocupa as posicoes 200 a 255 e 0 a 43
	UartBus_Read(255U, lidos, &quantidade);
	TEST_ASSERT_EQUAL_UINT8(100U, quantidade);
	for (uint8_t i = 0U; i < 100U; i++)
	{
		TEST_ASSERT_EQUAL_HEX8((uint8_t)(0x80U + i), lidos[i]);
	}
}

// 11. Buffer cheio (256 bytes): os que chegam depois sao descartados; os 256 primeiros ficam
void Test_UartBus_BufferCheio(void)
{
	uint8_t lidos[dUARTBUS_TAMANHO_MAX];
	uint8_t quantidade = 0U;

	IniciaUartBus();
	DutEnvia(0x00U, 300U);

	UartBus_Read(255U, lidos, &quantidade);
	TEST_ASSERT_EQUAL_UINT8(dUARTBUS_TAMANHO_MAX, quantidade);
	TEST_ASSERT_EQUAL_HEX8(0x00U, lidos[0]);
	UartBus_Read(255U, lidos, &quantidade);
	TEST_ASSERT_EQUAL_UINT8(dUARTBUS_TAMANHO_RX - dUARTBUS_TAMANHO_MAX, quantidade);
	TEST_ASSERT_EQUAL_HEX8(0xFFU, lidos[quantidade - 1U]);   // o 256o byte; do 257o em diante, descartados
}

// 12. UART_CONFIG esvazia o buffer (bytes num baud antigo nao valem)
void Test_UartBus_ConfigEsvazia(void)
{
	uint8_t lidos[dUARTBUS_TAMANHO_MAX];
	uint8_t quantidade = 0xFFU;

	IniciaUartBus();
	DutEnvia(0x00U, 8U);
	UartBus_Config(9600U);

	UartBus_Read(255U, lidos, &quantidade);
	TEST_ASSERT_EQUAL_UINT8(0U, quantidade);
}

// 13. A leitura copia com a recepcao travada; sem buffer e recusada
void Test_UartBus_ReadTrava(void)
{
	uint8_t lidos[dUARTBUS_TAMANHO_MAX];
	uint8_t quantidade = 0U;

	IniciaUartBus();
	uartFalsa.lockChamadas   = 0U;
	uartFalsa.unlockChamadas = 0U;

	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_OK, UartBus_Read(4U, lidos, &quantidade));
	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_INVALID_ARGUMENT, UartBus_Read(4U, NULL, &quantidade));
	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_INVALID_ARGUMENT, UartBus_Read(4U, lidos, NULL));
	TEST_ASSERT_EQUAL_UINT8(1, uartFalsa.lockChamadas);
	TEST_ASSERT_EQUAL_UINT8(1, uartFalsa.unlockChamadas);
}

// 14. Estado seguro: desliga, esvazia o buffer e desconfigura
void Test_UartBus_SafeState(void)
{
	const uint8_t dados[1] = {0x55U};

	IniciaUartBus();
	DutEnvia(0x00U, 8U);

	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_OK, UartBus_SafeState());
	TEST_ASSERT_EQUAL_UINT8(1, uartFalsa.offChamadas);
	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_NOT_CONFIGURED, UartBus_Write(dados, 1U));

	UartBus_Config(115200U);   // configurada de novo, o buffer continua vazio
	uint8_t lidos[dUARTBUS_TAMANHO_MAX];
	uint8_t quantidade = 0xFFU;
	UartBus_Read(255U, lidos, &quantidade);
	TEST_ASSERT_EQUAL_UINT8(0U, quantidade);
}

// 15. Falha da BSP ao desligar: desconfigura mesmo assim e informa o erro
void Test_UartBus_SafeStateComFalha(void)
{
	const uint8_t dados[1] = {0x55U};

	IniciaUartBus();
	uartFalsa.retornoOff = eUARTBUS_RETURN_ERROR;

	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_ERROR, UartBus_SafeState());
	TEST_ASSERT_EQUAL(eUARTBUS_RETURN_NOT_CONFIGURED, UartBus_Write(dados, 1U));
}
