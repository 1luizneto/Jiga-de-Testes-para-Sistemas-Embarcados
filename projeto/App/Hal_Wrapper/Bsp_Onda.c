/***********************************************************************************************************************
 *   @file         Bsp_Onda.c
 *   @addtogroup   App
 *   @brief        BSP do gerador de ondas: tick pelo canal 2 do TIM5, escritas nos destinos e o DAC por DMA
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Ferramentas:</b>
 *   - STM32CubeIDE / HAL STM32F7.
 *
 *   \n <b>Dependencias:</b>
 *   - htim5 (base de tempo de 1 us, Bsp_Tempo), hdac (MX_DAC_Init), Pwm (Pwm_Razao) e I2cBus (I2cBus_EscreveIsr).
 *
 *   \n <b>Observacoes:</b>
 *   - Tick: comparacao do canal 2 do TIM5 (o canal 1 e das esperas da Rotina). A IRQ do TIM5 tem prioridade 5, a
 *     mesma do I2C2: a escrita no mapa feita aqui nunca e interrompida por uma leitura do DUT, e vice-versa.
 *   - DAC por DMA (passo < 100 us): DAC1 com trigger do TIM7 no DMA1 Stream5, DAC2 com o TIM4 no DMA1 Stream6 (canal 7
 *     das duas streams), em modo circular. Esses timers e o DMA nao sao usados por mais nada e sao configurados aqui,
 *     em tempo de execucao, porque o DAC troca de "sem trigger" (comandos diretos) para "trigger do timer" (onda) e
 *     volta; a configuracao de boot continua no CubeMX. O DMA roda sem interrupcao; o fim dos ciclos vem do tick.
 *   - A interrupcao de underrun do DAC (ligada pelo HAL_DAC_Start_DMA) divide o vetor com o TIM6 da base de tempo do
 *     HAL: ela e desligada logo depois de iniciar o DMA.
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

#include "Bsp_Onda.h"
#include "Onda.h"
#include "Pwm.h"
#include "I2cBus.h"
#include "dac.h"
#include "tim.h"

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

// Antecedencia minima do tick: um alvo que ja passou (ou quase) e agendado para logo
#define dBSP_ONDA_ANTECEDENCIA_US (3U)

// Clock dos timers do APB1 (TIM4 e TIM7) em MHz: contagens por us
#define dBSP_ONDA_CLOCK_MHZ (108U)

/***********************************************************************************************************************
 * VARIAVEIS LOCAIS
 **********************************************************************************************************************/

static DMA_HandleTypeDef bspOndaDma[2];
static TIM_HandleTypeDef bspOndaTim[2];

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static uint32_t BspOnda_Agora(void);
static void     BspOnda_Agenda(uint32_t alvo);
static void     BspOnda_Desagenda(void);
static void     BspOnda_Dac(uint8_t canal, uint16_t valor);
static void     BspOnda_Pwm(uint8_t canal, uint16_t razao);
static void     BspOnda_I2c(uint8_t offset, const uint8_t *dados, uint8_t quantidade);
static uint8_t  BspOnda_DacDma(uint8_t canal, const uint16_t *amostras, uint16_t quantidade, uint32_t passoUs);
static void     BspOnda_DacDmaPara(uint8_t canal);
static uint32_t BspOnda_CanalHal(uint8_t canal);

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Prepara o DMA e os timers do DAC e entrega as funcoes ao modulo Onda. Chamado depois do BspTempo_Init.
 **********************************************************************************************************************/
bspOndaReturn_t BspOnda_Init(void)
{
	static const ondaFunc_t func =
	{
		.agora      = BspOnda_Agora,
		.agenda     = BspOnda_Agenda,
		.desagenda  = BspOnda_Desagenda,
		.dac        = BspOnda_Dac,
		.pwm        = BspOnda_Pwm,
		.i2c        = BspOnda_I2c,
		.dacDma     = BspOnda_DacDma,
		.dacDmaPara = BspOnda_DacDmaPara,
	};
	DMA_Stream_TypeDef *const streams[2] = {DMA1_Stream5, DMA1_Stream6};

	__HAL_RCC_DMA1_CLK_ENABLE();
	__HAL_RCC_TIM7_CLK_ENABLE();
	__HAL_RCC_TIM4_CLK_ENABLE();

	for (uint8_t i = 0U; i < 2U; i++)
	{
		bspOndaDma[i].Instance                 = streams[i];
		bspOndaDma[i].Init.Channel             = DMA_CHANNEL_7;
		bspOndaDma[i].Init.Direction           = DMA_MEMORY_TO_PERIPH;
		bspOndaDma[i].Init.PeriphInc           = DMA_PINC_DISABLE;
		bspOndaDma[i].Init.MemInc              = DMA_MINC_ENABLE;
		bspOndaDma[i].Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
		bspOndaDma[i].Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
		bspOndaDma[i].Init.Mode                = DMA_CIRCULAR;
		bspOndaDma[i].Init.Priority            = DMA_PRIORITY_HIGH;
		bspOndaDma[i].Init.FIFOMode            = DMA_FIFOMODE_DISABLE;

		if (HAL_DMA_Init(&bspOndaDma[i]) != HAL_OK)
		{
			return eBSP_ONDA_RETURN_ERROR;
		}
	}

	__HAL_LINKDMA(&hdac, DMA_Handle1, bspOndaDma[0]);
	__HAL_LINKDMA(&hdac, DMA_Handle2, bspOndaDma[1]);

	bspOndaTim[0].Instance = TIM7;
	bspOndaTim[1].Instance = TIM4;

	BspOnda_Desagenda();

	return (Onda_Init(&func) == eONDA_RETURN_OK) ? eBSP_ONDA_RETURN_OK : eBSP_ONDA_RETURN_ERROR;
}

/***********************************************************************************************************************
 * @brief      Comparacao do canal 2 do TIM5 (chamado pelo HAL_TIM_OC_DelayElapsedCallback da Bsp_Tempo).
 **********************************************************************************************************************/
void BspOnda_Tick(void)
{
	__HAL_TIM_DISABLE_IT(&htim5, TIM_IT_CC2);
	Onda_Tick();
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Instante atual em us (TIM5).
 **********************************************************************************************************************/
static uint32_t BspOnda_Agora(void)
{
	return htim5.Instance->CNT;
}

/***********************************************************************************************************************
 * @brief      Proximo tick em 'alvo'; um alvo que ja passou vira 'agora + antecedencia' (senao so viria na volta).
 **********************************************************************************************************************/
static void BspOnda_Agenda(uint32_t alvo)
{
	uint32_t minimo = htim5.Instance->CNT + dBSP_ONDA_ANTECEDENCIA_US;

	if ((int32_t)(alvo - minimo) < 0)
	{
		alvo = minimo;
	}

	__HAL_TIM_SET_COMPARE(&htim5, TIM_CHANNEL_2, alvo);
	__HAL_TIM_CLEAR_FLAG(&htim5, TIM_FLAG_CC2);
	__HAL_TIM_ENABLE_IT(&htim5, TIM_IT_CC2);
}

/***********************************************************************************************************************
 * @brief      Sem tick.
 **********************************************************************************************************************/
static void BspOnda_Desagenda(void)
{
	__HAL_TIM_DISABLE_IT(&htim5, TIM_IT_CC2);
	__HAL_TIM_CLEAR_FLAG(&htim5, TIM_FLAG_CC2);
}

/***********************************************************************************************************************
 * @brief      Escreve no DAC sem trigger (o canal ja foi ligado pelo estado seguro do boot).
 **********************************************************************************************************************/
static void BspOnda_Dac(uint8_t canal, uint16_t valor)
{
	(void)HAL_DAC_SetValue(&hdac, BspOnda_CanalHal(canal), DAC_ALIGN_12B_R, valor);
}

/***********************************************************************************************************************
 * @brief      Razao ciclica do PWM, so no registrador de comparacao (sem reiniciar o contador do timer).
 **********************************************************************************************************************/
static void BspOnda_Pwm(uint8_t canal, uint16_t razao)
{
	(void)Pwm_Razao(canal, razao);
}

/***********************************************************************************************************************
 * @brief      Registradores de dados do sensor emulado (mapa da escrava I2C).
 **********************************************************************************************************************/
static void BspOnda_I2c(uint8_t offset, const uint8_t *dados, uint8_t quantidade)
{
	(void)I2cBus_EscreveIsr(offset, dados, quantidade);
}

/***********************************************************************************************************************
 * @brief      Toca um periodo de amostras no DAC por DMA circular, com o trigger do timer do canal a cada passoUs.
 * @return     0 se o DMA comecou
 **********************************************************************************************************************/
static uint8_t BspOnda_DacDma(uint8_t canal, const uint16_t *amostras, uint16_t quantidade, uint32_t passoUs)
{
	static const uint32_t triggers[2] = {DAC_TRIGGER_T7_TRGO, DAC_TRIGGER_T4_TRGO};
	TIM_HandleTypeDef *tim = &bspOndaTim[canal - 1U];
	TIM_MasterConfigTypeDef mestre = {0};
	DAC_ChannelConfTypeDef dac = {0};
	uint32_t canalHal = BspOnda_CanalHal(canal);

	tim->Init.Prescaler         = 0U;
	tim->Init.CounterMode       = TIM_COUNTERMODE_UP;
	tim->Init.Period            = (passoUs * dBSP_ONDA_CLOCK_MHZ) - 1U;
	tim->Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
	tim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
	mestre.MasterOutputTrigger  = TIM_TRGO_UPDATE;
	mestre.MasterSlaveMode      = TIM_MASTERSLAVEMODE_DISABLE;
	dac.DAC_Trigger             = triggers[canal - 1U];
	dac.DAC_OutputBuffer        = DAC_OUTPUTBUFFER_ENABLE;

	(void)HAL_DAC_Stop(&hdac, canalHal);

	if ((HAL_TIM_Base_Init(tim) != HAL_OK) || (HAL_TIMEx_MasterConfigSynchronization(tim, &mestre) != HAL_OK) ||
	    (HAL_DAC_ConfigChannel(&hdac, &dac, canalHal) != HAL_OK) ||
	    (HAL_DAC_Start_DMA(&hdac, canalHal, (uint32_t *)amostras, quantidade, DAC_ALIGN_12B_R) != HAL_OK))
	{
		BspOnda_DacDmaPara(canal);
		return 1U;
	}

	__HAL_DAC_DISABLE_IT(&hdac, (canal == 1U) ? DAC_IT_DMAUDR1 : DAC_IT_DMAUDR2);

	return (HAL_TIM_Base_Start(tim) == HAL_OK) ? 0U : 1U;
}

/***********************************************************************************************************************
 * @brief      Para o DMA e o timer e devolve o canal ao modo sem trigger (escrita direta), ja ligado.
 **********************************************************************************************************************/
static void BspOnda_DacDmaPara(uint8_t canal)
{
	DAC_ChannelConfTypeDef dac = {0};
	uint32_t canalHal = BspOnda_CanalHal(canal);

	(void)HAL_TIM_Base_Stop(&bspOndaTim[canal - 1U]);
	(void)HAL_DAC_Stop_DMA(&hdac, canalHal);
	dac.DAC_Trigger      = DAC_TRIGGER_NONE;
	dac.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
	(void)HAL_DAC_ConfigChannel(&hdac, &dac, canalHal);
	(void)HAL_DAC_Start(&hdac, canalHal);
}

/***********************************************************************************************************************
 * @brief      Canal do protocolo (1, 2) -> canal do HAL.
 **********************************************************************************************************************/
static uint32_t BspOnda_CanalHal(uint8_t canal)
{
	return (canal == 1U) ? DAC_CHANNEL_1 : DAC_CHANNEL_2;
}

/** @} DOXYGEN GROUP TAG END OF FILE */
