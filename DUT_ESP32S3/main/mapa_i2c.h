/***********************************************************************************************************************
 *   @file       mapa_i2c.h
 *   @{
 **********************************************************************************************************************/

#ifndef MAIN_MAPA_I2C_H_
#define MAIN_MAPA_I2C_H_

/***********************************************************************************************************************
 * INCLUDES NECESSARIOS
 **********************************************************************************************************************/

#include <stddef.h>
#include <stdint.h>

/***********************************************************************************************************************
 * DEFINES PUBLICOS
 **********************************************************************************************************************/

// Registradores do mapa (o ponteiro e de 8 bits: depois de 0xFF volta a 0x00)
#define dMAPAI2C_TAMANHO (256U)

/***********************************************************************************************************************
 * TIPOS DE DADOS PUBLICOS
 **********************************************************************************************************************/

/// @brief Retornos publicos da lib
typedef enum mapaI2cReturn
{
	eMAPAI2C_RETURN_OK,
	eMAPAI2C_RETURN_INVALID_ARGUMENT,

	eMAPAI2C_RETURN_END_ENUM
} mapaI2cReturn_t;

/***********************************************************************************************************************
 * PROTOTIPOS PUBLICOS
 **********************************************************************************************************************/

void MapaI2c_Init(void);
mapaI2cReturn_t MapaI2c_Carrega(uint8_t offset, const uint8_t *dados, size_t quantidade);
mapaI2cReturn_t MapaI2c_Le(uint8_t offset, uint8_t *dados, size_t quantidade);

// Eventos do barramento (escrita e leitura do mestre)
void MapaI2c_Escrita(const uint8_t *dados, size_t quantidade);
void MapaI2c_Leitura(uint8_t *dados, size_t quantidade);

#endif /* MAIN_MAPA_I2C_H_ */

/** @} DOXYGEN GROUP TAG END OF FILE */
