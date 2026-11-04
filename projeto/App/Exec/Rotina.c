/***********************************************************************************************************************
 *   @file         Rotina.c
 *   @addtogroup   App
 *   @brief        Interpretador de rotinas: valida o RUN_ROUTINE, executa os passos na ordem e guarda os resultados
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Ferramentas:</b>
 *   - Generic.
 *
 *   \n <b>Dependencias:</b>
 *   - Exec (Exec_Passo executa os passos de estimulo, configuracao e aquisicao com as funcoes dos comandos diretos;
 *     Exec_SafeState no fim da rotina) e a base de tempo da BSP (agora, espera e acorda, no Rotina_Init).
 *
 *   \n <b>Observacoes:</b>
 *   - Nao conhece o HAL nem o FreeRTOS: os testes Unity registram uma base de tempo falsa.
 *   - Duas tasks usam o modulo: a TestExecTask carrega (Rotina_Carrega, pelo Exec) e executa (Rotina_Executa); a
 *     CommTask consulta (Rotina_Status, Rotina_EmExecucao) e interrompe (Rotina_Aborta). Os campos lidos pela
 *     CommTask sao volatile e de ate 16 bits, e o estado final so e publicado depois do estado seguro.
 *   - A rotina e validada inteira antes de executar (codigo, tamanho e faixa dos parametros de cada passo e o
 *     tamanho maximo dos resultados): uma rotina recusada nao executa nenhum passo. As faixas que dependem do
 *     hardware (frequencia do SPI) e os erros de execucao (NAO_CONFIGURADO, ERRO_PERIFERICO) terminam a rotina
 *     no estado ERRO, com o codigo do NACK que o comando direto teria recebido.
 *   - O tempo maximo e conferido entre os passos e durante as esperas; um passo de comunicacao ja iniciado nao e
 *     interrompido (o I2C e o SPI mestres tem timeout proprio).
 *
 *   Changelog
 *   @version      <b>1.0.0 - 03/11/2026</b> \n Luiz Neto \n Primeira versao
 *   @version      <b>1.1.0 - 04/11/2026</b> \n Luiz Neto \n Passos I2C_SLAVE_MASK e I2C_SLAVE_ADDR2
 *   @version      <b>1.2.0 - 04/11/2026</b> \n Luiz Neto \n Passo ONDA (gerador de ondas em segundo plano)
 *
 *   @copyright
 *   @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include "Rotina.h"
#include "Exec.h"
#include "AnalogOut.h"
#include "AnalogIn.h"
#include "DigitalIo.h"
#include "Pwm.h"
#include "I2cBus.h"
#include "SpiBus.h"
#include "UartBus.h"
#include "Onda.h"
#include <stddef.h>
#include <string.h>

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

/// Bytes de passos que cabem no RUN_ROUTINE (payload de 240 menos o cabecalho)
#define dROTINA_PASSOS_SIZE (dCOMM_PAYLOAD_MAX_SIZE - dROTINA_CABECALHO_SIZE)

/// Registro do ESPERA_GPIO: resultado (u8) + tempo decorrido em us (u32)
#define dROTINA_ESPERA_GPIO_REGISTRO (5U)

/// Resultado do ESPERA_GPIO
#define dROTINA_GPIO_OCORREU  (0U)
#define dROTINA_GPIO_ESGOTOU  (1U)

/***********************************************************************************************************************
 * TIPOS LOCAIS
 **********************************************************************************************************************/

/// Por que uma espera terminou, do ponto de vista da rotina
typedef enum rotinaFimEspera
{
	eROTINA_FIM_ALVO,      ///< Duracao do passo cumprida (ou tempo limite do ESPERA_GPIO)
	eROTINA_FIM_ENTRADA,   ///< Nivel esperado na entrada
	eROTINA_FIM_ABORT,
	eROTINA_FIM_LIMITE,    ///< Tempo maximo da rotina

	eROTINA_FIM_END_ENUM
} rotinaFimEspera_t;

static struct rotina
{
	rotinaFunc_t func;

	// Rotina carregada (copiada do payload: o quadro do RUN_ROUTINE e reutilizado pela CommTask)
	uint8_t  passos[dROTINA_PASSOS_SIZE];
	uint8_t  quantidadePassos;
	uint32_t tempoMaxMs;
	uint8_t  pendente;                  ///< 1: carregada e ainda nao executada

	// Estado publicado para a CommTask
	volatile uint8_t  estado;           ///< rotinaEstado_t
	volatile uint8_t  passosConcluidos;
	volatile uint16_t identificador;
	volatile uint8_t  erro;             ///< Codigo commError_t do passo que falhou (estado ERRO)
	volatile uint8_t  abortar;          ///< 1: ABORT recebido durante a execucao

	// Resultados
	uint8_t  resultados[dROTINA_RESULTADOS_SIZE];
	uint16_t resultadosSize;

	uint32_t inicio;                    ///< Instante do inicio da rotina (us)
	uint32_t limite;                    ///< inicio + tempo maximo (us)
} rotina;

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static uint16_t Rotina_Le16(const uint8_t *p);
static uint32_t Rotina_Le32(const uint8_t *p);
static void     Rotina_Escreve32(uint8_t *p, uint32_t valor);
static uint8_t  Rotina_ValidaPasso(const uint8_t *p, uint16_t disponivel, uint16_t *tamanho, uint16_t *registro);
static uint8_t  Rotina_Passou(uint32_t instante, uint32_t referencia);
static rotinaFimEspera_t Rotina_Aguarda(uint32_t alvo, uint8_t entrada, uint8_t nivel, uint32_t *instante);
static void     Rotina_Grava(uint8_t indice, uint32_t instante, const uint8_t *dados, uint8_t quantidade);
static void     Rotina_Finaliza(rotinaEstado_t estado, uint8_t erro);

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Registra a base de tempo da BSP e comeca OCIOSA, sem resultados.
 * @param[in]  func  Funcoes da BSP (todas obrigatorias)
 **********************************************************************************************************************/
rotinaReturn_t Rotina_Init(const rotinaFunc_t *func)
{
	if ((func == NULL) || (func->agora == NULL) || (func->espera == NULL) || (func->acorda == NULL))
	{
		return eROTINA_RETURN_INVALID_ARGUMENT;
	}

	memset(&rotina, 0, sizeof(rotina));
	rotina.func   = *func;
	rotina.estado = (uint8_t)eROTINA_ESTADO_OCIOSA;

	return eROTINA_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      RUN_ROUTINE: valida a rotina inteira e, se valida, carrega e publica EM_EXECUCAO.
 * @details    Payload [identificador u16][tempo maximo ms u32][numero de passos u8][passos]. A execucao fica para o
 *             Rotina_Executa, chamado pela TestExecTask depois de enviar o ACK. Os resultados da rotina anterior
 *             sao descartados so quando a nova e aceita.
 * @param[in]  payload, size  Payload do RUN_ROUTINE
 * @param[out] indice         Passo invalido (a partir de 0) ou dROTINA_INDICE_CABECALHO
 * @return     eROTINA_RETURN_OK, eROTINA_RETURN_INVALID_ROUTINE, eROTINA_RETURN_BUSY ou INVALID_ARGUMENT
 **********************************************************************************************************************/
rotinaReturn_t Rotina_Carrega(const uint8_t *payload, uint8_t size, uint8_t *indice)
{
	uint16_t posicao = dROTINA_CABECALHO_SIZE;
	uint16_t registros = 0U;

	if ((payload == NULL) || (indice == NULL))
	{
		return eROTINA_RETURN_INVALID_ARGUMENT;
	}

	*indice = dROTINA_INDICE_CABECALHO;

	if (rotina.estado == (uint8_t)eROTINA_ESTADO_EM_EXECUCAO)
	{
		return eROTINA_RETURN_BUSY;
	}

	if (size < dROTINA_CABECALHO_SIZE)
	{
		return eROTINA_RETURN_INVALID_ROUTINE;
	}

	uint32_t tempoMaxMs = Rotina_Le32(&payload[2]);
	uint8_t  quantidade = payload[6];

	if ((quantidade == 0U) || (tempoMaxMs == 0U) || (tempoMaxMs > dROTINA_TEMPO_MAX_MS))
	{
		return eROTINA_RETURN_INVALID_ROUTINE;
	}

	for (uint8_t i = 0U; i < quantidade; i++)
	{
		uint16_t tamanho = 0U;
		uint16_t registro = 0U;

		if ((posicao >= size) || (Rotina_ValidaPasso(&payload[posicao], (uint16_t)(size - posicao), &tamanho, &registro) == 0U))
		{
			*indice = i;
			return eROTINA_RETURN_INVALID_ROUTINE;
		}

		if (registro > 0U)
		{
			registros = (uint16_t)(registros + dROTINA_REGISTRO_CABECALHO + registro);

			if (registros > dROTINA_RESULTADOS_SIZE)
			{
				*indice = i;
				return eROTINA_RETURN_INVALID_ROUTINE;
			}
		}

		posicao = (uint16_t)(posicao + tamanho);
	}

	// Bytes depois do ultimo passo: o numero de passos nao bate com o encadeamento
	if (posicao != size)
	{
		return eROTINA_RETURN_INVALID_ROUTINE;
	}

	memcpy(rotina.passos, &payload[dROTINA_CABECALHO_SIZE], (size_t)(size - dROTINA_CABECALHO_SIZE));
	rotina.quantidadePassos = quantidade;
	rotina.tempoMaxMs       = tempoMaxMs;
	rotina.resultadosSize   = 0U;
	rotina.identificador    = Rotina_Le16(&payload[0]);
	rotina.passosConcluidos = 0U;
	rotina.erro             = 0U;
	rotina.abortar          = 0U;
	rotina.pendente         = 1U;
	rotina.estado           = (uint8_t)eROTINA_ESTADO_EM_EXECUCAO;

	return eROTINA_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Executa a rotina carregada, passo a passo, e aplica o estado seguro no fim.
 * @details    Bloqueia ate a rotina terminar (roda na TestExecTask). Antes de cada passo confere o ABORT e o tempo
 *             maximo; o instante de cada registro e o do inicio do passo (ou o da transicao, no ESPERA_GPIO),
 *             contado a partir do inicio da rotina.
 * @return     eROTINA_RETURN_OK ou eROTINA_RETURN_ERROR (nada carregado)
 **********************************************************************************************************************/
rotinaReturn_t Rotina_Executa(void)
{
	static uint8_t dados[dCOMM_PAYLOAD_MAX_SIZE];
	const uint8_t *passo = rotina.passos;

	if (rotina.pendente == 0U)
	{
		return eROTINA_RETURN_ERROR;
	}

	rotina.pendente = 0U;
	rotina.inicio   = rotina.func.agora();
	rotina.limite   = rotina.inicio + (rotina.tempoMaxMs * 1000U);

	for (uint8_t i = 0U; i < rotina.quantidadePassos; i++)
	{
		uint16_t tamanho = 0U;
		uint16_t registro = 0U;
		uint32_t inicioPasso = rotina.func.agora();
		uint32_t instante = inicioPasso;

		if (rotina.abortar != 0U)
		{
			Rotina_Finaliza(eROTINA_ESTADO_ABORTADA, 0U);
			return eROTINA_RETURN_OK;
		}

		if (Rotina_Passou(inicioPasso, rotina.limite) != 0U)
		{
			Rotina_Finaliza(eROTINA_ESTADO_TEMPO_ESGOTADO, 0U);
			return eROTINA_RETURN_OK;
		}

		// Ja validado no Rotina_Carrega: aqui so recupera o tamanho
		(void)Rotina_ValidaPasso(passo, dROTINA_PASSOS_SIZE, &tamanho, &registro);

		switch (passo[0])
		{
			case dROTINA_PASSO_ESPERA:
			{
				switch (Rotina_Aguarda(inicioPasso + Rotina_Le32(&passo[1]), 0U, 0U, &instante))
				{
					case eROTINA_FIM_ABORT:
						Rotina_Finaliza(eROTINA_ESTADO_ABORTADA, 0U);
						return eROTINA_RETURN_OK;

					case eROTINA_FIM_LIMITE:
						Rotina_Finaliza(eROTINA_ESTADO_TEMPO_ESGOTADO, 0U);
						return eROTINA_RETURN_OK;

					default:
					break;
				}
			}
			break;

			case dROTINA_PASSO_ESPERA_GPIO:
			{
				uint8_t registroGpio[dROTINA_ESPERA_GPIO_REGISTRO];
				uint8_t entrada = (uint8_t)(passo[1] - dROTINA_ENTRADA_MIN + dDIGITALIO_ENTRADA_MIN);

				switch (Rotina_Aguarda(inicioPasso + Rotina_Le32(&passo[3]), entrada, passo[2], &instante))
				{
					case eROTINA_FIM_ABORT:
						Rotina_Finaliza(eROTINA_ESTADO_ABORTADA, 0U);
						return eROTINA_RETURN_OK;

					case eROTINA_FIM_LIMITE:
						Rotina_Finaliza(eROTINA_ESTADO_TEMPO_ESGOTADO, 0U);
						return eROTINA_RETURN_OK;

					case eROTINA_FIM_ENTRADA:
						registroGpio[0] = dROTINA_GPIO_OCORREU;
					break;

					default:
						registroGpio[0] = dROTINA_GPIO_ESGOTOU;
					break;
				}

				Rotina_Escreve32(&registroGpio[1], instante - inicioPasso);
				Rotina_Grava(i, instante, registroGpio, dROTINA_ESPERA_GPIO_REGISTRO);
			}
			break;

			case dONDA_PASSO:
			{
				if (Onda_Inicia(&passo[1], (uint16_t)(tamanho - 1U)) != eONDA_RETURN_OK)
				{
					Rotina_Finaliza(eROTINA_ESTADO_ERRO, (uint8_t)eCOMM_ERROR_PERIFERICO);
					return eROTINA_RETURN_OK;
				}
			}
			break;

			default:
			{
				uint8_t quantidade = 0U;
				uint8_t erro = Exec_Passo(passo[0], &passo[1], (uint8_t)(tamanho - 1U), dados, &quantidade);

				if (erro != 0U)
				{
					Rotina_Finaliza(eROTINA_ESTADO_ERRO, erro);
					return eROTINA_RETURN_OK;
				}

				if (registro > 0U)
				{
					Rotina_Grava(i, inicioPasso, dados, quantidade);
				}
			}
			break;
		}

		rotina.passosConcluidos = (uint8_t)(i + 1U);
		passo = &passo[tamanho];
	}

	Rotina_Finaliza(eROTINA_ESTADO_CONCLUIDA, 0U);

	return eROTINA_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      ABORT: pede a interrupcao da rotina em execucao e acorda a espera em andamento.
 * @details    Chamado pela CommTask. Sem rotina em execucao nao faz nada (o ABORT recebe ACK do mesmo jeito).
 **********************************************************************************************************************/
rotinaReturn_t Rotina_Aborta(void)
{
	if (rotina.estado == (uint8_t)eROTINA_ESTADO_EM_EXECUCAO)
	{
		rotina.abortar = 1U;

		if (rotina.func.acorda != NULL)
		{
			rotina.func.acorda();
		}
	}

	return eROTINA_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      RESET_ALL: volta a OCIOSA e descarta os resultados.
 * @return     eROTINA_RETURN_OK ou eROTINA_RETURN_BUSY (rotina em execucao: a CommTask ja recusa o RESET_ALL)
 **********************************************************************************************************************/
rotinaReturn_t Rotina_Limpa(void)
{
	if (rotina.estado == (uint8_t)eROTINA_ESTADO_EM_EXECUCAO)
	{
		return eROTINA_RETURN_BUSY;
	}

	rotina.estado           = (uint8_t)eROTINA_ESTADO_OCIOSA;
	rotina.passosConcluidos = 0U;
	rotina.identificador    = 0U;
	rotina.erro             = 0U;
	rotina.resultadosSize   = 0U;
	rotina.pendente         = 0U;

	return eROTINA_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      ROUTINE_STATUS: [estado u8][passos concluidos u8][identificador u16][codigo de erro u8].
 * @param[out] dados  dROTINA_STATUS_SIZE bytes
 * @param[out] size   dROTINA_STATUS_SIZE
 **********************************************************************************************************************/
rotinaReturn_t Rotina_Status(uint8_t *dados, uint8_t *size)
{
	if ((dados == NULL) || (size == NULL))
	{
		return eROTINA_RETURN_INVALID_ARGUMENT;
	}

	uint16_t identificador = rotina.identificador;

	dados[0] = rotina.estado;
	dados[1] = rotina.passosConcluidos;
	dados[2] = (uint8_t)(identificador & 0xFFU);
	dados[3] = (uint8_t)(identificador >> 8);
	dados[4] = rotina.erro;
	*size    = dROTINA_STATUS_SIZE;

	return eROTINA_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      READ_RESULT: [identificador u16][tamanho total u16][ate 236 bytes a partir do offset].
 * @details    So depois que a rotina finalizou (CONCLUIDA, ABORTADA, TEMPO_ESGOTADO ou ERRO). Offset igual ao total
 *             devolve so o cabecalho; offset maior e recusado.
 * @param[in]  offset  Primeiro byte dos resultados
 * @param[out] dados   Ate dROTINA_LEITURA_CABECALHO + dROTINA_LEITURA_DADOS_MAX bytes
 * @param[out] size    Bytes escritos em dados
 * @return     eROTINA_RETURN_OK, eROTINA_RETURN_NO_RESULT ou eROTINA_RETURN_INVALID_ARGUMENT
 **********************************************************************************************************************/
rotinaReturn_t Rotina_LeResultado(uint16_t offset, uint8_t *dados, uint8_t *size)
{
	if ((dados == NULL) || (size == NULL))
	{
		return eROTINA_RETURN_INVALID_ARGUMENT;
	}

	if ((rotina.estado == (uint8_t)eROTINA_ESTADO_OCIOSA) || (rotina.estado == (uint8_t)eROTINA_ESTADO_EM_EXECUCAO))
	{
		return eROTINA_RETURN_NO_RESULT;
	}

	if (offset > rotina.resultadosSize)
	{
		return eROTINA_RETURN_INVALID_ARGUMENT;
	}

	uint16_t quantidade = (uint16_t)(rotina.resultadosSize - offset);

	if (quantidade > dROTINA_LEITURA_DADOS_MAX)
	{
		quantidade = dROTINA_LEITURA_DADOS_MAX;
	}

	dados[0] = (uint8_t)(rotina.identificador & 0xFFU);
	dados[1] = (uint8_t)(rotina.identificador >> 8);
	dados[2] = (uint8_t)(rotina.resultadosSize & 0xFFU);
	dados[3] = (uint8_t)(rotina.resultadosSize >> 8);
	memcpy(&dados[dROTINA_LEITURA_CABECALHO], &rotina.resultados[offset], quantidade);
	*size = (uint8_t)(dROTINA_LEITURA_CABECALHO + quantidade);

	return eROTINA_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      1 enquanto ha uma rotina carregada ou em execucao (a CommTask responde OCUPADO aos outros comandos).
 **********************************************************************************************************************/
uint8_t Rotina_EmExecucao(void)
{
	return (rotina.estado == (uint8_t)eROTINA_ESTADO_EM_EXECUCAO) ? 1U : 0U;
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

static uint16_t Rotina_Le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t Rotina_Le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void Rotina_Escreve32(uint8_t *p, uint32_t valor)
{
	p[0] = (uint8_t)(valor & 0xFFU);
	p[1] = (uint8_t)((valor >> 8) & 0xFFU);
	p[2] = (uint8_t)((valor >> 16) & 0xFFU);
	p[3] = (uint8_t)(valor >> 24);
}

/***********************************************************************************************************************
 * @brief      Confere um passo: codigo conhecido, tamanho que cabe no que sobrou do payload e faixa dos parametros.
 * @details    O tamanho do passo vem do codigo e dos campos de quantidade; os parametros seguem o comando direto.
 * @param[in]  p           Codigo do passo seguido dos parametros
 * @param[in]  disponivel  Bytes do payload a partir de p
 * @param[out] tamanho     Bytes do passo (codigo incluso)
 * @param[out] registro    Maior quantidade de dados do registro gerado (0 = passo sem registro)
 * @return     1 se valido
 **********************************************************************************************************************/
static uint8_t Rotina_ValidaPasso(const uint8_t *p, uint16_t disponivel, uint16_t *tamanho, uint16_t *registro)
{
	uint16_t t;
	uint8_t valido = 1U;

	*registro = 0U;

	// Tamanho fixo ou cabecalho de quem tem quantidade: o resto so e lido se couber
	switch (p[0])
	{
		case dCOMM_CMD_DAC_SET:         t = 4U; break;
		case dCOMM_CMD_GPIO_SET:        t = 3U; break;
		case dCOMM_CMD_GPIO_READ:       t = 1U; break;
		case dCOMM_CMD_PWM_SET:         t = 8U; break;
		case dCOMM_CMD_PWM_STOP:        t = 2U; break;
		case dCOMM_CMD_ADC_READ:        t = 2U; break;
		case dCOMM_CMD_I2C_CONFIG:      t = 4U; break;
		case dCOMM_CMD_I2C_SLAVE_LOAD:  t = 3U; break;
		case dCOMM_CMD_I2C_SLAVE_DUMP:  t = 3U; break;
		case dCOMM_CMD_I2C_MASTER_XFER: t = 4U; break;
		case dCOMM_CMD_I2C_SLAVE_MASK:  t = 3U; break;
		case dCOMM_CMD_I2C_SLAVE_ADDR2: t = 2U; break;
		case dCOMM_CMD_SPI_CONFIG:      t = 7U; break;
		case dCOMM_CMD_SPI_SLAVE_LOAD:  t = 2U; break;
		case dCOMM_CMD_SPI_SLAVE_READ:  t = 1U; break;
		case dCOMM_CMD_SPI_MASTER_XFER: t = 2U; break;
		case dCOMM_CMD_UART_CONFIG:     t = 5U; break;
		case dCOMM_CMD_UART_WRITE:      t = 2U; break;
		case dCOMM_CMD_UART_READ:       t = 2U; break;
		case dROTINA_PASSO_ESPERA:      t = 5U; break;
		case dROTINA_PASSO_ESPERA_GPIO: t = 7U; break;
		case dONDA_PASSO:               t = 1U + dONDA_CABECALHO_SIZE; break;
		default:                        return 0U;
	}

	if (t > disponivel)
	{
		return 0U;
	}

	switch (p[0])
	{
		case dCOMM_CMD_DAC_SET:
			valido = ((p[1] >= dANALOGOUT_CANAL_MIN) && (p[1] <= dANALOGOUT_CANAL_MAX) &&
			          (Rotina_Le16(&p[2]) <= dANALOGOUT_VALOR_MAX)) ? 1U : 0U;
		break;

		case dCOMM_CMD_GPIO_SET:
			valido = ((p[1] >= dDIGITALIO_SAIDA_MIN) && (p[1] <= dDIGITALIO_SAIDA_MAX) &&
			          (p[2] <= dDIGITALIO_NIVEL_ALTO)) ? 1U : 0U;
		break;

		case dCOMM_CMD_GPIO_READ:
			*registro = 1U;
		break;

		case dCOMM_CMD_PWM_SET:
		{
			uint32_t frequencia = Rotina_Le32(&p[2]);
			valido = ((p[1] >= dPWM_CANAL_MIN) && (p[1] <= dPWM_CANAL_MAX) && (frequencia >= dPWM_FREQUENCIA_MIN) &&
			          (frequencia <= dPWM_FREQUENCIA_MAX) && (Rotina_Le16(&p[6]) <= dPWM_RAZAO_MAX)) ? 1U : 0U;
		}
		break;

		case dCOMM_CMD_PWM_STOP:
			valido = ((p[1] >= dPWM_CANAL_MIN) && (p[1] <= dPWM_CANAL_MAX)) ? 1U : 0U;
		break;

		case dCOMM_CMD_ADC_READ:
			valido    = (p[1] <= dANALOGIN_CANAL_MAX) ? 1U : 0U;
			*registro = 3U;
		break;

		case dCOMM_CMD_I2C_CONFIG:
			valido = ((p[1] <= dI2CBUS_PAPEL_MESTRE) && (p[3] <= dI2CBUS_VELOCIDADE_MAX) &&
			          ((p[1] == dI2CBUS_PAPEL_MESTRE) ||
			           ((p[2] >= dI2CBUS_ENDERECO_MIN) && (p[2] <= dI2CBUS_ENDERECO_MAX)))) ? 1U : 0U;
		break;

		case dCOMM_CMD_I2C_SLAVE_LOAD:
			valido = ((p[2] > 0U) && (((uint16_t)p[1] + p[2]) <= dI2CBUS_TAMANHO_MAPA)) ? 1U : 0U;
			t      = (uint16_t)(t + p[2]);
		break;

		case dCOMM_CMD_I2C_SLAVE_MASK:
			valido = ((p[2] > 0U) && (((uint16_t)p[1] + p[2]) <= dI2CBUS_TAMANHO_MAPA)) ? 1U : 0U;
			t      = (uint16_t)(t + p[2]);
		break;

		case dCOMM_CMD_I2C_SLAVE_ADDR2:
			valido = ((p[1] == dI2CBUS_ENDERECO2_NENHUM) ||
			          ((p[1] >= dI2CBUS_ENDERECO_MIN) && (p[1] <= dI2CBUS_ENDERECO_MAX))) ? 1U : 0U;
		break;

		case dCOMM_CMD_I2C_SLAVE_DUMP:
			valido    = ((p[2] > 0U) && (p[2] <= dCOMM_PAYLOAD_MAX_SIZE) &&
			             (((uint16_t)p[1] + p[2]) <= dI2CBUS_TAMANHO_MAPA)) ? 1U : 0U;
			*registro = p[2];
		break;

		case dCOMM_CMD_I2C_MASTER_XFER:
			valido    = ((p[1] >= dI2CBUS_ENDERECO_MIN) && (p[1] <= dI2CBUS_ENDERECO_MAX) &&
			             ((p[2] > 0U) || (p[3] > 0U)) && (p[3] <= dCOMM_PAYLOAD_MAX_SIZE)) ? 1U : 0U;
			t         = (uint16_t)(t + p[2]);
			*registro = p[3];
		break;

		case dCOMM_CMD_SPI_CONFIG:
			valido = ((p[1] <= dSPIBUS_PAPEL_MESTRE) && (p[2] <= dSPIBUS_MODO_MAX)) ? 1U : 0U;
		break;

		case dCOMM_CMD_SPI_SLAVE_LOAD:
			valido = (p[1] <= dSPIBUS_TAMANHO_BUFFER) ? 1U : 0U;
			t      = (uint16_t)(t + p[1]);
		break;

		case dCOMM_CMD_SPI_SLAVE_READ:
			*registro = 1U + dSPIBUS_TAMANHO_BUFFER;
		break;

		case dCOMM_CMD_SPI_MASTER_XFER:
			valido    = ((p[1] > 0U) && (p[1] <= dSPIBUS_TAMANHO_BUFFER)) ? 1U : 0U;
			t         = (uint16_t)(t + p[1]);
			*registro = p[1];
		break;

		case dCOMM_CMD_UART_CONFIG:
		{
			uint32_t baud = Rotina_Le32(&p[1]);
			valido = ((baud >= dUARTBUS_BAUD_MIN) && (baud <= dUARTBUS_BAUD_MAX)) ? 1U : 0U;
		}
		break;

		case dCOMM_CMD_UART_WRITE:
			valido = ((p[1] > 0U) && (p[1] <= dUARTBUS_TAMANHO_MAX)) ? 1U : 0U;
			t      = (uint16_t)(t + p[1]);
		break;

		case dCOMM_CMD_UART_READ:
			valido    = (p[1] <= dUARTBUS_TAMANHO_MAX) ? 1U : 0U;
			*registro = (uint16_t)(1U + p[1]);
		break;

		case dROTINA_PASSO_ESPERA:
		break;

		case dONDA_PASSO:
			t      = (uint16_t)(1U + Onda_Tamanho(&p[1], (uint16_t)(disponivel - 1U)));
			valido = ((t <= disponivel) && (Onda_Valida(&p[1], (uint16_t)(t - 1U)) != 0U)) ? 1U : 0U;
		break;

		case dROTINA_PASSO_ESPERA_GPIO:
			valido    = ((p[1] >= dROTINA_ENTRADA_MIN) && (p[1] <= dROTINA_ENTRADA_MAX) &&
			             (p[2] <= dDIGITALIO_NIVEL_ALTO)) ? 1U : 0U;
			*registro = dROTINA_ESPERA_GPIO_REGISTRO;
		break;

		default:
			valido = 0U;
		break;
	}

	if ((valido == 0U) || (t > disponivel))
	{
		return 0U;
	}

	*tamanho = t;

	return 1U;
}

/***********************************************************************************************************************
 * @brief      1 se 'instante' ja chegou em 'referencia' (comparacao com sinal: vale na volta dos 32 bits).
 **********************************************************************************************************************/
static uint8_t Rotina_Passou(uint32_t instante, uint32_t referencia)
{
	return ((int32_t)(instante - referencia) >= 0) ? 1U : 0U;
}

/***********************************************************************************************************************
 * @brief      Espera ate 'alvo' (ou ate a entrada assumir o nivel), sem passar do tempo maximo da rotina.
 * @details    A BSP pode acordar antes (ABORT ou notificacao atrasada): o laco confere o motivo e volta a esperar.
 * @param[in]  alvo           Instante em que o passo termina (duracao do ESPERA ou tempo limite do ESPERA_GPIO)
 * @param[in]  entrada, nivel Entrada (1 a 4; 0 = nenhuma) e nivel esperado
 * @param[out] instante       Instante em que a espera terminou (o da transicao, no eROTINA_FIM_ENTRADA)
 **********************************************************************************************************************/
static rotinaFimEspera_t Rotina_Aguarda(uint32_t alvo, uint8_t entrada, uint8_t nivel, uint32_t *instante)
{
	// O passo termina antes do limite da rotina: espera o alvo; senao, so ate o limite
	uint8_t  limitado = (Rotina_Passou(alvo, rotina.limite) != 0U) && (alvo != rotina.limite) ? 1U : 0U;
	uint32_t ate      = (limitado != 0U) ? rotina.limite : alvo;

	for (;;)
	{
		uint32_t agora;

		if (rotina.abortar != 0U)
		{
			*instante = rotina.func.agora();
			return eROTINA_FIM_ABORT;
		}

		if (rotina.func.espera(ate, entrada, nivel, instante) == eROTINA_EVENTO_ENTRADA)
		{
			return eROTINA_FIM_ENTRADA;
		}

		agora = rotina.func.agora();

		if (rotina.abortar != 0U)
		{
			*instante = agora;
			return eROTINA_FIM_ABORT;
		}

		if (Rotina_Passou(agora, ate) != 0U)
		{
			*instante = agora;
			return (limitado != 0U) ? eROTINA_FIM_LIMITE : eROTINA_FIM_ALVO;
		}
	}
}

/***********************************************************************************************************************
 * @brief      Grava um registro [indice][instante desde o inicio u32][quantidade][dados] na area de resultados.
 * @details    O tamanho maximo ja foi conferido na validacao: o registro sempre cabe.
 **********************************************************************************************************************/
static void Rotina_Grava(uint8_t indice, uint32_t instante, const uint8_t *dados, uint8_t quantidade)
{
	uint8_t *p = &rotina.resultados[rotina.resultadosSize];

	if (((uint32_t)rotina.resultadosSize + dROTINA_REGISTRO_CABECALHO + quantidade) > dROTINA_RESULTADOS_SIZE)
	{
		return;
	}

	p[0] = indice;
	Rotina_Escreve32(&p[1], instante - rotina.inicio);
	p[5] = quantidade;
	memcpy(&p[dROTINA_REGISTRO_CABECALHO], dados, quantidade);

	rotina.resultadosSize = (uint16_t)(rotina.resultadosSize + dROTINA_REGISTRO_CABECALHO + quantidade);
}

/***********************************************************************************************************************
 * @brief      Aplica o estado seguro e so depois publica o estado final (a CommTask nunca ve a rotina finalizada
 *             com um estimulo ainda aplicado).
 **********************************************************************************************************************/
static void Rotina_Finaliza(rotinaEstado_t estado, uint8_t erro)
{
	(void)Exec_SafeState();

	rotina.erro    = erro;
	rotina.abortar = 0U;
	rotina.estado  = (uint8_t)estado;
}

/** @} DOXYGEN GROUP TAG END OF FILE */
