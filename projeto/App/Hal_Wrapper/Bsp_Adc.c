/***********************************************************************************************************************
 *   @file         Bsp_Adc.c
 *   @addtogroup   App
 *   @brief        Abstracao dos ADCs do STM32 (HAL) para o modulo AnalogIn
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Ferramentas:</b>
 *   - Generic.
 *
 *   \n <b>Dependencias:</b>
 *   - hadc1, hadc2 e hadc3 configurados pelo CubeMX (MX_ADCx_Init): 12 bits, 1 conversao regular por software,
 *     sampling time de 480 ciclos.
 *
 *   \n <b>Observacoes:</b>
 *   - Cada canal da jiga fica em um ADC separado: a conversao nao precisa trocar de canal.
 *
 *   Changelog
 *   @version      <b>1.0.0 - 16/10/2026</b> \n Luiz Neto \n Primeira versao
 *
 *   @copyright
 *   @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include "Bsp_Adc.h"
#include "AnalogIn.h"
#include "adc.h"

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

/// Tempo maximo de uma conversao
#define dBSP_ADC_TIMEOUT_MS (10U)

/***********************************************************************************************************************
 * VARIAVEIS LOCAIS
 **********************************************************************************************************************/

/// ADC de cada canal da jiga (indice = canal)
static ADC_HandleTypeDef * const bspAdcHandles[dANALOGIN_CANAL_MAX + 1U] =
{
		&hadc1, // canal 0: A0 (PA3, ADC1_IN3)
		&hadc2, // canal 1: A1 (PC0, ADC2_IN10)
		&hadc3, // canal 2: A2 (PC3, ADC3_IN13)
};

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static analogInReturn_t BspAdc_Read(uint8_t canal, uint16_t *valor);

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Entrega a conversao dos ADCs ao modulo AnalogIn.
 **********************************************************************************************************************/
bspAdcReturn_t BspAdc_Init(void)
{
	if (AnalogIn_Init(BspAdc_Read) != eANALOGIN_RETURN_OK)
	{
		return eBSP_ADC_RETURN_ERROR;
	}

	return eBSP_ADC_RETURN_OK;
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Faz uma conversao por polling no ADC do canal.
 * @details    O ADC e parado tambem quando a conversao estoura o tempo, para a proxima leitura comecar do zero.
 *             Canal ja foi validado pelo AnalogIn_Read.
 * @param[in]  canal  0 a 2
 * @param[out] valor  Resultado de 12 bits
 * @return     eANALOGIN_RETURN_OK ou eANALOGIN_RETURN_ERROR (falha ou timeout do HAL)
 **********************************************************************************************************************/
static analogInReturn_t BspAdc_Read(uint8_t canal, uint16_t *valor)
{
	ADC_HandleTypeDef *hadc = bspAdcHandles[canal];

	if (HAL_ADC_Start(hadc) != HAL_OK)
	{
		return eANALOGIN_RETURN_ERROR;
	}

	if (HAL_ADC_PollForConversion(hadc, dBSP_ADC_TIMEOUT_MS) != HAL_OK)
	{
		(void)HAL_ADC_Stop(hadc);
		return eANALOGIN_RETURN_ERROR;
	}

	*valor = (uint16_t)HAL_ADC_GetValue(hadc);

	if (HAL_ADC_Stop(hadc) != HAL_OK)
	{
		return eANALOGIN_RETURN_ERROR;
	}

	return eANALOGIN_RETURN_OK;
}

/** @} DOXYGEN GROUP TAG END OF FILE */






