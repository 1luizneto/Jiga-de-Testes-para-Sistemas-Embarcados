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

#endif /* APP_TEST_SUITES_TEST_SUITES_H_ */
