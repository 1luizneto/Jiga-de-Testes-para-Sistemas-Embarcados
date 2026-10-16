/***********************************************************************************************************************
 *   @file         DigitalIo.c
 *   @addtogroup   App
 *   @brief        Saidas (GPIO1-4) e entradas (GPIO5-8) digitais da jiga: validacao, mascara de leitura e estado seguro
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Ferramentas:</b>
 *   - Generic.
 *
 *   \n <b>Dependencias:</b>
 *   - Funcoes de escrita e leitura de pino fornecidas pela BSP (DigitalIo_Init).
 *
 *   \n <b>Observacoes:</b>
 *   - Nao conhece o HAL: os testes Unity registram um GPIO falso no DigitalIo_Init.
 *   - Entradas sem pull interno (pinos FT recebendo ate 5 V): sem resistor externo, entrada solta le valor indefinido.
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

#include "DigitalIo.h"
#include <stddef.h>

/***********************************************************************************************************************
 * TIPOS LOCAIS
 **********************************************************************************************************************/

static struct digitalIO
{
	struct func
	{
		digitalIoWriteFunc_t write;
		digitalIoReadFunc_t read;
	}func;
}digitalIo;

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

digitalIoReturn_t DigitalIO_Init(digitalIoWriteFunc_t writeFunc, digitalIoReadFunc_t readFunc)
{
	if ((writeFunc == NULL) || (readFunc == NULL))
	{
		return eDIGITALIO_RETURN_INVALID_ARGUMENT;
	}

	digitalIo.func.write = writeFunc;
	digitalIo.func.read  = readFunc;

	return eDIGITALIO_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Aplica um nivel logico em uma saida.
 *
 * @details    Parametro fora da faixa e recusado sem tocar no hardware ("nenhuma saida alterada", CT09).
 *
 * @param[in]  saida  1 a 4 (GPIO1 a GPIO4)
 * @param[in]  nivel  0 (baixo) ou 1 (alto)
 **********************************************************************************************************************/
digitalIoReturn_t DigitalIO_Set(uint8_t saida, uint8_t nivel)
{
	if (digitalIo.func.write == NULL)
	{
		return eDIGITALIO_RETURN_ERROR;
	}

	if ((saida < dDIGITALIO_SAIDA_MIN) || (saida > dDIGITALIO_ENTRADA_MAX))
	{
		return eDIGITALIO_RETURN_INVALID_ARGUMENT;
	}

	return digitalIo.func.write(saida, nivel);
}

/***********************************************************************************************************************
 * @brief      Le as quatro entradas e monta a mascara do GPIO_READ.
 *
 * @details    Bit 0 = GPIO5 ... bit 3 = GPIO8; bits 4 a 7 sempre em 0. Em falha de leitura a mascara nao e alterada.
 *
 * @param[out] mascara  Niveis das entradas
 **********************************************************************************************************************/
digitalIoReturn_t DigitalIO_Read(uint8_t *mascara)
{
	if (mascara == NULL)
	{
		return eDIGITALIO_RETURN_INVALID_ARGUMENT;
	}

	if (digitalIo.func.read == NULL)
	{
		return eDIGITALIO_RETURN_ERROR;
	}

	uint8_t leitura = 0U;

	for (uint8_t entrada = dDIGITALIO_ENTRADA_MIN; entrada <= dDIGITALIO_ENTRADA_MAX; entrada++)
	{
		uint8_t nivel = dDIGITALIO_NIVEL_BAIXO;

		if (digitalIo.func.read(entrada, &nivel) != eDIGITALIO_RETURN_OK)
		{
			return eDIGITALIO_RETURN_ERROR;
		}

		if (nivel != dDIGITALIO_NIVEL_BAIXO)
		{
			leitura |= (uint8_t)(1U << (entrada - dDIGITALIO_ENTRADA_MIN));
		}
	}

	*mascara = leitura;

	return eDIGITALIO_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Leva as quatro saidas ao estado seguro: nivel baixo.
 * @details    Tenta todas as saidas mesmo que uma falhe, para nao deixar um estimulo aplicado.
 **********************************************************************************************************************/
digitalIoReturn_t DigitalIo_SafeState(void)
{
	digitalIoReturn_t ret = eDIGITALIO_RETURN_OK;

	for (uint8_t saida = dDIGITALIO_SAIDA_MIN; saida <= dDIGITALIO_SAIDA_MAX; saida++)
	{
		if (DigitalIO_Set(saida, dDIGITALIO_NIVEL_BAIXO) != eDIGITALIO_RETURN_OK)
		{
			ret = eDIGITALIO_RETURN_ERROR;
		}
	}

	return ret;
}


/** @} DOXYGEN GROUP TAG END OF FILE */
