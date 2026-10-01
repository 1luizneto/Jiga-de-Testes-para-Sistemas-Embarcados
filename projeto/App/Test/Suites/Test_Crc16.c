#include "unity.h"
#include "Crc16.h"
#include "Bsp_Crc.h"

// 1. Teste padrao de conversao
void Test_Crc16_HardwareVetorReferencia(void)
{
	const uint8_t data[] = "123456789";
	uint16_t out = 0;

	BspCrc_Init();

	TEST_ASSERT_EQUAL(eBSP_CRC16_RETURN_OK, Crc16_Calcule(data, 9, &out, dCRC_INITIAL_VALUE));
	TEST_ASSERT_EQUAL_HEX16(0x29B1, out);


}

// 2. Teste mudando o inicial value
void Test_Crc16_HardwareInicialValue(void)
{

	const uint8_t data[] = "123456789";
	uint16_t out = 0;

	BspCrc_Init();

	TEST_ASSERT_EQUAL(eBSP_CRC16_RETURN_OK, Crc16_Calcule(data, 9, &out, 0x0000));
	TEST_ASSERT_EQUAL_HEX16(0x31C3, out);
}

// 3. Teste com o buffer data empty
void Test_Crc16_HardwareDataEmpty(void)
{

	const uint8_t data_empty[] = "";
	uint16_t out = 0;

	BspCrc_Init();

	TEST_ASSERT_EQUAL(eBSP_CRC16_RETURN_OK, Crc16_Calcule(data_empty, 0, &out, dCRC_INITIAL_VALUE));
	TEST_ASSERT_EQUAL_HEX16(0xFFFF, out);
}

// 4. Teste com calculos seguidos
void Test_Crc16_HardwareCalculosSeguidos(void)
{

	const uint8_t data_1[] = "123456789";
	const uint8_t data_2[] = "A";
	const uint8_t data_3[] = "Jiga";
	const uint8_t data_4[] = {0x00};
	const uint8_t data_5[] = {0xFF};
	const uint8_t data_6[] = {0x01, 0x02, 0x03, 0x04};
	const uint8_t data_7[] = {0x00, 0x00, 0x00, 0x00};
	const uint8_t data_8[] = "STM32F767";
	uint16_t out = 0;

	BspCrc_Init();

	TEST_ASSERT_EQUAL(eBSP_CRC16_RETURN_OK, Crc16_Calcule(data_1, 9, &out, dCRC_INITIAL_VALUE));
	TEST_ASSERT_EQUAL_HEX16(0x29B1, out);

	TEST_ASSERT_EQUAL(eBSP_CRC16_RETURN_OK, Crc16_Calcule(data_2, 1, &out, dCRC_INITIAL_VALUE));
	TEST_ASSERT_EQUAL_HEX16(0xB915, out);

	TEST_ASSERT_EQUAL(eBSP_CRC16_RETURN_OK, Crc16_Calcule(data_3, 4, &out, dCRC_INITIAL_VALUE));
	TEST_ASSERT_EQUAL_HEX16(0x6937, out);

	TEST_ASSERT_EQUAL(eBSP_CRC16_RETURN_OK, Crc16_Calcule(data_4, 1, &out, dCRC_INITIAL_VALUE));
	TEST_ASSERT_EQUAL_HEX16(0xE1F0, out);

	TEST_ASSERT_EQUAL(eBSP_CRC16_RETURN_OK, Crc16_Calcule(data_5, 1, &out, dCRC_INITIAL_VALUE));
	TEST_ASSERT_EQUAL_HEX16(0xFF00, out);

	TEST_ASSERT_EQUAL(eBSP_CRC16_RETURN_OK, Crc16_Calcule(data_6, 4, &out, dCRC_INITIAL_VALUE));
	TEST_ASSERT_EQUAL_HEX16(0x89C3, out);

	TEST_ASSERT_EQUAL(eBSP_CRC16_RETURN_OK, Crc16_Calcule(data_7, 4, &out, dCRC_INITIAL_VALUE));
	TEST_ASSERT_EQUAL_HEX16(0x84C0, out);

	TEST_ASSERT_EQUAL(eBSP_CRC16_RETURN_OK, Crc16_Calcule(data_8, 9, &out, dCRC_INITIAL_VALUE));
	TEST_ASSERT_EQUAL_HEX16(0x8348, out);

	TEST_ASSERT_EQUAL(eBSP_CRC16_RETURN_OK, Crc16_Calcule(data_1, 9, &out, dCRC_INITIAL_VALUE));
	TEST_ASSERT_EQUAL_HEX16(0x29B1, out);

}

// 5. Teste com o buffer maximo da comm
void Test_Crc16_HardwareBUfferMaximoComm(void)
{
	const uint8_t data[] = "012345678901234567890123456789012345678901234567890123456789"
			"012345678901234567890123456789012345678901234567890123456789"
			"012345678901234567890123456789012345678901234567890123456789"
			"012345678901234567890123456789012345678901234567890123456789012";
	uint16_t out = 0;

	BspCrc_Init();

	TEST_ASSERT_EQUAL(eBSP_CRC16_RETURN_OK, Crc16_Calcule(data, 243, &out, dCRC_INITIAL_VALUE));
	TEST_ASSERT_EQUAL_HEX16(0xB504, out);


}
