/*
 * Test_Exec.c
 *
 * Testes do Exec com um DAC falso: conferem o resultado (ACK/NACK) e, principalmente, se o
 * periferico foi ou nao acionado ("nenhuma saida alterada" do CT09), sem multimetro.
 */

#include <string.h>
#include "unity.h"
#include "Exec.h"
#include "AnalogOut.h"

/// Registro das chamadas ao DAC falso
static struct dacFalso
{
	uint8_t           chamadas;
	uint8_t           canal[4];
	uint16_t          valor[4];
	analogOutReturn_t retorno;   ///< o que o DAC falso devolve (simula falha do HAL)
} dacFalso;

static analogOutReturn_t DacFalso_Set(uint8_t canal, uint16_t valor)
{
	if (dacFalso.chamadas < 4U)
	{
		dacFalso.canal[dacFalso.chamadas] = canal;
		dacFalso.valor[dacFalso.chamadas] = valor;
	}
	dacFalso.chamadas++;

	return dacFalso.retorno;
}

// O AnalogOut passa a usar o DAC falso. Na build Test nada mais usa o DAC, entao nao precisa restaurar.
static void IniciaExec(void)
{
	memset(&dacFalso, 0, sizeof(dacFalso));
	dacFalso.retorno = eANALOGOUT_RETURN_OK;
	AnalogOut_Init(DacFalso_Set);
}

static void MontaComando(commFrame_t *frame, uint8_t seq, uint8_t cmd, const uint8_t *payload, uint8_t size)
{
	memset(frame, 0, sizeof(commFrame_t));
	frame->seq  = seq;
	frame->cmd  = cmd;
	frame->size = size;
	if (size > 0U)
	{
		memcpy(frame->payload, payload, size);
	}
}

// 1. DAC_SET valido: ACK e o DAC recebe canal e valor (u16 little-endian)
void Test_Exec_DacSetValido(void)
{
	const uint8_t payload[] = {0x01, 0x00, 0x08};   // canal 1, valor 0x0800 = 2048
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x05, dCOMM_CMD_DAC_SET, payload, sizeof(payload));

	TEST_ASSERT_EQUAL(eEXEC_RETURN_OK, Exec_Process(&frame, &result));
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0x05, result.seq);
	TEST_ASSERT_EQUAL_HEX8(dCOMM_CMD_DAC_SET, result.cmd);
	TEST_ASSERT_EQUAL_UINT8(0, result.size);
	TEST_ASSERT_EQUAL_UINT8(1, dacFalso.chamadas);
	TEST_ASSERT_EQUAL_UINT8(1, dacFalso.canal[0]);
	TEST_ASSERT_EQUAL_UINT16(2048, dacFalso.valor[0]);
}

// 2. Limites aceitos: canal 2 e valor 4095
void Test_Exec_DacSetLimites(void)
{
	const uint8_t payload[] = {0x02, 0xFF, 0x0F};   // canal 2, valor 0x0FFF = 4095
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x06, dCOMM_CMD_DAC_SET, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(2, dacFalso.canal[0]);
	TEST_ASSERT_EQUAL_UINT16(4095, dacFalso.valor[0]);
}

// 3. Canal inexistente: NACK PARAMETRO_INVALIDO e o DAC nao e acionado
void Test_Exec_DacSetCanalInvalido(void)
{
	const uint8_t payload[] = {0x03, 0x00, 0x08};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x07, dCOMM_CMD_DAC_SET, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, dacFalso.chamadas);
}

// 4. Valor acima de 4095: NACK PARAMETRO_INVALIDO e o DAC nao e acionado
void Test_Exec_DacSetValorInvalido(void)
{
	const uint8_t payload[] = {0x01, 0x00, 0x10};   // 0x1000 = 4096
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x08, dCOMM_CMD_DAC_SET, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, dacFalso.chamadas);
}

// 5. Payload com tamanho errado: NACK PARAMETRO_INVALIDO e o DAC nao e acionado
void Test_Exec_DacSetTamanhoErrado(void)
{
	const uint8_t payload[] = {0x01, 0x00};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x09, dCOMM_CMD_DAC_SET, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, dacFalso.chamadas);
}

// 6. RESET_ALL: ACK e as duas saidas vao para 0
void Test_Exec_ResetAll(void)
{
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x0A, dCOMM_CMD_RESET_ALL, NULL, 0);

	TEST_ASSERT_EQUAL(eEXEC_RETURN_OK, Exec_Process(&frame, &result));
	TEST_ASSERT_EQUAL_UINT8(0, result.erro);
	TEST_ASSERT_EQUAL_UINT8(2, dacFalso.chamadas);
	TEST_ASSERT_EQUAL_UINT8(1, dacFalso.canal[0]);
	TEST_ASSERT_EQUAL_UINT16(0, dacFalso.valor[0]);
	TEST_ASSERT_EQUAL_UINT8(2, dacFalso.canal[1]);
	TEST_ASSERT_EQUAL_UINT16(0, dacFalso.valor[1]);
}

// 7. RESET_ALL com payload: NACK PARAMETRO_INVALIDO e nada e acionado
void Test_Exec_ResetAllComPayload(void)
{
	const uint8_t payload[] = {0x00};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x0B, dCOMM_CMD_RESET_ALL, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PARAMETRO_INVALIDO, result.erro);
	TEST_ASSERT_EQUAL_UINT8(0, dacFalso.chamadas);
}

// 8. Comando desconhecido: NACK CMD_DESCONHECIDO e nada e acionado
void Test_Exec_CmdDesconhecido(void)
{
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x0C, 0x55, NULL, 0);

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_CMD_DESCONHECIDO, result.erro);
	TEST_ASSERT_EQUAL_HEX8(0x55, result.cmd);
	TEST_ASSERT_EQUAL_UINT8(0, dacFalso.chamadas);
}

// 9. Falha do periferico (HAL): NACK ERRO_PERIFERICO
void Test_Exec_DacSetFalhaPeriferico(void)
{
	const uint8_t payload[] = {0x01, 0x00, 0x08};
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	dacFalso.retorno = eANALOGOUT_RETURN_ERROR;
	MontaComando(&frame, 0x0D, dCOMM_CMD_DAC_SET, payload, sizeof(payload));

	Exec_Process(&frame, &result);
	TEST_ASSERT_EQUAL_UINT8(eCOMM_ERROR_PERIFERICO, result.erro);
}

// 10. Argumentos nulos
void Test_Exec_ArgumentosInvalidos(void)
{
	commFrame_t frame;
	execResult_t result;
	IniciaExec();
	MontaComando(&frame, 0x0E, dCOMM_CMD_RESET_ALL, NULL, 0);

	TEST_ASSERT_EQUAL(eEXEC_RETURN_INVALID_ARGUMENT, Exec_Process(NULL, &result));
	TEST_ASSERT_EQUAL(eEXEC_RETURN_INVALID_ARGUMENT, Exec_Process(&frame, NULL));
}
