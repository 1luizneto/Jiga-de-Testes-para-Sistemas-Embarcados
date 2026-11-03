/***********************************************************************************************************************
 *   @file         ventilador.c
 *   @brief        Comportamento conhecido do CT22: controle de ventilador com histerese (caixa-preta para a jiga)
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Dependencias:</b>
 *   - ESP-IDF v6.1 (esp_adc, esp_driver_gpio), so na parte da task (ESP_PLATFORM).
 *
 *   \n <b>Observacoes:</b>
 *   - A jiga nao manda nenhum comando ao DUT neste caso: aplica a tensao do "sensor" no DAC1 e confere a saida no
 *     GPIO5, como faria com um firmware qualquer (teste black-box, secao 3.7.4 da monografia).
 *   - A regra (Ventilador_Atualiza) nao depende do ESP-IDF e e testada no PC (test_host); a task le o ADC com a
 *     calibracao do chip a cada 1 ms e escreve a saida.
 *
 *   Changelog
 *   @version      <b>1.0.0 - 03/11/2026</b> \n Luiz Neto \n Primeira versao
 *
 *   @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include "ventilador.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "VENTILADOR";
#endif

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

/*******************************************************************************************************************//**
 * @brief       Proximo estado da saida a partir da tensao do sensor
 * @details     Liga acima de dVENTILADOR_LIGA_MV e desliga abaixo de dVENTILADOR_DESLIGA_MV; entre os dois, mantem
 *              o estado (histerese: o ruido do ADC perto do limiar nao faz a saida oscilar).
 * @param       ligado    Estado atual da saida
 * @param       tensaoMv  Tensao do sensor em mV
 * @return      Novo estado da saida
 **********************************************************************************************************************/
bool Ventilador_Atualiza(bool ligado, uint32_t tensaoMv)
{
	if (tensaoMv > dVENTILADOR_LIGA_MV)
	{
		return true;
	}
	if (tensaoMv < dVENTILADOR_DESLIGA_MV)
	{
		return false;
	}
	return ligado;
}

#ifdef ESP_PLATFORM

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

/*******************************************************************************************************************//**
 * @brief       Task do comportamento: le o sensor e atualiza a saida a cada dVENTILADOR_PERIODO_MS
 * @param       parametro  Nao usado
 **********************************************************************************************************************/
static void Ventilador_Task(void *parametro)
{
	(void)parametro;
	adc_oneshot_unit_handle_t adc;
	adc_cali_handle_t cali = NULL;
	adc_oneshot_unit_init_cfg_t unidade = {.unit_id = ADC_UNIT_1};
	adc_oneshot_chan_cfg_t canal = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
	adc_cali_curve_fitting_config_t calibracao = {.unit_id = ADC_UNIT_1, .chan = ADC_CHANNEL_0,
	                                              .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
	bool ligado = false;

	ESP_ERROR_CHECK(adc_oneshot_new_unit(&unidade, &adc));
	ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, ADC_CHANNEL_0, &canal));
	if (adc_cali_create_scheme_curve_fitting(&calibracao, &cali) != ESP_OK)
	{
		ESP_LOGW(TAG, "sem calibracao do ADC: usando a conversao linear");
		cali = NULL;
	}
	gpio_reset_pin(dVENTILADOR_PINO_SAIDA);
	gpio_set_direction(dVENTILADOR_PINO_SAIDA, GPIO_MODE_OUTPUT);
	gpio_set_level(dVENTILADOR_PINO_SAIDA, 0);

	TickType_t proximo = xTaskGetTickCount();
	for (;;)
	{
		int bruto = 0;
		int mv = 0;
		if (adc_oneshot_read(adc, ADC_CHANNEL_0, &bruto) == ESP_OK)
		{
			if (cali == NULL || adc_cali_raw_to_voltage(cali, bruto, &mv) != ESP_OK)
			{
				mv = bruto * 3100 / 4095;
			}
			bool novo = Ventilador_Atualiza(ligado, (uint32_t)mv);
			if (novo != ligado)
			{
				ligado = novo;
				gpio_set_level(dVENTILADOR_PINO_SAIDA, ligado ? 1 : 0);
			}
		}
		xTaskDelayUntil(&proximo, pdMS_TO_TICKS(dVENTILADOR_PERIODO_MS));
	}
}

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

/*******************************************************************************************************************//**
 * @brief       Inicia a task do comportamento do CT22 (roda o tempo todo, junto com o interpretador de comandos)
 **********************************************************************************************************************/
void Ventilador_Inicia(void)
{
	xTaskCreate(Ventilador_Task, "dut_ventilador", 4096, NULL, 4, NULL);
	ESP_LOGI(TAG, "sensor no GPIO%d (ADC1 canal 0), saida no GPIO%d: liga > %u mV, desliga < %u mV",
	         dVENTILADOR_PINO_SENSOR, dVENTILADOR_PINO_SAIDA, dVENTILADOR_LIGA_MV, dVENTILADOR_DESLIGA_MV);
}

#endif

/** @} */
