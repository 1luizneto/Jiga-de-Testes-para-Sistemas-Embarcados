/***********************************************************************************************************************
 *   @file         Onda.c
 *   @addtogroup   App
 *   @brief        Gerador de ondas das rotinas: uma tabela de pontos tocada no tempo em um destino (DAC, PWM, sensor)
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Ferramentas:</b>
 *   - Generic.
 *
 *   \n <b>Dependencias:</b>
 *   - Funcoes da BSP (Onda_Init): base de tempo (TIM5), agendamento do proximo tick (comparacao do canal 2 do TIM5),
 *     escrita no DAC, na razao do PWM e no mapa da escrava I2C, e o DAC por DMA.
 *
 *   \n <b>Observacoes:</b>
 *   - A forma (rampa, senoide, triangular, quadrada, exponencial, pontos...) e montada pelo PC: aqui so existe uma
 *     tabela de 2 a 32 pontos igualmente espacados no periodo, normalizados em 16 bits, com base e amplitude no valor
 *     bruto do destino. Entre dois pontos, interpolacao linear ou degrau; ruido opcional (LFSR) de +-'ruido' bruto.
 *   - A onda comeca quando o passo da rotina e executado e roda em segundo plano enquanto a rotina continua. Com
 *     'ciclos' > 0 ela para no fim do ultimo ciclo e mantem o ultimo ponto; com 0, roda ate o fim da rotina. O estado
 *     seguro (fim da rotina, ABORT, RESET_ALL) para todas as ondas antes de levar as saidas a 0 (Onda_Para).
 *   - Passo de 100 us ou mais: cada onda e atualizada na interrupcao do tick, agendada para o proximo passo da onda
 *     mais proxima. No DAC com passo menor que 100 us, a tabela vira um periodo de amostras (ate 1024) tocado por DMA
 *     com o trigger de um timer, sem ocupar o processador (o fim do ultimo ciclo continua vindo do tick).
 *
 *   Changelog
 *   @version      <b>1.0.0 - 04/11/2026</b> \n Luiz Neto \n Primeira versao
 *
 *   @copyright
 *   @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include "Onda.h"
#include <stddef.h>
#include <string.h>

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

// Limites de cada destino
#define dONDA_DAC_MAX  (4095)
#define dONDA_PWM_MAX  (10000)
#define dONDA_U20_MAX  (0xFFFFF)

/***********************************************************************************************************************
 * TIPOS LOCAIS
 **********************************************************************************************************************/

/// @brief Um gerador em uso
typedef struct ondaGerador
{
	uint8_t  ativo;
	uint8_t  dma;               ///< 1: DAC por DMA (o tick so encerra no fim dos ciclos)
	uint8_t  tipo;
	uint8_t  registro;
	uint32_t passoUs;
	uint32_t periodoUs;
	uint16_t ciclos;            ///< 0 = ate o fim da rotina
	uint32_t inicio;            ///< Instante do primeiro ponto (us, TIM5)
	uint32_t proximo;           ///< Proxima atualizacao (us, TIM5)
	uint8_t  parametros[dONDA_CABECALHO_SIZE + (2U * dONDA_PONTOS_MAX)];
} ondaGerador_t;

static struct onda
{
	ondaFunc_t    func;
	ondaGerador_t geradores[dONDA_GERADORES];
	uint32_t      lfsr;
	uint16_t      amostras[2][dONDA_AMOSTRAS_MAX];   ///< Um periodo de cada canal do DAC por DMA
} onda;

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static uint32_t Onda_Le32(const uint8_t *p);
static int32_t  Onda_Limita(uint8_t tipo, int32_t valor);
static int32_t  Onda_Bruto(int32_t base, int32_t amp, int64_t normalQ16);
static void     Onda_Escreve(const ondaGerador_t *g, int32_t valor);
static int32_t  Onda_Ruido(uint16_t ruido);
static void     Onda_Reagenda(void);
static uint8_t  Onda_Passou(uint32_t instante, uint32_t referencia);

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Registra as funcoes da BSP e comeca sem ondas.
 * @param[in]  func  Funcoes da BSP (todas obrigatorias)
 **********************************************************************************************************************/
ondaReturn_t Onda_Init(const ondaFunc_t *func)
{
	if ((func == NULL) || (func->agora == NULL) || (func->agenda == NULL) || (func->desagenda == NULL) ||
	    (func->dac == NULL) || (func->pwm == NULL) || (func->i2c == NULL) || (func->dacDma == NULL) ||
	    (func->dacDmaPara == NULL))
	{
		return eONDA_RETURN_INVALID_ARGUMENT;
	}

	onda.func = *func;
	memset(onda.geradores, 0, sizeof(onda.geradores));
	onda.lfsr = 0xACE1u;

	return eONDA_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Tamanho dos parametros do passo (sem o codigo), ou 0 se o cabecalho nao couber em 'disponivel'.
 **********************************************************************************************************************/
uint16_t Onda_Tamanho(const uint8_t *parametros, uint16_t disponivel)
{
	if ((parametros == NULL) || (disponivel < dONDA_CABECALHO_SIZE))
	{
		return 0U;
	}

	return (uint16_t)(dONDA_CABECALHO_SIZE + (2U * parametros[dONDA_CABECALHO_SIZE - 1U]));
}

/***********************************************************************************************************************
 * @brief      1 se os parametros do passo ONDA sao validos (o mesmo criterio do Onda_Inicia).
 * @details    Gerador 0-5; destino e registro coerentes (canal do DAC/PWM, 2 ou 3 bytes dentro do mapa); 2 a 32
 *             pontos; passo de 100 us ou mais, ou de 1 a 99 us so no DAC com no maximo 1024 amostras por periodo;
 *             periodo >= passo; modo 0/1; a base e a base + amplitude dentro da faixa do destino.
 **********************************************************************************************************************/
uint8_t Onda_Valida(const uint8_t *parametros, uint16_t size)
{
	if ((parametros == NULL) || (size < dONDA_CABECALHO_SIZE) || (Onda_Tamanho(parametros, size) != size))
	{
		return 0U;
	}

	uint8_t  gerador  = parametros[0];
	uint8_t  tipo     = parametros[1];
	uint8_t  registro = parametros[2];
	uint32_t passo    = Onda_Le32(&parametros[3]);
	uint32_t periodo  = Onda_Le32(&parametros[7]);
	uint8_t  modo     = parametros[13];
	int32_t  base     = (int32_t)Onda_Le32(&parametros[16]);
	int32_t  fim      = base + (int32_t)Onda_Le32(&parametros[20]);
	uint8_t  pontos   = parametros[24];

	if ((gerador >= dONDA_GERADORES) || (modo > dONDA_MODO_DEGRAU) || (pontos < dONDA_PONTOS_MIN) ||
	    (pontos > dONDA_PONTOS_MAX) || (passo == 0U) || (periodo < passo))
	{
		return 0U;
	}

	switch (tipo)
	{
		case dONDA_TIPO_DAC:
			if ((registro < 1U) || (registro > 2U))
			{
				return 0U;
			}
			if ((passo < dONDA_PASSO_MIN_US) && ((periodo / passo) > dONDA_AMOSTRAS_MAX))
			{
				return 0U;
			}
		break;

		case dONDA_TIPO_PWM:
			if ((registro < 1U) || (registro > 3U) || (passo < dONDA_PASSO_MIN_US))
			{
				return 0U;
			}
		break;

		case dONDA_TIPO_I2C_S16:
		case dONDA_TIPO_I2C_U16:
			if ((registro > 254U) || (passo < dONDA_PASSO_MIN_US))
			{
				return 0U;
			}
		break;

		case dONDA_TIPO_I2C_U20:
			if ((registro > 253U) || (passo < dONDA_PASSO_MIN_US))
			{
				return 0U;
			}
		break;

		default:
			return 0U;
	}

	return ((Onda_Limita(tipo, base) == base) && (Onda_Limita(tipo, fim) == fim)) ? 1U : 0U;
}

/***********************************************************************************************************************
 * @brief      Valor bruto da onda no instante t (us) desde o inicio, sem ruido e sem o fim dos ciclos.
 * @details    t e reduzido ao periodo; a tabela cobre o periodo com (n - 1) segmentos iguais.
 **********************************************************************************************************************/
int32_t Onda_Valor(const uint8_t *parametros, uint32_t t)
{
	uint32_t periodo  = Onda_Le32(&parametros[7]);
	uint8_t  modo     = parametros[13];
	int32_t  base     = (int32_t)Onda_Le32(&parametros[16]);
	int32_t  amp      = (int32_t)Onda_Le32(&parametros[20]);
	uint8_t  pontos   = parametros[24];
	const uint8_t *v  = &parametros[dONDA_CABECALHO_SIZE];

	uint64_t posicao = (uint64_t)(t % periodo) * (pontos - 1U);
	uint32_t indice  = (uint32_t)(posicao / periodo);
	uint64_t resto   = posicao % periodo;
	int64_t  a       = (int64_t)(v[2U * indice] | (v[(2U * indice) + 1U] << 8));
	int64_t  normal  = a * 65536;   // Q16: a fracao do segmento sem perder precisao

	if ((modo == dONDA_MODO_INTERPOLA) && (indice < (pontos - 1U)))
	{
		int64_t b = (int64_t)(v[2U * (indice + 1U)] | (v[(2U * (indice + 1U)) + 1U] << 8));
		normal += (b - a) * (int64_t)((resto << 16) / periodo);
	}

	return Onda_Bruto(base, amp, normal);
}

/***********************************************************************************************************************
 * @brief      Comeca uma onda (passo ONDA da rotina): substitui a que estiver no mesmo gerador.
 * @param[in]  parametros  Parametros do passo, sem o codigo (ja validados pela Rotina)
 * @param[in]  size        Tamanho dos parametros
 * @return     eONDA_RETURN_OK, _INVALID_ARGUMENT ou _ERROR (falha do DMA)
 **********************************************************************************************************************/
ondaReturn_t Onda_Inicia(const uint8_t *parametros, uint16_t size)
{
	if ((onda.func.agora == NULL) || (Onda_Valida(parametros, size) == 0U))
	{
		return eONDA_RETURN_INVALID_ARGUMENT;
	}

	ondaGerador_t *g = &onda.geradores[parametros[0]];

	onda.func.desagenda();
	if ((g->ativo != 0U) && (g->dma != 0U))
	{
		onda.func.dacDmaPara(g->registro);
	}

	// Outro gerador no mesmo canal do DMA: so um DMA por canal
	for (uint8_t i = 0U; i < dONDA_GERADORES; i++)
	{
		ondaGerador_t *outro = &onda.geradores[i];
		if ((outro != g) && (outro->ativo != 0U) && (outro->tipo == parametros[1]) &&
		    (outro->registro == parametros[2]))
		{
			if (outro->dma != 0U)
			{
				onda.func.dacDmaPara(outro->registro);
			}
			outro->ativo = 0U;
		}
	}

	memcpy(g->parametros, parametros, size);
	g->tipo      = parametros[1];
	g->registro  = parametros[2];
	g->passoUs   = Onda_Le32(&parametros[3]);
	g->periodoUs = Onda_Le32(&parametros[7]);
	g->ciclos    = (uint16_t)(parametros[11] | (parametros[12] << 8));
	g->dma       = ((g->tipo == dONDA_TIPO_DAC) && (g->passoUs < dONDA_PASSO_MIN_US)) ? 1U : 0U;
	g->inicio    = onda.func.agora();
	g->ativo     = 1U;

	if (g->dma != 0U)
	{
		uint16_t *amostras = onda.amostras[g->registro - 1U];
		uint16_t quantidade = (uint16_t)(g->periodoUs / g->passoUs);

		for (uint16_t k = 0U; k < quantidade; k++)
		{
			amostras[k] = (uint16_t)Onda_Limita(g->tipo, Onda_Valor(g->parametros, (uint32_t)k * g->passoUs));
		}
		if (onda.func.dacDma(g->registro, amostras, quantidade, g->passoUs) != 0U)
		{
			g->ativo = 0U;
			Onda_Reagenda();
			return eONDA_RETURN_ERROR;
		}
		g->proximo = g->inicio + ((uint32_t)g->ciclos * g->periodoUs);
	}
	else
	{
		Onda_Escreve(g, Onda_Limita(g->tipo, Onda_Valor(g->parametros, 0U)));
		g->proximo = g->inicio + g->passoUs;
	}

	Onda_Reagenda();

	return eONDA_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Interrupcao do gerador: atualiza as ondas cuja vez chegou e agenda o proximo tick.
 * @details    Uma onda atrasada (tick perdido) pula para o instante atual: o valor segue o tempo, nao os passos.
 **********************************************************************************************************************/
void Onda_Tick(void)
{
	uint32_t agora = onda.func.agora();

	for (uint8_t i = 0U; i < dONDA_GERADORES; i++)
	{
		ondaGerador_t *g = &onda.geradores[i];

		if ((g->ativo == 0U) || (Onda_Passou(agora, g->proximo) == 0U))
		{
			continue;
		}

		uint32_t t = agora - g->inicio;
		uint8_t  fim = ((g->ciclos > 0U) && (t >= ((uint32_t)g->ciclos * g->periodoUs))) ? 1U : 0U;

		if (fim != 0U)
		{
			// Ultimo ponto da tabela: a onda termina onde o ultimo ciclo termina
			const uint8_t *v = &g->parametros[dONDA_CABECALHO_SIZE + (2U * (g->parametros[24] - 1U))];
			int32_t ultimo = Onda_Bruto((int32_t)Onda_Le32(&g->parametros[16]), (int32_t)Onda_Le32(&g->parametros[20]),
			                            (int64_t)(v[0] | (v[1] << 8)) * 65536);

			if (g->dma != 0U)
			{
				onda.func.dacDmaPara(g->registro);
			}
			Onda_Escreve(g, Onda_Limita(g->tipo, ultimo));
			g->ativo = 0U;
			continue;
		}

		uint16_t ruido = (uint16_t)(g->parametros[14] | (g->parametros[15] << 8));
		Onda_Escreve(g, Onda_Limita(g->tipo, Onda_Valor(g->parametros, t) + Onda_Ruido(ruido)));

		g->proximo += g->passoUs;
		if (Onda_Passou(agora, g->proximo) != 0U)
		{
			g->proximo = agora + g->passoUs;
		}
	}

	Onda_Reagenda();
}

/***********************************************************************************************************************
 * @brief      Para todas as ondas (estado seguro): os destinos ficam no ultimo valor ate o estado seguro escrever.
 **********************************************************************************************************************/
void Onda_Para(void)
{
	if (onda.func.desagenda == NULL)
	{
		return;
	}

	onda.func.desagenda();

	for (uint8_t i = 0U; i < dONDA_GERADORES; i++)
	{
		if ((onda.geradores[i].ativo != 0U) && (onda.geradores[i].dma != 0U))
		{
			onda.func.dacDmaPara(onda.geradores[i].registro);
		}
		onda.geradores[i].ativo = 0U;
	}
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      u32 little-endian.
 **********************************************************************************************************************/
static uint32_t Onda_Le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/***********************************************************************************************************************
 * @brief      base + amplitude x normal / 65535, com normal em Q16 e uma divisao so, arredondada.
 **********************************************************************************************************************/
static int32_t Onda_Bruto(int32_t base, int32_t amp, int64_t normalQ16)
{
	int64_t numerador   = (int64_t)amp * normalQ16;
	int64_t denominador = (int64_t)dONDA_ESCALA * 65536;
	int64_t meio        = (numerador >= 0) ? (denominador / 2) : -(denominador / 2);

	return base + (int32_t)((numerador + meio) / denominador);
}

/***********************************************************************************************************************
 * @brief      Satura o valor na faixa do destino.
 **********************************************************************************************************************/
static int32_t Onda_Limita(uint8_t tipo, int32_t valor)
{
	int32_t minimo = 0;
	int32_t maximo;

	switch (tipo)
	{
		case dONDA_TIPO_DAC:     maximo = dONDA_DAC_MAX; break;
		case dONDA_TIPO_PWM:     maximo = dONDA_PWM_MAX; break;
		case dONDA_TIPO_I2C_S16: minimo = -32768; maximo = 32767; break;
		case dONDA_TIPO_I2C_U16: maximo = 65535; break;
		default:                 maximo = dONDA_U20_MAX; break;
	}

	return (valor < minimo) ? minimo : ((valor > maximo) ? maximo : valor);
}

/***********************************************************************************************************************
 * @brief      Escreve o valor bruto no destino da onda.
 **********************************************************************************************************************/
static void Onda_Escreve(const ondaGerador_t *g, int32_t valor)
{
	uint8_t bytes[3];

	switch (g->tipo)
	{
		case dONDA_TIPO_DAC:
			onda.func.dac(g->registro, (uint16_t)valor);
		break;

		case dONDA_TIPO_PWM:
			onda.func.pwm(g->registro, (uint16_t)valor);
		break;

		case dONDA_TIPO_I2C_S16:
		case dONDA_TIPO_I2C_U16:
			bytes[0] = (uint8_t)(((uint32_t)valor >> 8) & 0xFFU);
			bytes[1] = (uint8_t)((uint32_t)valor & 0xFFU);
			onda.func.i2c(g->registro, bytes, 2U);
		break;

		default:
			bytes[0] = (uint8_t)(((uint32_t)valor >> 12) & 0xFFU);
			bytes[1] = (uint8_t)(((uint32_t)valor >> 4) & 0xFFU);
			bytes[2] = (uint8_t)(((uint32_t)valor & 0x0FU) << 4);
			onda.func.i2c(g->registro, bytes, 3U);
		break;
	}
}

/***********************************************************************************************************************
 * @brief      Ruido uniforme de -ruido a +ruido (LFSR de Galois de 32 bits).
 **********************************************************************************************************************/
static int32_t Onda_Ruido(uint16_t ruido)
{
	if (ruido == 0U)
	{
		return 0;
	}

	onda.lfsr = (onda.lfsr >> 1) ^ ((uint32_t)(-(int32_t)(onda.lfsr & 1U)) & 0xD0000001U);

	return (int32_t)(onda.lfsr % ((2U * (uint32_t)ruido) + 1U)) - (int32_t)ruido;
}

/***********************************************************************************************************************
 * @brief      Agenda o tick para a proxima atualizacao (ou fim de ciclos do DMA); sem ondas, desagenda.
 **********************************************************************************************************************/
static void Onda_Reagenda(void)
{
	uint8_t  existe = 0U;
	uint32_t alvo = 0U;
	uint32_t agora = onda.func.agora();

	for (uint8_t i = 0U; i < dONDA_GERADORES; i++)
	{
		const ondaGerador_t *g = &onda.geradores[i];

		// DMA sem fim (ciclos = 0) nao precisa do tick
		if ((g->ativo == 0U) || ((g->dma != 0U) && (g->ciclos == 0U)))
		{
			continue;
		}

		if ((existe == 0U) || ((int32_t)((g->proximo - agora) - (alvo - agora)) < 0))
		{
			alvo   = g->proximo;
			existe = 1U;
		}
	}

	if (existe != 0U)
	{
		onda.func.agenda(alvo);
	}
	else
	{
		onda.func.desagenda();
	}
}

/***********************************************************************************************************************
 * @brief      1 se 'instante' ja chegou em 'referencia' (comparacao com sinal: vale na volta dos 32 bits).
 **********************************************************************************************************************/
static uint8_t Onda_Passou(uint32_t instante, uint32_t referencia)
{
	return ((int32_t)(instante - referencia) >= 0) ? 1U : 0U;
}

/** @} DOXYGEN GROUP TAG END OF FILE */
