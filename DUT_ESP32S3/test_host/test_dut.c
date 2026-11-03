/*
 * test_dut.c
 *
 * Testes no PC do interpretador de comandos e do mapa I2C do DUT de referencia, com barramentos falsos.
 * Usa o Unity do firmware da jiga (projeto/App/Test/Unity). Rodar com test_host/rodar.sh.
 */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "comandos.h"
#include "mapa_i2c.h"
#include "versao_dut.h"

/// Registro das chamadas aos barramentos falsos
static struct barramentosFalsos
{
	int         desliga;
	int         i2cMestre;
	uint8_t     endereco;
	uint8_t     escrita[dCOMANDOS_DADOS_MAX];
	size_t      nw;
	size_t      nr;
	int         i2cEscrava;
	int         spiMestre;
	uint8_t     modo;
	uint32_t    frequencia;
	size_t      quantidade;
	int         spiEscrava;
	uint8_t     spiCarga[dCOMANDOS_DADOS_MAX];
	size_t      spiCargaQtd;
	uint8_t     spiRecebido[dCOMANDOS_DADOS_MAX];
	size_t      spiRecebidoQtd;
	dutReturn_t retorno;   ///< o que as operacoes devolvem (simula NACK, ocupado, falha)
} falso;

static dutReturn_t Falso_Desliga(void)
{
	falso.desliga++;
	return falso.retorno;
}

// Le 0xA0, 0xA1, ... do dispositivo
static dutReturn_t Falso_I2cMestre(uint8_t endereco, const uint8_t *escrita, size_t nw, uint8_t *leitura, size_t nr)
{
	falso.i2cMestre++;
	falso.endereco = endereco;
	memcpy(falso.escrita, escrita, nw);
	falso.nw = nw;
	falso.nr = nr;
	for (size_t i = 0U; i < nr; i++)
	{
		leitura[i] = (uint8_t)(0xA0U + i);
	}
	return falso.retorno;
}

static dutReturn_t Falso_I2cEscrava(uint8_t endereco)
{
	falso.i2cEscrava++;
	falso.endereco = endereco;
	return falso.retorno;
}

static dutReturn_t Falso_I2cCarrega(uint8_t offset, const uint8_t *dados, size_t quantidade)
{
	return (MapaI2c_Carrega(offset, dados, quantidade) == eMAPAI2C_RETURN_OK) ? eDUT_RETURN_OK :
	       eDUT_RETURN_INVALID_ARGUMENT;
}

static dutReturn_t Falso_I2cLe(uint8_t offset, uint8_t *dados, size_t quantidade)
{
	return (MapaI2c_Le(offset, dados, quantidade) == eMAPAI2C_RETURN_OK) ? eDUT_RETURN_OK :
	       eDUT_RETURN_INVALID_ARGUMENT;
}

// Devolve o complemento de cada byte enviado
static dutReturn_t Falso_SpiMestre(uint8_t modo, uint32_t frequencia, const uint8_t *escrita, uint8_t *leitura,
                                   size_t quantidade)
{
	falso.spiMestre++;
	falso.modo = modo;
	falso.frequencia = frequencia;
	falso.quantidade = quantidade;
	for (size_t i = 0U; i < quantidade; i++)
	{
		leitura[i] = (uint8_t)~escrita[i];
	}
	return falso.retorno;
}

static dutReturn_t Falso_SpiEscrava(uint8_t modo)
{
	falso.spiEscrava++;
	falso.modo = modo;
	return falso.retorno;
}

static dutReturn_t Falso_SpiCarrega(const uint8_t *dados, size_t quantidade)
{
	memcpy(falso.spiCarga, dados, quantidade);
	falso.spiCargaQtd = quantidade;
	return falso.retorno;
}

static dutReturn_t Falso_SpiLe(uint8_t *dados, size_t *quantidade)
{
	memcpy(dados, falso.spiRecebido, falso.spiRecebidoQtd);
	*quantidade = falso.spiRecebidoQtd;
	falso.spiRecebidoQtd = 0U;
	return falso.retorno;
}

static const dutBarramentos_t barramentosFalsos =
{
	Falso_Desliga, Falso_I2cMestre, Falso_I2cEscrava, Falso_I2cCarrega, Falso_I2cLe,
	Falso_SpiMestre, Falso_SpiEscrava, Falso_SpiCarrega, Falso_SpiLe,
};

static char resposta[dCOMANDOS_RESPOSTA_MAX];

// Executa a linha e devolve a resposta (com o '\n')
static const char *Executa(const char *linha)
{
	memset(resposta, 0, sizeof(resposta));
	Comandos_Executa(linha, resposta, sizeof(resposta));
	return resposta;
}

void setUp(void)
{
	memset(&falso, 0, sizeof(falso));
	falso.retorno = eDUT_RETURN_OK;
	MapaI2c_Init();
	Comandos_Init(&barramentosFalsos);
}

void tearDown(void)
{
}

// 1. PING responde a identificacao e a versao; '\r\n' no fim da linha e ignorado
void Test_Dut_Ping(void)
{
	TEST_ASSERT_EQUAL_STRING("OK DUT ESP32-S3 " dDUT_VERSAO "\n", Executa("PING\r\n"));
	TEST_ASSERT_EQUAL_STRING("ERR ARG\n", Executa("PING 1"));
}

// 2. Linha vazia nao tem resposta; comando desconhecido ou em minusculas -> ERR CMD
void Test_Dut_LinhaVaziaEDesconhecido(void)
{
	char buffer[32] = "x";
	TEST_ASSERT_EQUAL_UINT32(0, Comandos_Executa("  \r\n", buffer, sizeof(buffer)));
	TEST_ASSERT_EQUAL_STRING("ERR CMD\n", Executa("RESET"));
	TEST_ASSERT_EQUAL_STRING("ERR CMD\n", Executa("ping"));
}

// 3. OFF desliga os barramentos
void Test_Dut_Off(void)
{
	TEST_ASSERT_EQUAL_STRING("OK\n", Executa("OFF"));
	TEST_ASSERT_EQUAL_INT(1, falso.desliga);
}

// 4. I2CM com escrita do registrador e leitura de 4 bytes: OK com os bytes lidos em hex
void Test_Dut_I2cMestreLeitura(void)
{
	TEST_ASSERT_EQUAL_STRING("OK A0A1A2A3\n", Executa("I2CM 50 10 4"));
	TEST_ASSERT_EQUAL_HEX8(0x50, falso.endereco);
	TEST_ASSERT_EQUAL_UINT32(1, falso.nw);
	TEST_ASSERT_EQUAL_HEX8(0x10, falso.escrita[0]);
	TEST_ASSERT_EQUAL_UINT32(4, falso.nr);
}

// 5. I2CM so de escrita ("OK" sem dados) e so de leitura ("-")
void Test_Dut_I2cMestreEscritaELeitura(void)
{
	TEST_ASSERT_EQUAL_STRING("OK\n", Executa("I2CM 28 00DEad 0"));
	TEST_ASSERT_EQUAL_UINT32(3, falso.nw);
	TEST_ASSERT_EQUAL_HEX8(0xAD, falso.escrita[2]);
	TEST_ASSERT_EQUAL_STRING("OK A0A1\n", Executa("I2CM 28 - 2"));
	TEST_ASSERT_EQUAL_UINT32(0, falso.nw);
}

// 6. I2CM com endereco reservado, hex invalido, nr 240 ou transacao vazia: ERR ARG sem tocar no barramento
void Test_Dut_I2cMestreInvalido(void)
{
	TEST_ASSERT_EQUAL_STRING("ERR ARG\n", Executa("I2CM 78 10 1"));
	TEST_ASSERT_EQUAL_STRING("ERR ARG\n", Executa("I2CM 07 10 1"));
	TEST_ASSERT_EQUAL_STRING("ERR ARG\n", Executa("I2CM 50 1 1"));
	TEST_ASSERT_EQUAL_STRING("ERR ARG\n", Executa("I2CM 50 1G 1"));
	TEST_ASSERT_EQUAL_STRING("ERR ARG\n", Executa("I2CM 50 10 240"));
	TEST_ASSERT_EQUAL_STRING("ERR ARG\n", Executa("I2CM 50 - 0"));
	TEST_ASSERT_EQUAL_STRING("ERR ARG\n", Executa("I2CM 50 10"));
	TEST_ASSERT_EQUAL_INT(0, falso.i2cMestre);
}

// 7. Dispositivo que nao responde: ERR NACK
void Test_Dut_I2cMestreNack(void)
{
	falso.retorno = eDUT_RETURN_NACK;
	TEST_ASSERT_EQUAL_STRING("ERR NACK\n", Executa("I2CM 50 10 4"));
}

// 8. I2CS no endereco; endereco invalido -> ERR ARG; barramento ocupado -> ERR ESTADO
void Test_Dut_I2cEscrava(void)
{
	TEST_ASSERT_EQUAL_STRING("OK\n", Executa("I2CS 28"));
	TEST_ASSERT_EQUAL_HEX8(0x28, falso.endereco);
	TEST_ASSERT_EQUAL_STRING("ERR ARG\n", Executa("I2CS 80"));
	falso.retorno = eDUT_RETURN_BUSY;
	TEST_ASSERT_EQUAL_STRING("ERR ESTADO\n", Executa("I2CS 28"));
	TEST_ASSERT_EQUAL_INT(2, falso.i2cEscrava);
}

// 9. I2CS_LOAD e I2CS_DUMP de ida e volta no mapa
void Test_Dut_I2cCarregaELe(void)
{
	TEST_ASSERT_EQUAL_STRING("OK\n", Executa("I2CS_LOAD FE CAFE"));
	TEST_ASSERT_EQUAL_STRING("OK 00CAFE\n", Executa("I2CS_DUMP FD 3"));
}

// 10. I2CS_LOAD/DUMP alem do fim do mapa, vazio ou com n 0: ERR ARG
void Test_Dut_I2cCarregaInvalido(void)
{
	TEST_ASSERT_EQUAL_STRING("ERR ARG\n", Executa("I2CS_LOAD FF 0102"));
	TEST_ASSERT_EQUAL_STRING("ERR ARG\n", Executa("I2CS_LOAD 00 -"));
	TEST_ASSERT_EQUAL_STRING("ERR ARG\n", Executa("I2CS_DUMP FF 2"));
	TEST_ASSERT_EQUAL_STRING("ERR ARG\n", Executa("I2CS_DUMP 00 0"));
}

// 11. SPIM no modo 3 a 1 MHz: OK com os bytes recebidos
void Test_Dut_SpiMestre(void)
{
	TEST_ASSERT_EQUAL_STRING("OK 60FF00\n", Executa("SPIM 3 1000000 9F00FF"));
	TEST_ASSERT_EQUAL_UINT8(3, falso.modo);
	TEST_ASSERT_EQUAL_UINT32(1000000, falso.frequencia);
	TEST_ASSERT_EQUAL_UINT32(3, falso.quantidade);
}

// 12. SPIM com modo 4, frequencia fora da faixa ou sem dados: ERR ARG sem tocar no barramento
void Test_Dut_SpiMestreInvalido(void)
{
	TEST_ASSERT_EQUAL_STRING("ERR ARG\n", Executa("SPIM 4 1000000 9F"));
	TEST_ASSERT_EQUAL_STRING("ERR ARG\n", Executa("SPIM 0 999 9F"));
	TEST_ASSERT_EQUAL_STRING("ERR ARG\n", Executa("SPIM 0 10000001 9F"));
	TEST_ASSERT_EQUAL_STRING("ERR ARG\n", Executa("SPIM 0 1000000 -"));
	TEST_ASSERT_EQUAL_STRING("ERR ARG\n", Executa("SPIM 0 1k 9F"));
	TEST_ASSERT_EQUAL_INT(0, falso.spiMestre);
}

// 13. SPIS arma o escravo; SPIS_LOAD carrega (vazio com "-"); SPIS_READ devolve o recebido e esvazia
void Test_Dut_SpiEscrava(void)
{
	TEST_ASSERT_EQUAL_STRING("OK\n", Executa("SPIS 1"));
	TEST_ASSERT_EQUAL_UINT8(1, falso.modo);
	TEST_ASSERT_EQUAL_STRING("OK\n", Executa("SPIS_LOAD 112233"));
	TEST_ASSERT_EQUAL_UINT32(3, falso.spiCargaQtd);
	TEST_ASSERT_EQUAL_HEX8(0x33, falso.spiCarga[2]);
	TEST_ASSERT_EQUAL_STRING("OK\n", Executa("SPIS_LOAD -"));
	TEST_ASSERT_EQUAL_UINT32(0, falso.spiCargaQtd);

	falso.spiRecebido[0] = 0x5A;
	falso.spiRecebido[1] = 0xA5;
	falso.spiRecebidoQtd = 2U;
	TEST_ASSERT_EQUAL_STRING("OK 5AA5\n", Executa("SPIS_READ"));
	TEST_ASSERT_EQUAL_STRING("OK\n", Executa("SPIS_READ"));
}

// 14. Resposta maxima: 239 bytes lidos cabem em dCOMANDOS_RESPOSTA_MAX ("OK " + 478 + "\n")
void Test_Dut_RespostaMaxima(void)
{
	char linha[64];
	snprintf(linha, sizeof(linha), "I2CM 50 - %u", (unsigned)dCOMANDOS_DADOS_MAX);
	const char *texto = Executa(linha);
	TEST_ASSERT_EQUAL_UINT32(3U + (2U * dCOMANDOS_DADOS_MAX) + 1U, strlen(texto));
	TEST_ASSERT_EQUAL_CHAR('\n', texto[strlen(texto) - 1U]);
}

// 15. Campos demais ou linha longa demais: ERR ARG
void Test_Dut_LinhaInvalida(void)
{
	static char longa[dCOMANDOS_LINHA_MAX + 8U];
	TEST_ASSERT_EQUAL_STRING("ERR ARG\n", Executa("I2CM 50 10 4 9 9"));
	memset(longa, 'A', sizeof(longa) - 1U);
	TEST_ASSERT_EQUAL_STRING("ERR ARG\n", Executa(longa));
}

// 16. Mapa: escrita do mestre com ponteiro + dados; leitura a partir do ponteiro, sem move-lo; volta de 0xFF a 0x00
void Test_Dut_MapaEscritaELeitura(void)
{
	const uint8_t escrita[] = {0xFE, 0x11, 0x22, 0x33};   // ponteiro 0xFE, grava FE, FF e 00
	uint8_t lido[3];

	MapaI2c_Escrita(escrita, sizeof(escrita));
	MapaI2c_Le(0xFE, lido, 2);
	TEST_ASSERT_EQUAL_HEX8(0x11, lido[0]);
	TEST_ASSERT_EQUAL_HEX8(0x22, lido[1]);
	MapaI2c_Le(0x00, lido, 1);
	TEST_ASSERT_EQUAL_HEX8(0x33, lido[0]);

	MapaI2c_Escrita(escrita, 1);   // so o ponteiro
	memset(lido, 0, sizeof(lido));
	MapaI2c_Leitura(lido, 3);
	TEST_ASSERT_EQUAL_HEX8(0x11, lido[0]);
	TEST_ASSERT_EQUAL_HEX8(0x33, lido[2]);
	MapaI2c_Leitura(lido, 1);       // de novo do ponteiro
	TEST_ASSERT_EQUAL_HEX8(0x11, lido[0]);
}

// 17. Mapa: carga e leitura sem mexer no ponteiro; limites
void Test_Dut_MapaCargaNaoMexeNoPonteiro(void)
{
	const uint8_t ponteiro[] = {0x10};
	const uint8_t carga[] = {0xAB};

	MapaI2c_Escrita(ponteiro, 1);
	TEST_ASSERT_EQUAL(eMAPAI2C_RETURN_OK, MapaI2c_Carrega(0x10, carga, 1));
	TEST_ASSERT_EQUAL(eMAPAI2C_RETURN_INVALID_ARGUMENT, MapaI2c_Carrega(0xFF, carga, 2));
	TEST_ASSERT_EQUAL(eMAPAI2C_RETURN_INVALID_ARGUMENT, MapaI2c_Carrega(0x00, carga, 0));
	uint8_t lido = 0U;
	MapaI2c_Leitura(&lido, 1);
	TEST_ASSERT_EQUAL_HEX8(0xAB, lido);
}

void TestOutput_PutChar(int c)
{
	putchar(c);
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(Test_Dut_Ping);
	RUN_TEST(Test_Dut_LinhaVaziaEDesconhecido);
	RUN_TEST(Test_Dut_Off);
	RUN_TEST(Test_Dut_I2cMestreLeitura);
	RUN_TEST(Test_Dut_I2cMestreEscritaELeitura);
	RUN_TEST(Test_Dut_I2cMestreInvalido);
	RUN_TEST(Test_Dut_I2cMestreNack);
	RUN_TEST(Test_Dut_I2cEscrava);
	RUN_TEST(Test_Dut_I2cCarregaELe);
	RUN_TEST(Test_Dut_I2cCarregaInvalido);
	RUN_TEST(Test_Dut_SpiMestre);
	RUN_TEST(Test_Dut_SpiMestreInvalido);
	RUN_TEST(Test_Dut_SpiEscrava);
	RUN_TEST(Test_Dut_RespostaMaxima);
	RUN_TEST(Test_Dut_LinhaInvalida);
	RUN_TEST(Test_Dut_MapaEscritaELeitura);
	RUN_TEST(Test_Dut_MapaCargaNaoMexeNoPonteiro);
	return UNITY_END();
}
