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
 *   - Comm (commFrame_t, codigos de comando e de erro) e os modulos de periferico (AnalogOut, DigitalIo, AnalogIn,
 *     Pwm, I2cBus).
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
 *   @version      <b>1.2.0 - 21/10/2026</b> \n Luiz Neto \n PWM_SET e PWM_STOP; estado seguro inclui o PWM
 *   @version      <b>1.3.0 - 27/10/2026</b> \n Luiz Neto \n I2C_CONFIG, I2C_SLAVE_LOAD, I2C_SLAVE_DUMP e
 *                                                          I2C_MASTER_XFER; estado seguro desliga o I2C
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
#include "Pwm.h"
#include "I2cBus.h"
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

/// Payload do PWM_SET: canal (u8) + frequencia (u32 little-endian) + razao ciclica (u16 little-endian)
#define dEXEC_PWM_SET_SIZE (7U)

/// Payload do PWM_STOP: canal (u8)
#define dEXEC_PWM_STOP_SIZE (1U)

/// Payload do I2C_CONFIG: papel (u8) + endereco (u8) + velocidade (u8)
#define dEXEC_I2C_CONFIG_SIZE (3U)

/// Cabecalho do I2C_SLAVE_LOAD: offset (u8) + quantidade (u8), seguido dos dados
#define dEXEC_I2C_LOAD_HEADER_SIZE (2U)

/// Payload do I2C_SLAVE_DUMP: offset (u8) + quantidade (u8)
#define dEXEC_I2C_DUMP_SIZE (2U)

/// Cabecalho do I2C_MASTER_XFER: endereco (u8) + nw (u8) + nr (u8), seguido dos nw bytes a escrever
#define dEXEC_I2C_XFER_HEADER_SIZE (3U)

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static uint8_t Exec_ResetAll(const commFrame_t *frame);
static uint8_t Exec_DacSet(const commFrame_t *frame);
static uint8_t Exec_GpioSet(const commFrame_t *frame);
static uint8_t Exec_GpioRead(const commFrame_t *frame, execResult_t *result);
static uint8_t Exec_AdcRead(const commFrame_t *frame, execResult_t *result);
static uint8_t Exec_PwmSet(const commFrame_t *frame);
static uint8_t Exec_PwmStop(const commFrame_t *frame);
static uint8_t Exec_ErroPwm(pwmReturn_t ret);
static uint8_t Exec_I2cConfig(const commFrame_t *frame);
static uint8_t Exec_I2cLoad(const commFrame_t *frame);
static uint8_t Exec_I2cDump(const commFrame_t *frame, execResult_t *result);
static uint8_t Exec_I2cXfer(const commFrame_t *frame, execResult_t *result);
static uint8_t Exec_ErroI2c(i2cBusReturn_t ret);

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

		case dCOMM_CMD_PWM_SET:

			result->erro = Exec_PwmSet(frame);

		break;

		case dCOMM_CMD_PWM_STOP:

			result->erro = Exec_PwmStop(frame);

		break;

		case dCOMM_CMD_I2C_CONFIG:

			result->erro = Exec_I2cConfig(frame);

		break;

		case dCOMM_CMD_I2C_SLAVE_LOAD:

			result->erro = Exec_I2cLoad(frame);

		break;

		case dCOMM_CMD_I2C_SLAVE_DUMP:

			result->erro = Exec_I2cDump(frame, result);

		break;

		case dCOMM_CMD_I2C_MASTER_XFER:

			result->erro = Exec_I2cXfer(frame, result);

		break;

		default:

			result->erro = (uint8_t)eCOMM_ERROR_CMD_DESCONHECIDO;

		break;
	}

	return eEXEC_RETURN_OK;
}


/***********************************************************************************************************************
 * @brief      Leva todas as saidas ao estado seguro: DAC em 0, GPIO1-4 em nivel baixo, PWM parado e I2C desligado
 *             (com o mapa de registradores zerado).
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

	if (Pwm_SafeState() != ePWM_RETURN_OK)
	{
		ret = eEXEC_RETURN_ERROR;
	}

	if (I2cBus_SafeState() != eI2CBUS_RETURN_OK)
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

/***********************************************************************************************************************
 * @brief      PWM_SET: payload [canal (u8, 1-3)][frequencia (u32 LE, Hz)][razao ciclica (u16 LE, centesimos de %)].
 * @details    Aqui so o tamanho do payload e conferido; a faixa e conferida pelo Pwm_Set, que recusa sem tocar no
 *             timer.
 * @param[in]  frame  Quadro do comando
 * @return     0 (ACK) ou codigo commError_t (NACK)
 **********************************************************************************************************************/
static uint8_t Exec_PwmSet(const commFrame_t *frame)
{
	if (frame->size != dEXEC_PWM_SET_SIZE)
	{
		return (uint8_t)eCOMM_ERROR_PARAMETRO_INVALIDO;
	}

	uint8_t  canal      = frame->payload[0];
	uint32_t frequencia = (uint32_t)frame->payload[1]
	                    | ((uint32_t)frame->payload[2] << 8)
	                    | ((uint32_t)frame->payload[3] << 16)
	                    | ((uint32_t)frame->payload[4] << 24);
	uint16_t razao      = (uint16_t)(frame->payload[5] | ((uint16_t)frame->payload[6] << 8));

	return Exec_ErroPwm(Pwm_Set(canal, frequencia, razao));
}

/***********************************************************************************************************************
 * @brief      PWM_STOP: payload [canal (u8, 1-3)]. Para o PWM (saida em nivel baixo, conduzida).
 * @param[in]  frame  Quadro do comando
 * @return     0 (ACK) ou codigo commError_t (NACK)
 **********************************************************************************************************************/
static uint8_t Exec_PwmStop(const commFrame_t *frame)
{
	if (frame->size != dEXEC_PWM_STOP_SIZE)
	{
		return (uint8_t)eCOMM_ERROR_PARAMETRO_INVALIDO;
	}

	return Exec_ErroPwm(Pwm_Stop(frame->payload[0]));
}

/***********************************************************************************************************************
 * @brief      Converte o retorno do Pwm no codigo de erro do NACK.
 * @param[in]  ret  Retorno do Pwm_Set ou do Pwm_Stop
 * @return     0 (ACK), PARAMETRO_INVALIDO ou ERRO_PERIFERICO
 **********************************************************************************************************************/
static uint8_t Exec_ErroPwm(pwmReturn_t ret)
{
	switch (ret)
	{
		case ePWM_RETURN_OK:

			return 0U;

		break;

		case ePWM_RETURN_INVALID_ARGUMENT:

			return (uint8_t)eCOMM_ERROR_PARAMETRO_INVALIDO;

		break;

		default:

			return (uint8_t)eCOMM_ERROR_PERIFERICO;

		break;
	}
}

/***********************************************************************************************************************
 * @brief      I2C_CONFIG: payload [papel (u8, 0 escravo, 1 mestre)][endereco (u8, 7 bits)][velocidade (u8, 0-1)].
 * @details    Aqui so o tamanho do payload e conferido; a faixa e conferida pelo I2cBus_Config, que recusa sem tocar
 *             no I2C.
 * @param[in]  frame  Quadro do comando
 * @return     0 (ACK) ou codigo commError_t (NACK)
 **********************************************************************************************************************/
static uint8_t Exec_I2cConfig(const commFrame_t *frame)
{
	if (frame->size != dEXEC_I2C_CONFIG_SIZE)
	{
		return (uint8_t)eCOMM_ERROR_PARAMETRO_INVALIDO;
	}

	return Exec_ErroI2c(I2cBus_Config(frame->payload[0], frame->payload[1], frame->payload[2]));
}

/***********************************************************************************************************************
 * @brief      I2C_SLAVE_LOAD: payload [offset (u8)][quantidade (u8)][dados (quantidade bytes)].
 * @details    A quantidade tem que bater com o tamanho do payload; o limite do mapa e conferido pelo I2cBus_Load.
 * @param[in]  frame  Quadro do comando
 * @return     0 (ACK) ou codigo commError_t (NACK)
 **********************************************************************************************************************/
static uint8_t Exec_I2cLoad(const commFrame_t *frame)
{
	if ((frame->size < dEXEC_I2C_LOAD_HEADER_SIZE) ||
	    (frame->size != (dEXEC_I2C_LOAD_HEADER_SIZE + frame->payload[1])))
	{
		return (uint8_t)eCOMM_ERROR_PARAMETRO_INVALIDO;
	}

	return Exec_ErroI2c(I2cBus_Load(frame->payload[0], frame->payload[1], &frame->payload[2]));
}

/***********************************************************************************************************************
 * @brief      I2C_SLAVE_DUMP: payload [offset (u8)][quantidade (u8)]; responde os bytes do mapa.
 * @details    A quantidade tem que caber no payload da resposta (240 bytes); o limite do mapa e conferido pelo
 *             I2cBus_Dump.
 * @param[in]  frame   Quadro do comando
 * @param[out] result  Recebe os bytes em dados (so no ACK)
 * @return     0 (ACK) ou codigo commError_t (NACK)
 **********************************************************************************************************************/
static uint8_t Exec_I2cDump(const commFrame_t *frame, execResult_t *result)
{
	if ((frame->size != dEXEC_I2C_DUMP_SIZE) || (frame->payload[1] > dCOMM_PAYLOAD_MAX_SIZE))
	{
		return (uint8_t)eCOMM_ERROR_PARAMETRO_INVALIDO;
	}

	uint8_t erro = Exec_ErroI2c(I2cBus_Dump(frame->payload[0], frame->payload[1], result->dados));

	if (erro == 0U)
	{
		result->size = frame->payload[1];
	}

	return erro;
}

/***********************************************************************************************************************
 * @brief      I2C_MASTER_XFER: payload [endereco (u8)][nw (u8)][nr (u8)][dados (nw bytes)]; responde os nr bytes lidos.
 * @details    nw tem que bater com o tamanho do payload e nr tem que caber no payload da resposta (240 bytes).
 * @param[in]  frame   Quadro do comando
 * @param[out] result  Recebe os bytes lidos em dados (so no ACK)
 * @return     0 (ACK) ou codigo commError_t (NACK); sem papel de mestre, NAO_CONFIGURADO
 **********************************************************************************************************************/
static uint8_t Exec_I2cXfer(const commFrame_t *frame, execResult_t *result)
{
	if ((frame->size < dEXEC_I2C_XFER_HEADER_SIZE) ||
	    (frame->size != (dEXEC_I2C_XFER_HEADER_SIZE + frame->payload[1])) ||
	    (frame->payload[2] > dCOMM_PAYLOAD_MAX_SIZE))
	{
		return (uint8_t)eCOMM_ERROR_PARAMETRO_INVALIDO;
	}

	uint8_t nr   = frame->payload[2];
	uint8_t erro = Exec_ErroI2c(I2cBus_MasterXfer(frame->payload[0], &frame->payload[3], frame->payload[1],
	                                              result->dados, nr));

	if (erro == 0U)
	{
		result->size = nr;
	}

	return erro;
}

/***********************************************************************************************************************
 * @brief      Converte o retorno do I2cBus no codigo de erro do NACK.
 * @param[in]  ret  Retorno do I2cBus
 * @return     0 (ACK), PARAMETRO_INVALIDO, NAO_CONFIGURADO ou ERRO_PERIFERICO (inclusive falta de ACK do dispositivo)
 **********************************************************************************************************************/
static uint8_t Exec_ErroI2c(i2cBusReturn_t ret)
{
	switch (ret)
	{
		case eI2CBUS_RETURN_OK:

			return 0U;

		break;

		case eI2CBUS_RETURN_INVALID_ARGUMENT:

			return (uint8_t)eCOMM_ERROR_PARAMETRO_INVALIDO;

		break;

		case eI2CBUS_RETURN_NOT_CONFIGURED:

			return (uint8_t)eCOMM_ERROR_NAO_CONFIGURADO;

		break;

		default:

			return (uint8_t)eCOMM_ERROR_PERIFERICO;

		break;
	}
}

/** @} DOXYGEN GROUP TAG END OF FILE */

