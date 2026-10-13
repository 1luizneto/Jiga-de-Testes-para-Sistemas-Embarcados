/***********************************************************************************************************************
 *   @file         Bsp_Dac.c
 *   @addtogroup   App
 *   @brief        Abstracao do DAC do STM32 (HAL) para o modulo AnalogOut
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Ferramentas:</b>
 *   - Generic.
 *
 *   \n <b>Dependencias:</b>
 *   - hdac configurado pelo CubeMX (MX_DAC_Init): canais 1 e 2, sem trigger, buffer de saida ligado.
 *
 *   \n <b>Observacoes:</b>
 *   - None.
 *
 *   Changelog
 *   @version      <b>1.0.0 - 13/10/2026</b> \n Luiz Neto \n Primeira versao
 *
 *   @copyright
 *   @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include "Bsp_Dac.h"
#include "AnalogOut.h"
#include "dac.h"

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static analogOutReturn_t BspDac_Set(uint8_t canal, uint16_t valor);

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Entrega a escrita no DAC ao modulo AnalogOut e ja aplica o estado seguro (saidas em 0).
 * @details    Chamado no Bsp_Init, logo apos o MX_DAC_Init: a jiga nunca liga com estimulo aplicado.
 **********************************************************************************************************************/
bspDacReturn_t BspDac_Init(void)
{
	if (AnalogOut_Init(BspDac_Set) != eANALOGOUT_RETURN_OK)
	{
		return eBSP_DAC_RETURN_ERROR;
	}

	if (AnalogOut_SafeState() != eANALOGOUT_RETURN_OK)
	{
		return eBSP_DAC_RETURN_ERROR;
	}

	return eBSP_DAC_RETURN_OK;
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

/**
 * @brief  Escreve o valor (12 bits, alinhado a direita) e habilita o canal.
 *         O valor e escrito antes de habilitar: na primeira vez, a saida ja liga no valor certo.
 *         Sem trigger, o valor passa para a saida logo apos a escrita.
 */
static analogOutReturn_t BspDac_Set(uint8_t canal, uint16_t valor)
{
	uint32_t canalHal = (canal == 1U) ? DAC_CHANNEL_1 : DAC_CHANNEL_2;

	if (HAL_DAC_SetValue(&hdac, canalHal, DAC_ALIGN_12B_R, valor) != HAL_OK)
	{
		return eANALOGOUT_RETURN_ERROR;
	}

	if (HAL_DAC_Start(&hdac, canalHal) != HAL_OK)
	{
		return eANALOGOUT_RETURN_ERROR;
	}

	return eANALOGOUT_RETURN_OK;

}

/** @} DOXYGEN GROUP TAG END OF FILE */
