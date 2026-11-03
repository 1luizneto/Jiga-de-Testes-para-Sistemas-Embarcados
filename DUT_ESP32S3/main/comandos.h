/***********************************************************************************************************************
 *   @file       comandos.h
 *   @{
 **********************************************************************************************************************/

#ifndef MAIN_COMANDOS_H_
#define MAIN_COMANDOS_H_

/***********************************************************************************************************************
 * INCLUDES NECESSARIOS
 **********************************************************************************************************************/

#include <stddef.h>
#include <stdint.h>

/***********************************************************************************************************************
 * DEFINES PUBLICOS
 **********************************************************************************************************************/

// Maior transferencia: o mesmo limite dos buffers da jiga ([quantidade][dados] nos 240 bytes do payload)
#define dCOMANDOS_DADOS_MAX (239U)

// Maior linha de comando (SPIM com 239 bytes em hex ~ 495 caracteres) e maior resposta ("OK " + 478 + "\n")
#define dCOMANDOS_LINHA_MAX    (512U)
#define dCOMANDOS_RESPOSTA_MAX (512U)

// Enderecos I2C de 7 bits validos (0x00-0x07 e 0x78-0x7F sao reservados pelo padrao)
#define dCOMANDOS_I2C_ENDERECO_MIN (0x08U)
#define dCOMANDOS_I2C_ENDERECO_MAX (0x77U)

// SPI: modos 0 a 3 e faixa do clock como mestre (a jiga escrava reage na ISR: ate ~1 MHz e o recomendado)
#define dCOMANDOS_SPI_MODO_MAX        (3U)
#define dCOMANDOS_SPI_FREQUENCIA_MIN  (1000U)
#define dCOMANDOS_SPI_FREQUENCIA_MAX  (10000000U)

/***********************************************************************************************************************
 * TIPOS DE DADOS PUBLICOS
 **********************************************************************************************************************/

/// @brief Resultado das operacoes de barramento (viram o texto da resposta)
typedef enum dutReturn
{
	eDUT_RETURN_OK,
	eDUT_RETURN_INVALID_ARGUMENT,   ///< "ERR ARG"
	eDUT_RETURN_NACK,               ///< "ERR NACK": dispositivo I2C nao respondeu
	eDUT_RETURN_BUSY,               ///< "ERR ESTADO": barramento ocupado no outro papel (mandar OFF antes)
	eDUT_RETURN_ERROR,              ///< "ERR PERIFERICO": falha do driver

	eDUT_RETURN_END_ENUM
} dutReturn_t;

/// @brief Operacoes de barramento fornecidas por barramentos.c (no PC, pelos falsos do test_host)
typedef struct dutBarramentos
{
	dutReturn_t (*desliga)(void);
	dutReturn_t (*i2cMestre)(uint8_t endereco, const uint8_t *escrita, size_t nw, uint8_t *leitura, size_t nr);
	dutReturn_t (*i2cEscrava)(uint8_t endereco);
	dutReturn_t (*i2cCarrega)(uint8_t offset, const uint8_t *dados, size_t quantidade);
	dutReturn_t (*i2cLe)(uint8_t offset, uint8_t *dados, size_t quantidade);
	dutReturn_t (*spiMestre)(uint8_t modo, uint32_t frequencia, const uint8_t *escrita, uint8_t *leitura, size_t quantidade);
	dutReturn_t (*spiEscrava)(uint8_t modo);
	dutReturn_t (*spiCarrega)(const uint8_t *dados, size_t quantidade);
	dutReturn_t (*spiLe)(uint8_t *dados, size_t *quantidade);
} dutBarramentos_t;

/***********************************************************************************************************************
 * PROTOTIPOS PUBLICOS
 **********************************************************************************************************************/

void Comandos_Init(const dutBarramentos_t *barramentos);
size_t Comandos_Executa(const char *linha, char *resposta, size_t tamanho);

#endif /* MAIN_COMANDOS_H_ */

/** @} DOXYGEN GROUP TAG END OF FILE */
