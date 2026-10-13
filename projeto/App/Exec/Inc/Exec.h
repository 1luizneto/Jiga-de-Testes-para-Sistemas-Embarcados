/***********************************************************************************************************************
 *   @file       Exec.h
 *   @addtogroup App
 *   @{
 **********************************************************************************************************************/

#ifndef EXEC_INC_EXEC_H_
#define EXEC_INC_EXEC_H_

/***********************************************************************************************************************
 * INCLUDES NECESSARIOS
 **********************************************************************************************************************/

#include "stdint.h"
#include "Comm.h"

/***********************************************************************************************************************
 * TIPOS DE DADOS PUBLICOS
 **********************************************************************************************************************/

/// @brief Retornos publicos da lib
typedef enum execReturn
{
    eEXEC_RETURN_OK,
    eEXEC_RETURN_INVALID_ARGUMENT,
    eEXEC_RETURN_ERROR,

    eEXEC_RETURN_END_ENUM
} execReturn_t;

/// @brief Resultado de um comando executado: a CommTask monta o ACK ou NACK
typedef struct execResult
{
    uint8_t seq;                            ///< SEQ do comando
    uint8_t cmd;                            ///< CMD do comando
    uint8_t erro;                           ///< 0 = sucesso; senao, codigo commError_t do NACK
    uint8_t size;                           ///< Bytes validos em dados
    uint8_t dados[dCOMM_PAYLOAD_MAX_SIZE];  ///< Dados de retorno
} execResult_t;

/***********************************************************************************************************************
 * PROTOTIPOS PUBLICOS
 **********************************************************************************************************************/

execReturn_t Exec_Process(const commFrame_t *frame, execResult_t *result);
execReturn_t Exec_SafeState(void);

#endif /* EXEC_INC_EXEC_H_ */

/** @} DOXYGEN GROUP TAG END OF FILE */
