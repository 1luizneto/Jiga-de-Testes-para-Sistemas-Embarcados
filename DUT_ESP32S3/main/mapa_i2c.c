/***********************************************************************************************************************
 *   @file         mapa_i2c.c
 *   @brief        Mapa de 256 registradores do DUT escravo I2C, no padrao de uma EEPROM 24C02 (o mesmo da jiga)
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Dependencias:</b>
 *   - Nenhuma (logica pura, testada no PC pelo test_host).
 *
 *   \n <b>Observacoes:</b>
 *   - Na escrita do mestre, o 1o byte e o ponteiro e os seguintes sao gravados com auto-incremento; a leitura
 *     sai a partir do ponteiro, sem move-lo (ver MapaI2c_Leitura). Depois de 0xFF volta a 0x00.
 *   - Nao tem protecao de concorrencia: quem chama (barramentos.c) usa um mutex.
 *
 *   Changelog
 *   @version      <b>1.0.0 - 03/11/2026</b> \n Luiz Neto \n Primeira versao
 *
 *   @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include "mapa_i2c.h"
#include <string.h>

/***********************************************************************************************************************
 * VARIAVEIS LOCAIS
 **********************************************************************************************************************/

static struct mapaI2c
{
	uint8_t registros[dMAPAI2C_TAMANHO];   ///< Registradores
	uint8_t ponteiro;                      ///< Proximo registrador lido ou escrito pelo mestre
} mapaI2c;

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Zera os registradores e o ponteiro.
 **********************************************************************************************************************/
void MapaI2c_Init(void)
{
	memset(&mapaI2c, 0, sizeof(mapaI2c));
}

/***********************************************************************************************************************
 * @brief      Carrega registradores (comando I2CS_LOAD), sem mexer no ponteiro.
 * @param[in]  offset      Primeiro registrador
 * @param[in]  dados       Bytes a gravar
 * @param[in]  quantidade  1 a 256, sem passar do fim do mapa
 * @return     eMAPAI2C_RETURN_OK ou eMAPAI2C_RETURN_INVALID_ARGUMENT
 **********************************************************************************************************************/
mapaI2cReturn_t MapaI2c_Carrega(uint8_t offset, const uint8_t *dados, size_t quantidade)
{
	if ((dados == NULL) || (quantidade == 0U) || (((size_t)offset + quantidade) > dMAPAI2C_TAMANHO))
	{
		return eMAPAI2C_RETURN_INVALID_ARGUMENT;
	}

	memcpy(&mapaI2c.registros[offset], dados, quantidade);

	return eMAPAI2C_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Le registradores (comando I2CS_DUMP), sem mexer no ponteiro.
 * @param[in]  offset      Primeiro registrador
 * @param[out] dados       Bytes lidos
 * @param[in]  quantidade  1 a 256, sem passar do fim do mapa
 * @return     eMAPAI2C_RETURN_OK ou eMAPAI2C_RETURN_INVALID_ARGUMENT
 **********************************************************************************************************************/
mapaI2cReturn_t MapaI2c_Le(uint8_t offset, uint8_t *dados, size_t quantidade)
{
	if ((dados == NULL) || (quantidade == 0U) || (((size_t)offset + quantidade) > dMAPAI2C_TAMANHO))
	{
		return eMAPAI2C_RETURN_INVALID_ARGUMENT;
	}

	memcpy(dados, &mapaI2c.registros[offset], quantidade);

	return eMAPAI2C_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Escrita do mestre: o 1o byte vira o ponteiro e os seguintes sao gravados a partir dele.
 * @param[in]  dados       Bytes recebidos numa transacao de escrita
 * @param[in]  quantidade  Quantidade recebida (0 nao faz nada)
 **********************************************************************************************************************/
void MapaI2c_Escrita(const uint8_t *dados, size_t quantidade)
{
	if ((dados == NULL) || (quantidade == 0U))
	{
		return;
	}

	mapaI2c.ponteiro = dados[0];

	for (size_t i = 1U; i < quantidade; i++)
	{
		mapaI2c.registros[mapaI2c.ponteiro] = dados[i];
		mapaI2c.ponteiro++;
	}
}

/***********************************************************************************************************************
 * @brief      Leitura do mestre: bytes a partir do ponteiro (voltando de 0xFF para 0x00), sem mover o ponteiro.
 * @details    O driver do ESP32 entrega ao mestre um bloco ja escrito na FIFO e nao informa quantos bytes ele leu:
 *             por isso toda leitura recomeca do ultimo ponteiro escrito (o mestre escreve o ponteiro antes de ler).
 * @param[out] dados       Bytes para a FIFO do escravo
 * @param[in]  quantidade  Quantidade (ate dMAPAI2C_TAMANHO)
 **********************************************************************************************************************/
void MapaI2c_Leitura(uint8_t *dados, size_t quantidade)
{
	uint8_t ponteiro = mapaI2c.ponteiro;

	if (dados == NULL)
	{
		return;
	}

	for (size_t i = 0U; (i < quantidade) && (i < dMAPAI2C_TAMANHO); i++)
	{
		dados[i] = mapaI2c.registros[ponteiro];
		ponteiro++;
	}
}

/** @} DOXYGEN GROUP TAG END OF FILE */
