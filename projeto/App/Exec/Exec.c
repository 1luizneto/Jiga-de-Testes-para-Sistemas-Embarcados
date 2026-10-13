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
 *   - Comm (commFrame_t, codigos de comando e de erro) e os modulos de periferico (AnalogOut).
 *
 *   \n <b>Observacoes:</b>
 *   - Nao conhece USB, filas nem o formato da resposta: roda na TestExecTask e e testavel no Unity com
 *     perifericos falsos.
 *   - O Exec confere o TAMANHO do payload; a FAIXA dos parametros e conferida pelo modulo do periferico.
 *
 *   Changelog
 *   @version      <b>1.0.0 - 13/10/2026</b> \n Luiz Neto \n Primeira versao: RESET_ALL e DAC_SET
 *
 *   @copyright
 *   @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include "Exec.h"
#include "AnalogOut.h"
#include <stddef.h>

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

/// Payload do DAC_SET: canal (u8) + valor
#define dEXEC_DAC_SET_SIZE (3U)

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static uint8_t Exec_ResetAll(const commFrame_t *frame);
static uint8_t Exec_DacSet(const commFrame_t *frame);

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

		default:

			result->erro = (uint8_t)eCOMM_ERROR_CMD_DESCONHECIDO;

		break;
	}

	return eEXEC_RETURN_OK;
}


/***********************************************************************************************************************
 * @brief      Leva todas as saidas ao estado seguro.
 **********************************************************************************************************************/
execReturn_t Exec_SafeState(void)
{
	if (AnalogOut_SafeState() != eANALOGOUT_RETURN_OK)
	{
		return eEXEC_RETURN_ERROR;
	}

	return eEXEC_RETURN_OK;
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

/** @} DOXYGEN GROUP TAG END OF FILE */

