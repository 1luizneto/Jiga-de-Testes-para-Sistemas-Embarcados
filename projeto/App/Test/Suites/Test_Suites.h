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


#endif /* APP_TEST_SUITES_TEST_SUITES_H_ */
