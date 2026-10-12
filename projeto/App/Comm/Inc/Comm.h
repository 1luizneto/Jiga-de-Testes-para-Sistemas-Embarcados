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

/***********************************************************************************************************************
 * PROTOTIPOS PUBLICOS
 **********************************************************************************************************************/


commReturn_t Comm_Init(void);
commReturn_t Comm_Reset(void);
commReturn_t Comm_Timeout(void);
commReturn_t Comm_ProcessByte(uint8_t byte, commFrame_t *frame);

#endif /* COMM_INC_COMM_H_ */
