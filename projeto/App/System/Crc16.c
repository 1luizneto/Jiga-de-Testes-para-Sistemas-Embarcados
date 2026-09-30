/*******************************************************************************************************
 *   @file         CRC16.c
 *   @addtogroup   System
 *   @brief        Lib para o cálculo do CRC16 tanto por software como por hardware.
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Ferramentas:</b>
 *   - Generic.
 *
 *   \n <b>Dependencias:</b>
 *   - Bsp;
 *
 *   \n <b>Observacoes:</b>
 *   - None.
 *
 *   Changelog
 *   @version      <b>1.0.0 - 04/05/2026</b> \n Luiz Neto \n Primeira versao
 *
 *   @copyright    --
 *   @{
 ********************************************************************************************************/

 /********************************************************************************************************
 *   INCLUDES
 ********************************************************************************************************/

#include "Crc16.h"

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/


/***********************************************************************************************************************
 * TIPOS LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief Estrutura de manipulacao geral da biblioteca
 *
 **********************************************************************************************************************/
static struct crc
{
    /// @brief Estrutura com os ponteiros de funções necessárias para a lib
    struct functions
    {
        void (*configCRC)(uint16_t initialValue);
        void (*crcInputData)(uint8_t data);
        void (*crcGetResult)(uint16_t *result);
    } functions;

} crc;

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/


/***********************************************************************************************************************
 *   FUNCOES PUBLICAS
 **********************************************************************************************************************/

crc16Return_t Crc16_Init(void (*configCRC)(uint16_t initialValue), void (*crcInputData)(uint8_t data), void (*crcGetResult)(uint16_t *result))
{
    crc.functions.configCRC    = configCRC;
    crc.functions.crcInputData = crcInputData;
    crc.functions.crcGetResult = crcGetResult;

    return eCRC16_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Calcula CRC-16 (Modbus) sobre um bloco de dados.
 * @details    Utiliza o polinomio 0xA001 (reflexo de 0x8005) com valor inicial
 *             0xFFFF.
 * @param[in]  data    Ponteiro para os dados de entrada
 * @param[in]  length  Numero de bytes
 * @param[in]  output  Ponteiro para o resultado do calculo do crc16
 * @return     CRCReturn_t
 **********************************************************************************************************************/
crc16Return_t Crc16_Calcule(const uint8_t *data, uint16_t length, uint16_t *output, uint16_t initialValue)
{

#if (dCRC_USE_HARDWARE == dFALSE)

    uint16_t crc = 0xFFFF;
    uint16_t i;

    for (i = 0; i < length; i++)
    {
        crc ^= (uint16_t)data[i];
        uint8_t bit;
        for (bit = 0; bit < 8; bit++)
        {
            if (crc & 0x0001)
            {
                crc = (crc >> 1) ^ dCRC_POLINOMIO;
            }
            else
            {
                crc = crc >> 1;
            }
        }
    }

    *output = crc;

    return eCRC16_RETURN_OK;
#else

    crc.functions.configCRC(initialValue);

    for (uint16_t i = 0; i < length; i++)
    {
        crc.functions.crcInputData(data[i]);
    }

    uint16_t result;
    crc.functions.crcGetResult(&result);

    *output = result;

    return eCRC16_RETURN_OK;

#endif

}

/***********************************************************************************************************************
 *   FUNCOES LOCAIS
 **********************************************************************************************************************/




