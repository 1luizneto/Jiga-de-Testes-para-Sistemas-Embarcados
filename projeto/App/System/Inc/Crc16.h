/***********************************************************************************************************************
 *   @file CRC16.h
 *   @addtogroup System
 *   @{
 **********************************************************************************************************************/

#ifndef SYSTEM_INC_CRC16_H_
#define SYSTEM_INC_CRC16_H_

 /********************************************************************************************************
 *   INCLUDES
 ********************************************************************************************************/

#include <stdint.h>

/***********************************************************************************************************************
 * DEFINES PUBLICOS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @addtogroup CRC_Setup
 *  @brief      Seleciona se utiliza o periférico CRC16 por hardware ou software.
 *  @param      dTRUE  - Utiliza o periferico CRC16 interno do STM32F767ZI
 *  @param      dFALSE - Utiliza calculo por software
 *  @{
**********************************************************************************************************************/

#define dCRC_USE_HARDWARE (1U)

/** @} CRC_Setup */

/***********************************************************************************************************************
 * @addtogroup CRC_Config
 *  @brief      Definas as configs padroes da lib.
 *  @{
**********************************************************************************************************************/

/// @brief Define o tamanho do CRC que tem que ser extraido
#define dCRC_SIZE (2U)

/// @brief Define o valor inicial do CRC segundo a norma
#define dCRC_INITIAL_VALUE (0xFFFFU)

/// @brief Define a operacao de deslocamento para o CRC
#define dCRC_LOW(crc) ((u8)((crc) & 0xFF))

/// @brief Define a operacao de deslocamento para o CRC
#define dCRC_HIGH(crc) ((u8)((crc) >> 8))

/// @brief Polinomio usado no calculo do CRC-16.
#define dCRC_POLINOMIO (0xA001)

/** @} CRC_Config */

/********************************************************************************************************
 *   TIPOS DE DADOS PUBLICOS
 ********************************************************************************************************/

/// @brief Retornos para controle de erros das funcos de CRC16
typedef enum crc16Return_t
{
    eCRC16_RETURN_OK,
    eCRC16_RETURN_INVALID_ARGUMENT,
    eCRC16_RETURN_ERROR,
    eCRC16_RETURN_IDLE,

    eCRC16_RETURN_END_ENUM
} crc16Return_t;


/***********************************************************************************************************************
 * PROTOTIPOS PUBLICOS
 **********************************************************************************************************************/

crc16Return_t Crc16_Init(void (*configCRC)(uint16_t initialValue), void (*crcInputData)(uint8_t data), void (*crcGetResult)(uint16_t *result));
crc16Return_t Crc16_Calcule(const uint8_t *data, uint16_t length, uint16_t *output, uint16_t initialValue);

#endif /* SYSTEM_INC_CRC16_H_*/
