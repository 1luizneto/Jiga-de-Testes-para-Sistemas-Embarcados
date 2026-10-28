/***********************************************************************************************************************
 *   @file         Bsp_Spi.c
 *   @addtogroup   App
 *   @brief        Abstracao do SPI2 (HAL) para o modulo SpiBus: escravo por interrupcao com o CS no PD7 e mestre por
 *                 polling
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Ferramentas:</b>
 *   - Generic.
 *
 *   \n <b>Dependencias:</b>
 *   - hspi2 configurado pelo CubeMX (MX_SPI2_Init): PB10 = SCK, PB15 = MOSI, PC2 = MISO, 8 bits, NSS por software,
 *     IRQ na prioridade 5. PD7 (SPI_CS) como saida em nivel alto (MX_GPIO_Init).
 *
 *   \n <b>Observacoes:</b>
 *   - PD7 tem dois papeis: como mestre, saida (CS que a jiga baixa durante a transferencia); como escrava,
 *     entrada com interrupcao nas duas bordas (EXTI9_5) e pull-up, porque o PD7 nao e o NSS de hardware do SPI2.
 *     O CS do DUT chega ao SPI pelo bit SSI: descida -> SSI = 0 (selecionado), subida -> SSI = 1.
 *   - Escrava sempre armada (HAL_SPI_TransmitReceive_IT) com o buffer de transmissao completo (SpiBus_PreparaTx):
 *     o 1o byte ja esta no FIFO antes do 1o clock do DUT. Na subida do CS: esvazia o FIFO de recepcao, entrega o
 *     recebido ao SpiBus, reseta o SPI2 pelo RCC (unico jeito de limpar o FIFO de transmissao do F7) e rearma.
 *   - Um byte por interrupcao: o clock do DUT como mestre deve ficar em ate ~1 MHz.
 *   - Define o EXTI9_5_IRQHandler e o HAL_GPIO_EXTI_Callback: se outro modulo precisar de EXTI, unificar aqui.
 *   - Desligado (boot e RESET_ALL) = HAL_SPI_DeInit: pinos soltos e PD7 como saida em nivel alto (CS inativo).
 *
 *   Changelog
 *   @version      <b>1.0.0 - 28/10/2026</b> \n Luiz Neto \n Primeira versao
 *
 *   @copyright
 *   @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include "Bsp_Spi.h"
#include "SpiBus.h"
#include "DebugLog.h"
#include "spi.h"
#include "main.h"

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

// Tempo maximo de uma transferencia como mestre: 239 bytes a ~211 kHz levam ~9 ms
#define dBSP_SPI_TIMEOUT_MS (50U)

/***********************************************************************************************************************
 * VARIAVEIS LOCAIS
 **********************************************************************************************************************/

static struct bspSpi
{
	volatile uint8_t escravo;                  ///< 1: escrava armada (o EXTI do CS repassa ao SpiBus)
	uint8_t tx[dSPIBUS_TAMANHO_BUFFER];        ///< Copia do buffer de transmissao da transacao armada
	uint8_t rx[dSPIBUS_TAMANHO_BUFFER];        ///< O que o DUT enviou na transacao armada
	uint8_t irqSpiLigada;                      ///< Estado das IRQs antes do lock
	uint8_t irqCsLigada;
} bspSpi;

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static spiBusReturn_t BspSpi_Config(uint8_t papel, uint8_t modo, uint16_t divisor);
static spiBusReturn_t BspSpi_Off(void);
static spiBusReturn_t BspSpi_Xfer(const uint8_t *escrita, uint8_t *leitura, uint8_t quantidade);
static void BspSpi_Lock(void);
static void BspSpi_Unlock(void);
static uint32_t BspSpi_Prescaler(uint16_t divisor);
static void BspSpi_CsSaida(void);
static void BspSpi_CsEntrada(void);
static spiBusReturn_t BspSpi_ArmaEscravo(void);
static void BspSpi_FimTransacao(void);

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Entrega o SPI2 ao modulo SpiBus e leva o barramento ao estado seguro (desligado).
 * @details    Chamado no Bsp_Init, depois do MX_SPI2_Init e do BspUart_Init (usa o DebugLog).
 **********************************************************************************************************************/
bspSpiReturn_t BspSpi_Init(void)
{
	static const spiBusFunc_t func =
	{
		BspSpi_Config,
		BspSpi_Off,
		BspSpi_Xfer,
		BspSpi_Lock,
		BspSpi_Unlock,
	};

	uint32_t clockHz = HAL_RCC_GetPCLK1Freq();   // SPI2 fica no APB1

	if (SpiBus_Init(&func, clockHz) != eSPIBUS_RETURN_OK)
	{
		return eBSP_SPI_RETURN_ERROR;
	}

	if (SpiBus_SafeState() != eSPIBUS_RETURN_OK)
	{
		return eBSP_SPI_RETURN_ERROR;
	}

	DebugLog_SendTerminal("[BSP -> SPI] SPI2 (PB10 SCK, PB15 MOSI, PC2 MISO, PD7 CS) desligado, clock %lu Hz\r\n",
	                      clockHz);

	return eBSP_SPI_RETURN_OK;
}

/***********************************************************************************************************************
 * INTERRUPCOES (CS no PD7)
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      IRQ das linhas EXTI 5 a 9: so o PD7 (CS do escravo) usa.
 **********************************************************************************************************************/
void EXTI9_5_IRQHandler(void)
{
	HAL_GPIO_EXTI_IRQHandler(SPI_CS_Pin);
}

/***********************************************************************************************************************
 * @brief      Borda no CS do DUT: descida seleciona a escrava; subida encerra a transacao.
 * @param[in]  GPIO_Pin  Pino que gerou a interrupcao
 **********************************************************************************************************************/
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	if ((GPIO_Pin != SPI_CS_Pin) || (bspSpi.escravo == 0U))
	{
		return;
	}

	if (HAL_GPIO_ReadPin(SPI_CS_GPIO_Port, SPI_CS_Pin) == GPIO_PIN_RESET)
	{
		CLEAR_BIT(hspi2.Instance->CR1, SPI_CR1_SSI);
	}
	else
	{
		BspSpi_FimTransacao();
	}
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Reinicia o SPI2 no papel e no modo pedidos; como escrava, arma a primeira transacao.
 * @param[in]  papel    dSPIBUS_PAPEL_ESCRAVO ou dSPIBUS_PAPEL_MESTRE (ja validado)
 * @param[in]  modo     0 a 3: bit 1 = CPOL, bit 0 = CPHA (ja validado)
 * @param[in]  divisor  2 a 256, potencia de 2 (so como mestre; ja calculado pelo SpiBus)
 * @return     eSPIBUS_RETURN_OK ou eSPIBUS_RETURN_ERROR (falha do HAL; o SPI2 fica desligado)
 **********************************************************************************************************************/
static spiBusReturn_t BspSpi_Config(uint8_t papel, uint8_t modo, uint16_t divisor)
{
	if (BspSpi_Off() != eSPIBUS_RETURN_OK)
	{
		return eSPIBUS_RETURN_ERROR;
	}

	hspi2.Init.Mode              = (papel == dSPIBUS_PAPEL_MESTRE) ? SPI_MODE_MASTER : SPI_MODE_SLAVE;
	hspi2.Init.Direction         = SPI_DIRECTION_2LINES;
	hspi2.Init.DataSize          = SPI_DATASIZE_8BIT;
	hspi2.Init.CLKPolarity       = ((modo & 0x02U) != 0U) ? SPI_POLARITY_HIGH : SPI_POLARITY_LOW;
	hspi2.Init.CLKPhase          = ((modo & 0x01U) != 0U) ? SPI_PHASE_2EDGE : SPI_PHASE_1EDGE;
	hspi2.Init.NSS               = SPI_NSS_SOFT;
	hspi2.Init.BaudRatePrescaler = BspSpi_Prescaler(divisor);
	hspi2.Init.FirstBit          = SPI_FIRSTBIT_MSB;
	hspi2.Init.TIMode            = SPI_TIMODE_DISABLE;
	hspi2.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
	hspi2.Init.NSSPMode          = SPI_NSS_PULSE_DISABLE;

	if (HAL_SPI_Init(&hspi2) != HAL_OK)
	{
		(void)BspSpi_Off();
		return eSPIBUS_RETURN_ERROR;
	}

	if (papel == dSPIBUS_PAPEL_ESCRAVO)
	{
		BspSpi_CsEntrada();

		if (BspSpi_ArmaEscravo() != eSPIBUS_RETURN_OK)
		{
			(void)BspSpi_Off();
			return eSPIBUS_RETURN_ERROR;
		}

		bspSpi.escravo = 1U;
	}

	return eSPIBUS_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Desliga o SPI2: sem escrava armada, pinos soltos e CS (PD7) como saida em nivel alto.
 **********************************************************************************************************************/
static spiBusReturn_t BspSpi_Off(void)
{
	bspSpi.escravo = 0U;
	HAL_NVIC_DisableIRQ(EXTI9_5_IRQn);
	BspSpi_CsSaida();

	if (HAL_SPI_DeInit(&hspi2) != HAL_OK)
	{
		return eSPIBUS_RETURN_ERROR;
	}

	return eSPIBUS_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Transferencia como mestre, por polling (chamada pela TestExecTask), com o CS baixo durante ela.
 * @return     eSPIBUS_RETURN_OK ou eSPIBUS_RETURN_ERROR (falha ou timeout do HAL)
 **********************************************************************************************************************/
static spiBusReturn_t BspSpi_Xfer(const uint8_t *escrita, uint8_t *leitura, uint8_t quantidade)
{
	HAL_GPIO_WritePin(SPI_CS_GPIO_Port, SPI_CS_Pin, GPIO_PIN_RESET);
	HAL_StatusTypeDef ret = HAL_SPI_TransmitReceive(&hspi2, (uint8_t *)escrita, leitura, quantidade,
	                                                dBSP_SPI_TIMEOUT_MS);
	HAL_GPIO_WritePin(SPI_CS_GPIO_Port, SPI_CS_Pin, GPIO_PIN_SET);

	return (ret == HAL_OK) ? eSPIBUS_RETURN_OK : eSPIBUS_RETURN_ERROR;
}

/***********************************************************************************************************************
 * @brief      Impede os eventos da escrava (IRQ do SPI2 e do CS) enquanto a task mexe nos buffers do SpiBus.
 **********************************************************************************************************************/
static void BspSpi_Lock(void)
{
	bspSpi.irqSpiLigada = (NVIC_GetEnableIRQ(SPI2_IRQn) != 0U) ? 1U : 0U;
	bspSpi.irqCsLigada  = (NVIC_GetEnableIRQ(EXTI9_5_IRQn) != 0U) ? 1U : 0U;
	HAL_NVIC_DisableIRQ(SPI2_IRQn);
	HAL_NVIC_DisableIRQ(EXTI9_5_IRQn);
}

/***********************************************************************************************************************
 * @brief      Libera os eventos da escrava (so religa as IRQs que estavam ligadas antes do lock).
 **********************************************************************************************************************/
static void BspSpi_Unlock(void)
{
	if (bspSpi.irqSpiLigada != 0U)
	{
		HAL_NVIC_EnableIRQ(SPI2_IRQn);
	}

	if (bspSpi.irqCsLigada != 0U)
	{
		HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
	}
}

/***********************************************************************************************************************
 * @brief      Converte o divisor do clock no campo BR do HAL (0 como escrava: o valor nao e usado).
 * @param[in]  divisor  2, 4, ..., 256
 **********************************************************************************************************************/
static uint32_t BspSpi_Prescaler(uint16_t divisor)
{
	switch (divisor)
	{
		case 4U:   return SPI_BAUDRATEPRESCALER_4;
		case 8U:   return SPI_BAUDRATEPRESCALER_8;
		case 16U:  return SPI_BAUDRATEPRESCALER_16;
		case 32U:  return SPI_BAUDRATEPRESCALER_32;
		case 64U:  return SPI_BAUDRATEPRESCALER_64;
		case 128U: return SPI_BAUDRATEPRESCALER_128;
		case 256U: return SPI_BAUDRATEPRESCALER_256;
		default:   return SPI_BAUDRATEPRESCALER_2;
	}
}

/***********************************************************************************************************************
 * @brief      PD7 como saida push-pull em nivel alto (CS inativo): mestre e estado seguro.
 **********************************************************************************************************************/
static void BspSpi_CsSaida(void)
{
	GPIO_InitTypeDef gpio = {0};

	HAL_GPIO_DeInit(SPI_CS_GPIO_Port, SPI_CS_Pin);   // tambem desfaz o EXTI da linha 7
	HAL_GPIO_WritePin(SPI_CS_GPIO_Port, SPI_CS_Pin, GPIO_PIN_SET);

	gpio.Pin   = SPI_CS_Pin;
	gpio.Mode  = GPIO_MODE_OUTPUT_PP;
	gpio.Pull  = GPIO_NOPULL;
	gpio.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(SPI_CS_GPIO_Port, &gpio);
}

/***********************************************************************************************************************
 * @brief      PD7 como entrada do CS do DUT: interrupcao nas duas bordas, pull-up (CS solto = nao selecionado).
 **********************************************************************************************************************/
static void BspSpi_CsEntrada(void)
{
	GPIO_InitTypeDef gpio = {0};

	HAL_GPIO_DeInit(SPI_CS_GPIO_Port, SPI_CS_Pin);

	gpio.Pin  = SPI_CS_Pin;
	gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
	gpio.Pull = GPIO_PULLUP;
	HAL_GPIO_Init(SPI_CS_GPIO_Port, &gpio);

	__HAL_GPIO_EXTI_CLEAR_IT(SPI_CS_Pin);
	HAL_NVIC_SetPriority(EXTI9_5_IRQn, 5, 0);
	HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}

/***********************************************************************************************************************
 * @brief      Arma a escrava para a proxima transacao: buffer de transmissao completo e recepcao de ate 239 bytes.
 * @details    Comeca nao selecionada (SSI = 1) se o CS estiver alto; a descida do CS libera.
 **********************************************************************************************************************/
static spiBusReturn_t BspSpi_ArmaEscravo(void)
{
	SpiBus_PreparaTx(bspSpi.tx);

	if (HAL_GPIO_ReadPin(SPI_CS_GPIO_Port, SPI_CS_Pin) == GPIO_PIN_SET)
	{
		SET_BIT(hspi2.Instance->CR1, SPI_CR1_SSI);
	}

	if (HAL_SPI_TransmitReceive_IT(&hspi2, bspSpi.tx, bspSpi.rx, dSPIBUS_TAMANHO_BUFFER) != HAL_OK)
	{
		return eSPIBUS_RETURN_ERROR;
	}

	return eSPIBUS_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Subida do CS: entrega ao SpiBus o que o DUT enviou e rearma a escrava do zero.
 * @details    Roda na ISR do EXTI, na mesma prioridade da IRQ do SPI2: um byte que chegou junto com o CS ainda pode
 *             estar no FIFO, entao a recepcao pendente e tratada antes de contar.
 **********************************************************************************************************************/
static void BspSpi_FimTransacao(void)
{
	SET_BIT(hspi2.Instance->CR1, SPI_CR1_SSI);

	while ((__HAL_SPI_GET_FLAG(&hspi2, SPI_FLAG_RXNE) != RESET) && (hspi2.RxXferCount > 0U))
	{
		HAL_SPI_IRQHandler(&hspi2);
	}

	SpiBus_FimTransacao(bspSpi.rx, (uint16_t)(dSPIBUS_TAMANHO_BUFFER - hspi2.RxXferCount));

	__HAL_RCC_SPI2_FORCE_RESET();
	__HAL_RCC_SPI2_RELEASE_RESET();
	HAL_NVIC_ClearPendingIRQ(SPI2_IRQn);

	if ((HAL_SPI_Init(&hspi2) != HAL_OK) || (BspSpi_ArmaEscravo() != eSPIBUS_RETURN_OK))
	{
		bspSpi.escravo = 0U;   // fica sem escrava ate o proximo SPI_CONFIG ou RESET_ALL
	}
}

/** @} DOXYGEN GROUP TAG END OF FILE */
