/***********************************************************************************************************************
 *   @file         Bsp_Tempo.c
 *   @addtogroup   App
 *   @brief        Base de tempo das rotinas (TIM5 a 1 MHz) e espera por instante ou por nivel em uma entrada
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Ferramentas:</b>
 *   - Generic.
 *
 *   \n <b>Dependencias:</b>
 *   - TIM5 configurado pelo CubeMX (MX_TIM5_Init): 108 MHz / 108 = 1 MHz, 32 bits, IRQ prioridade 5. O canal 1
 *     fica no modo de comparacao padrao (frozen): so a flag e a interrupcao do compare sao usadas.
 *   - Entradas GPIO5 a GPIO8 (PC12, PD2, PG2, PG3) configuradas como entrada sem pull pelo MX_GPIO_Init.
 *   - FreeRTOS: a task que espera bloqueia em ulTaskNotifyTake; o compare, a borda e o ABORT a notificam.
 *
 *   \n <b>Observacoes:</b>
 *   - A espera nao ocupa o processador: a task dorme ate o compare do TIM5 no instante alvo, ate a borda na
 *     entrada monitorada ou ate o ABORT (BspTempo_Acorda, chamado pela CommTask pela Rotina).
 *   - O instante de uma borda e lido do TIM5 dentro da ISR do EXTI: nao depende de quando a task volta a rodar.
 *   - PD2 e PG2 dividem a linha EXTI2 (assim como todo pino de numero 2): so ha um ESPERA_GPIO ativo por vez, e
 *     a linha e ligada na porta da entrada no inicio de cada espera e desligada no fim (o pino volta a entrada
 *     sem interrupcao). O EXTI9_5 continua do CS do SPI (Bsp_Spi), que repassa as outras bordas para ca.
 *   - A task fica guardada depois da primeira espera, para um ABORT que chegue entre a conferencia da Rotina e o
 *     bloqueio nao se perder: a notificacao fica pendente e a proxima espera retorna na hora. Uma notificacao
 *     que sobrar so faz a Rotina conferir o motivo e voltar a esperar.
 *   - Prioridade 5 nas IRQs do TIM5 e do EXTI: a mais alta que ainda pode chamar a API FromISR do FreeRTOS.
 *
 *   Changelog
 *   @version      <b>1.0.0 - 03/11/2026</b> \n Luiz Neto \n Primeira versao
 *   @version      <b>1.1.0 - 04/11/2026</b> \n Luiz Neto \n A comparacao do canal 2 do TIM5 vai para o gerador de ondas
 *
 *   @copyright
 *   @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include "Bsp_Tempo.h"
#include "Bsp_Onda.h"
#include "Rotina.h"
#include "DigitalIo.h"
#include "main.h"
#include "tim.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stddef.h>

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

/// Prioridade das IRQs do EXTI das entradas (a mesma do TIM5 e dos barramentos)
#define dBSP_TEMPO_PRIORIDADE_IRQ (5U)

/// Folga, em ticks, na espera da notificacao (o compare acorda antes; isto so protege de uma notificacao perdida)
#define dBSP_TEMPO_FOLGA_TICKS (2U)

/***********************************************************************************************************************
 * TIPOS LOCAIS
 **********************************************************************************************************************/

// Pino de uma entrada e a linha de interrupcao dele
typedef struct bspTempoEntrada
{
	GPIO_TypeDef *porta;
	uint16_t      pino;
	IRQn_Type     irq;
} bspTempoEntrada_t;

static struct bspTempo
{
	volatile TaskHandle_t tarefa;     ///< Task que espera (a TestExecTask; fica guardada depois da 1a espera)
	volatile uint8_t      entrada;    ///< Entrada monitorada (1 a 4; 0 = nenhuma)
	volatile uint8_t      nivel;      ///< Nivel esperado
	volatile uint8_t      ocorreu;    ///< 1: a entrada assumiu o nivel (a ISR preencheu o instante)
	volatile uint32_t     instante;   ///< TIM5 no momento da borda
} bspTempo;

/***********************************************************************************************************************
 * VARIAVEIS LOCAIS
 **********************************************************************************************************************/

/// Entradas 1 a 4 (GPIO5 a GPIO8; indice = entrada - 1)
static const bspTempoEntrada_t
bspTempoEntradas[dDIGITALIO_ENTRADA_MAX] =
{
		{GPIO1_IN_GPIO_Port, GPIO1_IN_Pin, EXTI15_10_IRQn}, // GPIO5 (PC12)
		{GPIO2_IN_GPIO_Port, GPIO2_IN_Pin, EXTI2_IRQn},     // GPIO6 (PD2)
		{GPIO3_IN_GPIO_Port, GPIO3_IN_Pin, EXTI2_IRQn},     // GPIO7 (PG2)
		{GPIO4_IN_GPIO_Port, GPIO4_IN_Pin, EXTI3_IRQn},     // GPIO8 (PG3)
};

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static uint32_t       BspTempo_Agora(void);
static rotinaEvento_t BspTempo_Espera(uint32_t alvo, uint8_t entrada, uint8_t nivel, uint32_t *instante);
static void           BspTempo_Acorda(void);
static void           BspTempo_LigaEntrada(const bspTempoEntrada_t *e);
static void           BspTempo_DesligaEntrada(const bspTempoEntrada_t *e);
static void           BspTempo_Notifica(void);

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Liga o TIM5 (contagem livre de 32 bits a 1 MHz) e entrega a base de tempo a Rotina.
 **********************************************************************************************************************/
bspTempoReturn_t BspTempo_Init(void)
{
	const rotinaFunc_t func =
	{
		.agora  = BspTempo_Agora,
		.espera = BspTempo_Espera,
		.acorda = BspTempo_Acorda,
	};

	bspTempo.tarefa  = NULL;
	bspTempo.entrada = 0U;

	__HAL_TIM_DISABLE_IT(&htim5, TIM_IT_CC1);

	if (HAL_TIM_Base_Start(&htim5) != HAL_OK)
	{
		return eBSP_TEMPO_RETURN_ERROR;
	}

	if (Rotina_Init(&func) != eROTINA_RETURN_OK)
	{
		return eBSP_TEMPO_RETURN_ERROR;
	}

	return eBSP_TEMPO_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Borda em uma entrada: se e a monitorada e esta no nivel esperado, guarda o instante e acorda a task.
 * @details    Roda na ISR do EXTI. O TIM5 e lido antes de tudo, para o instante ficar o mais perto da borda.
 * @param[in]  pino  Pino que gerou a interrupcao (GPIO_PIN_x)
 **********************************************************************************************************************/
void BspTempo_Borda(uint16_t pino)
{
	uint32_t agora = htim5.Instance->CNT;
	uint8_t entrada = bspTempo.entrada;

	if ((entrada == 0U) || (bspTempo.ocorreu != 0U))
	{
		return;
	}

	const bspTempoEntrada_t *e = &bspTempoEntradas[entrada - 1U];

	if (pino != e->pino)
	{
		return;
	}

	uint8_t nivel = (HAL_GPIO_ReadPin(e->porta, e->pino) == GPIO_PIN_SET) ? 1U : 0U;

	if (nivel != bspTempo.nivel)
	{
		return;
	}

	bspTempo.instante = agora;
	bspTempo.ocorreu  = 1U;
	CLEAR_BIT(EXTI->IMR, (uint32_t)e->pino);

	BspTempo_Notifica();
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Instante atual em us (contagem livre do TIM5).
 **********************************************************************************************************************/
static uint32_t BspTempo_Agora(void)
{
	return htim5.Instance->CNT;
}

/***********************************************************************************************************************
 * @brief      Bloqueia a task ate o instante alvo, ate a entrada assumir o nivel ou ate o ABORT.
 * @details    Se a entrada ja esta no nivel, retorna na hora com o instante atual. O compare e armado depois de
 *             ligar a entrada e conferido de novo depois de armado, para nao perder um alvo que ja passou.
 * @param[in]  alvo      Instante em que a espera termina (us)
 * @param[in]  entrada   1 a 4 (GPIO5 a GPIO8) ou 0 (so tempo)
 * @param[in]  nivel     Nivel esperado na entrada
 * @param[out] instante  Momento em que o nivel ocorreu (eROTINA_EVENTO_ENTRADA) ou em que a espera terminou
 **********************************************************************************************************************/
static rotinaEvento_t BspTempo_Espera(uint32_t alvo, uint8_t entrada, uint8_t nivel, uint32_t *instante)
{
	const bspTempoEntrada_t *e = NULL;
	rotinaEvento_t evento;

	bspTempo.ocorreu = 0U;
	bspTempo.nivel   = nivel;
	bspTempo.tarefa  = xTaskGetCurrentTaskHandle();

	if ((entrada >= dDIGITALIO_ENTRADA_MIN) && (entrada <= dDIGITALIO_ENTRADA_MAX))
	{
		e = &bspTempoEntradas[entrada - 1U];
		bspTempo.entrada = entrada;
		BspTempo_LigaEntrada(e);

		// Ja no nivel: nao ha borda para esperar
		uint8_t atual = (HAL_GPIO_ReadPin(e->porta, e->pino) == GPIO_PIN_SET) ? 1U : 0U;

		if ((atual == nivel) && (bspTempo.ocorreu == 0U))
		{
			bspTempo.instante = htim5.Instance->CNT;
			bspTempo.ocorreu  = 1U;
		}
	}

	if (bspTempo.ocorreu == 0U)
	{
		__HAL_TIM_SET_COMPARE(&htim5, TIM_CHANNEL_1, alvo);
		__HAL_TIM_CLEAR_FLAG(&htim5, TIM_FLAG_CC1);
		__HAL_TIM_ENABLE_IT(&htim5, TIM_IT_CC1);

		int32_t falta = (int32_t)(alvo - htim5.Instance->CNT);

		if ((falta > 0) && (bspTempo.ocorreu == 0U))
		{
			TickType_t ticks = pdMS_TO_TICKS((uint32_t)falta / 1000U) + dBSP_TEMPO_FOLGA_TICKS;
			(void)ulTaskNotifyTake(pdTRUE, ticks);
		}

		__HAL_TIM_DISABLE_IT(&htim5, TIM_IT_CC1);
	}

	if (e != NULL)
	{
		BspTempo_DesligaEntrada(e);
		bspTempo.entrada = 0U;
	}

	if (bspTempo.ocorreu != 0U)
	{
		*instante = bspTempo.instante;
		return eROTINA_EVENTO_ENTRADA;
	}

	*instante = htim5.Instance->CNT;
	evento    = ((int32_t)(*instante - alvo) >= 0) ? eROTINA_EVENTO_TEMPO : eROTINA_EVENTO_ACORDOU;

	return evento;
}

/***********************************************************************************************************************
 * @brief      ABORT: acorda a task que espera (sem espera em andamento, a notificacao fica pendente).
 **********************************************************************************************************************/
static void BspTempo_Acorda(void)
{
	TaskHandle_t tarefa = bspTempo.tarefa;

	if (tarefa != NULL)
	{
		xTaskNotifyGive(tarefa);
	}
}

/***********************************************************************************************************************
 * @brief      Liga a interrupcao nas duas bordas da entrada (a linha EXTI passa para a porta dela).
 **********************************************************************************************************************/
static void BspTempo_LigaEntrada(const bspTempoEntrada_t *e)
{
	GPIO_InitTypeDef config = {0};

	config.Pin  = e->pino;
	config.Mode = GPIO_MODE_IT_RISING_FALLING;
	config.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(e->porta, &config);

	__HAL_GPIO_EXTI_CLEAR_IT(e->pino);
	HAL_NVIC_SetPriority(e->irq, dBSP_TEMPO_PRIORIDADE_IRQ, 0);
	HAL_NVIC_EnableIRQ(e->irq);
}

/***********************************************************************************************************************
 * @brief      Desliga a interrupcao e devolve o pino a entrada sem pull (como no MX_GPIO_Init).
 **********************************************************************************************************************/
static void BspTempo_DesligaEntrada(const bspTempoEntrada_t *e)
{
	GPIO_InitTypeDef config = {0};

	HAL_NVIC_DisableIRQ(e->irq);
	HAL_GPIO_DeInit(e->porta, e->pino);   // desfaz o EXTI da linha

	config.Pin  = e->pino;
	config.Mode = GPIO_MODE_INPUT;
	config.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(e->porta, &config);
}

/***********************************************************************************************************************
 * @brief      Acorda a task da espera a partir de uma ISR.
 **********************************************************************************************************************/
static void BspTempo_Notifica(void)
{
	BaseType_t acordou = pdFALSE;
	TaskHandle_t tarefa = bspTempo.tarefa;

	if (tarefa != NULL)
	{
		vTaskNotifyGiveFromISR(tarefa, &acordou);
		portYIELD_FROM_ISR(acordou);
	}
}

/***********************************************************************************************************************
 * INTERRUPCOES
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Compare do TIM5 (chamado pelo HAL_TIM_IRQHandler): o instante alvo chegou.
 **********************************************************************************************************************/
void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef *htim)
{
	if (htim->Instance != TIM5)
	{
		return;
	}

	if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2)
	{
		BspOnda_Tick();
		return;
	}

	__HAL_TIM_DISABLE_IT(&htim5, TIM_IT_CC1);
	BspTempo_Notifica();
}

/***********************************************************************************************************************
 * @brief      IRQ da linha EXTI2: PD2 (GPIO6) ou PG2 (GPIO7), conforme a porta ligada na espera.
 **********************************************************************************************************************/
void EXTI2_IRQHandler(void)
{
	HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_2);
}

/***********************************************************************************************************************
 * @brief      IRQ da linha EXTI3: PG3 (GPIO8).
 **********************************************************************************************************************/
void EXTI3_IRQHandler(void)
{
	HAL_GPIO_EXTI_IRQHandler(GPIO4_IN_Pin);
}

/***********************************************************************************************************************
 * @brief      IRQ das linhas EXTI 10 a 15: so o PC12 (GPIO5) usa.
 **********************************************************************************************************************/
void EXTI15_10_IRQHandler(void)
{
	HAL_GPIO_EXTI_IRQHandler(GPIO1_IN_Pin);
}

/** @} DOXYGEN GROUP TAG END OF FILE */
