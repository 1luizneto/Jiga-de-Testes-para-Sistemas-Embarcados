/*
 * Test_SpiBus.c
 *
 * Testes do SpiBus com um barramento falso: divisor do clock do mestre, configuracao (sem tocar na BSP quando
 * invalida), buffers do escravo como o DUT os veria (transmissao a partir do byte 0, recepcao acumulada ate a
 * leitura), transferencia de mestre e estado seguro.
 */

#include <string.h>
#include "unity.h"
#include "SpiBus.h"

/// Clock do SPI2 na placa (APB1)
#define dTESTE_SPI_CLOCK_HZ (54000000U)

/// Registro das chamadas ao barramento falso
static struct barramentoFalso
{
	uint8_t configChamadas;
	uint8_t offChamadas;
	uint8_t xferChamadas;
	uint8_t lockChamadas;
	uint8_t unlockChamadas;

	uint8_t  papel;
	uint8_t  modo;
	uint16_t divisor;

	uint8_t xferEscrita[4];
	uint8_t xferQuantidade;

	spiBusReturn_t retornoConfig;   ///< simula falha do HAL
	spiBusReturn_t retornoOff;
	spiBusReturn_t retornoXfer;
} barramentoFalso;

static spiBusReturn_t BarramentoFalso_Config(uint8_t papel, uint8_t modo, uint16_t divisor)
{
	barramentoFalso.configChamadas++;
	barramentoFalso.papel   = papel;
	barramentoFalso.modo    = modo;
	barramentoFalso.divisor = divisor;

	return barramentoFalso.retornoConfig;
}

static spiBusReturn_t BarramentoFalso_Off(void)
{
	barramentoFalso.offChamadas++;

	return barramentoFalso.retornoOff;
}

// Guarda o que o mestre enviou e devolve o complemento de cada byte
static spiBusReturn_t BarramentoFalso_Xfer(const uint8_t *escrita, uint8_t *leitura, uint8_t quantidade)
{
	barramentoFalso.xferChamadas++;
	barramentoFalso.xferQuantidade = quantidade;
	memcpy(barramentoFalso.xferEscrita, escrita, (quantidade < 4U) ? quantidade : 4U);

	for (uint8_t i = 0U; i < quantidade; i++)
	{
		leitura[i] = (uint8_t)~escrita[i];
	}

	return barramentoFalso.retornoXfer;
}

static void BarramentoFalso_Lock(void)
{
	barramentoFalso.lockChamadas++;
}

static void BarramentoFalso_Unlock(void)
{
	barramentoFalso.unlockChamadas++;
}

static const spiBusFunc_t funcFalsas =
{
	BarramentoFalso_Config, BarramentoFalso_Off, BarramentoFalso_Xfer, BarramentoFalso_Lock, BarramentoFalso_Unlock,
};

// O SpiBus passa a usar o barramento falso. Na build Test nada mais usa o SPI2, entao nao precisa restaurar.
static void IniciaSpiBus(void)
{
	memset(&barramentoFalso, 0, sizeof(barramentoFalso));
	barramentoFalso.retornoConfig = eSPIBUS_RETURN_OK;
	barramentoFalso.retornoOff    = eSPIBUS_RETURN_OK;
	barramentoFalso.retornoXfer   = eSPIBUS_RETURN_OK;
	SpiBus_Init(&funcFalsas, dTESTE_SPI_CLOCK_HZ);
}

// 1. Init sem as funcoes da BSP ou sem clock e recusado
void Test_SpiBus_InitInvalido(void)
{
	spiBusFunc_t semXfer = funcFalsas;
	semXfer.xfer = NULL;

	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_INVALID_ARGUMENT, SpiBus_Init(NULL, dTESTE_SPI_CLOCK_HZ));
	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_INVALID_ARGUMENT, SpiBus_Init(&semXfer, dTESTE_SPI_CLOCK_HZ));
	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_INVALID_ARGUMENT, SpiBus_Init(&funcFalsas, 0U));
}

// 2. Divisor: a maior frequencia que nao passa da pedida (54 MHz / 2 a 54 MHz / 256)
void Test_SpiBus_Divisor(void)
{
	uint16_t divisor = 0U;

	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_OK, SpiBus_Divisor(dTESTE_SPI_CLOCK_HZ, 27000000U, &divisor));
	TEST_ASSERT_EQUAL_UINT16(2, divisor);
	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_OK, SpiBus_Divisor(dTESTE_SPI_CLOCK_HZ, 50000000U, &divisor));
	TEST_ASSERT_EQUAL_UINT16(2, divisor);
	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_OK, SpiBus_Divisor(dTESTE_SPI_CLOCK_HZ, 26999999U, &divisor));
	TEST_ASSERT_EQUAL_UINT16(4, divisor);
	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_OK, SpiBus_Divisor(dTESTE_SPI_CLOCK_HZ, 1000000U, &divisor));
	TEST_ASSERT_EQUAL_UINT16(64, divisor);   // 843,75 kHz
	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_OK, SpiBus_Divisor(dTESTE_SPI_CLOCK_HZ, 210938U, &divisor));
	TEST_ASSERT_EQUAL_UINT16(256, divisor);   // 210937,5 Hz
}

// 3. Frequencia abaixo de 54 MHz / 256 ou zero: sem divisor possivel
void Test_SpiBus_DivisorForaDaFaixa(void)
{
	uint16_t divisor = 0U;

	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_INVALID_ARGUMENT, SpiBus_Divisor(dTESTE_SPI_CLOCK_HZ, 210937U, &divisor));
	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_INVALID_ARGUMENT, SpiBus_Divisor(dTESTE_SPI_CLOCK_HZ, 0U, &divisor));
	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_INVALID_ARGUMENT, SpiBus_Divisor(dTESTE_SPI_CLOCK_HZ, 1000000U, NULL));
}

// 4. Mestre no modo 3 a 1 MHz: a BSP recebe papel, modo e o divisor 64
void Test_SpiBus_ConfigMestre(void)
{
	IniciaSpiBus();

	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_OK, SpiBus_Config(dSPIBUS_PAPEL_MESTRE, 3U, 1000000U));
	TEST_ASSERT_EQUAL_UINT8(1, barramentoFalso.configChamadas);
	TEST_ASSERT_EQUAL_UINT8(dSPIBUS_PAPEL_MESTRE, barramentoFalso.papel);
	TEST_ASSERT_EQUAL_UINT8(3U, barramentoFalso.modo);
	TEST_ASSERT_EQUAL_UINT16(64U, barramentoFalso.divisor);
}

// 5. Como escravo a frequencia nao e usada: 0 e aceito e a BSP recebe divisor 0
void Test_SpiBus_ConfigEscravoIgnoraFrequencia(void)
{
	IniciaSpiBus();

	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_OK, SpiBus_Config(dSPIBUS_PAPEL_ESCRAVO, 1U, 0U));
	TEST_ASSERT_EQUAL_UINT8(dSPIBUS_PAPEL_ESCRAVO, barramentoFalso.papel);
	TEST_ASSERT_EQUAL_UINT8(1U, barramentoFalso.modo);
	TEST_ASSERT_EQUAL_UINT16(0U, barramentoFalso.divisor);
}

// 6. Papel ou modo fora da faixa, ou mestre sem frequencia valida: recusado sem tocar na BSP
void Test_SpiBus_ConfigInvalido(void)
{
	IniciaSpiBus();

	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_INVALID_ARGUMENT, SpiBus_Config(2U, 0U, 1000000U));
	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_INVALID_ARGUMENT, SpiBus_Config(dSPIBUS_PAPEL_MESTRE, 4U, 1000000U));
	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_INVALID_ARGUMENT, SpiBus_Config(dSPIBUS_PAPEL_MESTRE, 0U, 0U));
	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_INVALID_ARGUMENT, SpiBus_Config(dSPIBUS_PAPEL_MESTRE, 0U, 100000U));
	TEST_ASSERT_EQUAL_UINT8(0, barramentoFalso.configChamadas);
}

// 7. Falha da BSP ao configurar: o barramento fica desconfigurado (sem papel de mestre)
void Test_SpiBus_ConfigFalhaBsp(void)
{
	const uint8_t escrita[1] = {0x55U};
	uint8_t leitura[1];

	IniciaSpiBus();
	barramentoFalso.retornoConfig = eSPIBUS_RETURN_ERROR;

	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_ERROR, SpiBus_Config(dSPIBUS_PAPEL_MESTRE, 0U, 1000000U));
	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_NOT_CONFIGURED, SpiBus_MasterXfer(escrita, leitura, 1U));
}

// 8. Load: a transacao do DUT recebe os bytes carregados e depois 0xFF, a partir do byte 0
void Test_SpiBus_LoadPreparaTx(void)
{
	const uint8_t dados[3] = {0xA1U, 0xA2U, 0xA3U};
	uint8_t tx[dSPIBUS_TAMANHO_BUFFER];

	IniciaSpiBus();
	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_OK, SpiBus_Load(dados, 3U));

	SpiBus_PreparaTx(tx);
	TEST_ASSERT_EQUAL_UINT8_ARRAY(dados, tx, 3);
	TEST_ASSERT_EQUAL_HEX8(dSPIBUS_BYTE_VAZIO, tx[3]);
	TEST_ASSERT_EQUAL_HEX8(dSPIBUS_BYTE_VAZIO, tx[dSPIBUS_TAMANHO_BUFFER - 1U]);

	SpiBus_PreparaTx(tx);   // proxima transacao: de novo a partir do byte 0
	TEST_ASSERT_EQUAL_HEX8(0xA1U, tx[0]);
}

// 9. Load de 239 bytes (o maximo) e Load de 0 (esvazia: o escravo envia so 0xFF)
void Test_SpiBus_LoadLimites(void)
{
	uint8_t dados[dSPIBUS_TAMANHO_BUFFER];
	uint8_t tx[dSPIBUS_TAMANHO_BUFFER];

	IniciaSpiBus();
	memset(dados, 0x3CU, sizeof(dados));

	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_OK, SpiBus_Load(dados, dSPIBUS_TAMANHO_BUFFER));
	SpiBus_PreparaTx(tx);
	TEST_ASSERT_EQUAL_HEX8(0x3CU, tx[dSPIBUS_TAMANHO_BUFFER - 1U]);

	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_OK, SpiBus_Load(NULL, 0U));
	SpiBus_PreparaTx(tx);
	TEST_ASSERT_EQUAL_HEX8(dSPIBUS_BYTE_VAZIO, tx[0]);
}

// 10. Load de 240 bytes ou sem dados: recusado e o buffer nao muda
void Test_SpiBus_LoadInvalido(void)
{
	const uint8_t dados[1] = {0x42U};
	uint8_t grande[dSPIBUS_TAMANHO_BUFFER + 1U] = {0};
	uint8_t tx[dSPIBUS_TAMANHO_BUFFER];

	IniciaSpiBus();
	SpiBus_Load(dados, 1U);

	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_INVALID_ARGUMENT, SpiBus_Load(grande, dSPIBUS_TAMANHO_BUFFER + 1U));
	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_INVALID_ARGUMENT, SpiBus_Load(NULL, 1U));

	SpiBus_PreparaTx(tx);
	TEST_ASSERT_EQUAL_HEX8(0x42U, tx[0]);
	TEST_ASSERT_EQUAL_HEX8(dSPIBUS_BYTE_VAZIO, tx[1]);
}

// 11. Duas transacoes do DUT se acumulam; o Read devolve tudo e esvazia
void Test_SpiBus_FimTransacaoERead(void)
{
	const uint8_t primeira[2] = {0x01U, 0x02U};
	const uint8_t segunda[3]  = {0x03U, 0x04U, 0x05U};
	const uint8_t esperado[5] = {0x01U, 0x02U, 0x03U, 0x04U, 0x05U};
	uint8_t lidos[dSPIBUS_TAMANHO_BUFFER];
	uint8_t quantidade = 0U;

	IniciaSpiBus();
	SpiBus_FimTransacao(primeira, 2U);
	SpiBus_FimTransacao(segunda, 3U);

	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_OK, SpiBus_Read(lidos, &quantidade));
	TEST_ASSERT_EQUAL_UINT8(5U, quantidade);
	TEST_ASSERT_EQUAL_UINT8_ARRAY(esperado, lidos, 5);

	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_OK, SpiBus_Read(lidos, &quantidade));
	TEST_ASSERT_EQUAL_UINT8(0U, quantidade);
}

// 12. Mais de 239 bytes entre leituras: guarda os primeiros 239 e descarta o resto
void Test_SpiBus_RecepcaoCheia(void)
{
	uint8_t bloco[200];
	uint8_t lidos[dSPIBUS_TAMANHO_BUFFER];
	uint8_t quantidade = 0U;

	IniciaSpiBus();
	memset(bloco, 0x11U, sizeof(bloco));
	SpiBus_FimTransacao(bloco, 200U);
	memset(bloco, 0x22U, sizeof(bloco));
	SpiBus_FimTransacao(bloco, 100U);
	SpiBus_FimTransacao(bloco, 5U);   // buffer ja cheio

	SpiBus_Read(lidos, &quantidade);
	TEST_ASSERT_EQUAL_UINT8(dSPIBUS_TAMANHO_BUFFER, quantidade);
	TEST_ASSERT_EQUAL_HEX8(0x11U, lidos[199]);
	TEST_ASSERT_EQUAL_HEX8(0x22U, lidos[200]);
	TEST_ASSERT_EQUAL_HEX8(0x22U, lidos[dSPIBUS_TAMANHO_BUFFER - 1U]);
}

// 13. Load e Read sem SPI_CONFIG, com as IRQs travadas na copia
void Test_SpiBus_LoadReadSemConfigTravam(void)
{
	const uint8_t dados[1] = {0x5AU};
	uint8_t lidos[dSPIBUS_TAMANHO_BUFFER];
	uint8_t quantidade = 0U;

	IniciaSpiBus();

	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_OK, SpiBus_Load(dados, 1U));
	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_OK, SpiBus_Read(lidos, &quantidade));
	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_INVALID_ARGUMENT, SpiBus_Read(NULL, &quantidade));
	TEST_ASSERT_EQUAL_UINT8(0, barramentoFalso.configChamadas);
	TEST_ASSERT_EQUAL_UINT8(2, barramentoFalso.lockChamadas);
	TEST_ASSERT_EQUAL_UINT8(2, barramentoFalso.unlockChamadas);
}

// 14. Transferencia de mestre sem o papel de mestre: NAO_CONFIGURADO, sem tocar no barramento
void Test_SpiBus_MasterXferNaoConfigurado(void)
{
	const uint8_t escrita[1] = {0x55U};
	uint8_t leitura[1];

	IniciaSpiBus();
	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_NOT_CONFIGURED, SpiBus_MasterXfer(escrita, leitura, 1U));

	SpiBus_Config(dSPIBUS_PAPEL_ESCRAVO, 0U, 0U);
	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_NOT_CONFIGURED, SpiBus_MasterXfer(escrita, leitura, 1U));
	TEST_ASSERT_EQUAL_UINT8(0, barramentoFalso.xferChamadas);
}

// 15. Mestre: envia 2 bytes e recebe 2; quantidade 0, 240 ou sem buffer e recusado; falha da BSP
void Test_SpiBus_MasterXfer(void)
{
	const uint8_t escrita[2] = {0x9FU, 0x00U};
	uint8_t leitura[2] = {0};

	IniciaSpiBus();
	SpiBus_Config(dSPIBUS_PAPEL_MESTRE, 0U, 1000000U);

	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_INVALID_ARGUMENT, SpiBus_MasterXfer(escrita, leitura, 0U));
	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_INVALID_ARGUMENT, SpiBus_MasterXfer(escrita, leitura, dSPIBUS_TAMANHO_BUFFER + 1U));
	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_INVALID_ARGUMENT, SpiBus_MasterXfer(NULL, leitura, 2U));
	TEST_ASSERT_EQUAL_UINT8(0, barramentoFalso.xferChamadas);

	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_OK, SpiBus_MasterXfer(escrita, leitura, 2U));
	TEST_ASSERT_EQUAL_UINT8(2U, barramentoFalso.xferQuantidade);
	TEST_ASSERT_EQUAL_HEX8(0x9FU, barramentoFalso.xferEscrita[0]);
	TEST_ASSERT_EQUAL_HEX8(0x60U, leitura[0]);
	TEST_ASSERT_EQUAL_HEX8(0xFFU, leitura[1]);

	barramentoFalso.retornoXfer = eSPIBUS_RETURN_ERROR;
	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_ERROR, SpiBus_MasterXfer(escrita, leitura, 2U));
}

// 16. Estado seguro: desliga o barramento, esvazia os buffers e desconfigura
void Test_SpiBus_SafeState(void)
{
	const uint8_t dados[2] = {0x99U, 0x98U};
	uint8_t tx[dSPIBUS_TAMANHO_BUFFER];
	uint8_t lidos[dSPIBUS_TAMANHO_BUFFER];
	uint8_t quantidade = 0xFFU;
	uint8_t leitura[1];

	IniciaSpiBus();
	SpiBus_Config(dSPIBUS_PAPEL_MESTRE, 0U, 1000000U);
	SpiBus_Load(dados, 2U);
	SpiBus_FimTransacao(dados, 2U);

	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_OK, SpiBus_SafeState());
	TEST_ASSERT_EQUAL_UINT8(1, barramentoFalso.offChamadas);

	SpiBus_PreparaTx(tx);
	TEST_ASSERT_EQUAL_HEX8(dSPIBUS_BYTE_VAZIO, tx[0]);
	SpiBus_Read(lidos, &quantidade);
	TEST_ASSERT_EQUAL_UINT8(0U, quantidade);
	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_NOT_CONFIGURED, SpiBus_MasterXfer(dados, leitura, 1U));
}

// 17. Falha da BSP ao desligar: os buffers sao esvaziados mesmo assim e o erro e informado
void Test_SpiBus_SafeStateComFalha(void)
{
	const uint8_t dados[1] = {0x77U};
	uint8_t tx[dSPIBUS_TAMANHO_BUFFER];

	IniciaSpiBus();
	SpiBus_Load(dados, 1U);
	barramentoFalso.retornoOff = eSPIBUS_RETURN_ERROR;

	TEST_ASSERT_EQUAL(eSPIBUS_RETURN_ERROR, SpiBus_SafeState());
	SpiBus_PreparaTx(tx);
	TEST_ASSERT_EQUAL_HEX8(dSPIBUS_BYTE_VAZIO, tx[0]);
}
