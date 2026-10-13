/***********************************************************************************************************************
 *   @file       Comm.h
 *   @addtogroup App
 *   @{
 **********************************************************************************************************************/

#ifndef COMM_INC_COMM_H_
#define COMM_INC_COMM_H_


/***********************************************************************************************************************
 * INCLUDES NECESSARIOS
 **********************************************************************************************************************/

#include "stdint.h"

/***********************************************************************************************************************
 * DEFINES PUBLICOS
 **********************************************************************************************************************/

// Tamanho máximo do payload
#define dCOMM_PAYLOAD_MAX_SIZE (240U)

// Bytes fixos de todo quadro: SOF + LEN + SEQ + CMD + CRC16
#define dCOMM_FRAME_OVERHEAD (6U)

// Tamanho maximo de um quadro completo
#define dCOMM_FRAME_MAX_SIZE (dCOMM_FRAME_OVERHEAD + dCOMM_PAYLOAD_MAX_SIZE)

// Versao do protocolo PC <-> Jiga
#define dCOMM_PROTOCOL_VERSION (1U)

// Codigos de comando
// Codigos de comando
#define dCOMM_CMD_PING      (0x01U)
#define dCOMM_CMD_RESET_ALL (0x02U)
#define dCOMM_CMD_DAC_SET   (0x10U)

// ACK: CMD de requisicao com o bit 7 ligado
#define dCOMM_ACK_FLAG (0x80U)

// CMD de todo NACK
#define dCOMM_CMD_NACK (0xFFU)

/***********************************************************************************************************************
 * TIPOS DE DADOS PUBLICOS
 **********************************************************************************************************************/

/// @brief Retornos publicos da lib
typedef enum commReturn
{
    eCOMM_RETURN_OK,
    eCOMM_RETURN_INVALID_ARGUMENT,
    eCOMM_RETURN_ERROR,
    eCOMM_RETURN_READING,
    eCOMM_RETURN_FRAME_OK,
    eCOMM_RETURN_CRC_ERROR,
    eCOMM_RETURN_LEN_ERROR,
    eCOMM_RETURN_TIMEOUT,
	eCOMM_RETURN_RESENT,
	eCOMM_RETURN_FORWARD,
	eCOMM_RETURN_IGNORED,

    eCOMM_RETURN_END_ENUM
} commReturn_t;

typedef struct commFrame
{
	uint8_t size;  //1 Byte (0 a 240)
	uint8_t seq;   //1 Byte
	uint8_t cmd;   //1 Byte Código do comando
	uint8_t payload[dCOMM_PAYLOAD_MAX_SIZE];
	uint16_t crc16;

} commFrame_t;

// @brief Codigos de erro do NACK
typedef enum commError
{
	eCOMM_ERROR_CMD_DESCONHECIDO   = 0x01,
	eCOMM_ERROR_PARAMETRO_INVALIDO = 0x02,
	eCOMM_ERROR_NAO_CONFIGURADO    = 0x03,
	eCOMM_ERROR_CRC_INVALIDO       = 0x04,
	eCOMM_ERROR_TAMANHO_EXCEDIDO   = 0x05,
	eCOMM_ERROR_PERIFERICO         = 0x06,
	eCOMM_ERROR_OCUPADO            = 0x07,
	eCOMM_ERROR_ROTINA_INVALIDA    = 0x08,
	eCOMM_ERROR_SEM_RESULTADO      = 0x09,

	eCOMM_ERROR_END_ENUM
} commError_t;

/***********************************************************************************************************************
 * PROTOTIPOS PUBLICOS
 **********************************************************************************************************************/


commReturn_t Comm_Init(void);
commReturn_t Comm_Reset(void);
commReturn_t Comm_Timeout(void);
commReturn_t Comm_ProcessByte(uint8_t byte, commFrame_t *frame);
commReturn_t Comm_BuildFrame(uint8_t seq, uint8_t cmd, const uint8_t *payload, uint8_t payloadSize,
                             uint8_t *buffer, uint16_t bufferSize, uint16_t *frameSize);
commReturn_t Comm_ProcessFrame(const commFrame_t *frame, uint8_t *response, uint16_t responseSize, uint16_t *responseLen);
commReturn_t Comm_BuildResult(uint8_t seq, uint8_t cmd, uint8_t erro, const uint8_t *dados, uint8_t dadosSize,
                              uint8_t *response, uint16_t responseSize, uint16_t *responseLen);

#endif /* COMM_INC_COMM_H_ */
