/***********************************************************************************************************************
 *   @file         barramentos.c
 *   @brief        I2C e SPI do ESP32-S3 (mestre e escravo) para o interpretador de comandos do DUT de referencia
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Dependencias:</b>
 *   - ESP-IDF v6.1: esp_driver_i2c (i2c_master/i2c_slave), esp_driver_spi (spi_master/spi_slave), esp_driver_gpio.
 *
 *   \n <b>Observacoes:</b>
 *   - Pinos soltos (entrada, sem pull) fora de uso: o mestre I2C e o SPI so ocupam o barramento durante o
 *     comando; os escravos, do I2CS/SPIS ate o OFF. Nunca ha dois mestres: a jiga tem que estar escrava antes do
 *     I2CM/SPIM, e vice-versa (o PC garante a ordem; ver README).
 *   - CS do mestre SPI por GPIO: em nivel baixo so durante a transferencia. Fora dela o pino volta a ser entrada,
 *     porque no estado seguro a jiga deixa o PD7 como saida em nivel alto.
 *   - Escravo I2C: o callback de escrita (ISR) entrega o ponteiro e os dados; o de pedido de leitura (no casamento
 *     do endereco, com o SCL segurado) avisa a task, que poe na FIFO o mapa a partir do ponteiro (ver mapa_i2c.c).
 *   - Escravo SPI: cada transacao do mestre comeca do byte 0 da carga, completada com FF (o mesmo da jiga); o que
 *     o mestre envia acumula ate o SPIS_READ.
 *
 *   Changelog
 *   @version      <b>1.0.0 - 03/11/2026</b> \n Luiz Neto \n Primeira versao
 *   @version      <b>1.1.0 - 04/11/2026</b> \n Luiz Neto \n spiRegistro: endereco e dados em duas transferencias com o
 *                                                          CS baixo (sensores por SPI)
 *
 *   @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include "barramentos.h"
#include "mapa_i2c.h"
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2c_slave.h"
#include "driver/spi_master.h"
#include "driver/spi_slave.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

#define dBAR_I2C_PORTA           (I2C_NUM_0)
#define dBAR_I2C_TIMEOUT_MS      (50)
#define dBAR_I2C_BLOCO_LEITURA   (128U)   ///< Bytes postos na FIFO a cada leitura do mestre
#define dBAR_SPI_HOST            (SPI2_HOST)
#define dBAR_SPI_BUFFER          (240U)   ///< Multiplo de 4 (DMA); a transacao usa ate 239
#define dBAR_SPI_ESPERA_MS       (100)    ///< Periodo em que a task do escravo confere o pedido de parada
#define dBAR_CS_FOLGA_US         (20U)    ///< Entre o CS e o clock: a jiga escrava reage ao CS na ISR do EXTI
#define dBAR_REGISTRO_PAUSA_US   (20U)    ///< Entre o endereco e os dados (registradores): a jiga busca o dado

static const char *TAG = "BAR";

/***********************************************************************************************************************
 * TIPOS LOCAIS
 **********************************************************************************************************************/

/// @brief Evento do escravo I2C, da ISR para a task
typedef struct barEventoI2c
{
	bool     leitura;                 ///< true: o mestre pediu leitura; false: escrita recebida
	uint16_t quantidade;              ///< Bytes recebidos (escrita)
	uint8_t  dados[dMAPAI2C_TAMANHO];
} barEventoI2c_t;

/***********************************************************************************************************************
 * VARIAVEIS LOCAIS
 **********************************************************************************************************************/

static struct barramentos
{
	SemaphoreHandle_t      mutex;          ///< Protege o mapa I2C e os buffers do escravo SPI
	QueueHandle_t          i2cEventos;
	i2c_slave_dev_handle_t i2cEscrava;     ///< NULL: escravo I2C desligado
	TaskHandle_t           spiTask;        ///< NULL: escravo SPI desligado
	volatile bool          spiParar;
	SemaphoreHandle_t      spiParou;
	uint8_t                spiModo;
	uint8_t                spiCarga[dCOMANDOS_DADOS_MAX];
	size_t                 spiCargaQtd;
	uint8_t                spiRecebido[dCOMANDOS_DADOS_MAX];
	size_t                 spiRecebidoQtd;
} bar;

DMA_ATTR WORD_ALIGNED_ATTR static uint8_t barSpiTx[dBAR_SPI_BUFFER];
DMA_ATTR WORD_ALIGNED_ATTR static uint8_t barSpiRx[dBAR_SPI_BUFFER];
DMA_ATTR WORD_ALIGNED_ATTR static uint8_t barSpiCabecalho[4];

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static dutReturn_t Barramentos_Desliga(void);
static dutReturn_t Barramentos_I2cMestre(uint8_t endereco, const uint8_t *escrita, size_t nw, uint8_t *leitura,
                                         size_t nr);
static dutReturn_t Barramentos_I2cEscrava(uint8_t endereco);
static dutReturn_t Barramentos_I2cCarrega(uint8_t offset, const uint8_t *dados, size_t quantidade);
static dutReturn_t Barramentos_I2cLe(uint8_t offset, uint8_t *dados, size_t quantidade);
static dutReturn_t Barramentos_SpiMestre(uint8_t modo, uint32_t frequencia, const uint8_t *escrita, uint8_t *leitura,
                                         size_t quantidade);
static dutReturn_t Barramentos_SpiEscrava(uint8_t modo);
static dutReturn_t Barramentos_SpiRegistro(uint8_t modo, uint32_t frequencia, uint8_t endereco, const uint8_t *escrita,
                                           uint8_t *leitura, size_t quantidade);
static dutReturn_t Barramentos_SpiTransfere(uint8_t modo, uint32_t frequencia, const uint8_t *cabecalho,
                                            const uint8_t *escrita, uint8_t *leitura, size_t quantidade);
static dutReturn_t Barramentos_SpiCarrega(const uint8_t *dados, size_t quantidade);
static dutReturn_t Barramentos_SpiLe(uint8_t *dados, size_t *quantidade);
static void Barramentos_SoltaPinos(uint64_t mascara);
static void Barramentos_DesligaI2cEscrava(void);
static void Barramentos_DesligaSpiEscrava(void);
static bool Barramentos_I2cRecebeuIsr(i2c_slave_dev_handle_t escrava, const i2c_slave_rx_done_event_data_t *evento,
                                      void *contexto);
static bool Barramentos_I2cPedidoIsr(i2c_slave_dev_handle_t escrava, const i2c_slave_request_event_data_t *evento,
                                     void *contexto);
static void Barramentos_I2cTask(void *parametro);
static void Barramentos_SpiTask(void *parametro);

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Cria o mutex, a fila e a task do escravo I2C, solta os pinos e devolve as operacoes para os comandos.
 **********************************************************************************************************************/
const dutBarramentos_t *Barramentos_Init(void)
{
	static const dutBarramentos_t funcoes =
	{
		Barramentos_Desliga,
		Barramentos_I2cMestre,
		Barramentos_I2cEscrava,
		Barramentos_I2cCarrega,
		Barramentos_I2cLe,
		Barramentos_SpiMestre,
		Barramentos_SpiEscrava,
		Barramentos_SpiCarrega,
		Barramentos_SpiLe,
		Barramentos_SpiRegistro,
	};

	memset(&bar, 0, sizeof(bar));
	MapaI2c_Init();
	bar.mutex      = xSemaphoreCreateMutex();
	bar.spiParou   = xSemaphoreCreateBinary();
	bar.i2cEventos = xQueueCreate(4, sizeof(barEventoI2c_t));
	xTaskCreate(Barramentos_I2cTask, "dut_i2c", 4096, NULL, 6, NULL);

	Barramentos_SoltaPinos((1ULL << dDUT_PINO_I2C_SDA) | (1ULL << dDUT_PINO_I2C_SCL) | (1ULL << dDUT_PINO_SPI_CS) |
	                       (1ULL << dDUT_PINO_SPI_MOSI) | (1ULL << dDUT_PINO_SPI_SCK) | (1ULL << dDUT_PINO_SPI_MISO));

	return &funcoes;
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      OFF: desliga os escravos e solta todos os pinos de barramento.
 **********************************************************************************************************************/
static dutReturn_t Barramentos_Desliga(void)
{
	Barramentos_DesligaI2cEscrava();
	Barramentos_DesligaSpiEscrava();

	return eDUT_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Transacao como mestre I2C a 100 kHz: escrita, leitura ou escrita + leitura com repeated start.
 * @return     OK, NACK (dispositivo nao respondeu), BUSY (escravo I2C ligado) ou ERROR
 **********************************************************************************************************************/
static dutReturn_t Barramentos_I2cMestre(uint8_t endereco, const uint8_t *escrita, size_t nw, uint8_t *leitura,
                                         size_t nr)
{
	i2c_master_bus_handle_t barramento = NULL;
	i2c_master_dev_handle_t dispositivo = NULL;
	esp_err_t erro;

	if (bar.i2cEscrava != NULL)
	{
		return eDUT_RETURN_BUSY;
	}

	const i2c_master_bus_config_t configBarramento =
	{
		.i2c_port = dBAR_I2C_PORTA,
		.sda_io_num = dDUT_PINO_I2C_SDA,
		.scl_io_num = dDUT_PINO_I2C_SCL,
		.clk_source = I2C_CLK_SRC_DEFAULT,
		.glitch_ignore_cnt = 7,
		.flags.enable_internal_pullup = 1,
	};
	const i2c_device_config_t configDispositivo =
	{
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = endereco,
		.scl_speed_hz = dDUT_I2C_FREQUENCIA,
	};

	erro = i2c_new_master_bus(&configBarramento, &barramento);
	if (erro == ESP_OK)
	{
		erro = i2c_master_bus_add_device(barramento, &configDispositivo, &dispositivo);
	}

	if (erro == ESP_OK)
	{
		if ((nw > 0U) && (nr > 0U))
		{
			erro = i2c_master_transmit_receive(dispositivo, escrita, nw, leitura, nr, dBAR_I2C_TIMEOUT_MS);
		}
		else if (nw > 0U)
		{
			erro = i2c_master_transmit(dispositivo, escrita, nw, dBAR_I2C_TIMEOUT_MS);
		}
		else
		{
			erro = i2c_master_receive(dispositivo, leitura, nr, dBAR_I2C_TIMEOUT_MS);
		}
	}

	if (dispositivo != NULL)
	{
		(void)i2c_master_bus_rm_device(dispositivo);
	}
	if (barramento != NULL)
	{
		(void)i2c_del_master_bus(barramento);
	}
	Barramentos_SoltaPinos((1ULL << dDUT_PINO_I2C_SDA) | (1ULL << dDUT_PINO_I2C_SCL));

	if (erro == ESP_ERR_INVALID_RESPONSE)
	{
		return eDUT_RETURN_NACK;
	}
	if (erro != ESP_OK)
	{
		ESP_LOGW(TAG, "I2C mestre: %s", esp_err_to_name(erro));
		return eDUT_RETURN_ERROR;
	}

	return eDUT_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Liga o escravo I2C no endereco (troca o endereco se ja estiver ligado).
 **********************************************************************************************************************/
static dutReturn_t Barramentos_I2cEscrava(uint8_t endereco)
{
	const i2c_slave_config_t config =
	{
		.i2c_port = dBAR_I2C_PORTA,
		.sda_io_num = dDUT_PINO_I2C_SDA,
		.scl_io_num = dDUT_PINO_I2C_SCL,
		.clk_source = I2C_CLK_SRC_DEFAULT,
		.send_buf_depth = 256,
		.receive_buf_depth = 256,
		.slave_addr = endereco,
		.addr_bit_len = I2C_ADDR_BIT_LEN_7,
		.flags.enable_internal_pullup = 1,
	};
	const i2c_slave_event_callbacks_t callbacks =
	{
		.on_request = Barramentos_I2cPedidoIsr,
		.on_receive = Barramentos_I2cRecebeuIsr,
	};

	Barramentos_DesligaI2cEscrava();

	if (i2c_new_slave_device(&config, &bar.i2cEscrava) != ESP_OK)
	{
		bar.i2cEscrava = NULL;
		return eDUT_RETURN_ERROR;
	}

	if (i2c_slave_register_event_callbacks(bar.i2cEscrava, &callbacks, NULL) != ESP_OK)
	{
		Barramentos_DesligaI2cEscrava();
		return eDUT_RETURN_ERROR;
	}

	return eDUT_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      I2CS_LOAD: grava no mapa (funciona com o escravo desligado).
 **********************************************************************************************************************/
static dutReturn_t Barramentos_I2cCarrega(uint8_t offset, const uint8_t *dados, size_t quantidade)
{
	xSemaphoreTake(bar.mutex, portMAX_DELAY);
	mapaI2cReturn_t ret = MapaI2c_Carrega(offset, dados, quantidade);
	xSemaphoreGive(bar.mutex);

	return (ret == eMAPAI2C_RETURN_OK) ? eDUT_RETURN_OK : eDUT_RETURN_INVALID_ARGUMENT;
}

/***********************************************************************************************************************
 * @brief      I2CS_DUMP: le o mapa.
 **********************************************************************************************************************/
static dutReturn_t Barramentos_I2cLe(uint8_t offset, uint8_t *dados, size_t quantidade)
{
	xSemaphoreTake(bar.mutex, portMAX_DELAY);
	mapaI2cReturn_t ret = MapaI2c_Le(offset, dados, quantidade);
	xSemaphoreGive(bar.mutex);

	return (ret == eMAPAI2C_RETURN_OK) ? eDUT_RETURN_OK : eDUT_RETURN_INVALID_ARGUMENT;
}

/***********************************************************************************************************************
 * @brief      Transferencia como mestre SPI: CS (GPIO) baixo, envia e recebe a mesma quantidade, CS alto e solta.
 * @return     OK, BUSY (escravo SPI ligado) ou ERROR
 **********************************************************************************************************************/
static dutReturn_t Barramentos_SpiMestre(uint8_t modo, uint32_t frequencia, const uint8_t *escrita, uint8_t *leitura,
                                         size_t quantidade)
{
	return Barramentos_SpiTransfere(modo, frequencia, NULL, escrita, leitura, quantidade);
}

/***********************************************************************************************************************
 * @brief      Registradores por SPI (sensor): o endereco numa transferencia e os dados em outra, com o CS baixo entre
 *             as duas. A pausa entre elas (dezenas de us) e o tempo que a jiga escrava precisa para buscar o dado.
 **********************************************************************************************************************/
static dutReturn_t Barramentos_SpiRegistro(uint8_t modo, uint32_t frequencia, uint8_t endereco, const uint8_t *escrita,
                                           uint8_t *leitura, size_t quantidade)
{
	static uint8_t vazio[dBAR_SPI_BUFFER];
	static uint8_t descarte[dBAR_SPI_BUFFER];

	if (quantidade > dBAR_SPI_BUFFER)
	{
		return eDUT_RETURN_INVALID_ARGUMENT;
	}

	return Barramentos_SpiTransfere(modo, frequencia, &endereco, (escrita != NULL) ? escrita : vazio,
	                                (leitura != NULL) ? leitura : descarte, quantidade);
}

/***********************************************************************************************************************
 * @brief      Uma transacao de mestre (CS por GPIO): o cabecalho opcional de 1 byte vai numa transferencia propria.
 **********************************************************************************************************************/
static dutReturn_t Barramentos_SpiTransfere(uint8_t modo, uint32_t frequencia, const uint8_t *cabecalho,
                                            const uint8_t *escrita, uint8_t *leitura, size_t quantidade)
{
	spi_device_handle_t dispositivo = NULL;
	esp_err_t erro;

	if (bar.spiTask != NULL)
	{
		return eDUT_RETURN_BUSY;
	}

	const spi_bus_config_t configBarramento =
	{
		.mosi_io_num = dDUT_PINO_SPI_MOSI,
		.miso_io_num = dDUT_PINO_SPI_MISO,
		.sclk_io_num = dDUT_PINO_SPI_SCK,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = dBAR_SPI_BUFFER,
	};
	const spi_device_interface_config_t configDispositivo =
	{
		.mode = modo,
		.clock_speed_hz = (int)frequencia,
		.spics_io_num = -1,
		.queue_size = 1,
	};
	spi_transaction_t transacao =
	{
		.length = quantidade * 8U,
		.tx_buffer = barSpiTx,
		.rx_buffer = barSpiRx,
	};

	memcpy(barSpiTx, escrita, quantidade);

	erro = spi_bus_initialize(dBAR_SPI_HOST, &configBarramento, SPI_DMA_CH_AUTO);
	if (erro == ESP_OK)
	{
		erro = spi_bus_add_device(dBAR_SPI_HOST, &configDispositivo, &dispositivo);
		if (erro == ESP_OK)
		{
			gpio_set_level(dDUT_PINO_SPI_CS, 1);
			gpio_set_direction(dDUT_PINO_SPI_CS, GPIO_MODE_OUTPUT);
			gpio_set_level(dDUT_PINO_SPI_CS, 0);
			esp_rom_delay_us(dBAR_CS_FOLGA_US);
			if (cabecalho != NULL)
			{
				spi_transaction_t endereco = {.length = 8U, .tx_buffer = barSpiCabecalho, .rx_buffer = NULL};
				barSpiCabecalho[0] = *cabecalho;
				erro = spi_device_transmit(dispositivo, &endereco);
				esp_rom_delay_us(dBAR_REGISTRO_PAUSA_US);
			}
			if (erro == ESP_OK)
			{
				erro = spi_device_transmit(dispositivo, &transacao);
			}
			esp_rom_delay_us(dBAR_CS_FOLGA_US);
			gpio_set_level(dDUT_PINO_SPI_CS, 1);
			esp_rom_delay_us(dBAR_CS_FOLGA_US);
			(void)spi_bus_remove_device(dispositivo);
		}
		(void)spi_bus_free(dBAR_SPI_HOST);
	}

	Barramentos_SoltaPinos((1ULL << dDUT_PINO_SPI_CS) | (1ULL << dDUT_PINO_SPI_MOSI) | (1ULL << dDUT_PINO_SPI_SCK) |
	                       (1ULL << dDUT_PINO_SPI_MISO));

	if (erro != ESP_OK)
	{
		ESP_LOGW(TAG, "SPI mestre: %s", esp_err_to_name(erro));
		return eDUT_RETURN_ERROR;
	}

	memcpy(leitura, barSpiRx, quantidade);

	return eDUT_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Arma o escravo SPI no modo pedido (religa se ja estava armado), com o recebido vazio.
 **********************************************************************************************************************/
static dutReturn_t Barramentos_SpiEscrava(uint8_t modo)
{
	const spi_bus_config_t configBarramento =
	{
		.mosi_io_num = dDUT_PINO_SPI_MOSI,
		.miso_io_num = dDUT_PINO_SPI_MISO,
		.sclk_io_num = dDUT_PINO_SPI_SCK,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = dBAR_SPI_BUFFER,
	};
	const spi_slave_interface_config_t configEscrava =
	{
		.spics_io_num = dDUT_PINO_SPI_CS,
		.queue_size = 1,
		.mode = modo,
	};

	Barramentos_DesligaSpiEscrava();

	if (spi_slave_initialize(dBAR_SPI_HOST, &configBarramento, &configEscrava, SPI_DMA_CH_AUTO) != ESP_OK)
	{
		return eDUT_RETURN_ERROR;
	}

	// CS com pull-up: sem mestre ligado, o escravo fica desselecionado
	gpio_set_pull_mode(dDUT_PINO_SPI_CS, GPIO_PULLUP_ONLY);

	xSemaphoreTake(bar.mutex, portMAX_DELAY);
	bar.spiRecebidoQtd = 0U;
	bar.spiModo = modo;
	xSemaphoreGive(bar.mutex);

	bar.spiParar = false;
	if (xTaskCreate(Barramentos_SpiTask, "dut_spi", 4096, NULL, 6, &bar.spiTask) != pdPASS)
	{
		bar.spiTask = NULL;
		(void)spi_slave_free(dBAR_SPI_HOST);
		return eDUT_RETURN_ERROR;
	}

	return eDUT_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      SPIS_LOAD: carga do escravo (vale a partir da proxima transacao que a task preparar).
 **********************************************************************************************************************/
static dutReturn_t Barramentos_SpiCarrega(const uint8_t *dados, size_t quantidade)
{
	xSemaphoreTake(bar.mutex, portMAX_DELAY);
	memcpy(bar.spiCarga, dados, quantidade);
	bar.spiCargaQtd = quantidade;
	xSemaphoreGive(bar.mutex);

	return eDUT_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      SPIS_READ: devolve o que o mestre enviou desde a ultima leitura e esvazia.
 **********************************************************************************************************************/
static dutReturn_t Barramentos_SpiLe(uint8_t *dados, size_t *quantidade)
{
	xSemaphoreTake(bar.mutex, portMAX_DELAY);
	memcpy(dados, bar.spiRecebido, bar.spiRecebidoQtd);
	*quantidade = bar.spiRecebidoQtd;
	bar.spiRecebidoQtd = 0U;
	xSemaphoreGive(bar.mutex);

	return eDUT_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Pinos como entrada, sem pull (a jiga ou os resistores externos definem o nivel).
 **********************************************************************************************************************/
static void Barramentos_SoltaPinos(uint64_t mascara)
{
	const gpio_config_t config =
	{
		.pin_bit_mask = mascara,
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE,
	};

	(void)gpio_config(&config);
}

/***********************************************************************************************************************
 * @brief      Desliga o escravo I2C (se ligado) e solta SDA/SCL.
 **********************************************************************************************************************/
static void Barramentos_DesligaI2cEscrava(void)
{
	if (bar.i2cEscrava != NULL)
	{
		(void)i2c_del_slave_device(bar.i2cEscrava);
		bar.i2cEscrava = NULL;
		Barramentos_SoltaPinos((1ULL << dDUT_PINO_I2C_SDA) | (1ULL << dDUT_PINO_I2C_SCL));
	}
}

/***********************************************************************************************************************
 * @brief      Pede para a task do escravo SPI parar, espera ela liberar o SPI2 e solta os pinos.
 **********************************************************************************************************************/
static void Barramentos_DesligaSpiEscrava(void)
{
	if (bar.spiTask != NULL)
	{
		bar.spiParar = true;
		(void)xSemaphoreTake(bar.spiParou, pdMS_TO_TICKS(4 * dBAR_SPI_ESPERA_MS));
		bar.spiTask = NULL;
		Barramentos_SoltaPinos((1ULL << dDUT_PINO_SPI_CS) | (1ULL << dDUT_PINO_SPI_MOSI) |
		                       (1ULL << dDUT_PINO_SPI_SCK) | (1ULL << dDUT_PINO_SPI_MISO));
	}
}

/***********************************************************************************************************************
 * @brief      ISR: escrita do mestre concluida (ponteiro + dados) -> task.
 **********************************************************************************************************************/
static IRAM_ATTR bool Barramentos_I2cRecebeuIsr(i2c_slave_dev_handle_t escrava,
                                                const i2c_slave_rx_done_event_data_t *evento, void *contexto)
{
	static barEventoI2c_t item;
	BaseType_t acordou = pdFALSE;

	(void)escrava;
	(void)contexto;

	item.leitura = false;
	item.quantidade = (uint16_t)((evento->length < dMAPAI2C_TAMANHO) ? evento->length : dMAPAI2C_TAMANHO);
	memcpy(item.dados, evento->buffer, item.quantidade);
	xQueueSendFromISR(bar.i2cEventos, &item, &acordou);

	return (acordou == pdTRUE);
}

/***********************************************************************************************************************
 * @brief      ISR: o mestre enderecou para leitura (SCL segurado ate a task escrever na FIFO) -> task.
 **********************************************************************************************************************/
static IRAM_ATTR bool Barramentos_I2cPedidoIsr(i2c_slave_dev_handle_t escrava,
                                               const i2c_slave_request_event_data_t *evento, void *contexto)
{
	static barEventoI2c_t item;
	BaseType_t acordou = pdFALSE;

	(void)escrava;
	(void)evento;
	(void)contexto;

	item.leitura = true;
	item.quantidade = 0U;
	xQueueSendFromISR(bar.i2cEventos, &item, &acordou);

	return (acordou == pdTRUE);
}

/***********************************************************************************************************************
 * @brief      Task do escravo I2C: aplica as escritas no mapa e responde os pedidos de leitura.
 * @details    Na leitura, esvazia o que sobrou na FIFO da leitura anterior e escreve o mapa a partir do ponteiro.
 **********************************************************************************************************************/
static void Barramentos_I2cTask(void *parametro)
{
	static barEventoI2c_t evento;
	static uint8_t bloco[dBAR_I2C_BLOCO_LEITURA];
	uint32_t escritos = 0U;

	(void)parametro;

	for (;;)
	{
		if (xQueueReceive(bar.i2cEventos, &evento, portMAX_DELAY) != pdTRUE)
		{
			continue;
		}

		xSemaphoreTake(bar.mutex, portMAX_DELAY);
		if (evento.leitura)
		{
			MapaI2c_Leitura(bloco, sizeof(bloco));
		}
		else
		{
			MapaI2c_Escrita(evento.dados, evento.quantidade);
		}
		xSemaphoreGive(bar.mutex);

		if (evento.leitura && (bar.i2cEscrava != NULL))
		{
			(void)i2c_slave_reset_tx_fifo(bar.i2cEscrava);
			(void)i2c_slave_write(bar.i2cEscrava, bloco, sizeof(bloco), &escritos, dBAR_I2C_TIMEOUT_MS);
		}
	}
}

/***********************************************************************************************************************
 * @brief      Task do escravo SPI: prepara uma transacao (carga + FF), espera o mestre e guarda o recebido.
 * @details    Confere o pedido de parada a cada dBAR_SPI_ESPERA_MS; ao parar, libera o SPI2 e avisa.
 **********************************************************************************************************************/
static void Barramentos_SpiTask(void *parametro)
{
	spi_slave_transaction_t transacao;
	spi_slave_transaction_t *concluida = NULL;

	(void)parametro;

	while (!bar.spiParar)
	{
		xSemaphoreTake(bar.mutex, portMAX_DELAY);
		memset(barSpiTx, 0xFF, sizeof(barSpiTx));
		memcpy(barSpiTx, bar.spiCarga, bar.spiCargaQtd);
		xSemaphoreGive(bar.mutex);

		memset(&transacao, 0, sizeof(transacao));
		transacao.length = dCOMANDOS_DADOS_MAX * 8U;
		transacao.tx_buffer = barSpiTx;
		transacao.rx_buffer = barSpiRx;

		if (spi_slave_queue_trans(dBAR_SPI_HOST, &transacao, portMAX_DELAY) != ESP_OK)
		{
			break;
		}

		while (!bar.spiParar)
		{
			if (spi_slave_get_trans_result(dBAR_SPI_HOST, &concluida, pdMS_TO_TICKS(dBAR_SPI_ESPERA_MS)) == ESP_OK)
			{
				size_t recebidos = concluida->trans_len / 8U;

				xSemaphoreTake(bar.mutex, portMAX_DELAY);
				for (size_t i = 0U; (i < recebidos) && (bar.spiRecebidoQtd < dCOMANDOS_DADOS_MAX); i++)
				{
					bar.spiRecebido[bar.spiRecebidoQtd++] = barSpiRx[i];
				}
				xSemaphoreGive(bar.mutex);
				break;
			}
		}
	}

	(void)spi_slave_free(dBAR_SPI_HOST);
	xSemaphoreGive(bar.spiParou);
	vTaskDelete(NULL);
}

/** @} DOXYGEN GROUP TAG END OF FILE */
