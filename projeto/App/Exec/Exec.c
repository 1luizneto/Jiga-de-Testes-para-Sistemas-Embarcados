/***********************************************************************************************************************
 *   @file         Exec.c
 *   @addtogroup   App
 *   @brief        Execucao dos comandos diretos: decodifica o payload, aciona o periferico e devolve o resultado
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Ferramentas:</b>
 *   - Generic.
 *
 *   \n <b>Dependencias:</b>
 *   - Comm (commFrame_t, codigos de comando e de erro) e os modulos de periferico (AnalogOut, DigitalIo, AnalogIn).
 *
 *   \n <b>Observacoes:</b>
 *   - Nao conhece USB, filas nem o formato da resposta: roda na TestExecTask e e testavel no Unity com
 *     perifericos falsos.
 *   - O Exec confere o TAMANHO do payload; a FAIXA dos parametros e conferida pelo modulo do periferico.
 *
 *   Changelog
 *   @version      <b>1.0.0 - 13/10/2026</b> \n Luiz Neto \n Primeira versao: RESET_ALL e DAC_SET
 *   @version      <b>1.1.0 - 16/10/2026</b> \n Luiz Neto \n GPIO_SET, GPIO_READ e ADC_READ; estado seguro inclui as
 *                                                          saidas digitais
 *
 *   @copyright
 *   @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include "Exec.h"
#include "AnalogOut.h"
#include "AnalogIn.h"
#include "DigitalIo.h"
#include <stddef.h>

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

/// Payload do DAC_SET: canal (u8) + valor
#define dEXEC_DAC_SET_SIZE (3U)

/// Payload do GPIO_SET: pino (u8) + nivel (u8)
#define dEXEC_GPIO_SET_SIZE (2U)

/// Resposta do GPIO_READ: mascara (u8)
#define dEXEC_GPIO_READ_RESP_SIZE (1U)

/// Payload do ADC_READ: canal (u8)
#define dEXEC_ADC_READ_SIZE (1U)

/// Resposta do ADC_READ: canal (u8) + valor (u16 little-endian)
#define dEXEC_ADC_READ_RESP_SIZE (3U)

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static uint8_t Exec_ResetAll(const commFrame_t *frame);
static uint8_t Exec_DacSet(const commFrame_t *frame);
static uint8_t Exec_GpioSet(const commFrame_t *frame);
static uint8_t Exec_GpioRead(const commFrame_t *frame, execResult_t *result);
static uint8_t Exec_AdcRead(const commFrame_t *frame, execResult_t *result);

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Executa um comando direto e preenche o resultado.
 *
 * @details    Comando invalido e recusado sem acionar nenhuma saida ("nenhuma saida alterada", CT09).
 *
 * @param[in]  frame   Quadro validado pelo Comm_ProcessByte
 * @param[out] result  SEQ e CMD copiados do quadro; erro 0 (ACK) ou codigo commError_t (NACK)
 *
 * @return     eEXEC_RETURN_OK (o resultado foi preenchido, seja ACK ou NACK) ou eEXEC_RETURN_INVALID_ARGUMENT
 **********************************************************************************************************************/
execReturn_t Exec_Process(const commFrame_t *frame, execResult_t *result)
{
	if ((frame == NULL) || (result == NULL))
	{
		return eEXEC_RETURN_INVALID_ARGUMENT;
	}

	result->seq  = frame->seq;
	result->cmd  = frame->cmd;
	result->size = 0U;

	switch (frame->cmd)
	{
		case dCOMM_CMD_RESET_ALL:

			result->erro = Exec_ResetAll(frame);

		break;

		case dCOMM_CMD_DAC_SET:

			result->erro = Exec_DacSet(frame);

		break;

		case dCOMM_CMD_GPIO_SET:

			result->erro = Exec_GpioSet(frame);

		break;

		case dCOMM_CMD_GPIO_READ:

			result->erro = Exec_GpioRead(frame, result);

		break;

		case dCOMM_CMD_ADC_READ:

			result->erro = Exec_AdcRead(frame, result);

		break;

		default:

			result->erro = (uint8_t)eCOMM_ERROR_CMD_DESCONHECIDO;

		break;
	}

	return eEXEC_RETURN_OK;
}


/***********************************************************************************************************************
 * @brief      Leva todas as saidas ao estado seguro: DAC em 0 e GPIO1-4 em nivel baixo.
 * @details    Tenta todos os perifericos mesmo que um falhe, para nao deixar um estimulo aplicado.
 **********************************************************************************************************************/
execReturn_t Exec_SafeState(void)
{
	execReturn_t ret = eEXEC_RETURN_OK;

	if (AnalogOut_SafeState() != eANALOGOUT_RETURN_OK)
	{
		ret = eEXEC_RETURN_ERROR;
	}

	if (DigitalIO_SafeState() != eDIGITALIO_RETURN_OK)
	{
		ret = eEXEC_RETURN_ERROR;
	}

	return ret;
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      RESET_ALL (sem payload): leva todas as saidas ao estado seguro.
 * @details    Payload nao vazio e recusado antes de mexer nas saidas.
 * @param[in]  frame  Quadro do comando
 * @return     0 (ACK) ou codigo commError_t (NACK)
 **********************************************************************************************************************/
static uint8_t Exec_ResetAll(const commFrame_t *frame)
{
	if (frame->size != 0U)
	{
		return (uint8_t)eCOMM_ERROR_PARAMETRO_INVALIDO;
	}

	if (Exec_SafeState() != eEXEC_RETURN_OK)
	{
		return (uint8_t)eCOMM_ERROR_PERIFERICO;
	}

	return 0U;
}

/***********************************************************************************************************************
 * @brief      DAC_SET: payload [canal (u8, 1-2)][valor (u16 little-endian, 0-4095)].
 * @details    Aqui so o tamanho do payload e conferido; a faixa de canal e valor e conferida pelo AnalogOut_Set,
 *             que recusa sem acionar o DAC.
 * @param[in]  frame  Quadro do comando
 * @return     0 (ACK) ou codigo commError_t (NACK)
 **********************************************************************************************************************/
static uint8_t Exec_DacSet(const commFrame_t *frame)
{
	if (frame->size != dEXEC_DAC_SET_SIZE)
	{
		return (uint8_t)eCOMM_ERROR_PARAMETRO_INVALIDO;
	}

	uint8_t canal = frame->payload[0];
	uint16_t valor = (uint16_t)(frame->payload[1] | ((uint16_t)frame->payload[2] << 8));

	switch (AnalogOut_Set(canal, valor))
	{
		case eANALOGOUT_RETURN_OK:

			return 0U;

		break;

		case eANALOGOUT_RETURN_INVALID_ARGUMENT:

			return (uint8_t)eCOMM_ERROR_PARAMETRO_INVALIDO;

		break;

		default:

			return (uint8_t)eCOMM_ERROR_PERIFERICO;

		break;
	}

}

/***********************************************************************************************************************
 * @brief      GPIO_SET: payload [pino (u8, 1-4)][nivel (u8, 0-1)].
 * @details    Aqui so o tamanho do payload e conferido; a faixa de pino e nivel e conferida pelo DigitalIO_Set,
 *             que recusa sem tocar no pino.
 * @param[in]  frame  Quadro do comando
 * @return     0 (ACK) ou codigo commError_t (NACK)
 **********************************************************************************************************************/
static uint8_t Exec_GpioSet(const commFrame_t *frame)
{
	if (frame->size != dEXEC_GPIO_SET_SIZE)
	{
		return (uint8_t)eCOMM_ERROR_PARAMETRO_INVALIDO;
	}

	switch (DigitalIO_Set(frame->payload[0], frame->payload[1]))
	{
		case eDIGITALIO_RETURN_OK:

			return 0U;

		break;

		case eDIGITALIO_RETURN_INVALID_ARGUMENT:

			return (uint8_t)eCOMM_ERROR_PARAMETRO_INVALIDO;

		break;

		default:

			return (uint8_t)eCOMM_ERROR_PERIFERICO;

		break;
	}
}

/***********************************************************************************************************************
 * @brief      GPIO_READ (sem payload): responde a mascara das entradas.
 * @details    Resposta [mascara (u8)]: bit 0 = GPIO5 ... bit 3 = GPIO8, bits 4 a 7 em 0.
 * @param[in]  frame   Quadro do comando
 * @param[out] result  Recebe a mascara em dados (so no ACK)
 * @return     0 (ACK) ou codigo commError_t (NACK)
 **********************************************************************************************************************/
static uint8_t Exec_GpioRead(const commFrame_t *frame, execResult_t *result)
{
	uint8_t mascara = 0U;

	if (frame->size != 0U)
	{
		return (uint8_t)eCOMM_ERROR_PARAMETRO_INVALIDO;
	}

	if (DigitalIO_Read(&mascara) != eDIGITALIO_RETURN_OK)
	{
		return (uint8_t)eCOMM_ERROR_PERIFERICO;
	}

	result->dados[0] = mascara;
	result->size     = dEXEC_GPIO_READ_RESP_SIZE;

	return 0U;
}

/***********************************************************************************************************************
 * @brief      ADC_READ: payload [canal (u8, 0-2)]; responde [canal (u8)][valor (u16 little-endian, 0-4095)].
 * @details    Aqui so o tamanho do payload e conferido; a faixa do canal e conferida pelo AnalogIn_Read, que recusa
 *             sem acionar o ADC.
 * @param[in]  frame   Quadro do comando
 * @param[out] result  Recebe canal e valor em dados (so no ACK)
 * @return     0 (ACK) ou codigo commError_t (NACK)
 **********************************************************************************************************************/
static uint8_t Exec_AdcRead(const commFrame_t *frame, execResult_t *result)
{
	uint16_t valor = 0U;

	if (frame->size != dEXEC_ADC_READ_SIZE)
	{
		return (uint8_t)eCOMM_ERROR_PARAMETRO_INVALIDO;
	}

	uint8_t canal = frame->payload[0];

	switch (AnalogIn_Read(canal, &valor))
	{
		case eANALOGIN_RETURN_OK:

			result->dados[0] = canal;
			result->dados[1] = (uint8_t)(valor & 0xFFU);
			result->dados[2] = (uint8_t)(valor >> 8);
			result->size     = dEXEC_ADC_READ_RESP_SIZE;

			return 0U;

		break;

		case eANALOGIN_RETURN_INVALID_ARGUMENT:

			return (uint8_t)eCOMM_ERROR_PARAMETRO_INVALIDO;

		break;

		default:

			return (uint8_t)eCOMM_ERROR_PERIFERICO;

		break;
	}
}

/** @} DOXYGEN GROUP TAG END OF FILE */

