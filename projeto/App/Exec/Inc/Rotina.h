/***********************************************************************************************************************
 *   @file       Rotina.h
 *   @addtogroup App
 *   @{
 **********************************************************************************************************************/

#ifndef EXEC_INC_ROTINA_H_
#define EXEC_INC_ROTINA_H_

/***********************************************************************************************************************
 * INCLUDES NECESSARIOS
 **********************************************************************************************************************/

#include "stdint.h"

/***********************************************************************************************************************
 * DEFINES PUBLICOS
 **********************************************************************************************************************/

// Passos que so existem dentro de uma rotina (dependem de tempo)
#define dROTINA_PASSO_ESPERA      (0x78U)
#define dROTINA_PASSO_ESPERA_GPIO (0x79U)

// Cabecalho do RUN_ROUTINE: identificador (u16) + tempo maximo em ms (u32) + numero de passos (u8)
#define dROTINA_CABECALHO_SIZE (7U)

// Tempo maximo aceito (1 h): em us, cabe com folga nos 32 bits do temporizador (~71 min ate dar a volta)
#define dROTINA_TEMPO_MAX_MS (3600000U)

// Area de resultados e cabecalho de cada registro: indice (u8) + instante em us (u32) + quantidade (u8)
#define dROTINA_RESULTADOS_SIZE    (2048U)
#define dROTINA_REGISTRO_CABECALHO (6U)

// Resposta do ROUTINE_STATUS: estado (u8) + passos concluidos (u8) + identificador (u16) + codigo de erro (u8)
#define dROTINA_STATUS_SIZE (5U)

// Resposta do READ_RESULT: identificador (u16) + tamanho total (u16) + ate 236 bytes de resultados
#define dROTINA_LEITURA_CABECALHO (4U)
#define dROTINA_LEITURA_DADOS_MAX (236U)

// Indice informado no NACK ROTINA_INVALIDA quando o erro nao e de um passo (cabecalho ou bytes sobrando)
#define dROTINA_INDICE_CABECALHO (0xFFU)

// Entradas do ESPERA_GPIO, com a numeracao da monografia (GPIO5 a GPIO8)
#define dROTINA_ENTRADA_MIN (5U)
#define dROTINA_ENTRADA_MAX (8U)

/***********************************************************************************************************************
 * TIPOS DE DADOS PUBLICOS
 **********************************************************************************************************************/

/// @brief Retornos publicos da lib
typedef enum rotinaReturn
{
	eROTINA_RETURN_OK,
	eROTINA_RETURN_INVALID_ARGUMENT,
	eROTINA_RETURN_INVALID_ROUTINE,   ///< Rotina recusada na validacao (NACK ROTINA_INVALIDA)
	eROTINA_RETURN_BUSY,              ///< Ja ha uma rotina em execucao
	eROTINA_RETURN_NO_RESULT,         ///< READ_RESULT sem rotina finalizada (NACK SEM_RESULTADO)
	eROTINA_RETURN_ERROR,

	eROTINA_RETURN_END_ENUM
} rotinaReturn_t;

/// @brief Estados informados pelo ROUTINE_STATUS (Apendice A)
typedef enum rotinaEstado
{
	eROTINA_ESTADO_OCIOSA         = 0,
	eROTINA_ESTADO_EM_EXECUCAO    = 1,
	eROTINA_ESTADO_CONCLUIDA      = 2,
	eROTINA_ESTADO_ABORTADA       = 3,
	eROTINA_ESTADO_TEMPO_ESGOTADO = 4,
	eROTINA_ESTADO_ERRO           = 5,

	eROTINA_ESTADO_END_ENUM
} rotinaEstado_t;

/// @brief Por que a espera da BSP terminou
typedef enum rotinaEvento
{
	eROTINA_EVENTO_TEMPO,     ///< O instante alvo chegou
	eROTINA_EVENTO_ENTRADA,   ///< A entrada monitorada assumiu o nivel esperado
	eROTINA_EVENTO_ACORDOU,   ///< Acordada por outro motivo (ABORT): a Rotina confere e decide

	eROTINA_EVENTO_END_ENUM
} rotinaEvento_t;

/// @brief Instante atual da base de tempo, em us (32 bits, volta a 0 depois de ~71 min)
typedef uint32_t (*rotinaAgoraFunc_t)(void);

/// @brief Bloqueia ate o instante alvo, ate a entrada (1 a 4; 0 = nenhuma) assumir o nivel, ou ate ser acordada.
///        Se a entrada ja estiver no nivel, retorna na hora. 'instante' recebe o momento em que o nivel ocorreu.
typedef rotinaEvento_t (*rotinaEsperaFunc_t)(uint32_t alvo, uint8_t entrada, uint8_t nivel, uint32_t *instante);

/// @brief Interrompe a espera em andamento (chamada pela CommTask, no ABORT)
typedef void (*rotinaAcordaFunc_t)(void);

/// @brief Funcoes fornecidas pela BSP
typedef struct rotinaFunc
{
	rotinaAgoraFunc_t  agora;
	rotinaEsperaFunc_t espera;
	rotinaAcordaFunc_t acorda;
} rotinaFunc_t;

/***********************************************************************************************************************
 * PROTOTIPOS PUBLICOS
 **********************************************************************************************************************/

rotinaReturn_t Rotina_Init(const rotinaFunc_t *func);
rotinaReturn_t Rotina_Carrega(const uint8_t *payload, uint8_t size, uint8_t *indice);
rotinaReturn_t Rotina_Executa(void);
rotinaReturn_t Rotina_Aborta(void);
rotinaReturn_t Rotina_Limpa(void);
rotinaReturn_t Rotina_Status(uint8_t *dados, uint8_t *size);
rotinaReturn_t Rotina_LeResultado(uint16_t offset, uint8_t *dados, uint8_t *size);
uint8_t        Rotina_EmExecucao(void);

#endif /* EXEC_INC_ROTINA_H_ */

/** @} DOXYGEN GROUP TAG END OF FILE */
