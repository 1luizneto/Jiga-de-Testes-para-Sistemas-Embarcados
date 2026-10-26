/***********************************************************************************************************************
 *   @file       I2cBus.h
 *   @addtogroup App
 *   @{
 **********************************************************************************************************************/

#ifndef SYSTEM_INC_I2CBUS_H_
#define SYSTEM_INC_I2CBUS_H_

/***********************************************************************************************************************
 * INCLUDES NECESSARIOS
 **********************************************************************************************************************/

#include "stdint.h"

/***********************************************************************************************************************
 * DEFINES PUBLICOS
 **********************************************************************************************************************/

// Papel da jiga no barramento (I2C_CONFIG)
#define dI2CBUS_PAPEL_ESCRAVO (0U)
#define dI2CBUS_PAPEL_MESTRE  (1U)

// Enderecos de 7 bits validos: 0x00-0x07 e 0x78-0x7F sao reservados pelo padrao I2C
#define dI2CBUS_ENDERECO_MIN (0x08U)
#define dI2CBUS_ENDERECO_MAX (0x77U)

// Velocidade: 0 = 100 kHz (Standard), 1 = 400 kHz (Fast)
#define dI2CBUS_VELOCIDADE_MAX (1U)

// Mapa de registradores emulado no papel de escravo
#define dI2CBUS_TAMANHO_MAPA (256U)

/***********************************************************************************************************************
 * TIPOS DE DADOS PUBLICOS
 **********************************************************************************************************************/

/// @brief Retornos publicos da lib
typedef enum i2cBusReturn
{
	eI2CBUS_RETURN_OK,
	eI2CBUS_RETURN_INVALID_ARGUMENT,
	eI2CBUS_RETURN_NOT_CONFIGURED,   ///< Transferencia de mestre sem o papel de mestre configurado
	eI2CBUS_RETURN_NACK,             ///< O dispositivo enderecado nao respondeu (falta de ACK)
	eI2CBUS_RETURN_ERROR,

	eI2CBUS_RETURN_END_ENUM
} i2cBusReturn_t;

/// @brief Configura o periferico no papel pedido e, como escravo, liga a escuta do endereco
typedef i2cBusReturn_t (*i2cBusConfigFunc_t)(uint8_t papel, uint8_t endereco, uint8_t velocidade);

/// @brief Desliga o periferico: barramento livre, pinos soltos
typedef i2cBusReturn_t (*i2cBusOffFunc_t)(void);

/// @brief Transferencia como mestre: escreve nw bytes e depois le nr bytes (repeated start entre as duas)
typedef i2cBusReturn_t (*i2cBusXferFunc_t)(uint8_t endereco, const uint8_t *escrita, uint8_t nw, uint8_t *leitura,
                                           uint8_t nr);

/// @brief Impede ou libera os eventos do barramento (IRQ de evento) enquanto a task mexe no mapa
typedef void (*i2cBusLockFunc_t)(void);

/// @brief Funcoes fornecidas pela BSP
typedef struct i2cBusFunc
{
	i2cBusConfigFunc_t config;
	i2cBusOffFunc_t    off;
	i2cBusXferFunc_t   xfer;
	i2cBusLockFunc_t   lock;
	i2cBusLockFunc_t   unlock;
} i2cBusFunc_t;

/***********************************************************************************************************************
 * PROTOTIPOS PUBLICOS
 **********************************************************************************************************************/

i2cBusReturn_t I2cBus_Init(const i2cBusFunc_t *func);
i2cBusReturn_t I2cBus_Config(uint8_t papel, uint8_t endereco, uint8_t velocidade);
i2cBusReturn_t I2cBus_Load(uint8_t offset, uint8_t quantidade, const uint8_t *dados);
i2cBusReturn_t I2cBus_Dump(uint8_t offset, uint8_t quantidade, uint8_t *dados);
i2cBusReturn_t I2cBus_MasterXfer(uint8_t endereco, const uint8_t *escrita, uint8_t nw, uint8_t *leitura, uint8_t nr);
i2cBusReturn_t I2cBus_SafeState(void);

// Eventos do barramento no papel de escravo: chamados pela BSP, dentro da ISR do I2C
void I2cBus_InicioEscrita(void);
void I2cBus_ByteRecebido(uint8_t byte);
uint8_t I2cBus_ProximoByte(void);
void I2cBus_FimLeitura(void);

#endif /* SYSTEM_INC_I2CBUS_H_ */

/** @} DOXYGEN GROUP TAG END OF FILE */
