/***********************************************************************************************************************
 *   @file       UartBus.h
 *   @addtogroup App
 *   @{
 **********************************************************************************************************************/

#ifndef SYSTEM_INC_UARTBUS_H_
#define SYSTEM_INC_UARTBUS_H_

/***********************************************************************************************************************
 * INCLUDES NECESSARIOS
 **********************************************************************************************************************/

#include "stdint.h"

/***********************************************************************************************************************
 * DEFINES PUBLICOS
 **********************************************************************************************************************/

// Faixa de baud aceita (8N1): 1 Mbaud sai exato com o clock de 54 MHz da UART4
#define dUARTBUS_BAUD_MIN (1200U)
#define dUARTBUS_BAUD_MAX (1000000U)

// Buffer circular de recepcao (indices de 8 bits: a volta e automatica)
#define dUARTBUS_TAMANHO_RX (256U)

// Maior escrita e maior leitura: [quantidade][dados] cabe nos 240 bytes do payload
#define dUARTBUS_TAMANHO_MAX (239U)

/***********************************************************************************************************************
 * TIPOS DE DADOS PUBLICOS
 **********************************************************************************************************************/

/// @brief Retornos publicos da lib
typedef enum uartBusReturn
{
	eUARTBUS_RETURN_OK,
	eUARTBUS_RETURN_INVALID_ARGUMENT,
	eUARTBUS_RETURN_NOT_CONFIGURED,   ///< Escrita ou leitura sem UART_CONFIG
	eUARTBUS_RETURN_ERROR,

	eUARTBUS_RETURN_END_ENUM
} uartBusReturn_t;

/// @brief Configura o periferico no baud pedido (8N1) e liga a recepcao
typedef uartBusReturn_t (*uartBusConfigFunc_t)(uint32_t baud);

/// @brief Desliga o periferico: pinos soltos
typedef uartBusReturn_t (*uartBusOffFunc_t)(void);

/// @brief Envia os bytes (bloqueia ate terminar)
typedef uartBusReturn_t (*uartBusWriteFunc_t)(const uint8_t *dados, uint8_t quantidade);

/// @brief Impede ou libera a recepcao (IRQ) enquanto a task mexe no buffer
typedef void (*uartBusLockFunc_t)(void);

/// @brief Funcoes fornecidas pela BSP
typedef struct uartBusFunc
{
	uartBusConfigFunc_t config;
	uartBusOffFunc_t    off;
	uartBusWriteFunc_t  write;
	uartBusLockFunc_t   lock;
	uartBusLockFunc_t   unlock;
} uartBusFunc_t;

/***********************************************************************************************************************
 * PROTOTIPOS PUBLICOS
 **********************************************************************************************************************/

uartBusReturn_t UartBus_Init(const uartBusFunc_t *func);
uartBusReturn_t UartBus_Config(uint32_t baud);
uartBusReturn_t UartBus_Write(const uint8_t *dados, uint8_t quantidade);
uartBusReturn_t UartBus_Read(uint8_t maximo, uint8_t *dados, uint8_t *quantidade);
uartBusReturn_t UartBus_SafeState(void);

// Evento da recepcao: chamado pela BSP, dentro da ISR da UART
void UartBus_ByteRecebido(uint8_t byte);

#endif /* SYSTEM_INC_UARTBUS_H_ */

/** @} DOXYGEN GROUP TAG END OF FILE */
