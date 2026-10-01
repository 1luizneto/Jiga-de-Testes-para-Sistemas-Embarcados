#include "unity.h"
#include "Crc16.h"
#include "Bsp_Crc.h"


void test_Crc16_hardware_vetor_referencia(void)
{
	const uint8_t data[] = "123456789";
	uint16_t out = 0;

	BspCrc_Init();

	TEST_ASSERT_EQUAL(eBSP_CRC16_RETURN_OK, Crc16_Calcule(data, 9, &out, dCRC_INITIAL_VALUE));
	TEST_ASSERT_EQUAL_HEX16(0x29B1, out);

}
