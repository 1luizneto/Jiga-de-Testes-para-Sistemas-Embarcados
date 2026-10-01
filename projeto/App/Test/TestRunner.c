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
	RUN_TEST(test_Crc16_hardware_vetor_referencia);
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
