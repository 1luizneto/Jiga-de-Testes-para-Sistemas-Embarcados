/***********************************************************************************************************************
 *   @file         Bsp_I2c.c
 *   @addtogroup   App
 *   @brief        Abstracao do I2C2 (HAL) para o modulo I2cBus: escravo por interrupcao e mestre por polling
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Ferramentas:</b>
 *   - Generic.
 *
 *   \n <b>Dependencias:</b>
 *   - hi2c2 configurado pelo CubeMX (MX_I2C2_Init): PF0 = SDA, PF1 = SCL, open-drain sem pull (pull-ups externos),
 *     clock stretching ligado, IRQs de evento e de erro na prioridade 5.
 *
 *   \n <b>Observacoes:</b>
 *   - Escravo byte a byte (HAL_I2C_Slave_Seq_*_IT com 1 byte): cada byte passa pelo I2cBus, que cuida do mapa e
 *     do ponteiro. Os callbacks do HAL rodam na ISR do I2C2: nada de DebugLog nem de FreeRTOS aqui.
 *   - Fim de uma leitura do DUT: o HAL descarta o byte ja recarregado no TXDR e chama o SlaveTxCpltCallback com o
 *     TXIS apagado (RM0410, slave transmitter: o ultimo dado escrito no TXDR nao e enviado).
 *   - Desligado (boot e RESET_ALL) = HAL_I2C_DeInit: periferico parado e PF0/PF1 soltos, barramento livre.
 *
 *   Changelog
 *   @version      <b>1.0.0 - 27/10/2026</b> \n Luiz Neto \n Primeira versao
 *
 *   @copyright
 *   @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include "Bsp_I2c.h"
#include "I2cBus.h"
#include "DebugLog.h"
#include "i2c.h"

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

// Tempo maximo de uma transferencia como mestre: 240 bytes a 100 kHz levam ~22 ms
#define dBSP_I2C_TIMEOUT_MS (50U)

/***********************************************************************************************************************
 * VARIAVEIS LOCAIS
 **********************************************************************************************************************/

/// Registrador TIMING do I2C2 (clock de 54 MHz) por velocidade, calculado pelo CubeMX: 0 = 100 kHz, 1 = 400 kHz
static const uint32_t bspI2cTiming[dI2CBUS_VELOCIDADE_MAX + 1U] =
{
	0x20404768U,
	0x6000030DU,
};

static struct bspI2c
{
	volatile uint8_t escravo;   ///< 1: escuta ligada (callbacks do HAL repassam ao I2cBus)
	volatile uint8_t lendo;     ///< 1: o DUT esta lendo a jiga (falta devolver o byte buscado a mais)
	uint8_t rxByte;             ///< Byte recebido do DUT
	uint8_t txByte;             ///< Byte enviado ao DUT
	uint8_t irqLigada;          ///< Estado da IRQ de evento antes do lock
} bspI2c;

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static i2cBusReturn_t BspI2c_Config(uint8_t papel, uint8_t endereco, uint8_t velocidade);
static i2cBusReturn_t BspI2c_Off(void);
static i2cBusReturn_t BspI2c_Xfer(uint8_t endereco, const uint8_t *escrita, uint8_t nw, uint8_t *leitura, uint8_t nr);
static void BspI2c_Lock(void);
static void BspI2c_Unlock(void);
static void BspI2c_FimLeitura(void);

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Entrega o I2C2 ao modulo I2cBus e leva o barramento ao estado seguro (desligado).
 * @details    Chamado no Bsp_Init, depois do MX_I2C2_Init e do BspUart_Init (usa o DebugLog).
 **********************************************************************************************************************/
bspI2cReturn_t BspI2c_Init(void)
{
	static const i2cBusFunc_t func =
	{
		BspI2c_Config,
		BspI2c_Off,
		BspI2c_Xfer,
		BspI2c_Lock,
		BspI2c_Unlock,
	};

	if (I2cBus_Init(&func) != eI2CBUS_RETURN_OK)
	{
		return eBSP_I2C_RETURN_ERROR;
	}

	if (I2cBus_SafeState() != eI2CBUS_RETURN_OK)
	{
		return eBSP_I2C_RETURN_ERROR;
	}

	DebugLog_SendTerminal("[BSP -> I2C] I2C2 (PF0 SDA, PF1 SCL) desligado ate o I2C_CONFIG\r\n");

	return eBSP_I2C_RETURN_OK;
}

/***********************************************************************************************************************
 * CALLBACKS DO HAL (ISR do I2C2)
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      O DUT enderecou a jiga: prepara o 1o byte da leitura ou a recepcao do 1o byte da escrita.
 * @param[in]  hi2c               Handle do I2C
 * @param[in]  TransferDirection  I2C_DIRECTION_TRANSMIT = o DUT escreve; I2C_DIRECTION_RECEIVE = o DUT le
 * @param[in]  AddrMatchCode      Endereco reconhecido (nao usado: so ha um)
 **********************************************************************************************************************/
void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, uint8_t TransferDirection, uint16_t AddrMatchCode)
{
	(void)AddrMatchCode;

	if ((hi2c->Instance != I2C2) || (bspI2c.escravo == 0U))
	{
		return;
	}

	if (TransferDirection == I2C_DIRECTION_TRANSMIT)
	{
		BspI2c_FimLeitura();   // repeated start depois de uma leitura
		I2cBus_InicioEscrita();
		(void)HAL_I2C_Slave_Seq_Receive_IT(hi2c, &bspI2c.rxByte, 1U, I2C_NEXT_FRAME);
	}
	else
	{
		BspI2c_FimLeitura();   // repeated start entre duas leituras
		bspI2c.lendo  = 1U;
		bspI2c.txByte = I2cBus_ProximoByte();
		(void)HAL_I2C_Slave_Seq_Transmit_IT(hi2c, &bspI2c.txByte, 1U, I2C_NEXT_FRAME);
	}
}

/***********************************************************************************************************************
 * @brief      Byte escrito pelo DUT: vai para o I2cBus e a recepcao do proximo e armada.
 **********************************************************************************************************************/
void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
	if ((hi2c->Instance != I2C2) || (bspI2c.escravo == 0U))
	{
		return;
	}

	I2cBus_ByteRecebido(bspI2c.rxByte);
	(void)HAL_I2C_Slave_Seq_Receive_IT(hi2c, &bspI2c.rxByte, 1U, I2C_NEXT_FRAME);
}

/***********************************************************************************************************************
 * @brief      Byte entregue ao TXDR. Com o TXIS ligado o DUT quer mais um; com ele apagado, o DUT respondeu NACK
 *             e o HAL descartou o byte buscado a mais (fim da leitura).
 **********************************************************************************************************************/
void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
	if ((hi2c->Instance != I2C2) || (bspI2c.escravo == 0U))
	{
		return;
	}

	if (__HAL_I2C_GET_FLAG(hi2c, I2C_FLAG_TXIS) != RESET)
	{
		bspI2c.txByte = I2cBus_ProximoByte();
		(void)HAL_I2C_Slave_Seq_Transmit_IT(hi2c, &bspI2c.txByte, 1U, I2C_NEXT_FRAME);
	}
	else
	{
		BspI2c_FimLeitura();
	}
}

/***********************************************************************************************************************
 * @brief      Fim da transacao (STOP): volta a escutar o endereco.
 **********************************************************************************************************************/
void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c)
{
	if ((hi2c->Instance != I2C2) || (bspI2c.escravo == 0U))
	{
		return;
	}

	BspI2c_FimLeitura();
	(void)HAL_I2C_EnableListen_IT(hi2c);
}

/***********************************************************************************************************************
 * @brief      Erro no barramento. O NACK no fim de uma escrita do DUT (recepcao armada sem byte) tambem chega aqui.
 * @details    Se o HAL saiu do modo escuta (erro de barramento), religa; senao o ListenCplt seguinte religa.
 **********************************************************************************************************************/
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
	if ((hi2c->Instance != I2C2) || (bspI2c.escravo == 0U))
	{
		return;
	}

	BspI2c_FimLeitura();

	if (HAL_I2C_GetState(hi2c) == HAL_I2C_STATE_READY)
	{
		(void)HAL_I2C_EnableListen_IT(hi2c);
	}
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Reinicia o I2C2 no papel pedido; como escravo, com o endereco da jiga e a escuta ligada.
 * @param[in]  papel       dI2CBUS_PAPEL_ESCRAVO ou dI2CBUS_PAPEL_MESTRE (ja validado)
 * @param[in]  endereco    Endereco de 7 bits como escrava (ja validado)
 * @param[in]  velocidade  Indice de bspI2cTiming (ja validado)
 * @return     eI2CBUS_RETURN_OK ou eI2CBUS_RETURN_ERROR (falha do HAL; o I2C2 fica desligado)
 **********************************************************************************************************************/
static i2cBusReturn_t BspI2c_Config(uint8_t papel, uint8_t endereco, uint8_t velocidade)
{
	if (BspI2c_Off() != eI2CBUS_RETURN_OK)
	{
		return eI2CBUS_RETURN_ERROR;
	}

	hi2c2.Init.Timing      = bspI2cTiming[velocidade];
	hi2c2.Init.OwnAddress1 = (papel == dI2CBUS_PAPEL_ESCRAVO) ? ((uint32_t)endereco << 1) : 0U;

	if ((HAL_I2C_Init(&hi2c2) != HAL_OK) ||
	    (HAL_I2CEx_ConfigAnalogFilter(&hi2c2, I2C_ANALOGFILTER_ENABLE) != HAL_OK) ||
	    (HAL_I2CEx_ConfigDigitalFilter(&hi2c2, 0U) != HAL_OK))
	{
		(void)BspI2c_Off();
		return eI2CBUS_RETURN_ERROR;
	}

	if (papel == dI2CBUS_PAPEL_ESCRAVO)
	{
		bspI2c.lendo   = 0U;
		bspI2c.escravo = 1U;

		if (HAL_I2C_EnableListen_IT(&hi2c2) != HAL_OK)
		{
			(void)BspI2c_Off();
			return eI2CBUS_RETURN_ERROR;
		}
	}

	return eI2CBUS_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Desliga o I2C2: sem escuta, periferico parado e pinos soltos (barramento livre para o DUT).
 **********************************************************************************************************************/
static i2cBusReturn_t BspI2c_Off(void)
{
	bspI2c.escravo = 0U;
	bspI2c.lendo   = 0U;

	if (HAL_I2C_DeInit(&hi2c2) != HAL_OK)
	{
		return eI2CBUS_RETURN_ERROR;
	}

	return eI2CBUS_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Transferencia como mestre, por polling (chamada pela TestExecTask).
 * @details    Escrita de 1 ou 2 bytes seguida de leitura = acesso a registrador (HAL_I2C_Mem_Read, com repeated
 *             start). Escrita maior seguida de leitura vira duas transacoes, com STOP entre elas.
 * @return     eI2CBUS_RETURN_OK, eI2CBUS_RETURN_NACK (o dispositivo nao respondeu) ou eI2CBUS_RETURN_ERROR
 **********************************************************************************************************************/
static i2cBusReturn_t BspI2c_Xfer(uint8_t endereco, const uint8_t *escrita, uint8_t nw, uint8_t *leitura, uint8_t nr)
{
	uint16_t endereco8 = (uint16_t)((uint16_t)endereco << 1);
	HAL_StatusTypeDef ret = HAL_OK;

	if ((nw > 0U) && (nw <= 2U) && (nr > 0U))
	{
		uint16_t registrador = (nw == 1U) ? escrita[0] : (uint16_t)(((uint16_t)escrita[0] << 8) | escrita[1]);
		uint16_t tamanho     = (nw == 1U) ? I2C_MEMADD_SIZE_8BIT : I2C_MEMADD_SIZE_16BIT;

		ret = HAL_I2C_Mem_Read(&hi2c2, endereco8, registrador, tamanho, leitura, nr, dBSP_I2C_TIMEOUT_MS);
	}
	else
	{
		if (nw > 0U)
		{
			ret = HAL_I2C_Master_Transmit(&hi2c2, endereco8, (uint8_t *)escrita, nw, dBSP_I2C_TIMEOUT_MS);
		}

		if ((ret == HAL_OK) && (nr > 0U))
		{
			ret = HAL_I2C_Master_Receive(&hi2c2, endereco8, leitura, nr, dBSP_I2C_TIMEOUT_MS);
		}
	}

	if (ret == HAL_OK)
	{
		return eI2CBUS_RETURN_OK;
	}

	return ((HAL_I2C_GetError(&hi2c2) & HAL_I2C_ERROR_AF) != 0U) ? eI2CBUS_RETURN_NACK : eI2CBUS_RETURN_ERROR;
}

/***********************************************************************************************************************
 * @brief      Impede os eventos do barramento enquanto a task copia o mapa (o DUT espera com o SCL segurado).
 **********************************************************************************************************************/
static void BspI2c_Lock(void)
{
	bspI2c.irqLigada = (NVIC_GetEnableIRQ(I2C2_EV_IRQn) != 0U) ? 1U : 0U;
	HAL_NVIC_DisableIRQ(I2C2_EV_IRQn);
}

/***********************************************************************************************************************
 * @brief      Libera os eventos do barramento (so religa a IRQ se ela estava ligada antes do lock).
 **********************************************************************************************************************/
static void BspI2c_Unlock(void)
{
	if (bspI2c.irqLigada != 0U)
	{
		HAL_NVIC_EnableIRQ(I2C2_EV_IRQn);
	}
}

/***********************************************************************************************************************
 * @brief      Se o DUT estava lendo, devolve ao I2cBus o byte buscado a mais (uma vez por leitura).
 **********************************************************************************************************************/
static void BspI2c_FimLeitura(void)
{
	if (bspI2c.lendo != 0U)
	{
		bspI2c.lendo = 0U;
		I2cBus_FimLeitura();
	}
}

/** @} DOXYGEN GROUP TAG END OF FILE */
