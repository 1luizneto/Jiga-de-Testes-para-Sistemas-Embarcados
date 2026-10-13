/***********************************************************************************************************************
 *   @file         AnalogOut.c
 *   @addtogroup   App
 *   @brief        Saidas analogicas da jiga (DAC1 e DAC2): validacao dos parametros e estado seguro
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Ferramentas:</b>
 *   - Generic.
 *
 *   \n <b>Dependencias:</b>
 *   - Funcao de escrita no DAC fornecida pela BSP (AnalogOut_Init).
 *
 *   \n <b>Observacoes:</b>
 *   - Nao conhece o HAL: os testes Unity registram um DAC falso no AnalogOut_Init.
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

#include "AnalogOut.h"
#include <stddef.h>

/***********************************************************************************************************************
 * TIPOS LOCAIS
 **********************************************************************************************************************/

static struct analogOut
{
	struct func
	{
		analogOutSetFunc_t set;
	} func;
} analogOut;

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

analogOutReturn_t AnalogOut_Init(analogOutSetFunc_t setFunc)
{
	if (setFunc == NULL)
	{
		return eANALOGOUT_RETURN_INVALID_ARGUMENT;
	}

	analogOut.func.set = setFunc;

	return eANALOGOUT_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Aplica um valor de 12 bits em um canal.
 *
 * @details    Parametro fora da faixa e recusado sem tocar no hardware ("nenhuma saida alterada", CT09).
 * @param[in]  canal  1 (DAC1, PA4) ou 2 (DAC2, PA5)
 *
 * @param[in]  valor  0 a dANALOGOUT_VALOR_MAX
 **********************************************************************************************************************/
analogOutReturn_t AnalogOut_Set(uint8_t canal, uint16_t valor)
{
	if (analogOut.func.set == NULL)
	{
		return eANALOGOUT_RETURN_ERROR;
	}

	if ((canal < dANALOGOUT_CANAL_MIN) || (canal > dANALOGOUT_CANAL_MAX) || (valor > dANALOGOUT_VALOR_MAX))
	{
		return eANALOGOUT_RETURN_INVALID_ARGUMENT;
	}

	return analogOut.func.set(canal, valor);
}

/***********************************************************************************************************************
 * @brief      Leva as duas saidas ao estado seguro: DAC em 0.
 * @details    Tenta os dois canais mesmo que o primeiro falhe, para nao deixar um estimulo aplicado.
 **********************************************************************************************************************/
analogOutReturn_t AnalogOut_SafeState(void)
{
	analogOutReturn_t ret = eANALOGOUT_RETURN_OK;

	for (uint8_t canal = dANALOGOUT_CANAL_MIN; canal <= dANALOGOUT_CANAL_MAX; canal++)
	{
		if (AnalogOut_Set(canal, 0U) != eANALOGOUT_RETURN_OK)
		{
			ret = eANALOGOUT_RETURN_ERROR;
		}
	}

	return ret;
}
