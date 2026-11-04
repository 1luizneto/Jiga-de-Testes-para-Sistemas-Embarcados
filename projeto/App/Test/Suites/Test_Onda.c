/*
 * Test_Onda.c
 *
 * Testes do gerador de ondas com tempo e destinos falsos: validacao do passo ONDA, valor no tempo (interpolacao,
 * degrau, periodo), primeira escrita e agendamento, ticks, fim dos ciclos com o ultimo ponto, ruido dentro da faixa,
 * saturacao, DAC por DMA (amostras de um periodo) e parada no estado seguro.
 */

#include <string.h>
#include "unity.h"
#include "Onda.h"

/// Registro das chamadas aos destinos falsos
static struct ondaFalsa
{
	uint32_t agora;
	uint8_t  agendado;
	uint32_t alvo;
	uint8_t  dacCanal;
	uint16_t dacValor;
	uint16_t dacEscritas;
	uint8_t  pwmCanal;
	uint16_t pwmRazao;
	uint8_t  i2cOffset;
	uint8_t  i2cBytes[3];
	uint8_t  i2cQuantidade;
	uint8_t  dmaCanal;
	uint16_t dmaQuantidade;
	uint32_t dmaPasso;
	uint16_t dmaAmostras[4];
	uint8_t  dmaParadas;
} falsa;

static uint32_t Falsa_Agora(void) { return falsa.agora; }
static void Falsa_Agenda(uint32_t alvo) { falsa.agendado = 1U; falsa.alvo = alvo; }
static void Falsa_Desagenda(void) { falsa.agendado = 0U; }
static void Falsa_Dac(uint8_t canal, uint16_t valor) { falsa.dacCanal = canal; falsa.dacValor = valor; falsa.dacEscritas++; }
static void Falsa_Pwm(uint8_t canal, uint16_t razao) { falsa.pwmCanal = canal; falsa.pwmRazao = razao; }

static void Falsa_I2c(uint8_t offset, const uint8_t *dados, uint8_t quantidade)
{
	falsa.i2cOffset     = offset;
	falsa.i2cQuantidade = quantidade;
	memcpy(falsa.i2cBytes, dados, quantidade);
}

static uint8_t Falsa_DacDma(uint8_t canal, const uint16_t *amostras, uint16_t quantidade, uint32_t passoUs)
{
	falsa.dmaCanal      = canal;
	falsa.dmaQuantidade = quantidade;
	falsa.dmaPasso      = passoUs;
	memcpy(falsa.dmaAmostras, amostras, sizeof(falsa.dmaAmostras));
	return 0U;
}

static void Falsa_DacDmaPara(uint8_t canal)
{
	(void)canal;
	falsa.dmaParadas++;
}

static const ondaFunc_t funcFalsas =
{
	Falsa_Agora, Falsa_Agenda, Falsa_Desagenda, Falsa_Dac, Falsa_Pwm, Falsa_I2c, Falsa_DacDma, Falsa_DacDmaPara,
};

static void IniciaOnda(void)
{
	memset(&falsa, 0, sizeof(falsa));
	falsa.agora = 1000U;
	TEST_ASSERT_EQUAL(eONDA_RETURN_OK, Onda_Init(&funcFalsas));
}

static void Escreve32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

// Parametros do passo ONDA (sem o codigo); devolve o tamanho
static uint16_t MontaOnda(uint8_t *p, uint8_t gerador, uint8_t tipo, uint8_t registro, uint32_t passo,
                          uint32_t periodo, uint16_t ciclos, uint8_t modo, uint16_t ruido, int32_t base, int32_t amp,
                          uint8_t n, const uint16_t *valores)
{
	p[0] = gerador; p[1] = tipo; p[2] = registro;
	Escreve32(&p[3], passo);
	Escreve32(&p[7], periodo);
	p[11] = (uint8_t)ciclos; p[12] = (uint8_t)(ciclos >> 8);
	p[13] = modo;
	p[14] = (uint8_t)ruido; p[15] = (uint8_t)(ruido >> 8);
	Escreve32(&p[16], (uint32_t)base);
	Escreve32(&p[20], (uint32_t)amp);
	p[24] = n;
	for (uint8_t i = 0U; i < n; i++)
	{
		p[25U + (2U * i)] = (uint8_t)valores[i];
		p[26U + (2U * i)] = (uint8_t)(valores[i] >> 8);
	}
	return (uint16_t)(25U + (2U * n));
}

// Rampa de 0 a 4000 no DAC1 em 1 s (1 ciclo), passo de 1 ms
static uint16_t Rampa(uint8_t *p)
{
	const uint16_t v[2] = {0U, 65535U};
	return MontaOnda(p, 0U, dONDA_TIPO_DAC, 1U, 1000U, 1000000U, 1U, dONDA_MODO_INTERPOLA, 0U, 0, 4000, 2U, v);
}

// 1. Validacao: o passo da rampa e valido; gerador, destino, pontos, passo, periodo e faixa fora sao recusados
void Test_Onda_Valida(void)
{
	uint8_t p[96];
	const uint16_t v[3] = {0U, 65535U, 0U};
	uint16_t n = Rampa(p);

	TEST_ASSERT_EQUAL_UINT16(29U, Onda_Tamanho(p, n));
	TEST_ASSERT_EQUAL_UINT8(1U, Onda_Valida(p, n));
	TEST_ASSERT_EQUAL_UINT8(0U, Onda_Valida(p, (uint16_t)(n - 1U)));
	p[0] = dONDA_GERADORES;
	TEST_ASSERT_EQUAL_UINT8(0U, Onda_Valida(p, n));

	n = MontaOnda(p, 0U, dONDA_TIPO_DAC, 3U, 1000U, 1000000U, 1U, 0U, 0U, 0, 4000, 2U, v);  // DAC3 nao existe
	TEST_ASSERT_EQUAL_UINT8(0U, Onda_Valida(p, n));
	n = MontaOnda(p, 0U, dONDA_TIPO_DAC, 1U, 1000U, 1000000U, 1U, 0U, 0U, 0, 4096, 2U, v);  // passa de 4095
	TEST_ASSERT_EQUAL_UINT8(0U, Onda_Valida(p, n));
	n = MontaOnda(p, 0U, dONDA_TIPO_PWM, 1U, 99U, 1000000U, 1U, 0U, 0U, 0, 10000, 3U, v);   // PWM abaixo de 100 us
	TEST_ASSERT_EQUAL_UINT8(0U, Onda_Valida(p, n));
	n = MontaOnda(p, 0U, dONDA_TIPO_DAC, 1U, 10U, 20000U, 0U, 0U, 0U, 0, 4095, 3U, v);      // 2000 amostras no DMA
	TEST_ASSERT_EQUAL_UINT8(0U, Onda_Valida(p, n));
	n = MontaOnda(p, 0U, dONDA_TIPO_DAC, 1U, 10U, 10000U, 0U, 0U, 0U, 0, 4095, 3U, v);      // 1000 amostras: ok
	TEST_ASSERT_EQUAL_UINT8(1U, Onda_Valida(p, n));
	n = MontaOnda(p, 0U, dONDA_TIPO_I2C_U20, 254U, 1000U, 1000U, 0U, 0U, 0U, 0, 1000, 3U, v); // 3 bytes passam do mapa
	TEST_ASSERT_EQUAL_UINT8(0U, Onda_Valida(p, n));
	n = MontaOnda(p, 0U, dONDA_TIPO_I2C_S16, 0x3FU, 1000U, 500U, 0U, 0U, 0U, -100, 200, 3U, v); // periodo < passo
	TEST_ASSERT_EQUAL_UINT8(0U, Onda_Valida(p, n));
	n = MontaOnda(p, 0U, dONDA_TIPO_I2C_S16, 0x3FU, 1000U, 1000U, 0U, 0U, 0U, -100, 200, 1U, v); // 1 ponto so
	TEST_ASSERT_EQUAL_UINT8(0U, Onda_Valida(p, n));
}

// 2. Valor no tempo: interpolacao linear, degrau e volta ao inicio a cada periodo
void Test_Onda_Valor(void)
{
	uint8_t p[96];
	const uint16_t v[3] = {0U, 65535U, 0U};   // triangular
	MontaOnda(p, 0U, dONDA_TIPO_DAC, 1U, 1000U, 1000U, 0U, dONDA_MODO_INTERPOLA, 0U, 100, 1000, 3U, v);

	TEST_ASSERT_EQUAL_INT32(100, Onda_Valor(p, 0U));
	TEST_ASSERT_EQUAL_INT32(600, Onda_Valor(p, 250U));
	TEST_ASSERT_EQUAL_INT32(1100, Onda_Valor(p, 500U));
	TEST_ASSERT_EQUAL_INT32(600, Onda_Valor(p, 750U));
	TEST_ASSERT_EQUAL_INT32(600, Onda_Valor(p, 1250U));       // segundo periodo

	p[13] = dONDA_MODO_DEGRAU;
	TEST_ASSERT_EQUAL_INT32(100, Onda_Valor(p, 499U));
	TEST_ASSERT_EQUAL_INT32(1100, Onda_Valor(p, 500U));
}

// 3. Inicio: escreve o primeiro ponto na hora e agenda o proximo passo; ticks escrevem o valor do instante
void Test_Onda_IniciaETicks(void)
{
	uint8_t p[96];
	uint16_t n;
	IniciaOnda();
	n = Rampa(p);

	TEST_ASSERT_EQUAL(eONDA_RETURN_OK, Onda_Inicia(p, n));
	TEST_ASSERT_EQUAL_UINT8(1U, falsa.dacCanal);
	TEST_ASSERT_EQUAL_UINT16(0U, falsa.dacValor);
	TEST_ASSERT_EQUAL_UINT8(1U, falsa.agendado);
	TEST_ASSERT_EQUAL_UINT32(2000U, falsa.alvo);

	falsa.agora = 2000U;
	Onda_Tick();
	TEST_ASSERT_EQUAL_UINT16(4U, falsa.dacValor);              // 1 ms de 1 s: 4000 / 1000
	TEST_ASSERT_EQUAL_UINT32(3000U, falsa.alvo);

	falsa.agora = 501000U;                                     // tick atrasado: segue o tempo
	Onda_Tick();
	TEST_ASSERT_EQUAL_UINT16(2000U, falsa.dacValor);
	TEST_ASSERT_EQUAL_UINT32(502000U, falsa.alvo);
}

// 4. Fim dos ciclos: o ultimo ponto fica na saida e o gerador para (sem mais ticks)
void Test_Onda_FimDosCiclos(void)
{
	uint8_t p[96];
	uint16_t n;
	IniciaOnda();
	n = Rampa(p);
	Onda_Inicia(p, n);

	falsa.agora = 1001000U;
	Onda_Tick();
	TEST_ASSERT_EQUAL_UINT16(4000U, falsa.dacValor);
	TEST_ASSERT_EQUAL_UINT8(0U, falsa.agendado);
}

// 5. Destinos: razao do PWM, sensor de 16 bits com sinal (big-endian) e de 20 bits (BME280); ruido dentro da faixa
void Test_Onda_DestinosERuido(void)
{
	uint8_t p[96];
	const uint16_t v[2] = {65535U, 65535U};
	uint16_t n;
	IniciaOnda();

	n = MontaOnda(p, 1U, dONDA_TIPO_PWM, 2U, 1000U, 2000U, 0U, 0U, 0U, 0, 7500, 2U, v);
	Onda_Inicia(p, n);
	TEST_ASSERT_EQUAL_UINT8(2U, falsa.pwmCanal);
	TEST_ASSERT_EQUAL_UINT16(7500U, falsa.pwmRazao);

	n = MontaOnda(p, 2U, dONDA_TIPO_I2C_S16, 0x3BU, 1000U, 2000U, 0U, 0U, 0U, 0, -16384, 2U, v);   // -0,5 g
	Onda_Inicia(p, n);
	TEST_ASSERT_EQUAL_UINT8(0x3BU, falsa.i2cOffset);
	TEST_ASSERT_EQUAL_UINT8(2U, falsa.i2cQuantidade);
	TEST_ASSERT_EQUAL_HEX8(0xC0U, falsa.i2cBytes[0]);
	TEST_ASSERT_EQUAL_HEX8(0x00U, falsa.i2cBytes[1]);

	n = MontaOnda(p, 3U, dONDA_TIPO_I2C_U20, 0xFAU, 1000U, 2000U, 0U, 0U, 0U, 0, 519888, 2U, v);   // adc_T do exemplo
	Onda_Inicia(p, n);
	TEST_ASSERT_EQUAL_UINT8(3U, falsa.i2cQuantidade);
	TEST_ASSERT_EQUAL_HEX8(0x7EU, falsa.i2cBytes[0]);
	TEST_ASSERT_EQUAL_HEX8(0xEDU, falsa.i2cBytes[1]);
	TEST_ASSERT_EQUAL_HEX8(0x00U, falsa.i2cBytes[2]);

	n = MontaOnda(p, 4U, dONDA_TIPO_DAC, 2U, 1000U, 2000U, 0U, 0U, 50U, 2000, 0, 2U, v);           // 2000 +- 50
	Onda_Inicia(p, n);
	for (uint16_t i = 1U; i <= 200U; i++)
	{
		falsa.agora = 1000U + (1000U * i);
		Onda_Tick();
		TEST_ASSERT_TRUE(falsa.dacValor >= 1950U);
		TEST_ASSERT_TRUE(falsa.dacValor <= 2050U);
	}
}

// 6. DAC por DMA (passo de 10 us): um periodo de amostras para o DMA; o fim dos ciclos para o DMA e fixa o ultimo
void Test_Onda_DacPorDma(void)
{
	uint8_t p[96];
	const uint16_t v[3] = {0U, 65535U, 0U};
	uint16_t n;
	IniciaOnda();
	n = MontaOnda(p, 0U, dONDA_TIPO_DAC, 2U, 10U, 1000U, 3U, dONDA_MODO_INTERPOLA, 0U, 0, 4000, 3U, v);

	TEST_ASSERT_EQUAL(eONDA_RETURN_OK, Onda_Inicia(p, n));
	TEST_ASSERT_EQUAL_UINT8(2U, falsa.dmaCanal);
	TEST_ASSERT_EQUAL_UINT16(100U, falsa.dmaQuantidade);
	TEST_ASSERT_EQUAL_UINT32(10U, falsa.dmaPasso);
	TEST_ASSERT_EQUAL_UINT16(0U, falsa.dmaAmostras[0]);
	TEST_ASSERT_EQUAL_UINT16(80U, falsa.dmaAmostras[1]);       // 10 us de 500 us ate 4000
	TEST_ASSERT_EQUAL_UINT16(0U, falsa.dacEscritas);
	TEST_ASSERT_EQUAL_UINT32(4000U, falsa.alvo);               // so o fim dos 3 ciclos

	falsa.agora = 4000U;
	Onda_Tick();
	TEST_ASSERT_EQUAL_UINT8(1U, falsa.dmaParadas);
	TEST_ASSERT_EQUAL_UINT16(0U, falsa.dacValor);
	TEST_ASSERT_EQUAL_UINT8(0U, falsa.agendado);
}

// 7. Estado seguro: para todas as ondas (e o DMA) e nenhum tick fica agendado
void Test_Onda_Para(void)
{
	uint8_t p[96];
	const uint16_t v[2] = {0U, 65535U};
	uint16_t n;
	IniciaOnda();
	n = MontaOnda(p, 0U, dONDA_TIPO_DAC, 1U, 5U, 1000U, 0U, 0U, 0U, 0, 4095, 2U, v);
	Onda_Inicia(p, n);
	n = Rampa(p);
	p[0] = 1U;
	p[2] = 2U;
	Onda_Inicia(p, n);
	TEST_ASSERT_EQUAL_UINT8(1U, falsa.agendado);

	Onda_Para();
	TEST_ASSERT_EQUAL_UINT8(0U, falsa.agendado);
	TEST_ASSERT_EQUAL_UINT8(1U, falsa.dmaParadas);
	falsa.agora = 5000U;
	uint16_t escritas = falsa.dacEscritas;
	Onda_Tick();
	TEST_ASSERT_EQUAL_UINT16(escritas, falsa.dacEscritas);
}
