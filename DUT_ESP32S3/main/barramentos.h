/***********************************************************************************************************************
 *   @file       barramentos.h
 *   @{
 **********************************************************************************************************************/

#ifndef MAIN_BARRAMENTOS_H_
#define MAIN_BARRAMENTOS_H_

/***********************************************************************************************************************
 * INCLUDES NECESSARIOS
 **********************************************************************************************************************/

#include "comandos.h"

/***********************************************************************************************************************
 * DEFINES PUBLICOS
 **********************************************************************************************************************/

// Ligacao com a jiga (NUCLEO-F767ZI). GND comum; tudo em 3,3 V
#define dDUT_PINO_UART_TX   (17)   ///< UART1 TX -> PD0 (UART4 RX da jiga)
#define dDUT_PINO_UART_RX   (18)   ///< UART1 RX <- PD1 (UART4 TX da jiga)
#define dDUT_PINO_I2C_SDA   (8)    ///< <-> PF0 (I2C2 SDA), pull-up de 4,7 kOhm para o 3V3
#define dDUT_PINO_I2C_SCL   (9)    ///< <-> PF1 (I2C2 SCL), pull-up de 4,7 kOhm para o 3V3
#define dDUT_PINO_SPI_CS    (10)   ///< <-> PD7 (CS por software na jiga)
#define dDUT_PINO_SPI_MOSI  (11)   ///< <-> PB15
#define dDUT_PINO_SPI_SCK   (12)   ///< <-> PB10
#define dDUT_PINO_SPI_MISO  (13)   ///< <-> PC2

// Velocidade do mestre I2C (a jiga escrava aceita 100 e 400 kHz)
#define dDUT_I2C_FREQUENCIA (100000U)

/***********************************************************************************************************************
 * PROTOTIPOS PUBLICOS
 **********************************************************************************************************************/

const dutBarramentos_t *Barramentos_Init(void);

#endif /* MAIN_BARRAMENTOS_H_ */

/** @} DOXYGEN GROUP TAG END OF FILE */
