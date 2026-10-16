/***********************************************************************************************************************
 *   @file         Bsp_Gpio.c
 *   @addtogroup   App
 *   @brief        Abstracao dos GPIOs da jiga (HAL) para o modulo DigitalIo
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Ferramentas:</b>
 *   - Generic.
 *
 *   \n <b>Dependencias:</b>
 *   - Pinos configurados pelo CubeMX (MX_GPIO_Init): GPIO1_OUT a GPIO4_OUT push-pull iniciando em nivel baixo;
 *     GPIO1_IN a GPIO4_IN (GPIO5 a GPIO8 da monografia) entradas sem pull.
 *
 *   \n <b>Observacoes:</b>
 *   - None.
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

#include "Bsp_Gpio.h"
#include "DigitalIo.h"
#include "main.h"

/***********************************************************************************************************************
 * TIPOS LOCAIS
 **********************************************************************************************************************/

// Portas e pinos da HAL de um GPIO da jiga
typedef struct bspGpioPino
{
	GPIO_TypeDef *porta;
	uint16_t      pino;
} bspGpioPino_t;

/***********************************************************************************************************************
 * VARIAVEIS LOCAIS
 **********************************************************************************************************************/

/// Saidas 1 a 4 (indice = saida - 1)
static const bspGpioPino_t
bspGpioSaidas[dDIGITALIO_SAIDA_MAX] =
{
		{GPIO1_OUT_GPIO_Port, GPIO1_OUT_Pin}, // GPIO1 (PC8)
		{GPIO2_OUT_GPIO_Port, GPIO2_OUT_Pin}, // GPIO2 (PC9)
		{GPIO3_OUT_GPIO_Port, GPIO3_OUT_Pin}, // GPIO3 (PC10)
		{GPIO4_OUT_GPIO_Port, GPIO4_OUT_Pin}, // GPIO4 (PC11)
};

/// Entradas 1 a 4 (indice = entrada - 1)
static const bspGpioPino_t
bspGpioEntradas[dDIGITALIO_ENTRADA_MAX] =
{
		{GPIO1_IN_GPIO_Port, GPIO1_IN_Pin}, // GPIO1 (PC12)
		{GPIO2_IN_GPIO_Port, GPIO2_IN_Pin}, // GPIO2 (PD2)
		{GPIO3_IN_GPIO_Port, GPIO3_IN_Pin}, // GPIO3 (PG2)
		{GPIO4_IN_GPIO_Port, GPIO4_IN_Pin}, // GPIO4 (PG3)
};

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static digitalIoReturn_t BspGpio_Write(uint8_t saida, uint8_t nivel);
static digitalIoReturn_t BspGpio_Read(uint8_t entrada, uint8_t *nivel);

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Entrega a escrita e a leitura dos pinos ao modulo DigitalIo.
 * @details    Nao aplica o estado seguro: o MX_GPIO_Init ja liga as saidas em nivel baixo.
 **********************************************************************************************************************/
static digitalIoReturn_t BspGpio_Write(uint8_t saidas, uint8_t nivel)
{
	const bspGpioPino_t *gpio = &bspGpioSaidas[saidas - 1U];

	HAL_GPIO_WritePin(gpio->porta, gpio->pino, (nivel != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/***********************************************************************************************************************
 * @brief      Le o nivel de uma entrada.
 * @details    Entrada ja foi validada pelo DigitalIo_Read.
 * @param[in]  entrada  1 a 4
 * @param[out] nivel    0 ou 1
 * @return     eDIGITALIO_RETURN_OK (o HAL_GPIO_ReadPin nao falha)
 **********************************************************************************************************************/
static digitalIoReturn_t BspGpio_Read(uint8_t entradas, uint8_t *nivel)
{
	const bspGpioPino_t *gpio = &bspGpioEntradas[entradas - 1U];

	*nivel = (HAL_GPIO_ReadPin(gpio->porta, gpio->pino) == GPIO_PIN_SET) ? 1U : 0U;

	return eDIGITALIO_RETURN_OK;
}

/** @} DOXYGEN GROUP TAG END OF FILE */









