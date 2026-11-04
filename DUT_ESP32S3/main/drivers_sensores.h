/***********************************************************************************************************************
 *   @file       drivers_sensores.h
 *   @{
 **********************************************************************************************************************/

#ifndef MAIN_DRIVERS_SENSORES_H_
#define MAIN_DRIVERS_SENSORES_H_

/***********************************************************************************************************************
 * INCLUDES NECESSARIOS
 **********************************************************************************************************************/

#include <stddef.h>
#include <stdint.h>
#include "comandos.h"

/***********************************************************************************************************************
 * DEFINES PUBLICOS
 **********************************************************************************************************************/

#define dSENSORES_MPU_ENDERECO (0x68U)
#define dSENSORES_BME_ENDERECO (0x76U)

/***********************************************************************************************************************
 * TIPOS DE DADOS PUBLICOS
 **********************************************************************************************************************/

/// @brief Transferencia I2C como mestre (a mesma de dutBarramentos_t)
typedef dutReturn_t (*sensoresI2cFunc_t)(uint8_t endereco, const uint8_t *escrita, size_t nw, uint8_t *leitura,
                                         size_t nr);

/// @brief Registradores por SPI (a mesma de dutBarramentos_t): bit 7 do endereco = leitura
typedef dutReturn_t (*sensoresSpiFunc_t)(uint8_t modo, uint32_t frequencia, uint8_t endereco, const uint8_t *escrita,
                                         uint8_t *leitura, size_t quantidade);

/// @brief Onde o sensor esta: I2C (i2c != NULL, endereco) ou SPI (spi != NULL, modo e frequencia)
typedef struct sensoresBarramento
{
	sensoresI2cFunc_t i2c;
	uint8_t           endereco;
	sensoresSpiFunc_t spi;
	uint8_t           modo;
	uint32_t          frequencia;
} sensoresBarramento_t;

/// @brief Leitura do MPU-6500 em unidades fisicas (fundo de escala de 2 g e 250 graus/s)
typedef struct sensoresMpu
{
	float aceleracao[3];   ///< g
	float giro[3];         ///< graus/s
	float temperatura;     ///< graus C
} sensoresMpu_t;

/// @brief Leitura do BME280 compensada
typedef struct sensoresBme
{
	float temperatura;     ///< graus C
	float pressao;         ///< hPa
	float umidade;         ///< %
} sensoresBme_t;

/***********************************************************************************************************************
 * PROTOTIPOS PUBLICOS
 **********************************************************************************************************************/

dutReturn_t Sensores_LeMpu(const sensoresBarramento_t *barramento, sensoresMpu_t *leitura);
dutReturn_t Sensores_LeBme(const sensoresBarramento_t *barramento, sensoresBme_t *leitura);

#endif /* MAIN_DRIVERS_SENSORES_H_ */

/** @} DOXYGEN GROUP TAG END OF FILE */
