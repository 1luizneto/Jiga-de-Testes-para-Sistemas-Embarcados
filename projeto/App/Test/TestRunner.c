#include "TestRunner.h"
#include "cmsis_os.h"
#include "usart.h"
#include "unity.h"
#include "Test_Suites.h"


// Unity chama issp pra cada caractere que quer imprimir
void TestOutput_PutChar(int c)
{
	uint8_t ch = (uint8_t)c;
	HAL_UART_Transmit(&huart3, &ch, 1, HAL_MAX_DELAY);
}

// Unity chama antes e depois de CADA teste

void setUp(void){}
void tearDown(void) {}

static void TestRunnerTast(void *argument)
{
	UNITY_BEGIN();

		RUN_TEST(Test_Crc16_HardwareVetorReferencia);
		RUN_TEST(Test_Crc16_HardwareInicialValue);
		RUN_TEST(Test_Crc16_HardwareDataEmpty);
		RUN_TEST(Test_Crc16_HardwareCalculosSeguidos);
		RUN_TEST(Test_Crc16_HardwareBUfferMaximoComm);

		RUN_TEST(Test_Comm_PingValido);
		RUN_TEST(Test_Comm_PayloadCom7E);
		RUN_TEST(Test_Comm_PayloadMaximo);
		RUN_TEST(Test_Comm_CrcInvalido);
		RUN_TEST(Test_Comm_LenInvalido);
		RUN_TEST(Test_Comm_LixoAntesDoSof);
		RUN_TEST(Test_Comm_DoisQuadrosSeguidos);
		RUN_TEST(Test_Comm_TimeoutDescartaQuadroParcial);
		RUN_TEST(Test_Comm_QuadroParcialSemTimeout);
		RUN_TEST(Test_Comm_FrameNulo);

		RUN_TEST(Test_Comm_BuildAckPing);
		RUN_TEST(Test_Comm_BuildAckSemPayload);
		RUN_TEST(Test_Comm_BuildNack);
		RUN_TEST(Test_Comm_BuildIdaEVolta);
		RUN_TEST(Test_Comm_BuildArgumentosInvalidos);
		RUN_TEST(Test_Comm_BuildBufferPequeno);

		RUN_TEST(Test_Comm_ProcessPing);
		RUN_TEST(Test_Comm_ProcessCmdDesconhecido);
		RUN_TEST(Test_Comm_ProcessSeqRepetido);
		RUN_TEST(Test_Comm_ProcessSeqNovo);
		RUN_TEST(Test_Comm_ProcessInitLimpaCache);
		RUN_TEST(Test_Comm_ProcessArgumentosInvalidos);

	UNITY_END();

	for (;;)
	{
		osDelay(1000);
	}
}

void TestRunner_Start(void)
{
	static const osThreadAttr_t attr = {
			.name = "TestRunner",
			.stack_size = 1024 * 4,
			.priority = osPriorityNormal,
	};

	osThreadNew(TestRunnerTast, NULL, &attr);
}
