/*
 * Test_I2cBus.c
 *
 * Testes do I2cBus com um barramento falso: configuracao (sem tocar na BSP quando invalida), carga e leitura do
 * mapa, eventos do barramento como o DUT faria (escrita do ponteiro, auto-incremento, volta de 0xFF para 0x00),
 * transferencia de mestre e estado seguro.
 */

#include <string.h>
#include "unity.h"
#include "I2cBus.h"

/// Registro das chamadas ao barramento falso
static struct barramentoFalso
{
	uint8_t configChamadas;
	uint8_t offChamadas;
	uint8_t xferChamadas;
	uint8_t lockChamadas;
	uint8_t unlockChamadas;

	uint8_t papel;
	uint8_t endereco;
	uint8_t velocidade;

	uint8_t xferEndereco;
	uint8_t xferEscrita[4];
	uint8_t xferNw;
	uint8_t xferNr;

	i2cBusReturn_t retornoConfig;   ///< simula falha do HAL
	i2cBusReturn_t retornoOff;
	i2cBusReturn_t retornoXfer;
} barramentoFalso;

static i2cBusReturn_t BarramentoFalso_Config(uint8_t papel, uint8_t endereco, uint8_t velocidade)
{
	barramentoFalso.configChamadas++;
	barramentoFalso.papel      = papel;
	barramentoFalso.endereco   = endereco;
	barramentoFalso.velocidade = velocidade;

	return barramentoFalso.retornoConfig;
}

static i2cBusReturn_t BarramentoFalso_Off(void)
{
	barramentoFalso.offChamadas++;

	return barramentoFalso.retornoOff;
}

// Guarda o que o mestre escreveu e devolve 0xA0, 0xA1, ... na leitura
static i2cBusReturn_t BarramentoFalso_Xfer(uint8_t endereco, const uint8_t *escrita, uint8_t nw, uint8_t *leitura,
                                           uint8_t nr)
{
	barramentoFalso.xferChamadas++;
	barramentoFalso.xferEndereco = endereco;
	barramentoFalso.xferNw       = nw;
	barramentoFalso.xferNr       = nr;
	memcpy(barramentoFalso.xferEscrita, escrita, (nw < 4U) ? nw : 4U);

	for (uint8_t i = 0U; i < nr; i++)
	{
		leitura[i] = (uint8_t)(0xA0U + i);
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

static const i2cBusFunc_t funcFalsas =
{
	BarramentoFalso_Config, BarramentoFalso_Off, BarramentoFalso_Xfer, BarramentoFalso_Lock, BarramentoFalso_Unlock,
};

// O I2cBus passa a usar o barramento falso. Na build Test nada mais usa o I2C2, entao nao precisa restaurar.
static void IniciaI2cBus(void)
{
	memset(&barramentoFalso, 0, sizeof(barramentoFalso));
	barramentoFalso.retornoConfig = eI2CBUS_RETURN_OK;
	barramentoFalso.retornoOff    = eI2CBUS_RETURN_OK;
	barramentoFalso.retornoXfer   = eI2CBUS_RETURN_OK;
	I2cBus_Init(&funcFalsas);
}

// Escrita do DUT como mestre: endereco (escrita) + ponteiro + dados
static void DutEscreve(uint8_t ponteiro, const uint8_t *dados, uint8_t quantidade)
{
	I2cBus_InicioEscrita();
	I2cBus_ByteRecebido(ponteiro);
	for (uint8_t i = 0U; i < quantidade; i++)
	{
		I2cBus_ByteRecebido(dados[i]);
	}
}

// 1. Init sem as funcoes da BSP e recusado
void Test_I2cBus_InitInvalido(void)
{
	i2cBusFunc_t semLock = funcFalsas;
	semLock.lock = NULL;

	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_INVALID_ARGUMENT, I2cBus_Init(NULL));
	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_INVALID_ARGUMENT, I2cBus_Init(&semLock));
}

// 2. Escravo no endereco 0x50 a 100 kHz: os parametros chegam na BSP
void Test_I2cBus_ConfigEscravo(void)
{
	IniciaI2cBus();

	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_OK, I2cBus_Config(dI2CBUS_PAPEL_ESCRAVO, 0x50U, 0U));
	TEST_ASSERT_EQUAL_UINT8(1, barramentoFalso.configChamadas);
	TEST_ASSERT_EQUAL_UINT8(dI2CBUS_PAPEL_ESCRAVO, barramentoFalso.papel);
	TEST_ASSERT_EQUAL_UINT8(0x50U, barramentoFalso.endereco);
	TEST_ASSERT_EQUAL_UINT8(0U, barramentoFalso.velocidade);
}

// 3. Endereco reservado, papel ou velocidade fora da faixa: recusado sem tocar na BSP
void Test_I2cBus_ConfigInvalido(void)
{
	IniciaI2cBus();

	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_INVALID_ARGUMENT, I2cBus_Config(dI2CBUS_PAPEL_ESCRAVO, 0x07U, 0U));
	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_INVALID_ARGUMENT, I2cBus_Config(dI2CBUS_PAPEL_ESCRAVO, 0x78U, 0U));
	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_INVALID_ARGUMENT, I2cBus_Config(2U, 0x50U, 0U));
	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_INVALID_ARGUMENT, I2cBus_Config(dI2CBUS_PAPEL_ESCRAVO, 0x50U, 2U));
	TEST_ASSERT_EQUAL_UINT8(0, barramentoFalso.configChamadas);
}

// 4. Como mestre o endereco nao e usado: 0 e aceito
void Test_I2cBus_ConfigMestreIgnoraEndereco(void)
{
	IniciaI2cBus();

	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_OK, I2cBus_Config(dI2CBUS_PAPEL_MESTRE, 0U, 1U));
	TEST_ASSERT_EQUAL_UINT8(dI2CBUS_PAPEL_MESTRE, barramentoFalso.papel);
	TEST_ASSERT_EQUAL_UINT8(1U, barramentoFalso.velocidade);
}

// 5. Falha da BSP ao configurar: o barramento fica desconfigurado (sem papel de mestre)
void Test_I2cBus_ConfigFalhaBsp(void)
{
	uint8_t leitura[1];

	IniciaI2cBus();
	barramentoFalso.retornoConfig = eI2CBUS_RETURN_ERROR;

	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_ERROR, I2cBus_Config(dI2CBUS_PAPEL_MESTRE, 0U, 0U));
	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_NOT_CONFIGURED, I2cBus_MasterXfer(0x50U, NULL, 0U, leitura, 1U));
}

// 6. Load e Dump: ida e volta, inclusive o fim do mapa
void Test_I2cBus_LoadDump(void)
{
	const uint8_t dados[6] = {0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U};
	uint8_t lidos[6] = {0};

	IniciaI2cBus();

	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_OK, I2cBus_Load(250U, 6U, dados));
	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_OK, I2cBus_Dump(250U, 6U, lidos));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(dados, lidos, 6);

	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_OK, I2cBus_Dump(249U, 1U, lidos));
	TEST_ASSERT_EQUAL_UINT8(0U, lidos[0]);
}

// 7. Quantidade 0, alem do fim do mapa ou sem buffer: recusado e o mapa nao muda
void Test_I2cBus_LoadDumpInvalido(void)
{
	const uint8_t dados[2] = {0x11U, 0x22U};
	uint8_t lidos[2] = {0};

	IniciaI2cBus();

	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_INVALID_ARGUMENT, I2cBus_Load(0U, 0U, dados));
	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_INVALID_ARGUMENT, I2cBus_Load(255U, 2U, dados));   // 255 + 2 = 257
	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_INVALID_ARGUMENT, I2cBus_Load(0U, 2U, NULL));
	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_INVALID_ARGUMENT, I2cBus_Dump(255U, 2U, lidos));
	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_INVALID_ARGUMENT, I2cBus_Dump(0U, 0U, lidos));
	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_INVALID_ARGUMENT, I2cBus_Dump(0U, 1U, NULL));

	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_OK, I2cBus_Dump(254U, 2U, lidos));
	TEST_ASSERT_EQUAL_UINT8(0U, lidos[0]);
	TEST_ASSERT_EQUAL_UINT8(0U, lidos[1]);
	TEST_ASSERT_EQUAL_UINT8(1, barramentoFalso.lockChamadas);   // so o Dump valido travou
}

// 8. Load e Dump sem I2C_CONFIG (o PC carrega o mapa antes de ligar o escravo), com a IRQ travada na copia
void Test_I2cBus_LoadSemConfigTrava(void)
{
	const uint8_t dados[1] = {0x5AU};
	uint8_t lidos[1] = {0};

	IniciaI2cBus();

	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_OK, I2cBus_Load(0x10U, 1U, dados));
	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_OK, I2cBus_Dump(0x10U, 1U, lidos));
	TEST_ASSERT_EQUAL_UINT8(0x5AU, lidos[0]);
	TEST_ASSERT_EQUAL_UINT8(0, barramentoFalso.configChamadas);
	TEST_ASSERT_EQUAL_UINT8(2, barramentoFalso.lockChamadas);
	TEST_ASSERT_EQUAL_UINT8(2, barramentoFalso.unlockChamadas);
}

// 9. DUT escreve o ponteiro 0x10 e dois bytes: vao para 0x10 e 0x11
void Test_I2cBus_EscritaDoDut(void)
{
	const uint8_t dados[2] = {0xAAU, 0xBBU};
	uint8_t lidos[3] = {0};

	IniciaI2cBus();
	DutEscreve(0x10U, dados, 2U);

	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_OK, I2cBus_Dump(0x0FU, 3U, lidos));
	TEST_ASSERT_EQUAL_UINT8(0x00U, lidos[0]);
	TEST_ASSERT_EQUAL_UINT8(0xAAU, lidos[1]);
	TEST_ASSERT_EQUAL_UINT8(0xBBU, lidos[2]);
}

// 10. DUT le a partir do ponteiro, com auto-incremento
void Test_I2cBus_LeituraDoDut(void)
{
	const uint8_t dados[2] = {0xC1U, 0xC2U};

	IniciaI2cBus();
	I2cBus_Load(0x20U, 2U, dados);
	DutEscreve(0x20U, NULL, 0U);

	TEST_ASSERT_EQUAL_UINT8(0xC1U, I2cBus_ProximoByte());
	TEST_ASSERT_EQUAL_UINT8(0xC2U, I2cBus_ProximoByte());
}

// 11. Leitura de sensor: escrita so do ponteiro, repeated start, leitura; depois uma leitura sem ponteiro continua
void Test_I2cBus_PonteiroEntreLeituras(void)
{
	const uint8_t dados[3] = {0x01U, 0x02U, 0x03U};

	IniciaI2cBus();
	I2cBus_Load(0x40U, 3U, dados);

	DutEscreve(0x40U, NULL, 0U);
	TEST_ASSERT_EQUAL_UINT8(0x01U, I2cBus_ProximoByte());
	TEST_ASSERT_EQUAL_UINT8(0x02U, I2cBus_ProximoByte());

	TEST_ASSERT_EQUAL_UINT8(0x03U, I2cBus_ProximoByte());   // nova leitura, sem escrever o ponteiro
}

// 12. Leitura como o F7 faz: busca um byte a mais e devolve no fim; a leitura seguinte continua no certo
void Test_I2cBus_FimLeituraDevolveByte(void)
{
	const uint8_t dados[3] = {0x0AU, 0x0BU, 0x0CU};

	IniciaI2cBus();
	I2cBus_Load(0x30U, 3U, dados);
	DutEscreve(0x30U, NULL, 0U);

	TEST_ASSERT_EQUAL_UINT8(0x0AU, I2cBus_ProximoByte());   // lido pelo DUT
	TEST_ASSERT_EQUAL_UINT8(0x0BU, I2cBus_ProximoByte());   // buscado a mais, descartado no NACK
	I2cBus_FimLeitura();

	TEST_ASSERT_EQUAL_UINT8(0x0BU, I2cBus_ProximoByte());   // proxima leitura
}

// 13. O ponteiro volta de 0xFF para 0x00, na leitura e na escrita
void Test_I2cBus_PonteiroVolta(void)
{
	const uint8_t carga[1] = {0xEEU};
	const uint8_t dados[2] = {0x7AU, 0x7BU};
	uint8_t lidos[1] = {0};

	IniciaI2cBus();
	I2cBus_Load(0xFFU, 1U, carga);

	DutEscreve(0xFFU, NULL, 0U);
	TEST_ASSERT_EQUAL_UINT8(0xEEU, I2cBus_ProximoByte());
	TEST_ASSERT_EQUAL_UINT8(0x00U, I2cBus_ProximoByte());   // mapa[0x00]

	DutEscreve(0xFFU, dados, 2U);
	I2cBus_Dump(0x00U, 1U, lidos);
	TEST_ASSERT_EQUAL_UINT8(0x7BU, lidos[0]);
}

// 14. Transferencia de mestre sem o papel de mestre: NAO_CONFIGURADO, sem tocar no barramento
void Test_I2cBus_MasterXferNaoConfigurado(void)
{
	uint8_t leitura[1];

	IniciaI2cBus();
	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_NOT_CONFIGURED, I2cBus_MasterXfer(0x50U, NULL, 0U, leitura, 1U));

	I2cBus_Config(dI2CBUS_PAPEL_ESCRAVO, 0x50U, 0U);
	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_NOT_CONFIGURED, I2cBus_MasterXfer(0x50U, NULL, 0U, leitura, 1U));
	TEST_ASSERT_EQUAL_UINT8(0, barramentoFalso.xferChamadas);
}

// 15. Mestre: escreve o registrador 0x0F e le 2 bytes; argumentos invalidos sao recusados; NACK do dispositivo
void Test_I2cBus_MasterXfer(void)
{
	const uint8_t escrita[1] = {0x0FU};
	uint8_t leitura[2] = {0};

	IniciaI2cBus();
	I2cBus_Config(dI2CBUS_PAPEL_MESTRE, 0U, 0U);

	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_INVALID_ARGUMENT, I2cBus_MasterXfer(0x50U, escrita, 0U, leitura, 0U));
	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_INVALID_ARGUMENT, I2cBus_MasterXfer(0x78U, escrita, 1U, leitura, 2U));
	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_INVALID_ARGUMENT, I2cBus_MasterXfer(0x50U, NULL, 1U, leitura, 2U));
	TEST_ASSERT_EQUAL_UINT8(0, barramentoFalso.xferChamadas);

	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_OK, I2cBus_MasterXfer(0x68U, escrita, 1U, leitura, 2U));
	TEST_ASSERT_EQUAL_UINT8(0x68U, barramentoFalso.xferEndereco);
	TEST_ASSERT_EQUAL_UINT8(0x0FU, barramentoFalso.xferEscrita[0]);
	TEST_ASSERT_EQUAL_UINT8(1U, barramentoFalso.xferNw);
	TEST_ASSERT_EQUAL_UINT8(2U, barramentoFalso.xferNr);
	TEST_ASSERT_EQUAL_UINT8(0xA0U, leitura[0]);
	TEST_ASSERT_EQUAL_UINT8(0xA1U, leitura[1]);

	barramentoFalso.retornoXfer = eI2CBUS_RETURN_NACK;
	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_NACK, I2cBus_MasterXfer(0x68U, escrita, 1U, leitura, 2U));
}

// 16. Estado seguro: desliga o barramento, zera o mapa e o ponteiro e desconfigura
void Test_I2cBus_SafeState(void)
{
	const uint8_t dados[2] = {0x99U, 0x98U};
	uint8_t lidos[2] = {0xFFU, 0xFFU};
	uint8_t leitura[1];

	IniciaI2cBus();
	I2cBus_Config(dI2CBUS_PAPEL_MESTRE, 0U, 0U);
	I2cBus_Load(0U, 2U, dados);
	DutEscreve(0x01U, NULL, 0U);

	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_OK, I2cBus_SafeState());
	TEST_ASSERT_EQUAL_UINT8(1, barramentoFalso.offChamadas);

	I2cBus_Dump(0U, 2U, lidos);
	TEST_ASSERT_EQUAL_UINT8(0U, lidos[0]);
	TEST_ASSERT_EQUAL_UINT8(0U, lidos[1]);
	TEST_ASSERT_EQUAL_UINT8(0U, I2cBus_ProximoByte());   // ponteiro voltou a 0 (mapa[0])
	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_NOT_CONFIGURED, I2cBus_MasterXfer(0x50U, NULL, 0U, leitura, 1U));
}

// 17. Falha da BSP ao desligar: o mapa e zerado mesmo assim e o erro e informado
void Test_I2cBus_SafeStateComFalha(void)
{
	const uint8_t dados[1] = {0x77U};
	uint8_t lidos[1] = {0xFFU};

	IniciaI2cBus();
	I2cBus_Load(0U, 1U, dados);
	barramentoFalso.retornoOff = eI2CBUS_RETURN_ERROR;

	TEST_ASSERT_EQUAL(eI2CBUS_RETURN_ERROR, I2cBus_SafeState());
	I2cBus_Dump(0U, 1U, lidos);
	TEST_ASSERT_EQUAL_UINT8(0U, lidos[0]);
}
