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
 *   - Define o EXTI9_5_IRQHandler e o HAL_GPIO_EXTI_Callback: as bordas de outros pinos (entradas do ESPERA_GPIO)
 *     sao repassadas ao BspTempo_Borda.
 *   - Desligado (boot e RESET_ALL) = HAL_SPI_DeInit: pinos soltos e PD7 como saida em nivel alto (CS inativo).
 *
 *   Changelog
 *   @version      <b>1.0.0 - 28/10/2026</b> \n Luiz Neto \n Primeira versao
 *   @version      <b>1.1.0 - 03/11/2026</b> \n Luiz Neto \n HAL_GPIO_EXTI_Callback repassa as bordas das entradas
 *                 ao BspTempo_Borda (ESPERA_GPIO das rotinas)
 *   @version      <b>1.2.0 - 04/11/2026</b> \n Luiz Neto \n Modo registradores da escrava (sensor SPI emulado): a ISR
 *                                                          trata byte a byte e enche a FIFO com os registradores
 *   @version      <b>1.1.1 - 03/11/2026</b> \n Luiz Neto \n BspSpi_Off reseta o SPI2 pelo RCC: a FIFO de recepcao
 *                 (4 bytes) guardava bytes da escrava e a 1a transferencia como mestre saia atrasada 4 bytes (CT13)
 *   @version      <b>1.2.1 - 04/11/2026</b> \n Luiz Neto \n BspSpi_Recarga: o SPI_SLAVE_LOAD com a escrava armada e o
 *                 CS alto rearma na hora (a 1a transacao do DUT saia com o buffer antigo, CT20); o rearme nao usa
 *                 mais o HAL_SPI_Abort, que travava ~200 ms esperando a ISR desligada; na subida do CS a FIFO de
 *                 recepcao e esvaziada pelo nivel (o ultimo byte de uma transacao impar se perdia)
 *
 *   @copyright
 *   @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include "Bsp_Spi.h"
#include "SpiBus.h"
#include "I2cBus.h"
#include "Bsp_Tempo.h"
#include "DebugLog.h"
#include "spi.h"
#include "main.h"

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

// Tempo maximo de uma transferencia como mestre: 239 bytes a ~211 kHz levam ~9 ms
#define dBSP_SPI_TIMEOUT_MS (50U)

// Modo registradores: o que o proximo byte recebido e
#define dBSP_SPI_REG_ENDERECO (0U)
#define dBSP_SPI_REG_LEITURA  (1U)
#define dBSP_SPI_REG_ESCRITA  (2U)

// Bit 7 do 1o byte: leitura (MPU-6500 e BME280)
#define dBSP_SPI_REG_LER (0x80U)

// Acesso de 8 bits ao DR (o de 16 bits empacotaria dois bytes na FIFO)
#define dBSP_SPI_DR8 (*(__IO uint8_t *)&hspi2.Instance->DR)

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
	volatile uint8_t registros;                ///< 1: modo registradores (sensor emulado, mapa da escrava I2C)
	uint8_t ou;                                ///< Completa o endereco de 7 bits (0x80 no BME280)
	uint8_t estado;                            ///< dBSP_SPI_REG_ENDERECO, _LEITURA ou _ESCRITA
	uint8_t registro;                          ///< Proximo registrador lido (posto na FIFO) ou escrito
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
static spiBusReturn_t BspSpi_Registros(uint8_t ativo, uint8_t ou);
static void BspSpi_EncheFifo(void);
static void BspSpi_Recarga(void);
static spiBusReturn_t BspSpi_Rearma(void);

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

	if ((SpiBus_InitRegistros(BspSpi_Registros) != eSPIBUS_RETURN_OK) ||
	    (SpiBus_InitRecarga(BspSpi_Recarga) != eSPIBUS_RETURN_OK) || (SpiBus_SafeState() != eSPIBUS_RETURN_OK))
	{
		return eBSP_SPI_RETURN_ERROR;
	}

	DebugLog_SendTerminal("[BSP -> SPI] SPI2 (PB10 SCK, PB15 MOSI, PC2 MISO, PD7 CS) desligado, clock %lu Hz\r\n",
	                      clockHz);

	return eBSP_SPI_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      IRQ do SPI2 no modo registradores (chamada no inicio do SPI2_IRQHandler): 1 se tratou.
 * @details    Byte a byte: o 1o e o endereco (bit 7 = leitura). Na leitura, a FIFO de transmissao (4 bytes) e cheia
 *             com os registradores seguintes, e reposta a cada byte: so entre o endereco e o 1o dado o mestre precisa
 *             de uma pausa (o dado so existe depois que o endereco chega). Na escrita, cada byte vai para o mapa
 *             (bits gravaveis) e a FIFO recebe 0xFF para nao ficar vazia.
 **********************************************************************************************************************/
uint8_t BspSpi_Irq(void)
{
	if ((bspSpi.registros == 0U) || (bspSpi.escravo == 0U))
	{
		return 0U;
	}

	while (__HAL_SPI_GET_FLAG(&hspi2, SPI_FLAG_RXNE) != RESET)
	{
		uint8_t byte = dBSP_SPI_DR8;

		switch (bspSpi.estado)
		{
			case dBSP_SPI_REG_ENDERECO:
				bspSpi.registro = (uint8_t)((byte & (uint8_t)~dBSP_SPI_REG_LER) | bspSpi.ou);
				bspSpi.estado   = ((byte & dBSP_SPI_REG_LER) != 0U) ? dBSP_SPI_REG_LEITURA : dBSP_SPI_REG_ESCRITA;
				if (bspSpi.estado == dBSP_SPI_REG_ESCRITA)
				{
					dBSP_SPI_DR8 = dSPIBUS_BYTE_VAZIO;
				}
			break;

			case dBSP_SPI_REG_ESCRITA:
				I2cBus_EscreveRegistro(bspSpi.registro, byte);
				bspSpi.registro++;
				dBSP_SPI_DR8 = dSPIBUS_BYTE_VAZIO;
			break;

			default:
			break;
		}

		if (bspSpi.estado == dBSP_SPI_REG_LEITURA)
		{
			BspSpi_EncheFifo();
		}
	}

	return 1U;
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
 * @details    Borda de outro pino e de uma entrada monitorada pelo ESPERA_GPIO: vai para o BspTempo_Borda.
 * @param[in]  GPIO_Pin  Pino que gerou a interrupcao
 **********************************************************************************************************************/
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	if (GPIO_Pin != SPI_CS_Pin)
	{
		BspTempo_Borda(GPIO_Pin);
		return;
	}

	if (bspSpi.escravo == 0U)
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
 * @details    O HAL_SPI_DeInit nao esvazia a FIFO de recepcao (4 bytes em 8 bits): bytes que a escrava recebeu e
 *             ninguem leu ficariam na frente da proxima transferencia. O reset pelo RCC zera FIFO e registradores.
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

	__HAL_RCC_SPI2_FORCE_RESET();
	__HAL_RCC_SPI2_RELEASE_RESET();

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
	if (bspSpi.registros != 0U)
	{
		// Modo registradores: so 1 byte (0xFF) na FIFO para o endereco; a ISR trata os seguintes
		bspSpi.estado = dBSP_SPI_REG_ENDERECO;
		if (HAL_GPIO_ReadPin(SPI_CS_GPIO_Port, SPI_CS_Pin) == GPIO_PIN_SET)
		{
			SET_BIT(hspi2.Instance->CR1, SPI_CR1_SSI);
		}
		__HAL_SPI_ENABLE(&hspi2);
		dBSP_SPI_DR8 = dSPIBUS_BYTE_VAZIO;
		__HAL_SPI_ENABLE_IT(&hspi2, SPI_IT_RXNE);
		HAL_NVIC_EnableIRQ(SPI2_IRQn);
		return eSPIBUS_RETURN_OK;
	}

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

	// Pelo nivel da FIFO, nao pelo RXNE: com mais de 1 byte pendente o HAL usa o limiar de 16 bits, e um byte
	// sozinho (transacao de tamanho impar) nao levanta o RXNE
	while (((hspi2.Instance->SR & SPI_SR_FRLVL) != 0U) && (hspi2.RxXferCount > 0U))
	{
		*hspi2.pRxBuffPtr = dBSP_SPI_DR8;
		hspi2.pRxBuffPtr++;
		hspi2.RxXferCount--;
	}

	if (bspSpi.registros != 0U)
	{
		(void)BspSpi_Irq();   // byte que chegou junto com a subida do CS
	}
	else
	{
		SpiBus_FimTransacao(bspSpi.rx, (uint16_t)(dSPIBUS_TAMANHO_BUFFER - hspi2.RxXferCount));
	}

	__HAL_RCC_SPI2_FORCE_RESET();
	__HAL_RCC_SPI2_RELEASE_RESET();
	HAL_NVIC_ClearPendingIRQ(SPI2_IRQn);

	if ((HAL_SPI_Init(&hspi2) != HAL_OK) || (BspSpi_ArmaEscravo() != eSPIBUS_RETURN_OK))
	{
		bspSpi.escravo = 0U;   // fica sem escrava ate o proximo SPI_CONFIG ou RESET_ALL
	}
}

/***********************************************************************************************************************
 * @brief      Liga/desliga o modo registradores; com a escrava armada, rearma do zero no modo novo.
 **********************************************************************************************************************/
static spiBusReturn_t BspSpi_Registros(uint8_t ativo, uint8_t ou)
{
	BspSpi_Lock();
	bspSpi.registros = ativo;
	bspSpi.ou        = ou;

	spiBusReturn_t ret = (bspSpi.escravo != 0U) ? BspSpi_Rearma() : eSPIBUS_RETURN_OK;

	BspSpi_Unlock();

	return ret;
}

/***********************************************************************************************************************
 * @brief      Depois de um SPI_SLAVE_LOAD: rearma a escrava para a 1a transacao ja sair com o buffer novo.
 * @details    So com a escrava armada, no modo buffers e o CS alto (nenhuma transacao em curso); com o CS baixo, o
 *             buffer novo vale a partir da transacao seguinte, como antes.
 **********************************************************************************************************************/
static void BspSpi_Recarga(void)
{
	BspSpi_Lock();

	if ((bspSpi.escravo != 0U) && (bspSpi.registros == 0U) &&
	    (HAL_GPIO_ReadPin(SPI_CS_GPIO_Port, SPI_CS_Pin) == GPIO_PIN_SET))
	{
		(void)BspSpi_Rearma();
	}

	BspSpi_Unlock();
}

/***********************************************************************************************************************
 * @brief      Rearma a escrava do zero (chamada com as IRQs travadas): descarta a transacao armada, limpa as FIFOs pelo
 *             RCC e arma de novo com o modo e o buffer atuais.
 * @details    Sem HAL_SPI_Abort: no modo IT ele espera a ISR, travada aqui, e so saia pelo timeout (~100 ms por
 *             sentido). O reset pelo RCC ja desliga o periferico e o HAL_SPI_Init devolve o handle a READY.
 **********************************************************************************************************************/
static spiBusReturn_t BspSpi_Rearma(void)
{
	SET_BIT(hspi2.Instance->CR1, SPI_CR1_SSI);
	__HAL_RCC_SPI2_FORCE_RESET();
	__HAL_RCC_SPI2_RELEASE_RESET();
	HAL_NVIC_ClearPendingIRQ(SPI2_IRQn);

	if ((HAL_SPI_Init(&hspi2) != HAL_OK) || (BspSpi_ArmaEscravo() != eSPIBUS_RETURN_OK))
	{
		bspSpi.escravo = 0U;   // fica sem escrava ate o proximo SPI_CONFIG ou RESET_ALL
		return eSPIBUS_RETURN_ERROR;
	}

	return eSPIBUS_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Poe na FIFO de transmissao os proximos registradores, ate ela encher (4 bytes em 8 bits).
 **********************************************************************************************************************/
static void BspSpi_EncheFifo(void)
{
	while ((hspi2.Instance->SR & SPI_SR_FTLVL) != SPI_SR_FTLVL)
	{
		dBSP_SPI_DR8 = I2cBus_LeRegistro(bspSpi.registro);
		bspSpi.registro++;
	}
}

/** @} DOXYGEN GROUP TAG END OF FILE */
