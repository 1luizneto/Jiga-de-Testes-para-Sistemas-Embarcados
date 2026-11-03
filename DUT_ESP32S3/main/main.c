/***********************************************************************************************************************
 *   @file         main.c
 *   @brief        DUT de referencia (ESP32-S3) da jiga de testes: atende comandos de texto por duas UARTs
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Dependencias:</b>
 *   - comandos.c (interpretador), barramentos.c (I2C/SPI), ventilador.c (CT22), ESP-IDF v6.1 (esp_driver_uart).
 *
 *   \n <b>Observacoes:</b>
 *   - UART1 (GPIO17 TX, GPIO18 RX, 115200 8N1) vai na UART4 da jiga: o PC manda o comando com UART_WRITE e le a
 *     resposta com UART_READ, entao o CT20 roda inteiro pela jiga.
 *   - UART0 (USB do kit, 115200) atende os mesmos comandos: serve para testar o DUT sozinho.
 *   - Uma linha por comando, terminada em '\n' ('\r' e ignorado); a resposta sai na mesma UART. Um mutex
 *     garante um comando por vez (o interpretador usa buffers estaticos).
 *
 *   Changelog
 *   @version      <b>1.0.0 - 03/11/2026</b> \n Luiz Neto \n Primeira versao
 *   @version      <b>1.1.0 - 03/11/2026</b> \n Luiz Neto \n Comportamento do ventilador (CT22) numa task propria
 *
 *   @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "barramentos.h"
#include "comandos.h"
#include "ventilador.h"
#include "versao_dut.h"

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

#define dMAIN_BAUD          (115200)
#define dMAIN_UART_BUFFER   (1024)

static const char *TAG = "DUT";

/***********************************************************************************************************************
 * VARIAVEIS LOCAIS
 **********************************************************************************************************************/

static SemaphoreHandle_t mainMutex;

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static void Main_ConfiguraUart(uart_port_t porta, int tx, int rx);
static void Main_UartTask(void *parametro);

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Inicia os barramentos (soltos), o interpretador e uma task por UART.
 **********************************************************************************************************************/
void app_main(void)
{
	mainMutex = xSemaphoreCreateMutex();
	Comandos_Init(Barramentos_Init());

	Main_ConfiguraUart(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
	Main_ConfiguraUart(UART_NUM_1, dDUT_PINO_UART_TX, dDUT_PINO_UART_RX);

	xTaskCreate(Main_UartTask, "dut_uart0", 6144, (void *)UART_NUM_0, 5, NULL);
	xTaskCreate(Main_UartTask, "dut_uart1", 6144, (void *)UART_NUM_1, 5, NULL);
	Ventilador_Inicia();

	ESP_LOGI(TAG, "DUT de referencia %s pronto. UART1: TX=%d RX=%d; I2C: SDA=%d SCL=%d; "
	         "SPI: CS=%d MOSI=%d SCK=%d MISO=%d", dDUT_VERSAO, dDUT_PINO_UART_TX, dDUT_PINO_UART_RX,
	         dDUT_PINO_I2C_SDA, dDUT_PINO_I2C_SCL, dDUT_PINO_SPI_CS, dDUT_PINO_SPI_MOSI, dDUT_PINO_SPI_SCK,
	         dDUT_PINO_SPI_MISO);
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Instala o driver da UART em 115200 8N1 (UART_PIN_NO_CHANGE mantem os pinos do console).
 **********************************************************************************************************************/
static void Main_ConfiguraUart(uart_port_t porta, int tx, int rx)
{
	const uart_config_t config =
	{
		.baud_rate = dMAIN_BAUD,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.source_clk = UART_SCLK_DEFAULT,
	};

	ESP_ERROR_CHECK(uart_driver_install(porta, dMAIN_UART_BUFFER, dMAIN_UART_BUFFER, 0, NULL, 0));
	ESP_ERROR_CHECK(uart_param_config(porta, &config));
	ESP_ERROR_CHECK(uart_set_pin(porta, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

/***********************************************************************************************************************
 * @brief      Junta os bytes de uma UART em linhas, executa cada uma e responde na mesma UART.
 * @details    Linha maior que dCOMANDOS_LINHA_MAX e descartada inteira (responde ERR ARG no '\n').
 **********************************************************************************************************************/
static void Main_UartTask(void *parametro)
{
	uart_port_t porta = (uart_port_t)(intptr_t)parametro;
	static char linhas[2][dCOMANDOS_LINHA_MAX + 1U];
	static char respostas[2][dCOMANDOS_RESPOSTA_MAX];
	char *linha = linhas[porta == UART_NUM_0 ? 0 : 1];
	char *resposta = respostas[porta == UART_NUM_0 ? 0 : 1];
	size_t tamanho = 0U;
	bool estourou = false;
	uint8_t byte;

	for (;;)
	{
		if (uart_read_bytes(porta, &byte, 1, portMAX_DELAY) != 1)
		{
			continue;
		}

		if (byte == '\r')
		{
			continue;
		}

		if (byte != '\n')
		{
			if (tamanho < dCOMANDOS_LINHA_MAX)
			{
				linha[tamanho++] = (char)byte;
			}
			else
			{
				estourou = true;
			}
			continue;
		}

		linha[tamanho] = '\0';

		xSemaphoreTake(mainMutex, portMAX_DELAY);
		size_t n = estourou ? (size_t)snprintf(resposta, dCOMANDOS_RESPOSTA_MAX, "ERR ARG\n") :
		           Comandos_Executa(linha, resposta, dCOMANDOS_RESPOSTA_MAX);
		xSemaphoreGive(mainMutex);

		if (n > 0U)
		{
			uart_write_bytes(porta, resposta, n);
		}

		tamanho = 0U;
		estourou = false;
	}
}

/** @} DOXYGEN GROUP TAG END OF FILE */
