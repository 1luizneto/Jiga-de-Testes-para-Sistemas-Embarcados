/***********************************************************************************************************************
 *   @file       Onda.h
 *   @{
 **********************************************************************************************************************/

#ifndef EXEC_INC_ONDA_H_
#define EXEC_INC_ONDA_H_

/***********************************************************************************************************************
 * INCLUDES NECESSARIOS
 **********************************************************************************************************************/

#include <stdint.h>

/***********************************************************************************************************************
 * DEFINES PUBLICOS
 **********************************************************************************************************************/

// Passo de rotina ONDA: [0x7A][gerador][tipo][registro][passo us u32][periodo us u32][ciclos u16][modo][ruido u16]
// [base s32][amplitude s32][n][n valores u16]. Valor bruto = base + amplitude x valor / 65535.
#define dONDA_PASSO           (0x7AU)
#define dONDA_CABECALHO_SIZE  (25U)       ///< Parametros antes dos valores (sem o codigo do passo)

#define dONDA_GERADORES       (6U)        ///< Ondas ao mesmo tempo
#define dONDA_PONTOS_MIN      (2U)
#define dONDA_PONTOS_MAX      (32U)
#define dONDA_PASSO_MIN_US    (100U)      ///< Atualizacao por interrupcao (sensores, PWM e DAC lento)
#define dONDA_PASSO_DMA_MIN   (1U)        ///< DAC por DMA: passo de 1 a 99 us
#define dONDA_AMOSTRAS_MAX    (1024U)     ///< Amostras de um periodo no DAC por DMA
#define dONDA_ESCALA          (65535U)

/// Destinos (byte 'tipo')
#define dONDA_TIPO_DAC        (0U)        ///< registro = canal 1-2, valor 0-4095
#define dONDA_TIPO_PWM        (1U)        ///< registro = canal 1-3, valor = razao 0-10000 (frequencia do PWM_SET)
#define dONDA_TIPO_I2C_S16    (2U)        ///< registro = offset no mapa, 2 bytes big-endian com sinal (MPU-6500)
#define dONDA_TIPO_I2C_U16    (3U)        ///< 2 bytes big-endian sem sinal (umidade do BME280)
#define dONDA_TIPO_I2C_U20    (4U)        ///< 20 bits em 3 bytes (msb, lsb, xlsb << 4: temperatura e pressao do BME280)

#define dONDA_MODO_INTERPOLA  (0U)
#define dONDA_MODO_DEGRAU     (1U)

/***********************************************************************************************************************
 * TIPOS DE DADOS PUBLICOS
 **********************************************************************************************************************/

/// @brief Retornos publicos da lib
typedef enum ondaReturn
{
	eONDA_RETURN_OK,
	eONDA_RETURN_INVALID_ARGUMENT,
	eONDA_RETURN_ERROR,

	eONDA_RETURN_END_ENUM
} ondaReturn_t;

/// @brief Funcoes da BSP. As de escrita rodam na interrupcao do gerador e nao podem bloquear.
typedef struct ondaFunc
{
	uint32_t (*agora)(void);                                      ///< Instante em us (TIM5)
	void     (*agenda)(uint32_t alvo);                            ///< Proximo Onda_Tick em 'alvo' (ou logo)
	void     (*desagenda)(void);                                  ///< Sem Onda_Tick
	void     (*dac)(uint8_t canal, uint16_t valor);
	void     (*pwm)(uint8_t canal, uint16_t razao);
	void     (*i2c)(uint8_t offset, const uint8_t *dados, uint8_t quantidade);
	uint8_t  (*dacDma)(uint8_t canal, const uint16_t *amostras, uint16_t quantidade, uint32_t passoUs); ///< 0 = OK
	void     (*dacDmaPara)(uint8_t canal);
} ondaFunc_t;

/***********************************************************************************************************************
 * PROTOTIPOS PUBLICOS
 **********************************************************************************************************************/

ondaReturn_t Onda_Init(const ondaFunc_t *func);
uint16_t     Onda_Tamanho(const uint8_t *parametros, uint16_t disponivel);
uint8_t      Onda_Valida(const uint8_t *parametros, uint16_t size);
ondaReturn_t Onda_Inicia(const uint8_t *parametros, uint16_t size);
void         Onda_Tick(void);
void         Onda_Para(void);
int32_t      Onda_Valor(const uint8_t *parametros, uint32_t t);

#endif /* EXEC_INC_ONDA_H_ */

/** @} DOXYGEN GROUP TAG END OF FILE */
