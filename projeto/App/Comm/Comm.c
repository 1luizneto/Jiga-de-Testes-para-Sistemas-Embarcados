/***********************************************************************************************************************
 *   @file         Comm.c
 *   @addtogroup   App/Comm
 *   @brief        Arquivo que implementa o protocolo de comunicacao
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Ferramentas:</b>
 *   - Generic.
 *
 *   \n <b>Dependencias:</b>
 *   - Crc16.
 *
 *   \n <b>Observacoes:</b>
 *   - Quadro: SOF(0x7E) | LEN | SEQ | CMD | PAYLOAD(0 a 240) | CRC16 (little-endian).
 *   - CRC calculado sobre LEN + SEQ + CMD + PAYLOAD.
 *
 *   Changelog
 *   @version      <b>1.0.0 - 01/10/2026</b> \n Luiz Neto \n Primeira versao
 *   @version      <b>1.1.0 - 12/10/2026</b> \n Luiz Neto \n Receptor de quadros (Comm_ProcessByte): maquina de
 *                 estados, validacao por CRC, descarte de LEN > 240 e timeout de pacote (Comm_Timeout)
 *   @version      <b>1.2.0 - 13/10/2026</b> \n Luiz Neto \n Respostas ao host: Comm_BuildFrame monta o quadro e
 *                 Comm_ProcessFrame decide ACK/NACK, com reenvio da ultima resposta quando o SEQ se repete
 *   @version      <b>1.3.0 - 13/10/2026</b> \n Luiz Neto \n Comandos alem do PING sao repassados (FORWARD) e
 *                 respondidos pelo Comm_BuildResult; um comando pendente por vez (retransmissao ignorada,
 *                 outro comando recebe NACK OCUPADO)
 *
 *   @copyright
 *   @{
 **********************************************************************************************************************/


/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include "Comm.h"
#include "Crc16.h"
#include <stddef.h>
#include <string.h>
#include "Version.h"

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

/// Byte de inicio de quadro
#define dCOMM_SOF (0x7EU)

/// Tamanho do cabecalho: LEN + SEQ + CMD
#define dCOMM_HEADER_SIZE (3U)

/// Tamanho do buffer bruto: cabecalho + payload maximo (sem SOF e sem CRC)
#define dCOMM_RAW_MAX_SIZE (dCOMM_HEADER_SIZE + dCOMM_PAYLOAD_MAX_SIZE)

/// Posicoes dos campos do cabecalho dentro do buffer bruto
#define dCOMM_IDX_LEN (0U)
#define dCOMM_IDX_SEQ (1U)
#define dCOMM_IDX_CMD (2U)

// Deslocamento dos campos no quadro de saída: o buffer comeca pelo SOF
#define dCOMM_FRAME_OFFSET (1U)

/***********************************************************************************************************************
 * TIPOS LOCAIS
 **********************************************************************************************************************/

typedef enum commState
{
	eCOMM_STATE_AGUARDA_SOF,
	eCOMM_STATE_LE_CABECALHO,
	eCOMM_STATE_LE_PAYLOAD,
	eCOMM_STATE_VALIDA_CRC,

	eCOMM_STATE_END_ENUM
} commState_t;

static struct comm
{
	commState_t commState;

	/// LEN, SEQ, CMD e PAYLOAD recebidos, na ordem em que chegam
	uint8_t raw[dCOMM_RAW_MAX_SIZE];

	/// Quantidade de bytes ja recebidos no estado atual
	uint16_t count;

	/// CRC recebido no quadro (little-endian)
	uint16_t crcReceived;

	// Ultima resposta montada e o SEQ dela: se o PC repetir o SEQ (retransmissao), reenvia sem executar
	uint8_t lastResponse[dCOMM_FRAME_MAX_SIZE];
	uint16_t lastResponseSize;
	uint8_t  lastSeq;
	uint8_t  hasLastResponse;
	/// Comando repassado para execucao e ainda sem resultado (um por vez)
	uint8_t  pendingSeq;
	uint8_t  hasPending;

} comm;

/***********************************************************************************************************************
 * VARIAVEIS LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static commReturn_t Comm_ValidaQuadro(commFrame_t *frame);
static commReturn_t Comm_RespondePing(uint8_t seq, uint8_t *response, uint16_t responseSize, uint16_t *responseLen);
static commReturn_t Comm_RespondeNack(uint8_t seq, uint8_t cmd, commError_t error,
									uint8_t *response, uint16_t responseSize, uint16_t *responseLen);
static void Comm_GuardaResposta(uint8_t seq, const uint8_t *response, uint16_t responseLen);

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

commReturn_t Comm_Init(void)
{
	comm.hasLastResponse  = 0U;
	comm.lastResponseSize = 0U;
	comm.lastSeq          = 0U;
	comm.hasPending       = 0U;
	comm.pendingSeq       = 0U;

	return Comm_Reset();
}

commReturn_t Comm_Reset(void)
{
	comm.commState   = eCOMM_STATE_AGUARDA_SOF;
	comm.count       = 0;
	comm.crcReceived = 0;

	return eCOMM_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Avisa o parser que a linha ficou em silencio (timeout de pacote).
 * @details    Descarta um quadro recebido pela metade. Quem mede o tempo e a CommTask, assim o parser nao depende
 *             do FreeRTOS e continua testavel.
 * @return     eCOMM_RETURN_TIMEOUT se havia um quadro em andamento, eCOMM_RETURN_OK se o parser ja estava ocioso
 **********************************************************************************************************************/
commReturn_t Comm_Timeout(void)
{
	if (comm.commState == eCOMM_STATE_AGUARDA_SOF)
	{
		return eCOMM_RETURN_OK;
	}

	Comm_Reset();

	return eCOMM_RETURN_TIMEOUT;
}

commReturn_t Comm_ProcessByte(uint8_t byte, commFrame_t *frame)
{
	if (frame == NULL)
	{
		return eCOMM_RETURN_INVALID_ARGUMENT;
	}

	switch (comm.commState)
	{
		case eCOMM_STATE_AGUARDA_SOF:

			// Qualquer byte diferente do SOF e descartado
			if (byte == dCOMM_SOF)
			{
				comm.count     = 0;
				comm.commState = eCOMM_STATE_LE_CABECALHO;
			}

		break;

		case eCOMM_STATE_LE_CABECALHO:

			// LEN invalido: descarta o quadro assim que ele chega
			if ((comm.count == dCOMM_IDX_LEN) && (byte > dCOMM_PAYLOAD_MAX_SIZE))
			{
				Comm_Reset();
				return eCOMM_RETURN_LEN_ERROR;
			}

			comm.raw[comm.count] = byte;
			comm.count++;

			if (comm.count == dCOMM_HEADER_SIZE)
			{
				comm.count     = 0;
				comm.commState = (comm.raw[dCOMM_IDX_LEN] == 0U) ? eCOMM_STATE_VALIDA_CRC : eCOMM_STATE_LE_PAYLOAD;
			}

		break;

		case eCOMM_STATE_LE_PAYLOAD:

			comm.raw[dCOMM_HEADER_SIZE + comm.count] = byte;
			comm.count++;

			if (comm.count == comm.raw[dCOMM_IDX_LEN])
			{
				comm.count     = 0;
				comm.commState = eCOMM_STATE_VALIDA_CRC;
			}

		break;

		case eCOMM_STATE_VALIDA_CRC:

			// 1o byte: parte baixa do CRC
			if (comm.count == 0U)
			{
				comm.crcReceived = byte;
				comm.count++;
				break;
			}

			// 2o byte: parte alta do CRC, quadro completo
			comm.crcReceived |= (uint16_t)((uint16_t)byte << 8);

			return Comm_ValidaQuadro(frame);

		default:

			Comm_Reset();
			return eCOMM_RETURN_ERROR;
	}

	return eCOMM_RETURN_READING;
}

/***********************************************************************************************************************
 * @brief      Monta um quadro completo (SOF ate CRC) a partir dos campos. Inverso do Comm_ProcessByte.
 *
 * @param[in]  seq         SEQ do quadro (na resposta, o mesmo SEQ da requisicao)
 * @param[in]  cmd         CMD do quadro (ACK: cmd | 0x80, NACK: 0xFF)
 * @param[in]  payload     Dados do payload, pode ser NULL se payloadSize for 0. Nao pode sobrepor o buffer de saida.
 * @param[in]  payloadSize Tamanho do payload (0 a dCOMM_PAYLOAD_MAX_SIZE)
 * @param[out] buffer      Onde o quadro e escrito
 * @param[in]  bufferSize  Capacidade do buffer, nada e escrito se o quadro nao couber
 * @param[out] frameSize   Quantidade de bytes escritos no buffer
 *
 * @return     eCOMM_RETURN_OK, eCOMM_RETURN_INVALID_ARGUMENT ou eCOMM_RETURN_ERROR (falha no CRC)
 **********************************************************************************************************************/
commReturn_t Comm_BuildFrame(uint8_t seq, uint8_t cmd, const uint8_t *payload, uint8_t payloadSize,
                             uint8_t *buffer, uint16_t bufferSize, uint16_t *frameSize)
{
	uint16_t size = (uint16_t)(dCOMM_FRAME_OVERHEAD + payloadSize);
	uint16_t crc;

	if ((buffer == NULL) || (frameSize == NULL) || (payloadSize > dCOMM_PAYLOAD_MAX_SIZE) ||
	    ((payload == NULL) && (payloadSize > 0U)) || (bufferSize < size))
	{
		return eCOMM_RETURN_INVALID_ARGUMENT;
	}

	buffer[0U]                                 = dCOMM_SOF;
	buffer[dCOMM_FRAME_OFFSET + dCOMM_IDX_LEN] = payloadSize;
	buffer[dCOMM_FRAME_OFFSET + dCOMM_IDX_SEQ] = seq;
	buffer[dCOMM_FRAME_OFFSET + dCOMM_IDX_CMD] = cmd;

	if (payloadSize > 0U)
	{
		memcpy(&buffer[dCOMM_FRAME_OFFSET + dCOMM_HEADER_SIZE], payload, payloadSize);
	}

	if (Crc16_Calcule(&buffer[dCOMM_FRAME_OFFSET], (uint16_t)(dCOMM_HEADER_SIZE + payloadSize), &crc,
	                  dCRC_INITIAL_VALUE) != eCRC16_RETURN_OK)
	{
		return eCOMM_RETURN_ERROR;
	}

	buffer[size - 2U] = (uint8_t)(crc & 0xFFU);
	buffer[size - 1U] = (uint8_t)(crc >> 8);

	*frameSize = size;

	return eCOMM_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Decide o que fazer com um quadro valido: responder na hora (PING, retransmissao, OCUPADO) ou
 *             repassar para execucao (FORWARD).
 * @details    Um comando pendente por vez (Apendice A). Enquanto ha um pendente:
 *             - mesmo SEQ do pendente: retransmissao, nada a responder (a resposta sai no Comm_BuildResult);
 *             - PING: atendido normalmente;
 *             - outro comando: NACK OCUPADO, sem guardar no cache (um novo envio desse SEQ sera executado).
 * @param[in]  frame         Quadro recebido e validado pelo Comm_ProcessByte
 * @param[out] response      Onde a resposta e montada (quando houver)
 * @param[in]  responseSize  Capacidade do buffer de resposta
 * @param[out] responseLen   Tamanho da resposta montada (0 em FORWARD e IGNORED)
 * @return     eCOMM_RETURN_OK (resposta nova), eCOMM_RETURN_RESENT (resposta guardada), eCOMM_RETURN_FORWARD,
 *             eCOMM_RETURN_IGNORED, eCOMM_RETURN_INVALID_ARGUMENT ou eCOMM_RETURN_ERROR
 **********************************************************************************************************************/
commReturn_t Comm_ProcessFrame(const commFrame_t *frame, uint8_t *response, uint16_t responseSize,
                               uint16_t *responseLen)
{
	commReturn_t ret;

	if ((frame == NULL) || (response == NULL) || (responseLen == NULL))
	{
		return eCOMM_RETURN_INVALID_ARGUMENT;
	}

	*responseLen = 0U;

	// Retransmissao do comando que ainda esta executando: a resposta vai sair quando o resultado chegar
	if ((comm.hasPending != 0U) && (frame->seq == comm.pendingSeq))
	{
		return eCOMM_RETURN_IGNORED;
	}

	// Retransmissao de um comando ja respondido: reenvia sem executar
	if ((comm.hasLastResponse != 0U) && (frame->seq == comm.lastSeq))
	{
		if (responseSize < comm.lastResponseSize)
		{
			return eCOMM_RETURN_INVALID_ARGUMENT;
		}

		memcpy(response, comm.lastResponse, comm.lastResponseSize);
		*responseLen = comm.lastResponseSize;

		return eCOMM_RETURN_RESENT;
	}

	// PING e respondido aqui mesmo, inclusive com um comando pendente
	if (frame->cmd == dCOMM_CMD_PING)
	{
		ret = Comm_RespondePing(frame->seq, response, responseSize, responseLen);

		if (ret == eCOMM_RETURN_OK)
		{
			Comm_GuardaResposta(frame->seq, response, *responseLen);
		}

		return ret;
	}

	// Outro comando enquanto um esta executando: recusa, sem guardar no cache
	if (comm.hasPending != 0U)
	{
		return Comm_RespondeNack(frame->seq, frame->cmd, eCOMM_ERROR_OCUPADO, response, responseSize, responseLen);
	}

	// Demais comandos: quem executa e o Exec; a resposta e montada no Comm_BuildResult
	comm.pendingSeq = frame->seq;
	comm.hasPending = 1U;

	return eCOMM_RETURN_FORWARD;
}

/***********************************************************************************************************************
 * @brief      Monta a resposta de um comando repassado, a partir do resultado da execucao.
 * @details    erro 0 -> ACK (CMD | 0x80) com os dados; senao NACK [cmd, erro]. Guarda a resposta no cache de SEQ
 *             e libera o pendente. Resultado que nao e do comando pendente (ex.: chegou depois de uma sessao nova)
 *             e descartado.
 * @param[in]  seq, cmd   SEQ e CMD do comando executado
 * @param[in]  erro       0 = sucesso; senao codigo commError_t
 * @param[in]  dados      Payload do ACK (pode ser NULL se dadosSize for 0)
 * @param[in]  dadosSize  Tamanho do payload do ACK
 * @param[out] response, responseSize, responseLen  Como no Comm_ProcessFrame
 * @return     eCOMM_RETURN_OK, eCOMM_RETURN_INVALID_ARGUMENT ou eCOMM_RETURN_ERROR (sem pendente com esse SEQ)
 **********************************************************************************************************************/
commReturn_t Comm_BuildResult(uint8_t seq, uint8_t cmd, uint8_t erro, const uint8_t *dados, uint8_t dadosSize,
                              uint8_t *response, uint16_t responseSize, uint16_t *responseLen)
{
	commReturn_t ret;

	if ((response == NULL) || (responseLen == NULL) || ((dados == NULL) && (dadosSize > 0U)))
	{
		return eCOMM_RETURN_INVALID_ARGUMENT;
	}

	*responseLen = 0U;

	if ((comm.hasPending == 0U) || (seq != comm.pendingSeq))
	{
		return eCOMM_RETURN_ERROR;
	}

	if (erro == 0U)
	{
		ret = Comm_BuildFrame(seq, (uint8_t)(cmd | dCOMM_ACK_FLAG), dados, dadosSize, response, responseSize, responseLen);
	}
	else
	{
		ret = Comm_RespondeNack(seq, cmd, (commError_t)erro, response, responseSize, responseLen);
	}

	if (ret == eCOMM_RETURN_OK)
	{
		Comm_GuardaResposta(seq, response, *responseLen);
		comm.hasPending = 0U;
	}

	return ret;
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Calcula o CRC do quadro recebido e, se valido, copia os campos para o frame.
 * @param[out] frame  Quadro de saida (so e escrito quando o CRC confere)
 * @return     eCOMM_RETURN_FRAME_OK, eCOMM_RETURN_CRC_ERROR ou eCOMM_RETURN_ERROR
 **********************************************************************************************************************/
static commReturn_t Comm_ValidaQuadro(commFrame_t *frame)
{
	uint8_t  len = comm.raw[dCOMM_IDX_LEN];
	uint16_t crcCalculated;
	commReturn_t ret;

	if (Crc16_Calcule(comm.raw, (uint16_t)(dCOMM_HEADER_SIZE + len), &crcCalculated, dCRC_INITIAL_VALUE) != eCRC16_RETURN_OK)
	{
		ret = eCOMM_RETURN_ERROR;
	}
	else if (crcCalculated != comm.crcReceived)
	{
		ret = eCOMM_RETURN_CRC_ERROR;
	}
	else
	{
		frame->size  = len;
		frame->seq   = comm.raw[dCOMM_IDX_SEQ];
		frame->cmd   = comm.raw[dCOMM_IDX_CMD];
		frame->crc16 = comm.crcReceived;
		memcpy(frame->payload, &comm.raw[dCOMM_HEADER_SIZE], len);

		ret = eCOMM_RETURN_FRAME_OK;
	}

	Comm_Reset();

	return ret;
}

/***********************************************************************************************************************
 * @brief      ACK do PING: versao do protocolo e versao do firmware (maior, menor, correcao).
 **********************************************************************************************************************/
static commReturn_t Comm_RespondePing(uint8_t seq, uint8_t *response, uint16_t responseSize, uint16_t *responseLen)
{
	const uint8_t payload[] = {dCOMM_PROTOCOL_VERSION, dFW_VERSION_MAJOR, dFW_VERSION_MINOR, dFW_VERSION_PATCH};

	return Comm_BuildFrame(seq, (uint8_t)(dCOMM_CMD_PING | dCOMM_ACK_FLAG), payload, sizeof(payload),
	                       response, responseSize, responseLen);
}

/***********************************************************************************************************************
 * @brief      NACK: payload com o comando recusado e o codigo do erro.
 **********************************************************************************************************************/
static commReturn_t Comm_RespondeNack(uint8_t seq, uint8_t cmd, commError_t error,
                                      uint8_t *response, uint16_t responseSize, uint16_t *responseLen)
{
	const uint8_t payload[] = {cmd, (uint8_t)error};

	return Comm_BuildFrame(seq, dCOMM_CMD_NACK, payload, sizeof(payload), response, responseSize, responseLen);
}

/***********************************************************************************************************************
 * @brief      Guarda a ultima resposta enviada e o SEQ dela, para reenviar se o PC retransmitir.
 **********************************************************************************************************************/
static void Comm_GuardaResposta(uint8_t seq, const uint8_t *response, uint16_t responseLen)
{
	memcpy(comm.lastResponse, response, responseLen);
	comm.lastResponseSize = responseLen;
	comm.lastSeq          = seq;
	comm.hasLastResponse  = 1U;
}

/** @} DOXYGEN GROUP TAG END OF FILE */
