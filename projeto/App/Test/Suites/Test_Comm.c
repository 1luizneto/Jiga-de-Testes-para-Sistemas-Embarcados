#include <string.h>
#include "unity.h"
#include "Comm.h"
#include "Bsp_Crc.h"
#include "Version.h"

// Quadros gerados com Software/protocolo.py (montar_quadro), CRC-16/CCITT-FALSE little-endian
static const uint8_t pingSeq1[] = {0x7E, 0x00, 0x01, 0x01, 0x8C, 0xEF};
static const uint8_t pingSeq2[] = {0x7E, 0x00, 0x02, 0x01, 0xDF, 0xBA};

// LEN=4, SEQ=5, CMD=0x10, payload com 0x7E no meio
static const uint8_t quadroPayload7E[] = {0x7E, 0x04, 0x05, 0x10, 0x11, 0x7E, 0x22, 0x7E, 0x94, 0x26};

// CRC de LEN=240, SEQ=3, CMD=0x10, payload 0x00..0xEF
#define dCRC_QUADRO_MAXIMO (0x4684U)

// Valor usado para conferir que o frame nao foi escrito
#define dFRAME_SENTINELA (0xAAU)

// Envia todos os bytes menos o ultimo conferindo READING; retorna o resultado do ultimo byte
static commReturn_t EnviaQuadro(const uint8_t *data, uint16_t size, commFrame_t *frame)
{
	for (uint16_t i = 0; i < (size - 1U); i++)
	{
		TEST_ASSERT_EQUAL_MESSAGE(eCOMM_RETURN_READING, Comm_ProcessByte(data[i], frame), "byte intermediario");
	}

	return Comm_ProcessByte(data[size - 1U], frame);
}

static void IniciaTeste(commFrame_t *frame)
{
	BspCrc_Init();
	Comm_Init();
	memset(frame, dFRAME_SENTINELA, sizeof(commFrame_t));
}

static void MontaRequisicao(commFrame_t *frame, uint8_t seq, uint8_t cmd)
{
	memset(frame, 0, sizeof(commFrame_t));
	frame->seq = seq;
	frame->cmd = cmd;
}

// 1. PING valido: READING 5x e FRAME_OK no ultimo byte
void Test_Comm_PingValido(void)
{
	commFrame_t frame;
	IniciaTeste(&frame);

	TEST_ASSERT_EQUAL(eCOMM_RETURN_FRAME_OK, EnviaQuadro(pingSeq1, sizeof(pingSeq1), &frame));
	TEST_ASSERT_EQUAL_UINT8(0, frame.size);
	TEST_ASSERT_EQUAL_UINT8(1, frame.seq);
	TEST_ASSERT_EQUAL_HEX8(0x01, frame.cmd);
	TEST_ASSERT_EQUAL_HEX16(0xEF8C, frame.crc16);
}

// 2. 0x7E dentro do payload e dado comum (enquadramento por LEN)
void Test_Comm_PayloadCom7E(void)
{
	const uint8_t esperado[] = {0x11, 0x7E, 0x22, 0x7E};
	commFrame_t frame;
	IniciaTeste(&frame);

	TEST_ASSERT_EQUAL(eCOMM_RETURN_FRAME_OK, EnviaQuadro(quadroPayload7E, sizeof(quadroPayload7E), &frame));
	TEST_ASSERT_EQUAL_UINT8(4, frame.size);
	TEST_ASSERT_EQUAL_UINT8(5, frame.seq);
	TEST_ASSERT_EQUAL_HEX8(0x10, frame.cmd);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(esperado, frame.payload, sizeof(esperado));
}

// 3. Payload maximo (240 B)
void Test_Comm_PayloadMaximo(void)
{
	commFrame_t frame;
	IniciaTeste(&frame);

	TEST_ASSERT_EQUAL(eCOMM_RETURN_READING, Comm_ProcessByte(0x7E, &frame));
	TEST_ASSERT_EQUAL(eCOMM_RETURN_READING, Comm_ProcessByte(dCOMM_PAYLOAD_MAX_SIZE, &frame));
	TEST_ASSERT_EQUAL(eCOMM_RETURN_READING, Comm_ProcessByte(3, &frame));
	TEST_ASSERT_EQUAL(eCOMM_RETURN_READING, Comm_ProcessByte(0x10, &frame));

	for (uint16_t i = 0; i < dCOMM_PAYLOAD_MAX_SIZE; i++)
	{
		TEST_ASSERT_EQUAL(eCOMM_RETURN_READING, Comm_ProcessByte((uint8_t)i, &frame));
	}

	TEST_ASSERT_EQUAL(eCOMM_RETURN_READING, Comm_ProcessByte((uint8_t)(dCRC_QUADRO_MAXIMO & 0xFFU), &frame));
	TEST_ASSERT_EQUAL(eCOMM_RETURN_FRAME_OK, Comm_ProcessByte((uint8_t)(dCRC_QUADRO_MAXIMO >> 8), &frame));

	TEST_ASSERT_EQUAL_UINT8(dCOMM_PAYLOAD_MAX_SIZE, frame.size);
	TEST_ASSERT_EQUAL_HEX8(0x00, frame.payload[0]);
	TEST_ASSERT_EQUAL_HEX8(0xEF, frame.payload[dCOMM_PAYLOAD_MAX_SIZE - 1U]);
}

// 4. CRC errado: CRC_ERROR e o frame nao e escrito
void Test_Comm_CrcInvalido(void)
{
	uint8_t quadro[sizeof(pingSeq1)];
	commFrame_t frame;
	IniciaTeste(&frame);

	memcpy(quadro, pingSeq1, sizeof(quadro));
	quadro[sizeof(quadro) - 1U] ^= 0xFFU;

	TEST_ASSERT_EQUAL(eCOMM_RETURN_CRC_ERROR, EnviaQuadro(quadro, sizeof(quadro), &frame));
	TEST_ASSERT_EQUAL_HEX8(dFRAME_SENTINELA, frame.seq);

	// Depois do erro o parser volta a aceitar quadros
	TEST_ASSERT_EQUAL(eCOMM_RETURN_FRAME_OK, EnviaQuadro(pingSeq2, sizeof(pingSeq2), &frame));
	TEST_ASSERT_EQUAL_UINT8(2, frame.seq);
}

// 5. LEN > 240: LEN_ERROR logo no 2o byte
void Test_Comm_LenInvalido(void)
{
	commFrame_t frame;
	IniciaTeste(&frame);

	TEST_ASSERT_EQUAL(eCOMM_RETURN_READING, Comm_ProcessByte(0x7E, &frame));
	TEST_ASSERT_EQUAL(eCOMM_RETURN_LEN_ERROR, Comm_ProcessByte(dCOMM_PAYLOAD_MAX_SIZE + 1U, &frame));

	TEST_ASSERT_EQUAL(eCOMM_RETURN_FRAME_OK, EnviaQuadro(pingSeq1, sizeof(pingSeq1), &frame));
}

// 6. Bytes soltos antes do SOF sao ignorados
void Test_Comm_LixoAntesDoSof(void)
{
	const uint8_t lixo[] = {0xAA, 0xBB, 0x00, 0xFF};
	commFrame_t frame;
	IniciaTeste(&frame);

	for (uint16_t i = 0; i < sizeof(lixo); i++)
	{
		TEST_ASSERT_EQUAL(eCOMM_RETURN_READING, Comm_ProcessByte(lixo[i], &frame));
	}

	TEST_ASSERT_EQUAL(eCOMM_RETURN_FRAME_OK, EnviaQuadro(pingSeq1, sizeof(pingSeq1), &frame));
}

// 7. Dois quadros seguidos
void Test_Comm_DoisQuadrosSeguidos(void)
{
	commFrame_t frame;
	IniciaTeste(&frame);

	TEST_ASSERT_EQUAL(eCOMM_RETURN_FRAME_OK, EnviaQuadro(pingSeq1, sizeof(pingSeq1), &frame));
	TEST_ASSERT_EQUAL_UINT8(1, frame.seq);

	TEST_ASSERT_EQUAL(eCOMM_RETURN_FRAME_OK, EnviaQuadro(pingSeq2, sizeof(pingSeq2), &frame));
	TEST_ASSERT_EQUAL_UINT8(2, frame.seq);
}

// 8. Timeout descarta quadro pela metade
void Test_Comm_TimeoutDescartaQuadroParcial(void)
{
	commFrame_t frame;
	IniciaTeste(&frame);

	// Parser ocioso: timeout nao descarta nada
	TEST_ASSERT_EQUAL(eCOMM_RETURN_OK, Comm_Timeout());

	TEST_ASSERT_EQUAL(eCOMM_RETURN_READING, Comm_ProcessByte(0x7E, &frame));
	TEST_ASSERT_EQUAL(eCOMM_RETURN_READING, Comm_ProcessByte(0x00, &frame));
	TEST_ASSERT_EQUAL(eCOMM_RETURN_TIMEOUT, Comm_Timeout());
	TEST_ASSERT_EQUAL(eCOMM_RETURN_OK, Comm_Timeout());

	TEST_ASSERT_EQUAL(eCOMM_RETURN_FRAME_OK, EnviaQuadro(pingSeq1, sizeof(pingSeq1), &frame));
}

// 9. Sem o timeout, os bytes do quadro seguinte completam o quadro pela metade (e o PING se perde)
void Test_Comm_QuadroParcialSemTimeout(void)
{
	commFrame_t frame;
	IniciaTeste(&frame);

	// Meio quadro com LEN=4: os 6 bytes do PING viram 3 de payload + 2 de CRC (+1 sobra)
	TEST_ASSERT_EQUAL(eCOMM_RETURN_READING, Comm_ProcessByte(0x7E, &frame));
	TEST_ASSERT_EQUAL(eCOMM_RETURN_READING, Comm_ProcessByte(0x04, &frame));
	TEST_ASSERT_EQUAL(eCOMM_RETURN_READING, Comm_ProcessByte(0x09, &frame));
	TEST_ASSERT_EQUAL(eCOMM_RETURN_READING, Comm_ProcessByte(0x10, &frame));
	TEST_ASSERT_EQUAL(eCOMM_RETURN_READING, Comm_ProcessByte(0x01, &frame));

	TEST_ASSERT_EQUAL(eCOMM_RETURN_CRC_ERROR, EnviaQuadro(pingSeq1, sizeof(pingSeq1) - 1U, &frame));
	TEST_ASSERT_EQUAL_HEX8(dFRAME_SENTINELA, frame.seq);
}

// 10. Frame nulo
void Test_Comm_FrameNulo(void)
{
	Comm_Init();

	TEST_ASSERT_EQUAL(eCOMM_RETURN_INVALID_ARGUMENT, Comm_ProcessByte(0x7E, NULL));
}

// 11. ACK do PING do Apendice A (protocolo 1, firmware 0.1.0)
void Test_Comm_BuildAckPing(void)
{
	const uint8_t payload[]  = {0x01, 0x00, 0x01, 0x00};
	const uint8_t esperado[] = {0x7E, 0x04, 0x01, 0x81, 0x01, 0x00, 0x01, 0x00, 0xAC, 0xB8};
	uint8_t buffer[dCOMM_FRAME_MAX_SIZE];
	uint16_t frameSize = 0;
	BspCrc_Init();

	TEST_ASSERT_EQUAL(eCOMM_RETURN_OK,
	                  Comm_BuildFrame(0x01, 0x81, payload, sizeof(payload), buffer, sizeof(buffer), &frameSize));
	TEST_ASSERT_EQUAL_UINT16(sizeof(esperado), frameSize);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(esperado, buffer, sizeof(esperado));
}

// 12. ACK sem payload (ACK do DAC_SET do Apendice A): payload NULL com tamanho 0 e aceito
void Test_Comm_BuildAckSemPayload(void)
{
	const uint8_t esperado[] = {0x7E, 0x00, 0x02, 0x90, 0x47, 0x29};
	uint8_t buffer[dCOMM_FRAME_MAX_SIZE];
	uint16_t frameSize = 0;
	BspCrc_Init();

	TEST_ASSERT_EQUAL(eCOMM_RETURN_OK,
	                  Comm_BuildFrame(0x02, 0x90, NULL, 0, buffer, sizeof(buffer), &frameSize));
	TEST_ASSERT_EQUAL_UINT16(sizeof(esperado), frameSize);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(esperado, buffer, sizeof(esperado));
}

// 13. NACK do Apendice A: comando 0x10 recusado com PARAMETRO_INVALIDO (0x02)
void Test_Comm_BuildNack(void)
{
	const uint8_t payload[]  = {0x10, 0x02};
	const uint8_t esperado[] = {0x7E, 0x02, 0x02, 0xFF, 0x10, 0x02, 0xB5, 0x54};
	uint8_t buffer[dCOMM_FRAME_MAX_SIZE];
	uint16_t frameSize = 0;
	BspCrc_Init();

	TEST_ASSERT_EQUAL(eCOMM_RETURN_OK,
	                  Comm_BuildFrame(0x02, 0xFF, payload, sizeof(payload), buffer, sizeof(buffer), &frameSize));
	TEST_ASSERT_EQUAL_UINT16(sizeof(esperado), frameSize);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(esperado, buffer, sizeof(esperado));
}

// 14. Ida e volta: o quadro montado pelo BuildFrame passa pelo parser e volta igual
void Test_Comm_BuildIdaEVolta(void)
{
	static uint8_t payload[dCOMM_PAYLOAD_MAX_SIZE];
	static uint8_t buffer[dCOMM_FRAME_MAX_SIZE];
	commFrame_t frame;
	uint16_t frameSize = 0;

	for (uint16_t i = 0; i < sizeof(payload); i++)
	{
		payload[i] = (uint8_t)i;
	}
	IniciaTeste(&frame);

	TEST_ASSERT_EQUAL(eCOMM_RETURN_OK,
	                  Comm_BuildFrame(0x03, 0x10, payload, (uint8_t)sizeof(payload), buffer, sizeof(buffer), &frameSize));
	TEST_ASSERT_EQUAL_UINT16(dCOMM_FRAME_MAX_SIZE, frameSize);

	TEST_ASSERT_EQUAL(eCOMM_RETURN_FRAME_OK, EnviaQuadro(buffer, frameSize, &frame));
	TEST_ASSERT_EQUAL_UINT8(dCOMM_PAYLOAD_MAX_SIZE, frame.size);
	TEST_ASSERT_EQUAL_UINT8(0x03, frame.seq);
	TEST_ASSERT_EQUAL_HEX8(0x10, frame.cmd);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, frame.payload, sizeof(payload));
	TEST_ASSERT_EQUAL_HEX16(dCRC_QUADRO_MAXIMO, frame.crc16);
}

// 15. Argumentos invalidos sao recusados
void Test_Comm_BuildArgumentosInvalidos(void)
{
	const uint8_t payload[] = {0x10, 0x02};
	uint8_t buffer[dCOMM_FRAME_MAX_SIZE];
	uint16_t frameSize = 0;
	BspCrc_Init();

	TEST_ASSERT_EQUAL_MESSAGE(eCOMM_RETURN_INVALID_ARGUMENT,
	    Comm_BuildFrame(0x01, 0xFF, payload, sizeof(payload), NULL, sizeof(buffer), &frameSize), "buffer NULL");
	TEST_ASSERT_EQUAL_MESSAGE(eCOMM_RETURN_INVALID_ARGUMENT,
	    Comm_BuildFrame(0x01, 0xFF, payload, sizeof(payload), buffer, sizeof(buffer), NULL), "frameSize NULL");
	TEST_ASSERT_EQUAL_MESSAGE(eCOMM_RETURN_INVALID_ARGUMENT,
	    Comm_BuildFrame(0x01, 0xFF, NULL, sizeof(payload), buffer, sizeof(buffer), &frameSize), "payload NULL com tamanho");
	TEST_ASSERT_EQUAL_MESSAGE(eCOMM_RETURN_INVALID_ARGUMENT,
	    Comm_BuildFrame(0x01, 0xFF, payload, dCOMM_PAYLOAD_MAX_SIZE + 1U, buffer, sizeof(buffer), &frameSize), "payload > 240");
}

// 16. Quadro que nao cabe no buffer: recusado sem escrever nada
void Test_Comm_BuildBufferPequeno(void)
{
	const uint8_t payload[] = {0x10, 0x02};
	uint8_t buffer[dCOMM_FRAME_OVERHEAD + sizeof(payload)];
	uint8_t intacto[sizeof(buffer)];
	uint16_t frameSize = 0;
	BspCrc_Init();

	memset(buffer, dFRAME_SENTINELA, sizeof(buffer));
	memset(intacto, dFRAME_SENTINELA, sizeof(intacto));

	// Capacidade um byte menor que o necessario
	TEST_ASSERT_EQUAL(eCOMM_RETURN_INVALID_ARGUMENT,
	                  Comm_BuildFrame(0x02, 0xFF, payload, sizeof(payload), buffer, sizeof(buffer) - 1U, &frameSize));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(intacto, buffer, sizeof(buffer));
	TEST_ASSERT_EQUAL_UINT16(0, frameSize);
}

// 17. PING: ACK com a versao do protocolo e do firmware
void Test_Comm_ProcessPing(void)
{
	const uint8_t payload[] = {dCOMM_PROTOCOL_VERSION, dFW_VERSION_MAJOR, dFW_VERSION_MINOR, dFW_VERSION_PATCH};
	uint8_t esperado[dCOMM_FRAME_MAX_SIZE];
	uint8_t resposta[dCOMM_FRAME_MAX_SIZE];
	uint16_t tamEsperado = 0, tamResposta = 0;
	commFrame_t frame;
	IniciaTeste(&frame);
	MontaRequisicao(&frame, 0x01, dCOMM_CMD_PING);

	// O BuildFrame ja foi validado contra o Apendice A: serve de referencia sem fixar a versao no teste
	TEST_ASSERT_EQUAL(eCOMM_RETURN_OK, Comm_BuildFrame(0x01, 0x81, payload, sizeof(payload),
	                                                   esperado, sizeof(esperado), &tamEsperado));

	TEST_ASSERT_EQUAL(eCOMM_RETURN_OK, Comm_ProcessFrame(&frame, resposta, sizeof(resposta), &tamResposta));
	TEST_ASSERT_EQUAL_UINT16(tamEsperado, tamResposta);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(esperado, resposta, tamEsperado);
}

// 18. Comando desconhecido: NACK [cmd, CMD_DESCONHECIDO]
void Test_Comm_ProcessCmdDesconhecido(void)
{
	const uint8_t payload[] = {0x55, eCOMM_ERROR_CMD_DESCONHECIDO};
	uint8_t esperado[dCOMM_FRAME_MAX_SIZE];
	uint8_t resposta[dCOMM_FRAME_MAX_SIZE];
	uint16_t tamEsperado = 0, tamResposta = 0;
	commFrame_t frame;
	IniciaTeste(&frame);
	MontaRequisicao(&frame, 0x02, 0x55);

	TEST_ASSERT_EQUAL(eCOMM_RETURN_OK, Comm_BuildFrame(0x02, dCOMM_CMD_NACK, payload, sizeof(payload),
	                                                   esperado, sizeof(esperado), &tamEsperado));

	TEST_ASSERT_EQUAL(eCOMM_RETURN_OK, Comm_ProcessFrame(&frame, resposta, sizeof(resposta), &tamResposta));
	TEST_ASSERT_EQUAL_UINT16(tamEsperado, tamResposta);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(esperado, resposta, tamEsperado);
}

// 19. SEQ repetido: reenvia a resposta guardada sem executar o novo comando
void Test_Comm_ProcessSeqRepetido(void)
{
	uint8_t primeira[dCOMM_FRAME_MAX_SIZE];
	uint8_t segunda[dCOMM_FRAME_MAX_SIZE];
	uint16_t tamPrimeira = 0, tamSegunda = 0;
	commFrame_t frame;
	IniciaTeste(&frame);

	MontaRequisicao(&frame, 0x03, dCOMM_CMD_PING);
	TEST_ASSERT_EQUAL(eCOMM_RETURN_OK, Comm_ProcessFrame(&frame, primeira, sizeof(primeira), &tamPrimeira));

	// Mesmo SEQ com outro CMD: se executasse, viria um NACK; tem que vir o ACK do PING guardado
	MontaRequisicao(&frame, 0x03, 0x55);
	TEST_ASSERT_EQUAL(eCOMM_RETURN_RESENT, Comm_ProcessFrame(&frame, segunda, sizeof(segunda), &tamSegunda));
	TEST_ASSERT_EQUAL_UINT16(tamPrimeira, tamSegunda);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(primeira, segunda, tamPrimeira);
}

// 20. SEQs diferentes: cada quadro e executado
void Test_Comm_ProcessSeqNovo(void)
{
	uint8_t resposta[dCOMM_FRAME_MAX_SIZE];
	uint16_t tamResposta = 0;
	commFrame_t frame;
	IniciaTeste(&frame);

	MontaRequisicao(&frame, 0x04, dCOMM_CMD_PING);
	TEST_ASSERT_EQUAL(eCOMM_RETURN_OK, Comm_ProcessFrame(&frame, resposta, sizeof(resposta), &tamResposta));

	MontaRequisicao(&frame, 0x05, dCOMM_CMD_PING);
	TEST_ASSERT_EQUAL(eCOMM_RETURN_OK, Comm_ProcessFrame(&frame, resposta, sizeof(resposta), &tamResposta));
	TEST_ASSERT_EQUAL_UINT8(0x05, resposta[2]); // SEQ da resposta = SEQ da requisicao
}

// 21. Comm_Init esquece a ultima resposta (ex.: placa reiniciada)
void Test_Comm_ProcessInitLimpaCache(void)
{
	uint8_t resposta[dCOMM_FRAME_MAX_SIZE];
	uint16_t tamResposta = 0;
	commFrame_t frame;
	IniciaTeste(&frame);

	MontaRequisicao(&frame, 0x06, dCOMM_CMD_PING);
	TEST_ASSERT_EQUAL(eCOMM_RETURN_OK, Comm_ProcessFrame(&frame, resposta, sizeof(resposta), &tamResposta));

	Comm_Init();
	TEST_ASSERT_EQUAL(eCOMM_RETURN_OK, Comm_ProcessFrame(&frame, resposta, sizeof(resposta), &tamResposta));
}

// 22. Argumentos invalidos e buffer de resposta pequeno
void Test_Comm_ProcessArgumentosInvalidos(void)
{
	uint8_t resposta[dCOMM_FRAME_MAX_SIZE];
	uint16_t tamResposta = 0;
	commFrame_t frame;
	IniciaTeste(&frame);
	MontaRequisicao(&frame, 0x07, dCOMM_CMD_PING);

	TEST_ASSERT_EQUAL(eCOMM_RETURN_INVALID_ARGUMENT, Comm_ProcessFrame(NULL, resposta, sizeof(resposta), &tamResposta));
	TEST_ASSERT_EQUAL(eCOMM_RETURN_INVALID_ARGUMENT, Comm_ProcessFrame(&frame, NULL, sizeof(resposta), &tamResposta));
	TEST_ASSERT_EQUAL(eCOMM_RETURN_INVALID_ARGUMENT, Comm_ProcessFrame(&frame, resposta, sizeof(resposta), NULL));

	// ACK do PING tem 10 bytes: com 9 nao cabe
	TEST_ASSERT_EQUAL(eCOMM_RETURN_INVALID_ARGUMENT, Comm_ProcessFrame(&frame, resposta, 9U, &tamResposta));
}
