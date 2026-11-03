/*
 * Test_Suites.h
 *
 *  Created on: 30 de set. de 2026
 *      Author: LuizOliveiradeSouzaN
 */

#ifndef APP_TEST_SUITES_TEST_SUITES_H_
#define APP_TEST_SUITES_TEST_SUITES_H_

// Testes CRC16
void Test_Crc16_HardwareVetorReferencia(void);
void Test_Crc16_HardwareInicialValue(void);
void Test_Crc16_HardwareDataEmpty(void);
void Test_Crc16_HardwareCalculosSeguidos(void);
void Test_Crc16_HardwareBUfferMaximoComm(void);

// Testes do parser de quadros (Comm)
void Test_Comm_PingValido(void);
void Test_Comm_PayloadCom7E(void);
void Test_Comm_PayloadMaximo(void);
void Test_Comm_CrcInvalido(void);
void Test_Comm_LenInvalido(void);
void Test_Comm_LixoAntesDoSof(void);
void Test_Comm_DoisQuadrosSeguidos(void);
void Test_Comm_TimeoutDescartaQuadroParcial(void);
void Test_Comm_QuadroParcialSemTimeout(void);
void Test_Comm_FrameNulo(void);

// Testes da montagem de quadros (Comm_BuildFrame)
void Test_Comm_BuildAckPing(void);
void Test_Comm_BuildAckSemPayload(void);
void Test_Comm_BuildNack(void);
void Test_Comm_BuildIdaEVolta(void);
void Test_Comm_BuildArgumentosInvalidos(void);
void Test_Comm_BuildBufferPequeno(void);

// Testes da decisao de resposta (Comm_ProcessFrame)
void Test_Comm_ProcessPing(void);
void Test_Comm_ProcessRepassaComando(void);
void Test_Comm_ProcessSeqRepetido(void);
void Test_Comm_ProcessSeqNovo(void);
void Test_Comm_ProcessInitLimpaCache(void);
void Test_Comm_ProcessArgumentosInvalidos(void);

// Testes do despachante de comandos (Exec)
void Test_Exec_DacSetValido(void);
void Test_Exec_DacSetLimites(void);
void Test_Exec_DacSetCanalInvalido(void);
void Test_Exec_DacSetValorInvalido(void);
void Test_Exec_DacSetTamanhoErrado(void);
void Test_Exec_ResetAll(void);
void Test_Exec_ResetAllComPayload(void);
void Test_Exec_CmdDesconhecido(void);
void Test_Exec_DacSetFalhaPeriferico(void);
void Test_Exec_ArgumentosInvalidos(void);
void Test_Exec_GpioSetValido(void);
void Test_Exec_GpioSetParametroInvalido(void);
void Test_Exec_GpioSetTamanhoErrado(void);
void Test_Exec_GpioReadValido(void);
void Test_Exec_GpioReadComPayload(void);
void Test_Exec_AdcReadValido(void);
void Test_Exec_AdcReadCanalInvalido(void);
void Test_Exec_AdcReadTamanhoErrado(void);
void Test_Exec_AdcReadFalhaPeriferico(void);
void Test_Exec_PwmSetValido(void);
void Test_Exec_PwmSetFrequenciaU32(void);
void Test_Exec_PwmSetParametroInvalido(void);
void Test_Exec_PwmSetTamanhoErrado(void);
void Test_Exec_PwmStopValido(void);
void Test_Exec_PwmStopInvalido(void);
void Test_Exec_PwmSetFalhaPeriferico(void);
void Test_Exec_I2cConfigValido(void);
void Test_Exec_I2cConfigInvalido(void);
void Test_Exec_I2cLoadDump(void);
void Test_Exec_I2cLoadInvalido(void);
void Test_Exec_I2cDumpInvalido(void);
void Test_Exec_I2cXferNaoConfigurado(void);
void Test_Exec_I2cXferValido(void);
void Test_Exec_I2cXferInvalido(void);
void Test_Exec_I2cXferSemAck(void);
void Test_Exec_ResetAllZeraMapaI2c(void);
void Test_Exec_SpiConfigMestre(void);
void Test_Exec_SpiConfigInvalido(void);
void Test_Exec_SpiLoadValido(void);
void Test_Exec_SpiLoadInvalido(void);
void Test_Exec_SpiReadValido(void);
void Test_Exec_SpiReadCheio(void);
void Test_Exec_SpiReadComPayload(void);
void Test_Exec_SpiXferNaoConfigurado(void);
void Test_Exec_SpiXferValido(void);
void Test_Exec_SpiXferInvalido(void);
void Test_Exec_SpiXferFalhaPeriferico(void);
void Test_Exec_ResetAllEsvaziaSpi(void);
void Test_Exec_UartConfigValido(void);
void Test_Exec_UartConfigInvalido(void);
void Test_Exec_UartWriteNaoConfigurado(void);
void Test_Exec_UartWriteValido(void);
void Test_Exec_UartWriteInvalido(void);
void Test_Exec_UartWriteFalhaPeriferico(void);
void Test_Exec_UartReadNaoConfigurado(void);
void Test_Exec_UartReadValido(void);
void Test_Exec_UartReadMaximoGrande(void);
void Test_Exec_UartReadTamanhoErrado(void);
void Test_Exec_ResetAllDesligaUart(void);

// Testes do repasse e do resultado (Comm_BuildResult)
void Test_Comm_ResultAck(void);
void Test_Comm_ResultNack(void);
void Test_Comm_ResultComDados(void);
void Test_Comm_PendenteRetransmissaoIgnorada(void);
void Test_Comm_PendenteOcupado(void);
void Test_Comm_PendentePingAtendido(void);
void Test_Comm_ResultInesperado(void);
void Test_Comm_ResultArgumentosInvalidos(void);

// Testes das entradas e saidas digitais (DigitalIo)
void Test_DigitalIo_SetValido(void);
void Test_DigitalIo_SetLimites(void);
void Test_DigitalIo_SetSaidaInvalida(void);
void Test_DigitalIo_SetNivelInvalido(void);
void Test_DigitalIo_ReadMascara(void);
void Test_DigitalIo_ReadExtremos(void);
void Test_DigitalIo_ReadFalha(void);
void Test_DigitalIo_SafeState(void);
void Test_DigitalIo_SafeStateComFalha(void);
void Test_DigitalIo_ArgumentosInvalidos(void);

// Testes das entradas analogicas (AnalogIn)
void Test_AnalogIn_ReadValido(void);
void Test_AnalogIn_ReadLimites(void);
void Test_AnalogIn_ReadCanalInvalido(void);
void Test_AnalogIn_ReadFalhaPeriferico(void);
void Test_AnalogIn_ReadValorForaDaFaixa(void);
void Test_AnalogIn_ArgumentosInvalidos(void);

// Testes das saidas PWM (Pwm)
void Test_Pwm_Calcula1kHzTim1(void);
void Test_Pwm_Calcula100kHz(void);
void Test_Pwm_Calcula1Hz(void);
void Test_Pwm_CalculaFrequenciasTipicas(void);
void Test_Pwm_CalculaRazaoLimites(void);
void Test_Pwm_CalculaForaDaFaixa(void);
void Test_Pwm_SetValido(void);
void Test_Pwm_SetInvalido(void);
void Test_Pwm_StopMantemFrequencia(void);
void Test_Pwm_SafeState(void);
void Test_Pwm_SafeStateComFalha(void);
void Test_Pwm_FalhaNaoGuardaConfig(void);
void Test_Pwm_ArgumentosInvalidos(void);

// Testes do barramento I2C (I2cBus)
void Test_I2cBus_InitInvalido(void);
void Test_I2cBus_ConfigEscravo(void);
void Test_I2cBus_ConfigInvalido(void);
void Test_I2cBus_ConfigMestreIgnoraEndereco(void);
void Test_I2cBus_ConfigFalhaBsp(void);
void Test_I2cBus_LoadDump(void);
void Test_I2cBus_LoadDumpInvalido(void);
void Test_I2cBus_LoadSemConfigTrava(void);
void Test_I2cBus_EscritaDoDut(void);
void Test_I2cBus_LeituraDoDut(void);
void Test_I2cBus_PonteiroEntreLeituras(void);
void Test_I2cBus_FimLeituraDevolveByte(void);
void Test_I2cBus_PonteiroVolta(void);
void Test_I2cBus_MasterXferNaoConfigurado(void);
void Test_I2cBus_MasterXfer(void);
void Test_I2cBus_SafeState(void);
void Test_I2cBus_SafeStateComFalha(void);

// Testes do barramento SPI (SpiBus)
void Test_SpiBus_InitInvalido(void);
void Test_SpiBus_Divisor(void);
void Test_SpiBus_DivisorForaDaFaixa(void);
void Test_SpiBus_ConfigMestre(void);
void Test_SpiBus_ConfigEscravoIgnoraFrequencia(void);
void Test_SpiBus_ConfigInvalido(void);
void Test_SpiBus_ConfigFalhaBsp(void);
void Test_SpiBus_LoadPreparaTx(void);
void Test_SpiBus_LoadLimites(void);
void Test_SpiBus_LoadInvalido(void);
void Test_SpiBus_FimTransacaoERead(void);
void Test_SpiBus_RecepcaoCheia(void);
void Test_SpiBus_LoadReadSemConfigTravam(void);
void Test_SpiBus_MasterXferNaoConfigurado(void);
void Test_SpiBus_MasterXfer(void);
void Test_SpiBus_SafeState(void);
void Test_SpiBus_SafeStateComFalha(void);

// Testes da UART do DUT (UartBus)
void Test_UartBus_InitInvalido(void);
void Test_UartBus_ConfigValido(void);
void Test_UartBus_ConfigInvalido(void);
void Test_UartBus_ConfigFalhaBsp(void);
void Test_UartBus_SemConfig(void);
void Test_UartBus_Write(void);
void Test_UartBus_RecebeERead(void);
void Test_UartBus_ReadParcial(void);
void Test_UartBus_ReadLimitado(void);
void Test_UartBus_BufferCircularVolta(void);
void Test_UartBus_BufferCheio(void);
void Test_UartBus_ConfigEsvazia(void);
void Test_UartBus_ReadTrava(void);
void Test_UartBus_SafeState(void);
void Test_UartBus_SafeStateComFalha(void);

#endif /* APP_TEST_SUITES_TEST_SUITES_H_ */
