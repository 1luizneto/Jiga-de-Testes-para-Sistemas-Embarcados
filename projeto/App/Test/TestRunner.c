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
		RUN_TEST(Test_Comm_ProcessRepassaComando);
		RUN_TEST(Test_Comm_ProcessSeqRepetido);
		RUN_TEST(Test_Comm_ProcessSeqNovo);
		RUN_TEST(Test_Comm_ProcessInitLimpaCache);
		RUN_TEST(Test_Comm_ProcessArgumentosInvalidos);

		RUN_TEST(Test_Exec_DacSetValido);
		RUN_TEST(Test_Exec_DacSetLimites);
		RUN_TEST(Test_Exec_DacSetCanalInvalido);
		RUN_TEST(Test_Exec_DacSetValorInvalido);
		RUN_TEST(Test_Exec_DacSetTamanhoErrado);
		RUN_TEST(Test_Exec_ResetAll);
		RUN_TEST(Test_Exec_ResetAllComPayload);
		RUN_TEST(Test_Exec_CmdDesconhecido);
		RUN_TEST(Test_Exec_DacSetFalhaPeriferico);
		RUN_TEST(Test_Exec_ArgumentosInvalidos);
		RUN_TEST(Test_Exec_GpioSetValido);
		RUN_TEST(Test_Exec_GpioSetParametroInvalido);
		RUN_TEST(Test_Exec_GpioSetTamanhoErrado);
		RUN_TEST(Test_Exec_GpioReadValido);
		RUN_TEST(Test_Exec_GpioReadComPayload);
		RUN_TEST(Test_Exec_AdcReadValido);
		RUN_TEST(Test_Exec_AdcReadCanalInvalido);
		RUN_TEST(Test_Exec_AdcReadTamanhoErrado);
		RUN_TEST(Test_Exec_AdcReadFalhaPeriferico);
		RUN_TEST(Test_Exec_PwmSetValido);
		RUN_TEST(Test_Exec_PwmSetFrequenciaU32);
		RUN_TEST(Test_Exec_PwmSetParametroInvalido);
		RUN_TEST(Test_Exec_PwmSetTamanhoErrado);
		RUN_TEST(Test_Exec_PwmStopValido);
		RUN_TEST(Test_Exec_PwmStopInvalido);
		RUN_TEST(Test_Exec_PwmSetFalhaPeriferico);
		RUN_TEST(Test_Exec_I2cConfigValido);
		RUN_TEST(Test_Exec_I2cConfigInvalido);
		RUN_TEST(Test_Exec_I2cLoadDump);
		RUN_TEST(Test_Exec_I2cLoadInvalido);
		RUN_TEST(Test_Exec_I2cDumpInvalido);
		RUN_TEST(Test_Exec_I2cXferNaoConfigurado);
		RUN_TEST(Test_Exec_I2cXferValido);
		RUN_TEST(Test_Exec_I2cXferInvalido);
		RUN_TEST(Test_Exec_I2cXferSemAck);
		RUN_TEST(Test_Exec_ResetAllZeraMapaI2c);

		RUN_TEST(Test_Comm_ResultAck);
		RUN_TEST(Test_Comm_ResultNack);
		RUN_TEST(Test_Comm_ResultComDados);
		RUN_TEST(Test_Comm_PendenteRetransmissaoIgnorada);
		RUN_TEST(Test_Comm_PendenteOcupado);
		RUN_TEST(Test_Comm_PendentePingAtendido);
		RUN_TEST(Test_Comm_ResultInesperado);
		RUN_TEST(Test_Comm_ResultArgumentosInvalidos);

		RUN_TEST(Test_DigitalIo_SetValido);
		RUN_TEST(Test_DigitalIo_SetLimites);
		RUN_TEST(Test_DigitalIo_SetSaidaInvalida);
		RUN_TEST(Test_DigitalIo_SetNivelInvalido);
		RUN_TEST(Test_DigitalIo_ReadMascara);
		RUN_TEST(Test_DigitalIo_ReadExtremos);
		RUN_TEST(Test_DigitalIo_ReadFalha);
		RUN_TEST(Test_DigitalIo_SafeState);
		RUN_TEST(Test_DigitalIo_SafeStateComFalha);
		RUN_TEST(Test_DigitalIo_ArgumentosInvalidos);

		RUN_TEST(Test_AnalogIn_ReadValido);
		RUN_TEST(Test_AnalogIn_ReadLimites);
		RUN_TEST(Test_AnalogIn_ReadCanalInvalido);
		RUN_TEST(Test_AnalogIn_ReadFalhaPeriferico);
		RUN_TEST(Test_AnalogIn_ReadValorForaDaFaixa);
		RUN_TEST(Test_AnalogIn_ArgumentosInvalidos);

		RUN_TEST(Test_Pwm_Calcula1kHzTim1);
		RUN_TEST(Test_Pwm_Calcula100kHz);
		RUN_TEST(Test_Pwm_Calcula1Hz);
		RUN_TEST(Test_Pwm_CalculaFrequenciasTipicas);
		RUN_TEST(Test_Pwm_CalculaRazaoLimites);
		RUN_TEST(Test_Pwm_CalculaForaDaFaixa);
		RUN_TEST(Test_Pwm_SetValido);
		RUN_TEST(Test_Pwm_SetInvalido);
		RUN_TEST(Test_Pwm_StopMantemFrequencia);
		RUN_TEST(Test_Pwm_SafeState);
		RUN_TEST(Test_Pwm_SafeStateComFalha);
		RUN_TEST(Test_Pwm_FalhaNaoGuardaConfig);
		RUN_TEST(Test_Pwm_ArgumentosInvalidos);

		RUN_TEST(Test_I2cBus_InitInvalido);
		RUN_TEST(Test_I2cBus_ConfigEscravo);
		RUN_TEST(Test_I2cBus_ConfigInvalido);
		RUN_TEST(Test_I2cBus_ConfigMestreIgnoraEndereco);
		RUN_TEST(Test_I2cBus_ConfigFalhaBsp);
		RUN_TEST(Test_I2cBus_LoadDump);
		RUN_TEST(Test_I2cBus_LoadDumpInvalido);
		RUN_TEST(Test_I2cBus_LoadSemConfigTrava);
		RUN_TEST(Test_I2cBus_EscritaDoDut);
		RUN_TEST(Test_I2cBus_LeituraDoDut);
		RUN_TEST(Test_I2cBus_PonteiroEntreLeituras);
		RUN_TEST(Test_I2cBus_FimLeituraDevolveByte);
		RUN_TEST(Test_I2cBus_PonteiroVolta);
		RUN_TEST(Test_I2cBus_MasterXferNaoConfigurado);
		RUN_TEST(Test_I2cBus_MasterXfer);
		RUN_TEST(Test_I2cBus_SafeState);
		RUN_TEST(Test_I2cBus_SafeStateComFalha);

		RUN_TEST(Test_SpiBus_InitInvalido);
		RUN_TEST(Test_SpiBus_Divisor);
		RUN_TEST(Test_SpiBus_DivisorForaDaFaixa);
		RUN_TEST(Test_SpiBus_ConfigMestre);
		RUN_TEST(Test_SpiBus_ConfigEscravoIgnoraFrequencia);
		RUN_TEST(Test_SpiBus_ConfigInvalido);
		RUN_TEST(Test_SpiBus_ConfigFalhaBsp);
		RUN_TEST(Test_SpiBus_LoadPreparaTx);
		RUN_TEST(Test_SpiBus_LoadLimites);
		RUN_TEST(Test_SpiBus_LoadInvalido);
		RUN_TEST(Test_SpiBus_FimTransacaoERead);
		RUN_TEST(Test_SpiBus_RecepcaoCheia);
		RUN_TEST(Test_SpiBus_LoadReadSemConfigTravam);
		RUN_TEST(Test_SpiBus_MasterXferNaoConfigurado);
		RUN_TEST(Test_SpiBus_MasterXfer);
		RUN_TEST(Test_SpiBus_SafeState);
		RUN_TEST(Test_SpiBus_SafeStateComFalha);

		RUN_TEST(Test_UartBus_InitInvalido);
		RUN_TEST(Test_UartBus_ConfigValido);
		RUN_TEST(Test_UartBus_ConfigInvalido);
		RUN_TEST(Test_UartBus_ConfigFalhaBsp);
		RUN_TEST(Test_UartBus_SemConfig);
		RUN_TEST(Test_UartBus_Write);
		RUN_TEST(Test_UartBus_RecebeERead);
		RUN_TEST(Test_UartBus_ReadParcial);
		RUN_TEST(Test_UartBus_ReadLimitado);
		RUN_TEST(Test_UartBus_BufferCircularVolta);
		RUN_TEST(Test_UartBus_BufferCheio);
		RUN_TEST(Test_UartBus_ConfigEsvazia);
		RUN_TEST(Test_UartBus_ReadTrava);
		RUN_TEST(Test_UartBus_SafeState);
		RUN_TEST(Test_UartBus_SafeStateComFalha);

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
