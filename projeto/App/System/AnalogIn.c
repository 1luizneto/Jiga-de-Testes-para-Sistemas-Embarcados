/***********************************************************************************************************************
 *   @file         AnalogIn.c
 *   @addtogroup   App
 *   @brief        Entradas analogicas da jiga (A0 a A2): validacao do canal e da leitura
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Ferramentas:</b>
 *   - Generic.
 *
 *   \n <b>Dependencias:</b>
 *   - Funcao de conversao fornecida pela BSP (AnalogIn_Init).
 *
 *   \n <b>Observacoes:</b>
 *   - Nao conhece o HAL: os testes Unity registram um ADC falso no AnalogIn_Init.
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

#include "AnalogIn.h"
#include <stddef.h>

/***********************************************************************************************************************
 * TIPOS LOCAIS
 **********************************************************************************************************************/

static struct analogIn
{
	struct func
	{
		analogInReadFunc_t read;
	} func;
}analogIn;

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

analogInReturn_t AnalogIn_Init(analogInReadFunc_t readFunc)
{
	if (readFunc == NULL)
	{
		return eANALOGIN_RETURN_INVALID_ARGUMENT;
	}

	analogIn.func.read = readFunc;

	return eANALOGIN_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Faz uma conversao de 12 bits em um canal.
 *
 * @details    Canal inexistente e recusado sem acionar o ADC. Valor acima de 12 bits vindo da BSP e tratado como
 *             falha do periferico: o valor so e escrito quando a leitura e valida.
 *
 * @param[in]  canal  0 a dANALOGIN_CANAL_MAX
 * @param[out] valor  0 a dANALOGIN_VALOR_MAX
 **********************************************************************************************************************/
analogInReturn_t AnalogIn_Read(uint8_t canal, uint16_t *valor)
{
	if ((valor == NULL) || (canal > dANALOGIN_CANAL_MAX))
	{
		return eANALOGIN_RETURN_INVALID_ARGUMENT;
	}

	if (analogIn.func.read == NULL)
	{
		return eANALOGIN_RETURN_ERROR;
	}

	uint16_t leitura = 0U;

	if (analogIn.func.read(canal, &leitura) != eANALOGIN_RETURN_OK)
	{
		return eANALOGIN_RETURN_OK;
	}

	if (leitura > dANALOGIN_VALOR_MAX)
	{
		return eANALOGIN_RETURN_ERROR;
	}

	*valor = leitura;

	return eANALOGIN_RETURN_OK;
}

/** @} DOXYGEN GROUP TAG END OF FILE */


