/***********************************************************************************************************************
 *   @file         drivers_sensores.c
 *   @brief        Drivers do MPU-6500 e do BME280 do DUT de referencia, como um firmware de produto os escreveria
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Dependencias:</b>
 *   - Transferencia I2C como mestre (barramentos.c no ESP32; falsa no test_host).
 *
 *   \n <b>Observacoes:</b>
 *   - Usados para validar a emulacao de sensores da jiga: o DUT nao sabe que fala com a jiga. Cada leitura segue a
 *     sequencia de um driver real: identificacao, reset (com espera do fim), configuracao e leitura dos dados.
 *   - MPU-6500: reset pelo bit 7 do PWR_MGMT_1 (espera ele voltar a 0), WHO_AM_I = 0x70, fundo de escala de 2 g e
 *     250 graus/s, leitura de 14 bytes a partir do 0x3B (big-endian).
 *   - BME280: chip_id = 0x60, soft reset (0xB6 no 0xE0), espera o im_update do status, le a calibracao e compensa
 *     com as formulas inteiras da folha de dados (as mesmas do Software/sensores.py).
 *
 *   Changelog
 *   @version      <b>1.0.0 - 04/11/2026</b> \n Luiz Neto \n Primeira versao
 *
 *   @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include "drivers_sensores.h"

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

#define dMPU_WHO_AM_I       (0x75U)
#define dMPU_WHO_AM_I_VALOR (0x70U)
#define dMPU_GYRO_CONFIG    (0x1BU)
#define dMPU_ACCEL_CONFIG   (0x1CU)
#define dMPU_ACCEL_XOUT_H   (0x3BU)
#define dMPU_PWR_MGMT_1     (0x6BU)
#define dMPU_DEVICE_RESET   (0x80U)
#define dMPU_CLKSEL_PLL     (0x01U)

#define dBME_CHIP_ID        (0xD0U)
#define dBME_CHIP_ID_VALOR  (0x60U)
#define dBME_RESET          (0xE0U)
#define dBME_RESET_VALOR    (0xB6U)
#define dBME_STATUS         (0xF3U)
#define dBME_IM_UPDATE      (0x01U)
#define dBME_CTRL_HUM       (0xF2U)
#define dBME_CTRL_MEAS      (0xF4U)
#define dBME_CALIB_T_P      (0x88U)
#define dBME_CALIB_H        (0xE1U)
#define dBME_DADOS          (0xF7U)

// Quantas vezes o driver confere o fim do reset antes de desistir
#define dSENSORES_TENTATIVAS (10U)

/***********************************************************************************************************************
 * TIPOS LOCAIS
 **********************************************************************************************************************/

/// @brief Coeficientes de calibracao do BME280 (nomes da folha de dados)
typedef struct bmeCalib
{
	uint16_t t1;
	int16_t  t2, t3;
	uint16_t p1;
	int16_t  p2, p3, p4, p5, p6, p7, p8, p9;
	uint8_t  h1, h3;
	int16_t  h2, h4, h5;
	int8_t   h6;
} bmeCalib_t;

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static dutReturn_t Sensores_Escreve(sensoresI2cFunc_t i2c, uint8_t endereco, uint8_t registro, uint8_t valor);
static dutReturn_t Sensores_Le(sensoresI2cFunc_t i2c, uint8_t endereco, uint8_t registro, uint8_t *dados, size_t n);
static int32_t Sensores_BmeTFine(const bmeCalib_t *c, int32_t adcT);
static uint32_t Sensores_BmePressao(const bmeCalib_t *c, int32_t adcP, int32_t tFine);
static uint32_t Sensores_BmeUmidade(const bmeCalib_t *c, int32_t adcH, int32_t tFine);

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Le o MPU-6500: reset, identificacao, configuracao (2 g, 250 graus/s) e os 14 bytes de dados.
 * @param[in]  i2c       Transferencia como mestre
 * @param[in]  endereco  0x68 ou 0x69
 * @param[out] leitura   Aceleracao (g), giroscopio (graus/s) e temperatura (graus C)
 * @return     eDUT_RETURN_OK, o erro do barramento ou eDUT_RETURN_ERROR (WHO_AM_I errado ou reset que nao termina)
 **********************************************************************************************************************/
dutReturn_t Sensores_LeMpu(sensoresI2cFunc_t i2c, uint8_t endereco, sensoresMpu_t *leitura)
{
	uint8_t dados[14];
	uint8_t valor = 0U;
	dutReturn_t ret;

	if ((i2c == NULL) || (leitura == NULL))
	{
		return eDUT_RETURN_INVALID_ARGUMENT;
	}

	ret = Sensores_Escreve(i2c, endereco, dMPU_PWR_MGMT_1, dMPU_DEVICE_RESET);
	for (uint8_t i = 0U; (ret == eDUT_RETURN_OK) && (i < dSENSORES_TENTATIVAS); i++)
	{
		ret = Sensores_Le(i2c, endereco, dMPU_PWR_MGMT_1, &valor, 1U);
		if ((ret == eDUT_RETURN_OK) && ((valor & dMPU_DEVICE_RESET) == 0U))
		{
			break;
		}
	}
	if ((ret == eDUT_RETURN_OK) && ((valor & dMPU_DEVICE_RESET) != 0U))
	{
		return eDUT_RETURN_ERROR;
	}

	if (ret == eDUT_RETURN_OK) { ret = Sensores_Escreve(i2c, endereco, dMPU_PWR_MGMT_1, dMPU_CLKSEL_PLL); }
	if (ret == eDUT_RETURN_OK) { ret = Sensores_Le(i2c, endereco, dMPU_WHO_AM_I, &valor, 1U); }
	if ((ret == eDUT_RETURN_OK) && (valor != dMPU_WHO_AM_I_VALOR))
	{
		return eDUT_RETURN_ERROR;
	}
	if (ret == eDUT_RETURN_OK) { ret = Sensores_Escreve(i2c, endereco, dMPU_GYRO_CONFIG, 0x00U); }
	if (ret == eDUT_RETURN_OK) { ret = Sensores_Escreve(i2c, endereco, dMPU_ACCEL_CONFIG, 0x00U); }
	if (ret == eDUT_RETURN_OK) { ret = Sensores_Le(i2c, endereco, dMPU_ACCEL_XOUT_H, dados, sizeof(dados)); }
	if (ret != eDUT_RETURN_OK)
	{
		return ret;
	}

	for (uint8_t i = 0U; i < 3U; i++)
	{
		int16_t acel = (int16_t)(((uint16_t)dados[2U * i] << 8) | dados[(2U * i) + 1U]);
		int16_t giro = (int16_t)(((uint16_t)dados[8U + (2U * i)] << 8) | dados[9U + (2U * i)]);
		leitura->aceleracao[i] = (float)acel * 2.0f / 32768.0f;
		leitura->giro[i]       = (float)giro * 250.0f / 32768.0f;
	}
	leitura->temperatura = ((float)(int16_t)(((uint16_t)dados[6] << 8) | dados[7]) / 333.87f) + 21.0f;

	return eDUT_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Le o BME280: identificacao, soft reset, calibracao, modo normal e os 8 bytes de dados compensados.
 * @param[in]  i2c       Transferencia como mestre
 * @param[in]  endereco  0x76 ou 0x77
 * @param[out] leitura   Temperatura (graus C), pressao (hPa) e umidade (%)
 * @return     eDUT_RETURN_OK, o erro do barramento ou eDUT_RETURN_ERROR (chip_id errado ou reset que nao termina)
 **********************************************************************************************************************/
dutReturn_t Sensores_LeBme(sensoresI2cFunc_t i2c, uint8_t endereco, sensoresBme_t *leitura)
{
	uint8_t tp[26];
	uint8_t h[7];
	uint8_t d[8];
	uint8_t valor = 0U;
	bmeCalib_t c;
	dutReturn_t ret;

	if ((i2c == NULL) || (leitura == NULL))
	{
		return eDUT_RETURN_INVALID_ARGUMENT;
	}

	ret = Sensores_Le(i2c, endereco, dBME_CHIP_ID, &valor, 1U);
	if ((ret == eDUT_RETURN_OK) && (valor != dBME_CHIP_ID_VALOR))
	{
		return eDUT_RETURN_ERROR;
	}
	if (ret == eDUT_RETURN_OK) { ret = Sensores_Escreve(i2c, endereco, dBME_RESET, dBME_RESET_VALOR); }
	for (uint8_t i = 0U; (ret == eDUT_RETURN_OK) && (i < dSENSORES_TENTATIVAS); i++)
	{
		ret = Sensores_Le(i2c, endereco, dBME_STATUS, &valor, 1U);
		if ((ret == eDUT_RETURN_OK) && ((valor & dBME_IM_UPDATE) == 0U))
		{
			break;
		}
	}
	if ((ret == eDUT_RETURN_OK) && ((valor & dBME_IM_UPDATE) != 0U))
	{
		return eDUT_RETURN_ERROR;
	}
	if (ret == eDUT_RETURN_OK) { ret = Sensores_Le(i2c, endereco, dBME_CALIB_T_P, tp, sizeof(tp)); }
	if (ret == eDUT_RETURN_OK) { ret = Sensores_Le(i2c, endereco, dBME_CALIB_H, h, sizeof(h)); }
	if (ret == eDUT_RETURN_OK) { ret = Sensores_Escreve(i2c, endereco, dBME_CTRL_HUM, 0x01U); }
	if (ret == eDUT_RETURN_OK) { ret = Sensores_Escreve(i2c, endereco, dBME_CTRL_MEAS, 0x27U); }
	if (ret == eDUT_RETURN_OK) { ret = Sensores_Le(i2c, endereco, dBME_DADOS, d, sizeof(d)); }
	if (ret != eDUT_RETURN_OK)
	{
		return ret;
	}

	c.t1 = (uint16_t)(tp[0] | (tp[1] << 8));
	c.t2 = (int16_t)(tp[2] | (tp[3] << 8));
	c.t3 = (int16_t)(tp[4] | (tp[5] << 8));
	c.p1 = (uint16_t)(tp[6] | (tp[7] << 8));
	c.p2 = (int16_t)(tp[8] | (tp[9] << 8));
	c.p3 = (int16_t)(tp[10] | (tp[11] << 8));
	c.p4 = (int16_t)(tp[12] | (tp[13] << 8));
	c.p5 = (int16_t)(tp[14] | (tp[15] << 8));
	c.p6 = (int16_t)(tp[16] | (tp[17] << 8));
	c.p7 = (int16_t)(tp[18] | (tp[19] << 8));
	c.p8 = (int16_t)(tp[20] | (tp[21] << 8));
	c.p9 = (int16_t)(tp[22] | (tp[23] << 8));
	c.h1 = tp[25];
	c.h2 = (int16_t)(h[0] | (h[1] << 8));
	c.h3 = h[2];
	c.h4 = (int16_t)((int16_t)((int8_t)h[3]) * 16 | (h[4] & 0x0F));
	c.h5 = (int16_t)((int16_t)((int8_t)h[5]) * 16 | (h[4] >> 4));
	c.h6 = (int8_t)h[6];

	int32_t adcP = (int32_t)(((uint32_t)d[0] << 12) | ((uint32_t)d[1] << 4) | ((uint32_t)d[2] >> 4));
	int32_t adcT = (int32_t)(((uint32_t)d[3] << 12) | ((uint32_t)d[4] << 4) | ((uint32_t)d[5] >> 4));
	int32_t adcH = (int32_t)(((uint32_t)d[6] << 8) | d[7]);
	int32_t tFine = Sensores_BmeTFine(&c, adcT);

	leitura->temperatura = (float)((tFine * 5 + 128) >> 8) / 100.0f;
	leitura->pressao     = (float)Sensores_BmePressao(&c, adcP, tFine) / 256.0f / 100.0f;
	leitura->umidade     = (float)Sensores_BmeUmidade(&c, adcH, tFine) / 1024.0f;

	return eDUT_RETURN_OK;
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Escreve um registrador: [registro][valor].
 **********************************************************************************************************************/
static dutReturn_t Sensores_Escreve(sensoresI2cFunc_t i2c, uint8_t endereco, uint8_t registro, uint8_t valor)
{
	const uint8_t escrita[2] = {registro, valor};

	return i2c(endereco, escrita, sizeof(escrita), NULL, 0U);
}

/***********************************************************************************************************************
 * @brief      Le n registradores a partir de registro (escrita do ponteiro + leitura com repeated start).
 **********************************************************************************************************************/
static dutReturn_t Sensores_Le(sensoresI2cFunc_t i2c, uint8_t endereco, uint8_t registro, uint8_t *dados, size_t n)
{
	return i2c(endereco, &registro, 1U, dados, n);
}

/***********************************************************************************************************************
 * @brief      t_fine da folha de dados (temperatura em resolucao fina, usada pela pressao e pela umidade).
 **********************************************************************************************************************/
static int32_t Sensores_BmeTFine(const bmeCalib_t *c, int32_t adcT)
{
	int32_t var1 = ((((adcT >> 3) - ((int32_t)c->t1 << 1))) * ((int32_t)c->t2)) >> 11;
	int32_t var2 = (((((adcT >> 4) - ((int32_t)c->t1)) * ((adcT >> 4) - ((int32_t)c->t1))) >> 12) *
	                ((int32_t)c->t3)) >> 14;

	return var1 + var2;
}

/***********************************************************************************************************************
 * @brief      Pressao em Pa no formato Q24.8 (formula de 64 bits da folha de dados).
 **********************************************************************************************************************/
static uint32_t Sensores_BmePressao(const bmeCalib_t *c, int32_t adcP, int32_t tFine)
{
	int64_t var1 = ((int64_t)tFine) - 128000;
	int64_t var2 = var1 * var1 * (int64_t)c->p6;
	int64_t p;

	var2 = var2 + ((var1 * (int64_t)c->p5) * 131072);
	var2 = var2 + (((int64_t)c->p4) * 34359738368LL);
	var1 = ((var1 * var1 * (int64_t)c->p3) >> 8) + ((var1 * (int64_t)c->p2) * 4096);
	var1 = (((((int64_t)1) * 140737488355328LL) + var1) * ((int64_t)c->p1)) >> 33;
	if (var1 == 0)
	{
		return 0U;
	}
	p = 1048576 - adcP;
	p = (((p * 2147483648LL) - var2) * 3125) / var1;
	var1 = (((int64_t)c->p9) * (p >> 13) * (p >> 13)) >> 25;
	var2 = (((int64_t)c->p8) * p) >> 19;
	p = ((p + var1 + var2) >> 8) + (((int64_t)c->p7) << 4);

	return (uint32_t)p;
}

/***********************************************************************************************************************
 * @brief      Umidade em % no formato Q22.10 (formula de 32 bits da folha de dados).
 **********************************************************************************************************************/
static uint32_t Sensores_BmeUmidade(const bmeCalib_t *c, int32_t adcH, int32_t tFine)
{
	int32_t v = tFine - 76800;

	v = (((((adcH << 14) - (((int32_t)c->h4) << 20) - (((int32_t)c->h5) * v)) + 16384) >> 15) *
	     (((((((v * ((int32_t)c->h6)) >> 10) * (((v * ((int32_t)c->h3)) >> 11) + 32768)) >> 10) + 2097152) *
	       ((int32_t)c->h2) + 8192) >> 14));
	v = v - (((((v >> 15) * (v >> 15)) >> 7) * ((int32_t)c->h1)) >> 4);
	v = (v < 0) ? 0 : v;
	v = (v > 419430400) ? 419430400 : v;

	return (uint32_t)(v >> 12);
}

/** @} DOXYGEN GROUP TAG END OF FILE */
