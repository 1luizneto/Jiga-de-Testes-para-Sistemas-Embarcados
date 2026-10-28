/***********************************************************************************************************************
 *   @file       SpiBus.h
 *   @addtogroup App
 *   @{
 **********************************************************************************************************************/

#ifndef SYSTEM_INC_SPIBUS_H_
#define SYSTEM_INC_SPIBUS_H_

/***********************************************************************************************************************
 * INCLUDES NECESSARIOS
 **********************************************************************************************************************/

#include "stdint.h"

/***********************************************************************************************************************
 * DEFINES PUBLICOS
 **********************************************************************************************************************/

// Papel da jiga no barramento (SPI_CONFIG)
#define dSPIBUS_PAPEL_ESCRAVO (0U)
#define dSPIBUS_PAPEL_MESTRE  (1U)

// Modos 0 a 3: bit 1 = CPOL, bit 0 = CPHA
#define dSPIBUS_MODO_MAX (3U)

// Divisores do clock do SPI (potencias de 2)
#define dSPIBUS_DIVISOR_MIN (2U)
#define dSPIBUS_DIVISOR_MAX (256U)

// Buffers de transmissao e recepcao do escravo e maior transferencia do mestre: [quantidade][dados] cabe nos
// 240 bytes do payload
#define dSPIBUS_TAMANHO_BUFFER (239U)

// Byte enviado pelo escravo depois do fim do buffer de transmissao
#define dSPIBUS_BYTE_VAZIO (0xFFU)

/***********************************************************************************************************************
 * TIPOS DE DADOS PUBLICOS
 **********************************************************************************************************************/

/// @brief Retornos publicos da lib
typedef enum spiBusReturn
{
	eSPIBUS_RETURN_OK,
	eSPIBUS_RETURN_INVALID_ARGUMENT,
	eSPIBUS_RETURN_NOT_CONFIGURED,   ///< Transferencia de mestre sem o papel de mestre configurado
	eSPIBUS_RETURN_ERROR,

	eSPIBUS_RETURN_END_ENUM
} spiBusReturn_t;

/// @brief Configura o periferico no papel e no modo pedidos; divisor do clock so como mestre (0 como escravo)
typedef spiBusReturn_t (*spiBusConfigFunc_t)(uint8_t papel, uint8_t modo, uint16_t divisor);

/// @brief Desliga o periferico: pinos soltos e CS (PD7) em nivel alto
typedef spiBusReturn_t (*spiBusOffFunc_t)(void);

/// @brief Transferencia como mestre, com o CS em nivel baixo: envia e recebe a mesma quantidade
typedef spiBusReturn_t (*spiBusXferFunc_t)(const uint8_t *escrita, uint8_t *leitura, uint8_t quantidade);

/// @brief Impede ou libera os eventos do escravo (IRQs) enquanto a task mexe nos buffers
typedef void (*spiBusLockFunc_t)(void);

/// @brief Funcoes fornecidas pela BSP
typedef struct spiBusFunc
{
	spiBusConfigFunc_t config;
	spiBusOffFunc_t    off;
	spiBusXferFunc_t   xfer;
	spiBusLockFunc_t   lock;
	spiBusLockFunc_t   unlock;
} spiBusFunc_t;

/***********************************************************************************************************************
 * PROTOTIPOS PUBLICOS
 **********************************************************************************************************************/

spiBusReturn_t SpiBus_Init(const spiBusFunc_t *func, uint32_t clockHz);
spiBusReturn_t SpiBus_Divisor(uint32_t clockHz, uint32_t frequencia, uint16_t *divisor);
spiBusReturn_t SpiBus_Config(uint8_t papel, uint8_t modo, uint32_t frequencia);
spiBusReturn_t SpiBus_Load(const uint8_t *dados, uint8_t quantidade);
spiBusReturn_t SpiBus_Read(uint8_t *dados, uint8_t *quantidade);
spiBusReturn_t SpiBus_MasterXfer(const uint8_t *escrita, uint8_t *leitura, uint8_t quantidade);
spiBusReturn_t SpiBus_SafeState(void);

// Eventos do escravo: chamados pela BSP, dentro da ISR (SPI2 ou EXTI do CS)
void SpiBus_PreparaTx(uint8_t tx[dSPIBUS_TAMANHO_BUFFER]);
void SpiBus_FimTransacao(const uint8_t *rx, uint16_t quantidade);

#endif /* SYSTEM_INC_SPIBUS_H_ */

/** @} DOXYGEN GROUP TAG END OF FILE */
