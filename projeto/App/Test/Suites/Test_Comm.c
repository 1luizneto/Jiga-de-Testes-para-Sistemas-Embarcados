#include <string.h>
#include "unity.h"
#include "Comm.h"
#include "Bsp_Crc.h"

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
