/***********************************************************************************************************************
 *   @file         comandos.c
 *   @brief        Interpretador dos comandos de texto do DUT de referencia (uma linha por comando)
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Dependencias:</b>
 *   - Operacoes de barramento recebidas no Comandos_Init (barramentos.c no ESP32; falsos no test_host).
 *
 *   \n <b>Observacoes:</b>
 *   - Logica pura: confere os argumentos, chama o barramento e monta a resposta. Roda no PC (test_host).
 *   - Comandos (dados em hex sem separador, "-" = nenhum byte; quantidades e frequencia em decimal):
 *       PING                         -> OK DUT ESP32-S3 <versao>
 *       OFF                          -> OK (libera I2C e SPI: pinos como entrada)
 *       I2CM <end> <escrita> <nr>    -> OK [lidos]   (mestre: escreve e le, com repeated start)
 *       I2CS <end>                   -> OK           (escravo no endereco, mapa 24C02)
 *       I2CS_LOAD <offset> <dados>   -> OK
 *       I2CS_DUMP <offset> <n>       -> OK <dados>
 *       SPIM <modo> <freq> <dados>   -> OK <lidos>   (mestre: CS baixo so durante a transferencia)
 *       SPIS <modo>                  -> OK           (escravo armado)
 *       SPIS_LOAD <dados>            -> OK           (o que o escravo envia; completa com FF)
 *       SPIS_READ                    -> OK [dados]   (recebido do mestre desde a ultima leitura)
 *   - Erros: ERR CMD (comando desconhecido), ERR ARG, ERR NACK, ERR ESTADO, ERR PERIFERICO.
 *
 *   Changelog
 *   @version      <b>1.0.0 - 03/11/2026</b> \n Luiz Neto \n Primeira versao
 *
 *   @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include "comandos.h"
#include "versao_dut.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

// Maior numero de campos numa linha (nome + 3 argumentos)
#define dCOMANDOS_CAMPOS_MAX (4U)

/***********************************************************************************************************************
 * TIPOS LOCAIS
 **********************************************************************************************************************/

/// @brief Funcao de um comando: confere os argumentos, executa e devolve os dados da resposta (pode ser 0 byte)
typedef dutReturn_t (*comandoFunc_t)(char **argumentos, uint8_t *dados, size_t *quantidade);

/// @brief Linha da tabela de comandos
typedef struct comando
{
	const char    *nome;
	size_t         argumentos;   ///< Quantidade exata de argumentos
	comandoFunc_t  executa;
} comando_t;

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static dutReturn_t Comandos_Off(char **argumentos, uint8_t *dados, size_t *quantidade);
static dutReturn_t Comandos_I2cMestre(char **argumentos, uint8_t *dados, size_t *quantidade);
static dutReturn_t Comandos_I2cEscrava(char **argumentos, uint8_t *dados, size_t *quantidade);
static dutReturn_t Comandos_I2cCarrega(char **argumentos, uint8_t *dados, size_t *quantidade);
static dutReturn_t Comandos_I2cLe(char **argumentos, uint8_t *dados, size_t *quantidade);
static dutReturn_t Comandos_SpiMestre(char **argumentos, uint8_t *dados, size_t *quantidade);
static dutReturn_t Comandos_SpiEscrava(char **argumentos, uint8_t *dados, size_t *quantidade);
static dutReturn_t Comandos_SpiCarrega(char **argumentos, uint8_t *dados, size_t *quantidade);
static dutReturn_t Comandos_SpiLe(char **argumentos, uint8_t *dados, size_t *quantidade);
static bool Comandos_Hex(const char *texto, uint8_t *dados, size_t *quantidade);
static bool Comandos_Decimal(const char *texto, uint32_t maximo, uint32_t *valor);
static bool Comandos_ByteHex(const char *texto, uint8_t *valor);
static size_t Comandos_Resposta(char *resposta, size_t tamanho, dutReturn_t ret, const uint8_t *dados,
                                size_t quantidade);

/***********************************************************************************************************************
 * VARIAVEIS LOCAIS
 **********************************************************************************************************************/

static const dutBarramentos_t *comandosBarramentos = NULL;

static const comando_t comandosTabela[] =
{
	{"OFF",       0U, Comandos_Off},
	{"I2CM",      3U, Comandos_I2cMestre},
	{"I2CS",      1U, Comandos_I2cEscrava},
	{"I2CS_LOAD", 2U, Comandos_I2cCarrega},
	{"I2CS_DUMP", 2U, Comandos_I2cLe},
	{"SPIM",      3U, Comandos_SpiMestre},
	{"SPIS",      1U, Comandos_SpiEscrava},
	{"SPIS_LOAD", 1U, Comandos_SpiCarrega},
	{"SPIS_READ", 0U, Comandos_SpiLe},
};

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Guarda as operacoes de barramento usadas pelos comandos.
 **********************************************************************************************************************/
void Comandos_Init(const dutBarramentos_t *barramentos)
{
	comandosBarramentos = barramentos;
}

/***********************************************************************************************************************
 * @brief      Executa uma linha de comando e monta a resposta (sempre terminada em '\n').
 * @param[in]  linha     Comando, com ou sem '\r'/'\n' no fim
 * @param[out] resposta  Texto da resposta, terminado em '\0'
 * @param[in]  tamanho   Tamanho do buffer da resposta (dCOMANDOS_RESPOSTA_MAX basta para qualquer comando)
 * @return     Caracteres escritos em resposta (sem o '\0'); 0 se a linha estiver vazia (nada a responder)
 **********************************************************************************************************************/
size_t Comandos_Executa(const char *linha, char *resposta, size_t tamanho)
{
	static uint8_t dados[dCOMANDOS_DADOS_MAX];
	char copia[dCOMANDOS_LINHA_MAX + 1U];
	char *campos[dCOMANDOS_CAMPOS_MAX + 1U];
	size_t numeroCampos = 0U;
	size_t quantidade = 0U;

	if ((linha == NULL) || (resposta == NULL) || (tamanho < 16U))
	{
		return 0U;
	}

	if (strlen(linha) > dCOMANDOS_LINHA_MAX)
	{
		return Comandos_Resposta(resposta, tamanho, eDUT_RETURN_INVALID_ARGUMENT, NULL, 0U);
	}

	strcpy(copia, linha);

	for (char *campo = strtok(copia, " \t\r\n"); campo != NULL; campo = strtok(NULL, " \t\r\n"))
	{
		if (numeroCampos > dCOMANDOS_CAMPOS_MAX)
		{
			return Comandos_Resposta(resposta, tamanho, eDUT_RETURN_INVALID_ARGUMENT, NULL, 0U);
		}
		campos[numeroCampos++] = campo;
	}

	if (numeroCampos == 0U)
	{
		return 0U;
	}

	if (strcmp(campos[0], "PING") == 0)
	{
		if (numeroCampos != 1U)
		{
			return Comandos_Resposta(resposta, tamanho, eDUT_RETURN_INVALID_ARGUMENT, NULL, 0U);
		}
		return (size_t)snprintf(resposta, tamanho, "OK DUT ESP32-S3 %s\n", dDUT_VERSAO);
	}

	for (size_t i = 0U; i < (sizeof(comandosTabela) / sizeof(comandosTabela[0])); i++)
	{
		if (strcmp(campos[0], comandosTabela[i].nome) == 0)
		{
			dutReturn_t ret = eDUT_RETURN_INVALID_ARGUMENT;

			if ((numeroCampos - 1U) == comandosTabela[i].argumentos)
			{
				ret = (comandosBarramentos == NULL) ? eDUT_RETURN_ERROR :
				      comandosTabela[i].executa(&campos[1], dados, &quantidade);
			}

			return Comandos_Resposta(resposta, tamanho, ret, dados, quantidade);
		}
	}

	return (size_t)snprintf(resposta, tamanho, "ERR CMD\n");
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      OFF: libera o I2C e o SPI (pinos como entrada).
 **********************************************************************************************************************/
static dutReturn_t Comandos_Off(char **argumentos, uint8_t *dados, size_t *quantidade)
{
	(void)argumentos;
	(void)dados;
	(void)quantidade;

	return comandosBarramentos->desliga();
}

/***********************************************************************************************************************
 * @brief      I2CM <endereco hex> <escrita hex ou -> <nr decimal>: como mestre, escreve e depois le nr bytes.
 * @details    nw + nr de 1 a 239 cada um, com ao menos um byte na transacao.
 **********************************************************************************************************************/
static dutReturn_t Comandos_I2cMestre(char **argumentos, uint8_t *dados, size_t *quantidade)
{
	static uint8_t escrita[dCOMANDOS_DADOS_MAX];
	uint8_t endereco = 0U;
	size_t nw = 0U;
	uint32_t nr = 0U;

	if (!Comandos_ByteHex(argumentos[0], &endereco) || (endereco < dCOMANDOS_I2C_ENDERECO_MIN) ||
	    (endereco > dCOMANDOS_I2C_ENDERECO_MAX) || !Comandos_Hex(argumentos[1], escrita, &nw) ||
	    !Comandos_Decimal(argumentos[2], dCOMANDOS_DADOS_MAX, &nr) || ((nw + nr) == 0U))
	{
		return eDUT_RETURN_INVALID_ARGUMENT;
	}

	dutReturn_t ret = comandosBarramentos->i2cMestre(endereco, escrita, nw, dados, nr);

	if (ret == eDUT_RETURN_OK)
	{
		*quantidade = nr;
	}

	return ret;
}

/***********************************************************************************************************************
 * @brief      I2CS <endereco hex>: vira escravo no endereco, servindo o mapa de registradores.
 **********************************************************************************************************************/
static dutReturn_t Comandos_I2cEscrava(char **argumentos, uint8_t *dados, size_t *quantidade)
{
	uint8_t endereco = 0U;

	(void)dados;
	(void)quantidade;

	if (!Comandos_ByteHex(argumentos[0], &endereco) || (endereco < dCOMANDOS_I2C_ENDERECO_MIN) ||
	    (endereco > dCOMANDOS_I2C_ENDERECO_MAX))
	{
		return eDUT_RETURN_INVALID_ARGUMENT;
	}

	return comandosBarramentos->i2cEscrava(endereco);
}

/***********************************************************************************************************************
 * @brief      I2CS_LOAD <offset hex> <dados hex>: carrega o mapa do escravo (funciona sem I2CS).
 **********************************************************************************************************************/
static dutReturn_t Comandos_I2cCarrega(char **argumentos, uint8_t *dados, size_t *quantidade)
{
	static uint8_t carga[dCOMANDOS_DADOS_MAX];
	uint8_t offset = 0U;
	size_t n = 0U;

	(void)dados;
	(void)quantidade;

	if (!Comandos_ByteHex(argumentos[0], &offset) || !Comandos_Hex(argumentos[1], carga, &n) || (n == 0U) ||
	    (((size_t)offset + n) > 256U))
	{
		return eDUT_RETURN_INVALID_ARGUMENT;
	}

	return comandosBarramentos->i2cCarrega(offset, carga, n);
}

/***********************************************************************************************************************
 * @brief      I2CS_DUMP <offset hex> <n decimal>: le o mapa do escravo (o que o mestre escreveu).
 **********************************************************************************************************************/
static dutReturn_t Comandos_I2cLe(char **argumentos, uint8_t *dados, size_t *quantidade)
{
	uint8_t offset = 0U;
	uint32_t n = 0U;

	if (!Comandos_ByteHex(argumentos[0], &offset) || !Comandos_Decimal(argumentos[1], dCOMANDOS_DADOS_MAX, &n) ||
	    (n == 0U) || (((size_t)offset + n) > 256U))
	{
		return eDUT_RETURN_INVALID_ARGUMENT;
	}

	dutReturn_t ret = comandosBarramentos->i2cLe(offset, dados, n);

	if (ret == eDUT_RETURN_OK)
	{
		*quantidade = n;
	}

	return ret;
}

/***********************************************************************************************************************
 * @brief      SPIM <modo> <frequencia Hz> <dados hex>: como mestre, envia os dados e recebe a mesma quantidade.
 **********************************************************************************************************************/
static dutReturn_t Comandos_SpiMestre(char **argumentos, uint8_t *dados, size_t *quantidade)
{
	static uint8_t escrita[dCOMANDOS_DADOS_MAX];
	uint32_t modo = 0U;
	uint32_t frequencia = 0U;
	size_t n = 0U;

	if (!Comandos_Decimal(argumentos[0], dCOMANDOS_SPI_MODO_MAX, &modo) ||
	    !Comandos_Decimal(argumentos[1], dCOMANDOS_SPI_FREQUENCIA_MAX, &frequencia) ||
	    (frequencia < dCOMANDOS_SPI_FREQUENCIA_MIN) || !Comandos_Hex(argumentos[2], escrita, &n) || (n == 0U))
	{
		return eDUT_RETURN_INVALID_ARGUMENT;
	}

	dutReturn_t ret = comandosBarramentos->spiMestre((uint8_t)modo, frequencia, escrita, dados, n);

	if (ret == eDUT_RETURN_OK)
	{
		*quantidade = n;
	}

	return ret;
}

/***********************************************************************************************************************
 * @brief      SPIS <modo>: arma o escravo SPI (o CS vem do mestre).
 **********************************************************************************************************************/
static dutReturn_t Comandos_SpiEscrava(char **argumentos, uint8_t *dados, size_t *quantidade)
{
	uint32_t modo = 0U;

	(void)dados;
	(void)quantidade;

	if (!Comandos_Decimal(argumentos[0], dCOMANDOS_SPI_MODO_MAX, &modo))
	{
		return eDUT_RETURN_INVALID_ARGUMENT;
	}

	return comandosBarramentos->spiEscrava((uint8_t)modo);
}

/***********************************************************************************************************************
 * @brief      SPIS_LOAD <dados hex ou ->: o que o escravo envia a cada transacao (do byte 0, completando com FF).
 **********************************************************************************************************************/
static dutReturn_t Comandos_SpiCarrega(char **argumentos, uint8_t *dados, size_t *quantidade)
{
	static uint8_t carga[dCOMANDOS_DADOS_MAX];
	size_t n = 0U;

	(void)dados;
	(void)quantidade;

	if (!Comandos_Hex(argumentos[0], carga, &n))
	{
		return eDUT_RETURN_INVALID_ARGUMENT;
	}

	return comandosBarramentos->spiCarrega(carga, n);
}

/***********************************************************************************************************************
 * @brief      SPIS_READ: o que o mestre enviou ao escravo desde a ultima leitura (e esvazia).
 **********************************************************************************************************************/
static dutReturn_t Comandos_SpiLe(char **argumentos, uint8_t *dados, size_t *quantidade)
{
	(void)argumentos;

	return comandosBarramentos->spiLe(dados, quantidade);
}

/***********************************************************************************************************************
 * @brief      Converte "DEADBEEF" (ou "-", nenhum byte) em bytes; aceita maiusculas e minusculas.
 * @param[in]  texto       Campo do comando
 * @param[out] dados       Ate dCOMANDOS_DADOS_MAX bytes
 * @param[out] quantidade  Bytes convertidos
 * @return     false se o tamanho for impar, passar do limite ou tiver caractere que nao e hex
 **********************************************************************************************************************/
static bool Comandos_Hex(const char *texto, uint8_t *dados, size_t *quantidade)
{
	size_t tamanho = strlen(texto);

	if (strcmp(texto, "-") == 0)
	{
		*quantidade = 0U;
		return true;
	}

	if ((tamanho == 0U) || ((tamanho % 2U) != 0U) || ((tamanho / 2U) > dCOMANDOS_DADOS_MAX))
	{
		return false;
	}

	for (size_t i = 0U; i < tamanho; i += 2U)
	{
		char par[3] = {texto[i], texto[i + 1U], '\0'};

		if (!Comandos_ByteHex(par, &dados[i / 2U]))
		{
			return false;
		}
	}

	*quantidade = tamanho / 2U;

	return true;
}

/***********************************************************************************************************************
 * @brief      Converte um numero decimal sem sinal, ate maximo.
 * @return     false se estiver vazio, tiver caractere que nao e digito ou passar do maximo
 **********************************************************************************************************************/
static bool Comandos_Decimal(const char *texto, uint32_t maximo, uint32_t *valor)
{
	uint64_t acumulado = 0U;

	if (*texto == '\0')
	{
		return false;
	}

	for (const char *c = texto; *c != '\0'; c++)
	{
		if ((*c < '0') || (*c > '9'))
		{
			return false;
		}

		acumulado = (acumulado * 10U) + (uint64_t)(*c - '0');

		if (acumulado > maximo)
		{
			return false;
		}
	}

	*valor = (uint32_t)acumulado;

	return true;
}

/***********************************************************************************************************************
 * @brief      Converte 1 ou 2 digitos hex num byte (ex.: "50", "F", "a0").
 **********************************************************************************************************************/
static bool Comandos_ByteHex(const char *texto, uint8_t *valor)
{
	size_t tamanho = strlen(texto);
	uint8_t acumulado = 0U;

	if ((tamanho == 0U) || (tamanho > 2U))
	{
		return false;
	}

	for (size_t i = 0U; i < tamanho; i++)
	{
		char c = texto[i];
		uint8_t digito;

		if ((c >= '0') && (c <= '9'))
		{
			digito = (uint8_t)(c - '0');
		}
		else if ((c >= 'A') && (c <= 'F'))
		{
			digito = (uint8_t)(c - 'A' + 10);
		}
		else if ((c >= 'a') && (c <= 'f'))
		{
			digito = (uint8_t)(c - 'a' + 10);
		}
		else
		{
			return false;
		}

		acumulado = (uint8_t)((acumulado << 4) | digito);
	}

	*valor = acumulado;

	return true;
}

/***********************************************************************************************************************
 * @brief      Monta "OK", "OK <hex>" ou "ERR <motivo>", terminado em '\n'.
 **********************************************************************************************************************/
static size_t Comandos_Resposta(char *resposta, size_t tamanho, dutReturn_t ret, const uint8_t *dados,
                                size_t quantidade)
{
	static const char *const erros[eDUT_RETURN_END_ENUM] =
	{
		"",
		"ERR ARG",
		"ERR NACK",
		"ERR ESTADO",
		"ERR PERIFERICO",
	};
	size_t escritos;

	if ((ret != eDUT_RETURN_OK) || (dados == NULL))
	{
		quantidade = 0U;
	}

	if (ret != eDUT_RETURN_OK)
	{
		return (size_t)snprintf(resposta, tamanho, "%s\n", (ret < eDUT_RETURN_END_ENUM) ? erros[ret] : "ERR");
	}

	escritos = (size_t)snprintf(resposta, tamanho, (quantidade > 0U) ? "OK " : "OK");

	for (size_t i = 0U; (i < quantidade) && ((escritos + 3U) < tamanho); i++)
	{
		escritos += (size_t)snprintf(&resposta[escritos], tamanho - escritos, "%02X", dados[i]);
	}

	escritos += (size_t)snprintf(&resposta[escritos], tamanho - escritos, "\n");

	return escritos;
}

/** @} DOXYGEN GROUP TAG END OF FILE */
