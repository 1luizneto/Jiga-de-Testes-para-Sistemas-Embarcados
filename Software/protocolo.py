"""
Protocolo binario PC <-> jiga (lado do PC).

Quadro: SOF(0x7E) | LEN | SEQ | CMD | PAYLOAD(0 a 240) | CRC-16 (little-endian)

- CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, sem reflexao, sem XOR final)
  calculado sobre LEN + SEQ + CMD + PAYLOAD.
- O 0x7E so marca inicio de quadro quando o receptor esta procurando o SOF;
  dentro do quadro ele e um byte comum (enquadramento por comprimento).

O ReceptorQuadros espelha o parser do firmware (projeto/App/Comm/Comm.c) e a
JigaSimulada espelha a decisao da resposta (Comm_ProcessFrame): os testes do
software e os casos de teste enviados para a placa usam os dois para saber o
resultado esperado no DebugLog e a resposta esperada no USB.

Respostas da jiga (Apendice A): ACK = mesmo SEQ, CMD | 0x80, payload com os
dados; NACK = mesmo SEQ, CMD 0xFF, payload [comando recusado, codigo do erro] e,
no ROTINA_INVALIDA, o indice do passo. Quadro com CRC errado ou LEN invalido nao
tem resposta.
"""

from dataclasses import dataclass, field

from dut import DUT_BAUD, DUT_IDENTIFICACAO, DutSimulado, linha_dut, resposta_dut
from versao import __version__

SOF = 0x7E
TAMANHO_CABECALHO = 3            # LEN + SEQ + CMD
PAYLOAD_MAX = 240
TAMANHO_QUADRO_MAX = 1 + TAMANHO_CABECALHO + PAYLOAD_MAX + 2
CRC_INICIAL = 0xFFFF
CRC_POLINOMIO = 0x1021

# Comandos ja implementados no firmware (os demais entram com o Apendice A)
CMD_PING = 0x01
CMD_RESET_ALL = 0x02
CMD_RUN_ROUTINE = 0x08
CMD_ROUTINE_STATUS = 0x09
CMD_READ_RESULT = 0x0A
CMD_ABORT = 0x0B
CMD_DAC_SET = 0x10
CMD_GPIO_SET = 0x20
CMD_GPIO_READ = 0x21
CMD_PWM_SET = 0x30
CMD_PWM_STOP = 0x31
CMD_ADC_READ = 0x40
CMD_I2C_CONFIG = 0x50
CMD_I2C_SLAVE_LOAD = 0x51
CMD_I2C_SLAVE_DUMP = 0x52
CMD_I2C_MASTER_XFER = 0x53
CMD_I2C_SLAVE_MASK = 0x54      # bits que o DUT pode escrever em cada registrador
CMD_I2C_SLAVE_ADDR2 = 0x56     # segundo endereco da jiga escrava (0x55 continua inexistente)
CMD_SPI_CONFIG = 0x60
CMD_SPI_SLAVE_LOAD = 0x61
CMD_SPI_SLAVE_READ = 0x62
CMD_SPI_MASTER_XFER = 0x63
CMD_UART_CONFIG = 0x70
CMD_UART_WRITE = 0x71
CMD_UART_READ = 0x72
CMD_NACK = 0xFF
FLAG_ACK = 0x80

# Codigo que nenhum comando do Apendice A usa: a jiga sempre responde NACK CMD_DESCONHECIDO.
# Os casos de teste usam ele para nao mudar de resposta quando os comandos forem implementados.
CMD_INEXISTENTE = 0x55

NOMES_COMANDO = {
    CMD_PING: "PING", CMD_RESET_ALL: "RESET_ALL", CMD_RUN_ROUTINE: "RUN_ROUTINE",
    CMD_ROUTINE_STATUS: "ROUTINE_STATUS", CMD_READ_RESULT: "READ_RESULT", CMD_ABORT: "ABORT", CMD_DAC_SET: "DAC_SET",
    CMD_GPIO_SET: "GPIO_SET", CMD_GPIO_READ: "GPIO_READ", CMD_PWM_SET: "PWM_SET",
    CMD_PWM_STOP: "PWM_STOP", CMD_ADC_READ: "ADC_READ", CMD_I2C_CONFIG: "I2C_CONFIG",
    CMD_I2C_SLAVE_LOAD: "I2C_SLAVE_LOAD", CMD_I2C_SLAVE_DUMP: "I2C_SLAVE_DUMP",
    CMD_I2C_MASTER_XFER: "I2C_MASTER_XFER", CMD_SPI_CONFIG: "SPI_CONFIG", CMD_SPI_SLAVE_LOAD: "SPI_SLAVE_LOAD",
    CMD_SPI_SLAVE_READ: "SPI_SLAVE_READ", CMD_SPI_MASTER_XFER: "SPI_MASTER_XFER", CMD_UART_CONFIG: "UART_CONFIG",
    CMD_UART_WRITE: "UART_WRITE", CMD_UART_READ: "UART_READ", CMD_I2C_SLAVE_MASK: "I2C_SLAVE_MASK",
    CMD_I2C_SLAVE_ADDR2: "I2C_SLAVE_ADDR2",
}

# Versao do protocolo informada no ACK do PING (dCOMM_PROTOCOL_VERSION no firmware)
VERSAO_PROTOCOLO = 1

# DAC (Apendice A): canal 1 = DAC1 (PA4), 2 = DAC2 (PA5); 12 bits
DAC_CANAIS = (1, 2)
DAC_VALOR_MAX = 4095

# GPIO (Apendice A): saidas GPIO1 a GPIO4 (PC8 a PC11), nivel 0 ou 1. O GPIO_READ devolve as
# entradas GPIO5 a GPIO8 (PC12, PD2, PG2, PG3) numa mascara: bit 0 = GPIO5 ... bit 3 = GPIO8
GPIO_SAIDAS = (1, 2, 3, 4)
GPIO_NIVEIS = (0, 1)
GPIO_MASCARA_ENTRADAS = 0x0F

# ADC (Apendice A): canal 0 = A0 (PA3), 1 = A1 (PC0), 2 = A2 (PC3); 12 bits, referencia de 3,3 V
ADC_CANAIS = (0, 1, 2)
ADC_VALOR_MAX = 4095
TENSAO_REFERENCIA = 3.3

# PWM (Apendice A): canal 1 = PE9 (TIM1), 2 = PA0 (TIM2), 3 = PA6 (TIM3). Frequencia em Hz (u32) e
# razao ciclica em centesimos de por cento (u16: 5000 = 50 %, 10000 = sempre alto). O PWM_STOP (e o
# RESET_ALL) zera a razao e mantem a frequencia: o timer segue rodando com a saida em nivel baixo
PWM_CANAIS = (1, 2, 3)
PWM_FREQUENCIA_MIN = 1
PWM_FREQUENCIA_MAX = 100_000
PWM_RAZAO_MAX = 10_000
PWM_FREQUENCIA_PADRAO = 1000

# Clock e ARR maximo do timer de cada canal (pwmTimer_t da BSP): APB2 = 108 MHz e APB1 = 54 MHz,
# com o clock dos timers = 2 x PCLK. O prescaler e de 16 bits em todos (divisor ate 65536)
PWM_TIMERS = {1: (216_000_000, 0xFFFF), 2: (108_000_000, 0xFFFF_FFFF), 3: (108_000_000, 0xFFFF)}
PWM_DIVISOR_MAX = 65536
PWM_PINOS = {1: "PE9", 2: "PA0", 3: "PA6"}

# I2C (Apendice A): I2C2 em PF0 (SDA) e PF1 (SCL), pull-ups externos. Como escrava, a jiga emula um mapa de
# 256 registradores no padrao de uma EEPROM 24C02: na escrita do DUT o 1o byte e o ponteiro e os seguintes
# sao gravados com auto-incremento; a leitura sai a partir do ponteiro. O LOAD/DUMP funciona sem I2C_CONFIG
# e o RESET_ALL desliga o barramento e zera o mapa
I2C_PAPEL_ESCRAVO = 0
I2C_PAPEL_MESTRE = 1
I2C_ENDERECO_MIN = 0x08          # 0x00-0x07 e 0x78-0x7F sao reservados pelo padrao I2C
I2C_ENDERECO_MAX = 0x77
I2C_VELOCIDADES = {0: 100_000, 1: 400_000}
I2C_TAMANHO_MAPA = 256

# SPI (Apendice A): SPI2 em PB10 (SCK), PB15 (MOSI), PC2 (MISO) e PD7 (CS, ativo em nivel baixo). Como
# escrava, a jiga envia o buffer do SPI_SLAVE_LOAD a cada transacao do DUT (do byte 0, completando com 0xFF)
# e acumula o que o DUT enviou ate o SPI_SLAVE_READ, que esvazia o buffer. LOAD e READ funcionam sem
# SPI_CONFIG. Como mestre, a frequencia e a maior de 54 MHz / 2..256 que nao passa da pedida. O RESET_ALL
# desliga o barramento (CS em nivel alto) e esvazia os buffers
SPI_PAPEL_ESCRAVO = 0
SPI_PAPEL_MESTRE = 1
SPI_MODO_MAX = 3                 # bit 1 = CPOL, bit 0 = CPHA
SPI_CLOCK = 54_000_000           # PCLK1
SPI_DIVISORES = tuple(2 ** n for n in range(1, 9))
SPI_FREQUENCIA_MIN = -(-SPI_CLOCK // SPI_DIVISORES[-1])   # 210 938 Hz (54 MHz / 256, arredondado para cima)
SPI_TAMANHO_BUFFER = 239         # [quantidade][dados] cabe nos 240 bytes do payload
SPI_BYTE_VAZIO = 0xFF

# UART (Apendice A): UART4 em PD1 (TX) e PD0 (RX), 8N1. Os bytes do DUT entram num buffer circular de 256
# bytes (com ele cheio, os novos sao descartados) e o UART_READ tira os mais antigos. O UART_CONFIG esvazia o
# buffer; o RESET_ALL desliga a UART (o WRITE e o READ voltam a pedir UART_CONFIG)
UART_BAUD_MIN = 1200
UART_BAUD_MAX = 1_000_000
UART_TAMANHO_RX = 256
UART_TAMANHO_MAX = 239           # maior WRITE e maior READ: [quantidade][dados] cabe nos 240 bytes

# Rotinas (Apendice A): RUN_ROUTINE [identificador u16][tempo maximo ms u32][numero de passos u8][passos].
# Cada passo e o codigo de um comando direto seguido do payload dele; ESPERA e ESPERA_GPIO so existem em rotina.
# A jiga valida a rotina inteira antes de executar (NACK ROTINA_INVALIDA com o indice do passo, ou 0xFF quando o
# erro e do cabecalho ou de bytes sobrando), responde o ACK e executa; o PC acompanha com ROUTINE_STATUS e le os
# registros com READ_RESULT. Cada passo de aquisicao gera [indice u8][instante us u32][quantidade u8][dados]
PASSO_ESPERA = 0x78
PASSO_ESPERA_GPIO = 0x79
NOMES_PASSO = {PASSO_ESPERA: "ESPERA", PASSO_ESPERA_GPIO: "ESPERA_GPIO"}
ROTINA_CABECALHO = 7
ROTINA_PASSOS_MAX = PAYLOAD_MAX - ROTINA_CABECALHO
ROTINA_TEMPO_MAX_MS = 3_600_000          # 1 h: em us cabe nos 32 bits do TIM5 (~71 min ate dar a volta)
ROTINA_RESULTADOS_MAX = 2048
ROTINA_REGISTRO_CABECALHO = 6
ROTINA_LEITURA_MAX = 236                 # bytes de resultados por READ_RESULT
ROTINA_INDICE_CABECALHO = 0xFF
ROTINA_ENTRADAS = (5, 6, 7, 8)           # ESPERA_GPIO: GPIO5 a GPIO8
GPIO_OCORREU, GPIO_ESGOTOU = 0, 1        # resultado do ESPERA_GPIO
ESTADO_OCIOSA, ESTADO_EM_EXECUCAO, ESTADO_CONCLUIDA = 0, 1, 2
ESTADO_ABORTADA, ESTADO_TEMPO_ESGOTADO, ESTADO_ERRO = 3, 4, 5
ESTADOS_ROTINA = {0: "OCIOSA", 1: "EM_EXECUCAO", 2: "CONCLUIDA", 3: "ABORTADA", 4: "TEMPO_ESGOTADO", 5: "ERRO"}
ESTADOS_FINAIS = (ESTADO_CONCLUIDA, ESTADO_ABORTADA, ESTADO_TEMPO_ESGOTADO, ESTADO_ERRO)

# Loopback dos casos CT10, CT11 e CT13 (jumpers na placa): DAC1 (PA4) -> A0 (PA3), DAC2 (PA5) -> A1 (PC0),
# GPIO1-4 (PC8-PC11) -> GPIO5-8 (PC12, PD2, PG2, PG3), MOSI (PB15) -> MISO (PC2) e TX (PD1) -> RX (PD0)
LOOPBACK_DAC_ADC = {1: 0, 2: 1}

# CT10: tolerancia de 1 % do fundo de escala (33 mV ~ 41 codigos) e faixa util do DAC com o buffer
# de saida ligado (~0,1 a ~3,2 V; satura a ~50 mV dos trilhos, medido na v0.4.0)
TOLERANCIA_ADC = round(0.01 * ADC_VALOR_MAX)
DAC_FAIXA_UTIL = (124, 3971)

# A CommTask le o USB em blocos de ate 64 bytes (rxBuffer) e so confere o resultado da TestExecTask
# depois de processar o bloco inteiro: um comando que chega no mesmo bloco de outro pendente recebe
# OCUPADO antes de o pendente ser respondido
TAMANHO_BLOCO_RX = 64

# Codigos de erro do NACK (Apendice A, commError_t no firmware)
ERRO_CMD_DESCONHECIDO = 0x01
ERRO_PARAMETRO_INVALIDO = 0x02
ERRO_NAO_CONFIGURADO = 0x03
ERRO_PERIFERICO = 0x06
ERRO_OCUPADO = 0x07
ERRO_ROTINA_INVALIDA = 0x08
ERRO_SEM_RESULTADO = 0x09
CODIGOS_ERRO = {
    0x01: "CMD_DESCONHECIDO",
    0x02: "PARAMETRO_INVALIDO",
    0x03: "NAO_CONFIGURADO",
    0x04: "CRC_INVALIDO",
    0x05: "TAMANHO_EXCEDIDO",
    0x06: "ERRO_PERIFERICO",
    0x07: "OCUPADO",
    0x08: "ROTINA_INVALIDA",
    0x09: "SEM_RESULTADO",
}

# Resultados do receptor (mesmos nomes dos retornos eCOMM_RETURN_* do firmware)
LENDO = "READING"
QUADRO_OK = "FRAME_OK"
ERRO_CRC = "CRC_ERROR"
ERRO_LEN = "LEN_ERROR"
TIMEOUT = "TIMEOUT"
OCIOSO = "OK"


def crc16_ccitt(dados, inicial=CRC_INICIAL):
    """CRC-16/CCITT-FALSE, bit a bit (mesmo resultado do periferico CRC do STM32)."""
    crc = inicial
    for byte in dados:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ CRC_POLINOMIO) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def montar_quadro(seq, cmd, payload=b""):
    """Monta um quadro completo, com SOF e CRC."""
    payload = bytes(payload)
    if len(payload) > PAYLOAD_MAX:
        raise ValueError(f"payload de {len(payload)} B; maximo {PAYLOAD_MAX} B")
    if not 0 <= seq <= 0xFF or not 0 <= cmd <= 0xFF:
        raise ValueError("SEQ e CMD precisam caber em 1 byte")
    corpo = bytes([len(payload), seq, cmd]) + payload
    return bytes([SOF]) + corpo + crc16_ccitt(corpo).to_bytes(2, "little")


@dataclass
class Quadro:
    seq: int
    cmd: int
    payload: bytes = b""
    crc16: int = 0

    @property
    def tamanho(self):
        return len(self.payload)


@dataclass
class ReceptorQuadros:
    """Maquina de estados do receptor, igual a do firmware."""

    estado: str = field(default="AGUARDA_SOF", init=False)
    _bruto: bytearray = field(default_factory=bytearray, init=False)
    _crc: bytearray = field(default_factory=bytearray, init=False)

    def reiniciar(self):
        self.estado = "AGUARDA_SOF"
        self._bruto.clear()
        self._crc.clear()

    def timeout(self):
        """Equivale a Comm_Timeout(): descarta quadro pela metade."""
        if self.estado == "AGUARDA_SOF":
            return OCIOSO
        self.reiniciar()
        return TIMEOUT

    def processar_byte(self, byte):
        """Retorna (resultado, quadro). O quadro so vem preenchido em QUADRO_OK."""
        if self.estado == "AGUARDA_SOF":
            if byte == SOF:
                self.reiniciar()
                self.estado = "LE_CABECALHO"
            return LENDO, None

        if self.estado == "LE_CABECALHO":
            if not self._bruto and byte > PAYLOAD_MAX:
                self.reiniciar()
                return ERRO_LEN, None
            self._bruto.append(byte)
            if len(self._bruto) == TAMANHO_CABECALHO:
                self.estado = "VALIDA_CRC" if self._bruto[0] == 0 else "LE_PAYLOAD"
            return LENDO, None

        if self.estado == "LE_PAYLOAD":
            self._bruto.append(byte)
            if len(self._bruto) == TAMANHO_CABECALHO + self._bruto[0]:
                self.estado = "VALIDA_CRC"
            return LENDO, None

        # VALIDA_CRC
        self._crc.append(byte)
        if len(self._crc) < 2:
            return LENDO, None
        recebido = int.from_bytes(self._crc, "little")
        bruto = bytes(self._bruto)
        self.reiniciar()
        if crc16_ccitt(bruto) != recebido:
            return ERRO_CRC, None
        return QUADRO_OK, Quadro(seq=bruto[1], cmd=bruto[2], payload=bruto[3:], crc16=recebido)

    def processar(self, dados):
        """Processa varios bytes; retorna a lista de (resultado, quadro) diferentes de LENDO."""
        eventos = []
        for byte in dados:
            resultado, quadro = self.processar_byte(byte)
            if resultado != LENDO:
                eventos.append((resultado, quadro))
        return eventos


# --- respostas da jiga -------------------------------------------------------


def versao_em_bytes(versao=__version__):
    """'0.2.0' -> (0, 2, 0): como a versao do firmware vai no ACK do PING."""
    return tuple(int(parte) for parte in versao.split("."))


def montar_ack(seq, cmd, dados=b""):
    return montar_quadro(seq, cmd | FLAG_ACK, dados)


def montar_nack(seq, cmd, erro, detalhe=b""):
    """NACK [cmd][erro][detalhe]: o detalhe so existe no ROTINA_INVALIDA (indice do passo)."""
    return montar_quadro(seq, CMD_NACK, bytes([cmd, erro]) + bytes(detalhe))


def montar_ack_ping(seq, versao=__version__):
    """ACK do PING: versao do protocolo e versao do firmware (maior, menor, correcao).

    O firmware e o software tem a mesma versao (tests/test_versao.py), entao o ACK esperado usa a
    versao do software: se a placa estiver com um firmware antigo gravado, o caso de teste falha.
    """
    return montar_ack(seq, CMD_PING, bytes([VERSAO_PROTOCOLO, *versao_em_bytes(versao)]))


def montar_dac_set(seq, canal, valor):
    """DAC_SET: payload [canal (u8)][valor (u16, little-endian)].

    Aceita canal e valor fora da faixa do DAC (so precisam caber em u8/u16), para os casos de
    teste montarem comandos invalidos de proposito.
    """
    if not 0 <= canal <= 0xFF or not 0 <= valor <= 0xFFFF:
        raise ValueError("canal precisa caber em 1 byte e valor em 2 bytes")
    return montar_quadro(seq, CMD_DAC_SET, bytes([canal]) + valor.to_bytes(2, "little"))


def montar_gpio_set(seq, pino, nivel):
    """GPIO_SET: payload [pino (u8)][nivel (u8)]. Aceita pino e nivel fora da faixa (casos invalidos)."""
    if not 0 <= pino <= 0xFF or not 0 <= nivel <= 0xFF:
        raise ValueError("pino e nivel precisam caber em 1 byte")
    return montar_quadro(seq, CMD_GPIO_SET, bytes([pino, nivel]))


def montar_gpio_read(seq):
    """GPIO_READ: sem payload."""
    return montar_quadro(seq, CMD_GPIO_READ)


def montar_adc_read(seq, canal):
    """ADC_READ: payload [canal (u8)]. Aceita canal fora da faixa (casos invalidos)."""
    if not 0 <= canal <= 0xFF:
        raise ValueError("canal precisa caber em 1 byte")
    return montar_quadro(seq, CMD_ADC_READ, bytes([canal]))


def montar_pwm_set(seq, canal, frequencia, razao):
    """PWM_SET: payload [canal (u8)][frequencia em Hz (u32, LE)][razao em centesimos de % (u16, LE)].

    Aceita valores fora da faixa do PWM (so precisam caber em u8/u32/u16), para os casos invalidos.
    """
    if not 0 <= canal <= 0xFF or not 0 <= frequencia <= 0xFFFF_FFFF or not 0 <= razao <= 0xFFFF:
        raise ValueError("canal precisa caber em 1 byte, frequencia em 4 bytes e razao em 2 bytes")
    payload = bytes([canal]) + frequencia.to_bytes(4, "little") + razao.to_bytes(2, "little")
    return montar_quadro(seq, CMD_PWM_SET, payload)


def montar_pwm_stop(seq, canal):
    """PWM_STOP: payload [canal (u8)]. Aceita canal fora da faixa (casos invalidos)."""
    if not 0 <= canal <= 0xFF:
        raise ValueError("canal precisa caber em 1 byte")
    return montar_quadro(seq, CMD_PWM_STOP, bytes([canal]))


def _u8(*valores):
    if not all(0 <= valor <= 0xFF for valor in valores):
        raise ValueError("os campos precisam caber em 1 byte")


def montar_i2c_config(seq, papel, endereco, velocidade):
    """I2C_CONFIG: payload [papel (u8, 0 escravo, 1 mestre)][endereco de 7 bits (u8)][velocidade (u8, 0-1)]."""
    _u8(papel, endereco, velocidade)
    return montar_quadro(seq, CMD_I2C_CONFIG, bytes([papel, endereco, velocidade]))


def montar_i2c_load(seq, offset, dados):
    """I2C_SLAVE_LOAD: payload [offset (u8)][quantidade (u8)][dados]."""
    _u8(offset, len(dados))
    return montar_quadro(seq, CMD_I2C_SLAVE_LOAD, bytes([offset, len(dados)]) + bytes(dados))


def montar_i2c_mask(seq, offset, mascaras):
    """I2C_SLAVE_MASK: payload [offset (u8)][quantidade (u8)][mascaras]; bit 1 = o DUT pode escrever."""
    _u8(offset, len(mascaras))
    return montar_quadro(seq, CMD_I2C_SLAVE_MASK, bytes([offset, len(mascaras)]) + bytes(mascaras))


def montar_i2c_addr2(seq, endereco2):
    """I2C_SLAVE_ADDR2: payload [segundo endereco (u8, 0 = nenhum)]; vale a partir do proximo I2C_CONFIG."""
    _u8(endereco2)
    return montar_quadro(seq, CMD_I2C_SLAVE_ADDR2, bytes([endereco2]))


def montar_i2c_dump(seq, offset, quantidade):
    """I2C_SLAVE_DUMP: payload [offset (u8)][quantidade (u8)]; o ACK traz os bytes do mapa."""
    _u8(offset, quantidade)
    return montar_quadro(seq, CMD_I2C_SLAVE_DUMP, bytes([offset, quantidade]))


def montar_i2c_xfer(seq, endereco, escrita=b"", nr=0):
    """I2C_MASTER_XFER: payload [endereco (u8)][nw (u8)][nr (u8)][escrita]; o ACK traz os nr bytes lidos."""
    _u8(endereco, len(escrita), nr)
    return montar_quadro(seq, CMD_I2C_MASTER_XFER, bytes([endereco, len(escrita), nr]) + bytes(escrita))


def montar_spi_config(seq, papel, modo, frequencia=0):
    """SPI_CONFIG: payload [papel (u8, 0 escravo, 1 mestre)][modo (u8, 0-3)][frequencia em Hz (u32, LE)].

    A frequencia so vale como mestre (a escrava segue o clock do DUT).
    """
    _u8(papel, modo)
    if not 0 <= frequencia <= 0xFFFF_FFFF:
        raise ValueError("frequencia precisa caber em 4 bytes")
    return montar_quadro(seq, CMD_SPI_CONFIG, bytes([papel, modo]) + frequencia.to_bytes(4, "little"))


def montar_spi_load(seq, dados):
    """SPI_SLAVE_LOAD: payload [quantidade (u8)][dados]; sem dados, a escrava passa a enviar so 0xFF."""
    _u8(len(dados))
    return montar_quadro(seq, CMD_SPI_SLAVE_LOAD, bytes([len(dados)]) + bytes(dados))


def montar_spi_read(seq):
    """SPI_SLAVE_READ: sem payload; o ACK traz [quantidade][dados] recebidos do DUT."""
    return montar_quadro(seq, CMD_SPI_SLAVE_READ)


def montar_spi_xfer(seq, dados):
    """SPI_MASTER_XFER: payload [quantidade (u8)][dados]; o ACK traz os bytes lidos (a mesma quantidade)."""
    _u8(len(dados))
    return montar_quadro(seq, CMD_SPI_MASTER_XFER, bytes([len(dados)]) + bytes(dados))


def montar_uart_config(seq, baud):
    """UART_CONFIG: payload [baud (u32, LE)]; 8N1 fixo."""
    if not 0 <= baud <= 0xFFFF_FFFF:
        raise ValueError("baud precisa caber em 4 bytes")
    return montar_quadro(seq, CMD_UART_CONFIG, baud.to_bytes(4, "little"))


def montar_uart_write(seq, dados):
    """UART_WRITE: payload [quantidade (u8)][dados]; o ACK so sai depois de enviar."""
    _u8(len(dados))
    return montar_quadro(seq, CMD_UART_WRITE, bytes([len(dados)]) + bytes(dados))


def montar_uart_read(seq, maximo=UART_TAMANHO_MAX):
    """UART_READ: payload [maximo (u8)]; o ACK traz [quantidade][dados] recebidos do DUT."""
    _u8(maximo)
    return montar_quadro(seq, CMD_UART_READ, bytes([maximo]))


# --- rotinas ---------------------------------------------------------------------------------------------

def passo(quadro):
    """Passo de rotina a partir do quadro de um comando direto: [codigo][payload] (sem SOF, LEN, SEQ e CRC)."""
    return bytes([quadro[3]]) + bytes(quadro[4:-2])


def passo_espera(duracao_us):
    """ESPERA: [0x78][duracao em us (u32)]."""
    return bytes([PASSO_ESPERA]) + int(duracao_us).to_bytes(4, "little")


def passo_espera_gpio(entrada, nivel, limite_us):
    """ESPERA_GPIO: [0x79][entrada 5-8][nivel][tempo limite em us (u32)]."""
    return bytes([PASSO_ESPERA_GPIO, entrada, nivel]) + int(limite_us).to_bytes(4, "little")


def montar_rotina(identificador, tempo_max_ms, passos):
    """Payload do RUN_ROUTINE; ValueError se nao couber em um quadro (sem validar os passos: a jiga valida)."""
    if not 0 <= identificador <= 0xFFFF or not 0 <= tempo_max_ms <= 0xFFFF_FFFF or len(passos) > 0xFF:
        raise ValueError("identificador u16, tempo maximo u32 e ate 255 passos")
    corpo = b"".join(bytes(p) for p in passos)
    payload = identificador.to_bytes(2, "little") + tempo_max_ms.to_bytes(4, "little") + bytes([len(passos)]) + corpo
    if len(payload) > PAYLOAD_MAX:
        raise ValueError(f"rotina com {len(payload)} B; cabem {PAYLOAD_MAX} B ({ROTINA_PASSOS_MAX} B de passos)")
    return payload


def montar_run_routine(seq, identificador, tempo_max_ms, passos):
    return montar_quadro(seq, CMD_RUN_ROUTINE, montar_rotina(identificador, tempo_max_ms, passos))


def montar_routine_status(seq):
    return montar_quadro(seq, CMD_ROUTINE_STATUS)


def montar_read_result(seq, offset=0):
    if not 0 <= offset <= 0xFFFF:
        raise ValueError("offset precisa caber em 2 bytes")
    return montar_quadro(seq, CMD_READ_RESULT, offset.to_bytes(2, "little"))


def montar_abort(seq):
    return montar_quadro(seq, CMD_ABORT)


def _le32(dados, i):
    return int.from_bytes(dados[i:i + 4], "little")


def tamanho_passo(passos, i):
    """Espelho do Rotina_ValidaPasso: (tamanho, maior registro) do passo em passos[i], ou None se invalido."""
    disponivel = len(passos) - i
    fixos = {CMD_DAC_SET: 4, CMD_GPIO_SET: 3, CMD_GPIO_READ: 1, CMD_PWM_SET: 8, CMD_PWM_STOP: 2, CMD_ADC_READ: 2,
             CMD_I2C_CONFIG: 4, CMD_I2C_SLAVE_LOAD: 3, CMD_I2C_SLAVE_DUMP: 3, CMD_I2C_MASTER_XFER: 4,
             CMD_I2C_SLAVE_MASK: 3, CMD_I2C_SLAVE_ADDR2: 2,
             CMD_SPI_CONFIG: 7, CMD_SPI_SLAVE_LOAD: 2, CMD_SPI_SLAVE_READ: 1, CMD_SPI_MASTER_XFER: 2,
             CMD_UART_CONFIG: 5, CMD_UART_WRITE: 2, CMD_UART_READ: 2, PASSO_ESPERA: 5, PASSO_ESPERA_GPIO: 7}
    if disponivel <= 0 or passos[i] not in fixos or fixos[passos[i]] > disponivel:
        return None
    p, t, registro = passos[i:], fixos[passos[i]], 0
    cmd = p[0]
    if cmd == CMD_DAC_SET:
        valido = p[1] in DAC_CANAIS and int.from_bytes(p[2:4], "little") <= DAC_VALOR_MAX
    elif cmd == CMD_GPIO_SET:
        valido = p[1] in GPIO_SAIDAS and p[2] in GPIO_NIVEIS
    elif cmd == CMD_GPIO_READ:
        valido, registro = True, 1
    elif cmd == CMD_PWM_SET:
        valido = (p[1] in PWM_CANAIS and PWM_FREQUENCIA_MIN <= _le32(p, 2) <= PWM_FREQUENCIA_MAX
                  and int.from_bytes(p[6:8], "little") <= PWM_RAZAO_MAX)
    elif cmd == CMD_PWM_STOP:
        valido = p[1] in PWM_CANAIS
    elif cmd == CMD_ADC_READ:
        valido, registro = p[1] in ADC_CANAIS, 3
    elif cmd == CMD_I2C_CONFIG:
        valido = (p[1] <= I2C_PAPEL_MESTRE and p[3] in I2C_VELOCIDADES and
                  (p[1] == I2C_PAPEL_MESTRE or I2C_ENDERECO_MIN <= p[2] <= I2C_ENDERECO_MAX))
    elif cmd == CMD_I2C_SLAVE_LOAD:
        valido, t = p[2] > 0 and p[1] + p[2] <= I2C_TAMANHO_MAPA, t + p[2]
    elif cmd == CMD_I2C_SLAVE_MASK:
        valido, t = p[2] > 0 and p[1] + p[2] <= I2C_TAMANHO_MAPA, t + p[2]
    elif cmd == CMD_I2C_SLAVE_ADDR2:
        valido = p[1] == 0 or I2C_ENDERECO_MIN <= p[1] <= I2C_ENDERECO_MAX
    elif cmd == CMD_I2C_SLAVE_DUMP:
        valido, registro = 0 < p[2] <= PAYLOAD_MAX and p[1] + p[2] <= I2C_TAMANHO_MAPA, p[2]
    elif cmd == CMD_I2C_MASTER_XFER:
        valido = I2C_ENDERECO_MIN <= p[1] <= I2C_ENDERECO_MAX and (p[2] or p[3]) and p[3] <= PAYLOAD_MAX
        t, registro = t + p[2], p[3]
    elif cmd == CMD_SPI_CONFIG:
        valido = p[1] <= SPI_PAPEL_MESTRE and p[2] <= SPI_MODO_MAX
    elif cmd == CMD_SPI_SLAVE_LOAD:
        valido, t = p[1] <= SPI_TAMANHO_BUFFER, t + p[1]
    elif cmd == CMD_SPI_SLAVE_READ:
        valido, registro = True, 1 + SPI_TAMANHO_BUFFER
    elif cmd == CMD_SPI_MASTER_XFER:
        valido, t, registro = 0 < p[1] <= SPI_TAMANHO_BUFFER, t + p[1], p[1]
    elif cmd == CMD_UART_CONFIG:
        valido = UART_BAUD_MIN <= _le32(p, 1) <= UART_BAUD_MAX
    elif cmd == CMD_UART_WRITE:
        valido, t = 0 < p[1] <= UART_TAMANHO_MAX, t + p[1]
    elif cmd == CMD_UART_READ:
        valido, registro = p[1] <= UART_TAMANHO_MAX, 1 + p[1]
    elif cmd == PASSO_ESPERA:
        valido = True
    else:
        valido, registro = p[1] in ROTINA_ENTRADAS and p[2] in GPIO_NIVEIS, 5
    if not valido or t > disponivel:
        return None
    return t, registro


def separar_passos(passos):
    """Bytes dos passos -> lista de passos (pelo tamanho de cada um); ValueError no passo que nao fecha."""
    lista, i = [], 0
    while i < len(passos):
        medida = tamanho_passo(passos, i)
        if medida is None:
            raise ValueError(f"passo {len(lista)} invalido ou truncado (byte {i})")
        lista.append(passos[i:i + medida[0]])
        i += medida[0]
    return lista


def validar_rotina(payload):
    """Espelho do Rotina_Carrega: (None, passos decodificados) se valida; senao (indice, None).

    Cada passo decodificado e (codigo, parametros). O indice e o do passo invalido, ou ROTINA_INDICE_CABECALHO.
    """
    payload = bytes(payload)
    if len(payload) < ROTINA_CABECALHO:
        return ROTINA_INDICE_CABECALHO, None
    tempo, quantidade = _le32(payload, 2), payload[6]
    if quantidade == 0 or not 0 < tempo <= ROTINA_TEMPO_MAX_MS:
        return ROTINA_INDICE_CABECALHO, None
    passos, i, registros = [], ROTINA_CABECALHO, 0
    for indice in range(quantidade):
        medida = tamanho_passo(payload, i)
        if medida is None:
            return indice, None
        tamanho, registro = medida
        if registro:
            registros += ROTINA_REGISTRO_CABECALHO + registro
            if registros > ROTINA_RESULTADOS_MAX:
                return indice, None
        passos.append((payload[i], payload[i + 1:i + tamanho]))
        i += tamanho
    if i != len(payload):
        return ROTINA_INDICE_CABECALHO, None
    return None, passos


@dataclass
class StatusRotina:
    """Payload do ACK do ROUTINE_STATUS."""

    estado: int
    passos: int
    identificador: int
    erro: int

    @classmethod
    def de_bytes(cls, dados):
        if len(dados) != 5:
            raise ValueError(f"ROUTINE_STATUS com {len(dados)} B; esperado 5")
        return cls(dados[0], dados[1], int.from_bytes(dados[2:4], "little"), dados[4])

    def em_bytes(self):
        return bytes([self.estado, self.passos]) + self.identificador.to_bytes(2, "little") + bytes([self.erro])

    @property
    def finalizada(self):
        return self.estado in ESTADOS_FINAIS

    def descrever(self):
        texto = f"rotina {self.identificador}: {ESTADOS_ROTINA.get(self.estado, self.estado)}, {self.passos} passo(s)"
        if self.erro:
            texto += f", erro {CODIGOS_ERRO.get(self.erro, self.erro)}"
        return texto


@dataclass
class Registro:
    """Registro de um passo de aquisicao: indice do passo, instante desde o inicio da rotina (us) e dados."""

    indice: int
    instante_us: int
    dados: bytes

    def em_bytes(self):
        return bytes([self.indice]) + self.instante_us.to_bytes(4, "little") + bytes([len(self.dados)]) + self.dados


def interpretar_resultados(dados):
    """Area de resultados (todos os READ_RESULT juntos) -> lista de Registro; ValueError se truncada."""
    registros, i = [], 0
    while i < len(dados):
        if i + ROTINA_REGISTRO_CABECALHO > len(dados):
            raise ValueError("registro truncado")
        quantidade = dados[i + 5]
        fim = i + ROTINA_REGISTRO_CABECALHO + quantidade
        if fim > len(dados):
            raise ValueError("dados do registro truncados")
        registros.append(Registro(dados[i], _le32(dados, i + 1), bytes(dados[i + 6:fim])))
        i = fim
    return registros


def spi_divisor(frequencia):
    """Espelho do SpiBus_Divisor: menor divisor cuja frequencia nao passa da pedida; ValueError se nao houver."""
    for divisor in SPI_DIVISORES:
        if SPI_CLOCK <= frequencia * divisor:
            return divisor
    raise ValueError(f"frequencia abaixo de {SPI_FREQUENCIA_MIN} Hz (54 MHz / 256)")


def descrever_spi(frequencia):
    """Texto com o divisor e a frequencia que o mestre gera, ex.: 1 MHz -> 54 MHz / 64 = 843750 Hz."""
    divisor = spi_divisor(frequencia)
    return f"SPI mestre: 54 MHz / {divisor} = {SPI_CLOCK // divisor} Hz"


def calcular_pwm(canal, frequencia, razao):
    """Espelho do Pwm_Calcula: (PSC, ARR, CCR) que a jiga aplica no timer do canal.

    So aritmetica inteira, como no C: (a + b // 2) // b arredonda o meio para cima (o round() do
    Python arredonda o meio para o par) e -(-a // b) e a divisao com teto. Fora da faixa: ValueError.
    """
    if canal not in PWM_CANAIS:
        raise ValueError(f"canal {canal} nao existe (use {PWM_CANAIS})")
    if not PWM_FREQUENCIA_MIN <= frequencia <= PWM_FREQUENCIA_MAX:
        raise ValueError(f"frequencia fora de {PWM_FREQUENCIA_MIN} a {PWM_FREQUENCIA_MAX} Hz")
    if not 0 <= razao <= PWM_RAZAO_MAX:
        raise ValueError(f"razao fora de 0 a {PWM_RAZAO_MAX} (centesimos de %)")

    clock, arr_max = PWM_TIMERS[canal]
    periodo_max = arr_max + 1
    contagens = (clock + frequencia // 2) // frequencia
    divisor = max(1, -(-contagens // periodo_max))
    if divisor > PWM_DIVISOR_MAX:
        raise ValueError("frequencia baixa demais para o timer")
    passo = divisor * frequencia
    periodo = min((clock + passo // 2) // passo, periodo_max)
    if periodo < 2:
        raise ValueError("frequencia alta demais para o timer")
    return divisor - 1, periodo - 1, (razao * periodo + PWM_RAZAO_MAX // 2) // PWM_RAZAO_MAX


def porcento_para_razao(porcento):
    """Razao ciclica em % (ex.: 12.34) -> centesimos de % do PWM_SET (1234)."""
    return round(float(porcento) * 100)


def pwm_gerado(canal, frequencia, razao):
    """Frequencia (Hz) e razao ciclica (%) que o timer gera de fato: e o que o CT12 mede no pino.

    A frequencia sai do clock dividido por (PSC + 1) x (ARR + 1) e a razao de CCR / (ARR + 1); as
    duas diferem da pedida pelo arredondamento dos registradores (ex.: 50 Hz no TIM3 -> 49,9997 Hz).
    """
    psc, arr, ccr = calcular_pwm(canal, frequencia, razao)
    clock, _ = PWM_TIMERS[canal]
    return clock / ((psc + 1) * (arr + 1)), 100 * ccr / (arr + 1)


def descrever_pwm(canal, frequencia, razao):
    """Texto com os registradores e a forma de onda gerada, ex.: para mostrar antes de medir."""
    psc, arr, ccr = calcular_pwm(canal, frequencia, razao)
    gerada, porcento = pwm_gerado(canal, frequencia, razao)
    return (f"PWM{canal} ({PWM_PINOS[canal]}): PSC={psc} ARR={arr} CCR={ccr} -> "
            f"{gerada:.4f} Hz, {porcento:.2f} %".replace(".", ","))


def codigo_para_tensao(valor):
    """Codigo de 12 bits do ADC/DAC -> tensao em V (referencia de 3,3 V)."""
    return valor * TENSAO_REFERENCIA / ADC_VALOR_MAX


def nome_comando(cmd):
    return NOMES_COMANDO.get(cmd, f"0x{cmd:02X}")


@dataclass
class Resposta:
    """Resposta da jiga (ACK ou NACK) ja interpretada."""

    seq: int
    ack: bool
    cmd: int            # comando ao qual a jiga respondeu
    dados: bytes = b""  # payload do ACK (no NACK, o detalhe depois do codigo do erro)
    erro: int = 0       # codigo do erro, no NACK

    def descrever(self):
        if not self.ack:
            nome = CODIGOS_ERRO.get(self.erro, f"0x{self.erro:02X}")
            texto = f"NACK {nome_comando(self.cmd)} SEQ={self.seq}: {nome}"
            if self.erro == ERRO_ROTINA_INVALIDA and len(self.dados) == 1:
                texto += " (cabecalho)" if self.dados[0] == ROTINA_INDICE_CABECALHO else f" (passo {self.dados[0]})"
            return texto
        texto = f"ACK {nome_comando(self.cmd)} SEQ={self.seq}"
        if self.cmd == CMD_PING and len(self.dados) == 4:
            protocolo, maior, menor, correcao = self.dados
            texto += f" (protocolo {protocolo}, firmware {maior}.{menor}.{correcao})"
        elif self.cmd == CMD_GPIO_READ and len(self.dados) == 1:
            mascara = self.dados[0]
            niveis = " ".join(f"GPIO{5 + bit}={(mascara >> bit) & 1}" for bit in range(4))
            texto += f" mascara=0x{mascara:02X} ({niveis})"
        elif self.cmd == CMD_ADC_READ and len(self.dados) == 3:
            canal, valor = self.dados[0], int.from_bytes(self.dados[1:3], "little")
            tensao = f"{codigo_para_tensao(valor):.3f}".replace(".", ",")
            texto += f" canal {canal} = {valor} ({tensao} V)"
        elif self.cmd == CMD_ROUTINE_STATUS and len(self.dados) == 5:
            texto += " " + StatusRotina.de_bytes(self.dados).descrever()
        elif self.cmd == CMD_READ_RESULT and len(self.dados) >= 4:
            texto += (f" rotina {int.from_bytes(self.dados[0:2], 'little')}: {len(self.dados) - 4} B de "
                      f"{int.from_bytes(self.dados[2:4], 'little')} B de resultados")
        elif self.cmd in (CMD_SPI_SLAVE_READ, CMD_UART_READ) and self.dados and self.dados[0] == len(self.dados) - 1:
            texto += f" {self.dados[0]} B do DUT"
            if self.dados[0]:
                texto += f": {self.dados[1:].hex(' ').upper()}"
        elif self.dados:
            texto += f" dados={self.dados.hex(' ').upper()}"
        return texto


def interpretar_resposta(quadro):
    """Converte um Quadro vindo da jiga em Resposta; ValueError se nao for ACK nem NACK valido."""
    if quadro.cmd == CMD_NACK:
        if len(quadro.payload) < 2:
            raise ValueError(f"NACK com {len(quadro.payload)} B de payload; esperado [cmd, erro]")
        return Resposta(seq=quadro.seq, ack=False, cmd=quadro.payload[0], erro=quadro.payload[1],
                        dados=bytes(quadro.payload[2:]))
    if quadro.cmd & FLAG_ACK:
        return Resposta(seq=quadro.seq, ack=True, cmd=quadro.cmd & ~FLAG_ACK, dados=quadro.payload)
    raise ValueError(f"CMD 0x{quadro.cmd:02X} nao e ACK nem NACK")


def descrever_respostas(dados):
    """Descreve cada quadro de um fluxo vindo da jiga, para exibir. Bytes fora de quadro sao ignorados."""
    receptor = ReceptorQuadros()
    descricoes = []
    for resultado, quadro in receptor.processar(dados):
        if resultado != QUADRO_OK:
            descricoes.append(f"quadro invalido ({resultado})")
            continue
        try:
            descricoes.append(interpretar_resposta(quadro).descrever())
        except ValueError as e:
            descricoes.append(f"resposta invalida: {e}")
    if receptor.estado != "AGUARDA_SOF":
        descricoes.append("quadro incompleto")
    return descricoes


@dataclass
class JigaSimulada:
    """Espelho do firmware: parser (Comm_ProcessByte), resposta (Comm_ProcessFrame/Comm_BuildResult)
    e execucao (Exec_Process, na TestExecTask).

    - SEQ do comando pendente -> ignorado (retransmissao; a resposta sai quando ele terminar);
    - SEQ do ultimo quadro respondido -> reenvia a resposta guardada, sem executar;
    - PING -> ACK com as versoes, na hora (inclusive com um comando pendente);
    - outro comando com um pendente -> NACK OCUPADO, sem guardar no cache;
    - demais -> executados: RESET_ALL, DAC_SET, GPIO_SET, GPIO_READ, ADC_READ, PWM_SET, PWM_STOP e os
      quatro de I2C, os quatro de SPI e os tres de UART; codigo desconhecido -> NACK CMD_DESCONHECIDO.

    I2C: 'i2c_papel' (None = desligado), 'i2c_mapa' (os 256 registradores da jiga escrava) e
    'i2c_dispositivos' (endereco -> registradores dos escravos no barramento, para o papel de mestre).
    Endereco sem dispositivo -> falta de ACK (NACK ERRO_PERIFERICO), como na placa sem DUT.

    SPI: 'spi_papel' (None = desligado), 'spi_tx' (o que a escrava envia) e 'spi_rx' (o que o DUT enviou);
    transacao_spi() faz o papel do DUT. Como mestre, o MISO le o MOSI com loopback (jumper PB15 -> PC2) e
    0xFF sem ele (nominal: na placa, sem DUT, o MISO fica solto).

    UART: 'uart_baud' (None = desligada) e 'uart_rx' (buffer de recepcao); recepcao_uart() faz o papel do DUT.
    Com loopback (jumper PD1 -> PD0), o que o UART_WRITE envia volta para o buffer.

    DUT de referencia (CT20): com 'dut' (DutSimulado), o UART_WRITE a 115200 baud chega ao DUT e a resposta
    dele entra no buffer da UART; o DUT mestre le e escreve a jiga escrava (i2c_mestre_externo,
    spi_mestre_externo) e a jiga mestra le e escreve o DUT escravo.

    Rotinas: RUN_ROUTINE valida (validar_rotina), responde o ACK e comeca a executar no relogio simulado
    ('relogio_us'): os passos sem espera levam 0 us e o relogio so anda com avancar() (os passos "esperar" dos
    casos). ROUTINE_STATUS e ABORT sao atendidos na hora; durante a rotina os outros comandos recebem OCUPADO.
    O ESPERA_GPIO confere a entrada no inicio: no nivel, registra na hora; senao, ninguem muda a entrada durante
    a espera (os estimulos sao da propria rotina) e ela esgota o tempo limite.

    Como no firmware, o resultado do pendente so e respondido depois do bloco de bytes inteiro
    (TAMANHO_BLOCO_RX): por isso um comando que chega junto recebe OCUPADO antes.

    Sem loopback, as entradas leem 'entradas' (mascara) e o ADC le 'adc'. Com loopback=True, como
    nos jumpers do CT10 e CT11, as entradas copiam as saidas GPIO1-4 e o A0/A1 leem o DAC1/DAC2.
    """

    receptor: ReceptorQuadros = field(default_factory=ReceptorQuadros)
    ultimo_seq: int = None
    ultima_resposta: bytes = b""
    pendente: Quadro = None
    loopback: bool = False
    dac: dict = field(default_factory=lambda: {canal: 0 for canal in DAC_CANAIS})
    gpio: dict = field(default_factory=lambda: {pino: 0 for pino in GPIO_SAIDAS})
    entradas: int = 0
    adc: dict = field(default_factory=lambda: {canal: 0 for canal in ADC_CANAIS})
    pwm: dict = field(default_factory=lambda: {canal: (PWM_FREQUENCIA_PADRAO, 0) for canal in PWM_CANAIS})
    i2c_papel: int = None
    i2c_endereco: int = None
    i2c_ponteiro: int = 0
    i2c_mapa: bytearray = field(default_factory=lambda: bytearray(I2C_TAMANHO_MAPA))
    i2c_mascara: bytearray = field(default_factory=lambda: bytearray([0xFF] * I2C_TAMANHO_MAPA))
    i2c_endereco2: int = 0             # segundo endereco pedido (I2C_SLAVE_ADDR2), aplicado no I2C_CONFIG
    i2c_endereco2_ativo: int = None    # segundo endereco em que a escrava responde agora
    i2c_dispositivos: dict = field(default_factory=dict)
    spi_papel: int = None
    spi_tx: bytes = b""
    spi_rx: bytearray = field(default_factory=bytearray)
    uart_baud: int = None
    uart_rx: bytearray = field(default_factory=bytearray)
    dut: DutSimulado = None
    relogio_us: int = 0
    rotina: StatusRotina = field(default_factory=lambda: StatusRotina(ESTADO_OCIOSA, 0, 0, 0))
    rotina_resultados: bytearray = field(default_factory=bytearray)
    _execucao: dict = None             # rotina em execucao: passos, proximo, inicio, limite e a espera atual

    def __post_init__(self):
        if self.dut is not None:
            self.dut.jiga = self

    def nova_sessao(self):
        """Programa abriu a porta no PC (DTR): a jiga esquece a ultima resposta e o pendente."""
        self.receptor.reiniciar()
        self.ultimo_seq = None
        self.ultima_resposta = b""
        self.pendente = None

    def _guardar(self, seq, resposta):
        self.ultimo_seq, self.ultima_resposta = seq, resposta
        return resposta

    def _responder(self, quadro):
        """Equivale ao Comm_ProcessFrame: resposta imediata (bytes) ou b"" (ignorado/repassado)."""
        if self.pendente is not None and quadro.seq == self.pendente.seq:
            return b""
        if quadro.seq == self.ultimo_seq:
            return self.ultima_resposta
        if quadro.cmd == CMD_PING:
            return self._guardar(quadro.seq, montar_ack_ping(quadro.seq))
        if quadro.cmd in (CMD_ROUTINE_STATUS, CMD_ABORT):
            return self._guardar(quadro.seq, self._responder_rotina(quadro))
        if self.pendente is not None or self.rotina.estado == ESTADO_EM_EXECUCAO:
            return montar_nack(quadro.seq, quadro.cmd, ERRO_OCUPADO)
        self.pendente = quadro
        return b""

    def _responder_rotina(self, quadro):
        """Equivale ao CommTask_RespondeRotina: ROUTINE_STATUS e ABORT, sem payload."""
        if quadro.payload:
            return montar_nack(quadro.seq, quadro.cmd, ERRO_PARAMETRO_INVALIDO)
        if quadro.cmd == CMD_ROUTINE_STATUS:
            return montar_ack(quadro.seq, CMD_ROUTINE_STATUS, self.rotina.em_bytes())
        if self.rotina.estado == ESTADO_EM_EXECUCAO:
            self._finalizar_rotina(ESTADO_ABORTADA)
        return montar_ack(quadro.seq, CMD_ABORT)

    def _estado_seguro(self):
        """Exec_SafeState: DAC 0, GPIO baixo, PWM parado, I2C/SPI/UART desligados (mapa e buffers vazios)."""
        self.dac = {canal: 0 for canal in DAC_CANAIS}
        self.gpio = {pino: 0 for pino in GPIO_SAIDAS}
        self.pwm = {canal: (frequencia, 0) for canal, (frequencia, _) in self.pwm.items()}
        self.i2c_papel, self.i2c_endereco, self.i2c_ponteiro = None, None, 0
        self.i2c_mapa = bytearray(I2C_TAMANHO_MAPA)
        self.i2c_mascara = bytearray([0xFF] * I2C_TAMANHO_MAPA)
        self.i2c_endereco2, self.i2c_endereco2_ativo = 0, None
        self.spi_papel, self.spi_tx, self.spi_rx = None, b"", bytearray()
        self.uart_baud, self.uart_rx = None, bytearray()

    def _carregar_rotina(self, payload):
        """Rotina_Carrega: (0, b"") e a rotina comeca, ou (ROTINA_INVALIDA, indice)."""
        indice, passos = validar_rotina(payload)
        if passos is None:
            return ERRO_ROTINA_INVALIDA, bytes([indice])
        tempo_ms = _le32(payload, 2)
        self.rotina = StatusRotina(ESTADO_EM_EXECUCAO, 0, int.from_bytes(payload[0:2], "little"), 0)
        self.rotina_resultados = bytearray()
        self._execucao = {"passos": passos, "proximo": 0, "inicio": self.relogio_us,
                          "limite": self.relogio_us + tempo_ms * 1000, "espera": None}
        return 0, b""

    def _registrar(self, indice, instante, dados):
        self.rotina_resultados += Registro(indice, instante - self._execucao["inicio"], bytes(dados)).em_bytes()

    def _finalizar_rotina(self, estado, erro=0):
        """Rotina_Finaliza: estado seguro e so depois o estado final."""
        self._estado_seguro()
        self.rotina = StatusRotina(estado, self.rotina.passos, self.rotina.identificador, erro)
        self._execucao = None

    def _rodar_rotina(self):
        """Executa os passos a partir do relogio atual, ate o fim ou ate uma espera que ainda nao terminou."""
        while self._execucao is not None:
            ex = self._execucao
            if ex["espera"] is not None:
                fim, inicio_passo, limitado, gpio = ex["espera"]
                if self.relogio_us < fim:
                    return
                ex["espera"] = None
                if limitado:
                    self._finalizar_rotina(ESTADO_TEMPO_ESGOTADO)
                    return
                if gpio:
                    self._registrar(ex["proximo"], fim, bytes([GPIO_ESGOTOU]) + (fim - inicio_passo).to_bytes(4, "little"))
                ex["proximo"] += 1
                self.rotina.passos = ex["proximo"]
                continue
            if ex["proximo"] == len(ex["passos"]):
                self._finalizar_rotina(ESTADO_CONCLUIDA)
                return
            if self.relogio_us >= ex["limite"]:
                self._finalizar_rotina(ESTADO_TEMPO_ESGOTADO)
                return
            cmd, parametros = ex["passos"][ex["proximo"]]
            agora = self.relogio_us
            if cmd == PASSO_ESPERA:
                fim = agora + _le32(parametros, 0)
                ex["espera"] = (min(fim, ex["limite"]), agora, fim > ex["limite"], False)
                continue
            if cmd == PASSO_ESPERA_GPIO:
                entrada, nivel, limite_gpio = parametros[0], parametros[1], _le32(parametros, 2)
                if (self.ler_entradas() >> (entrada - ROTINA_ENTRADAS[0])) & 1 == nivel:
                    self._registrar(ex["proximo"], agora, bytes([GPIO_OCORREU]) + bytes(4))
                else:
                    fim = agora + limite_gpio
                    ex["espera"] = (min(fim, ex["limite"]), agora, fim > ex["limite"], True)
                    continue
            else:
                erro, dados = self._executar(Quadro(0, cmd, bytes(parametros)))
                if erro:
                    self._finalizar_rotina(ESTADO_ERRO, erro)
                    return
                if tamanho_passo(bytes([cmd]) + bytes(parametros), 0)[1]:
                    self._registrar(ex["proximo"], agora, dados)
            ex["proximo"] += 1
            self.rotina.passos = ex["proximo"]

    def avancar(self, segundos):
        """Passa o tempo no relogio simulado, executando as esperas da rotina que terminam nesse intervalo."""
        alvo = self.relogio_us + round(segundos * 1_000_000)
        while self._execucao is not None and self._execucao["espera"] is not None and self._execucao["espera"][0] <= alvo:
            self.relogio_us = max(self.relogio_us, self._execucao["espera"][0])
            self._rodar_rotina()
        self.relogio_us = alvo

    def _ler_resultado(self, payload):
        """Rotina_LeResultado: [identificador][total][ate 236 bytes a partir do offset]."""
        if len(payload) != 2:
            return ERRO_PARAMETRO_INVALIDO, b""
        if self.rotina.estado not in ESTADOS_FINAIS:
            return ERRO_SEM_RESULTADO, b""
        offset, total = int.from_bytes(payload, "little"), len(self.rotina_resultados)
        if offset > total:
            return ERRO_PARAMETRO_INVALIDO, b""
        parte = bytes(self.rotina_resultados[offset:offset + ROTINA_LEITURA_MAX])
        return 0, self.rotina.identificador.to_bytes(2, "little") + total.to_bytes(2, "little") + parte

    def ler_entradas(self):
        """Mascara do GPIO_READ (bit 0 = GPIO5): com loopback, GPIO1-4 ligados a GPIO5-8."""
        if self.loopback:
            return sum(nivel << (pino - 1) for pino, nivel in self.gpio.items())
        return self.entradas & GPIO_MASCARA_ENTRADAS

    def ler_adc(self, canal):
        """Valor do ADC_READ: com loopback, A0/A1 leem o DAC1/DAC2 (sem erro de conversao)."""
        if self.loopback:
            for canal_dac, canal_adc in LOOPBACK_DAC_ADC.items():
                if canal_adc == canal:
                    return self.dac[canal_dac]
        return self.adc[canal]

    def _executar(self, quadro):
        """Equivale ao Exec_Process: (0, dados) no ACK ou (codigo de erro, b"") no NACK.

        Comando invalido nao mexe em nenhuma saida.
        """
        payload = quadro.payload
        if quadro.cmd == CMD_RESET_ALL:
            if payload:
                return ERRO_PARAMETRO_INVALIDO, b""
            self.rotina, self.rotina_resultados = StatusRotina(ESTADO_OCIOSA, 0, 0, 0), bytearray()
            self._estado_seguro()
            return 0, b""
        if quadro.cmd == CMD_RUN_ROUTINE:
            return self._carregar_rotina(payload)
        if quadro.cmd == CMD_READ_RESULT:
            return self._ler_resultado(payload)
        if quadro.cmd == CMD_DAC_SET:
            if len(payload) != 3:
                return ERRO_PARAMETRO_INVALIDO, b""
            canal, valor = payload[0], int.from_bytes(payload[1:3], "little")
            if canal not in DAC_CANAIS or valor > DAC_VALOR_MAX:
                return ERRO_PARAMETRO_INVALIDO, b""
            self.dac[canal] = valor
            return 0, b""
        if quadro.cmd == CMD_GPIO_SET:
            if len(payload) != 2 or payload[0] not in GPIO_SAIDAS or payload[1] not in GPIO_NIVEIS:
                return ERRO_PARAMETRO_INVALIDO, b""
            self.gpio[payload[0]] = payload[1]
            return 0, b""
        if quadro.cmd == CMD_GPIO_READ:
            if payload:
                return ERRO_PARAMETRO_INVALIDO, b""
            return 0, bytes([self.ler_entradas()])
        if quadro.cmd == CMD_ADC_READ:
            if len(payload) != 1 or payload[0] not in ADC_CANAIS:
                return ERRO_PARAMETRO_INVALIDO, b""
            return 0, bytes([payload[0]]) + self.ler_adc(payload[0]).to_bytes(2, "little")
        if quadro.cmd == CMD_PWM_SET:
            if len(payload) != 7:
                return ERRO_PARAMETRO_INVALIDO, b""
            canal = payload[0]
            frequencia, razao = int.from_bytes(payload[1:5], "little"), int.from_bytes(payload[5:7], "little")
            try:
                calcular_pwm(canal, frequencia, razao)
            except ValueError:
                return ERRO_PARAMETRO_INVALIDO, b""
            self.pwm[canal] = (frequencia, razao)
            return 0, b""
        if quadro.cmd == CMD_PWM_STOP:
            if len(payload) != 1 or payload[0] not in PWM_CANAIS:
                return ERRO_PARAMETRO_INVALIDO, b""
            frequencia, _ = self.pwm[payload[0]]
            self.pwm[payload[0]] = (frequencia, 0)
            return 0, b""
        if CMD_I2C_CONFIG <= quadro.cmd <= CMD_I2C_SLAVE_MASK or quadro.cmd == CMD_I2C_SLAVE_ADDR2:
            return self._executar_i2c(quadro.cmd, payload)
        if CMD_SPI_CONFIG <= quadro.cmd <= CMD_SPI_MASTER_XFER:
            return self._executar_spi(quadro.cmd, payload)
        if CMD_UART_CONFIG <= quadro.cmd <= CMD_UART_READ:
            return self._executar_uart(quadro.cmd, payload)
        return ERRO_CMD_DESCONHECIDO, b""

    def _executar_i2c(self, cmd, payload):
        """Espelho do Exec (tamanhos) e do I2cBus (faixas, mapa, papel)."""
        if cmd == CMD_I2C_CONFIG:
            if len(payload) != 3:
                return ERRO_PARAMETRO_INVALIDO, b""
            papel, endereco, velocidade = payload
            if papel > I2C_PAPEL_MESTRE or velocidade not in I2C_VELOCIDADES or (
                    papel == I2C_PAPEL_ESCRAVO and not I2C_ENDERECO_MIN <= endereco <= I2C_ENDERECO_MAX):
                return ERRO_PARAMETRO_INVALIDO, b""
            self.i2c_papel = papel
            self.i2c_endereco = endereco if papel == I2C_PAPEL_ESCRAVO else None
            escravo2 = papel == I2C_PAPEL_ESCRAVO and self.i2c_endereco2 not in (0, endereco)
            self.i2c_endereco2_ativo = self.i2c_endereco2 if escravo2 else None
            return 0, b""
        if cmd == CMD_I2C_SLAVE_MASK:
            if len(payload) < 2 or len(payload) != 2 + payload[1]:
                return ERRO_PARAMETRO_INVALIDO, b""
            offset, quantidade = payload[0], payload[1]
            if quantidade == 0 or offset + quantidade > I2C_TAMANHO_MAPA:
                return ERRO_PARAMETRO_INVALIDO, b""
            self.i2c_mascara[offset:offset + quantidade] = payload[2:]
            return 0, b""
        if cmd == CMD_I2C_SLAVE_ADDR2:
            if len(payload) != 1 or (payload[0] != 0 and not I2C_ENDERECO_MIN <= payload[0] <= I2C_ENDERECO_MAX):
                return ERRO_PARAMETRO_INVALIDO, b""
            self.i2c_endereco2 = payload[0]
            return 0, b""
        if cmd == CMD_I2C_SLAVE_LOAD:
            if len(payload) < 2 or len(payload) != 2 + payload[1]:
                return ERRO_PARAMETRO_INVALIDO, b""
            offset, quantidade = payload[0], payload[1]
            if quantidade == 0 or offset + quantidade > I2C_TAMANHO_MAPA:
                return ERRO_PARAMETRO_INVALIDO, b""
            self.i2c_mapa[offset:offset + quantidade] = payload[2:]
            return 0, b""
        if cmd == CMD_I2C_SLAVE_DUMP:
            if len(payload) != 2 or payload[1] > PAYLOAD_MAX:
                return ERRO_PARAMETRO_INVALIDO, b""
            offset, quantidade = payload
            if quantidade == 0 or offset + quantidade > I2C_TAMANHO_MAPA:
                return ERRO_PARAMETRO_INVALIDO, b""
            return 0, bytes(self.i2c_mapa[offset:offset + quantidade])
        if len(payload) < 3 or len(payload) != 3 + payload[1] or payload[2] > PAYLOAD_MAX:
            return ERRO_PARAMETRO_INVALIDO, b""
        endereco, nw, nr = payload[0], payload[1], payload[2]
        if not I2C_ENDERECO_MIN <= endereco <= I2C_ENDERECO_MAX or nw + nr == 0:
            return ERRO_PARAMETRO_INVALIDO, b""
        if self.i2c_papel != I2C_PAPEL_MESTRE:
            return ERRO_NAO_CONFIGURADO, b""
        if self.dut is not None:
            lidos = self.dut.i2c_jiga_mestre(endereco, bytes(payload[3:]), nr)
            if lidos is not None:
                return 0, lidos
        if endereco not in self.i2c_dispositivos:
            return ERRO_PERIFERICO, b""
        registros, escrita = self.i2c_dispositivos[endereco], payload[3:]
        ponteiro = escrita[0] if escrita else 0
        for i, byte in enumerate(escrita[1:]):
            registros[(ponteiro + i) % I2C_TAMANHO_MAPA] = byte
        return 0, bytes(registros[(ponteiro + i) % I2C_TAMANHO_MAPA] for i in range(nr))

    def _executar_spi(self, cmd, payload):
        """Espelho do Exec (tamanhos) e do SpiBus (faixas, buffers, papel)."""
        if cmd == CMD_SPI_CONFIG:
            if len(payload) != 6:
                return ERRO_PARAMETRO_INVALIDO, b""
            papel, modo, frequencia = payload[0], payload[1], int.from_bytes(payload[2:6], "little")
            if papel > SPI_PAPEL_MESTRE or modo > SPI_MODO_MAX:
                return ERRO_PARAMETRO_INVALIDO, b""
            if papel == SPI_PAPEL_MESTRE:
                try:
                    spi_divisor(frequencia)
                except ValueError:
                    return ERRO_PARAMETRO_INVALIDO, b""
            self.spi_papel = papel
            return 0, b""
        if cmd == CMD_SPI_SLAVE_READ:
            if payload:
                return ERRO_PARAMETRO_INVALIDO, b""
            recebidos, self.spi_rx = bytes(self.spi_rx), bytearray()
            return 0, bytes([len(recebidos)]) + recebidos
        if len(payload) < 1 or len(payload) != 1 + payload[0]:
            return ERRO_PARAMETRO_INVALIDO, b""
        dados = payload[1:]
        if len(dados) > SPI_TAMANHO_BUFFER:
            return ERRO_PARAMETRO_INVALIDO, b""
        if cmd == CMD_SPI_SLAVE_LOAD:
            self.spi_tx = bytes(dados)
            return 0, b""
        if not dados:
            return ERRO_PARAMETRO_INVALIDO, b""
        if self.spi_papel != SPI_PAPEL_MESTRE:
            return ERRO_NAO_CONFIGURADO, b""
        if self.dut is not None:
            lidos = self.dut.spi_jiga_mestre(bytes(dados))
            if lidos is not None:
                return 0, lidos
        return 0, bytes(dados) if self.loopback else bytes([SPI_BYTE_VAZIO]) * len(dados)

    def transacao_spi(self, enviados):
        """Uma transacao do DUT com a jiga escrava (CS baixo, bytes, CS alto): retorna o que a jiga enviou.

        Como no SpiBus, a transmissao recomeca do byte 0 e o que passa do buffer de recepcao e descartado.
        """
        livre = SPI_TAMANHO_BUFFER - len(self.spi_rx)
        self.spi_rx += bytes(enviados[:livre])
        tx = self.spi_tx + bytes([SPI_BYTE_VAZIO]) * SPI_TAMANHO_BUFFER
        return tx[:len(enviados)]

    def spi_mestre_externo(self, enviados):
        """Transacao do DUT mestre: o que a jiga escrava enviou, ou None se ela nao for escrava."""
        return self.transacao_spi(enviados) if self.spi_papel == SPI_PAPEL_ESCRAVO else None

    def i2c_mestre_externo(self, endereco, escrita, nr):
        """Transacao do DUT mestre com a jiga escrava (24C02, como o I2cBus): None se ela nao responde.

        Escrita: o 1o byte e o ponteiro e os demais vao para o mapa com auto-incremento, so nos bits gravaveis
        (I2C_SLAVE_MASK). Leitura: a partir do ponteiro, que avanca (uma leitura sem escrita continua de onde parou).
        Responde no endereco do I2C_CONFIG e no segundo (I2C_SLAVE_ADDR2), que dividem o mapa.
        """
        if self.i2c_papel != I2C_PAPEL_ESCRAVO or endereco not in (self.i2c_endereco, self.i2c_endereco2_ativo):
            return None
        if escrita:
            self.i2c_ponteiro = escrita[0]
            for byte in escrita[1:]:
                mascara = self.i2c_mascara[self.i2c_ponteiro]
                self.i2c_mapa[self.i2c_ponteiro] = (self.i2c_mapa[self.i2c_ponteiro] & ~mascara & 0xFF) | (byte & mascara)
                self.i2c_ponteiro = (self.i2c_ponteiro + 1) % I2C_TAMANHO_MAPA
        lidos = bytearray()
        for _ in range(nr):
            lidos.append(self.i2c_mapa[self.i2c_ponteiro])
            self.i2c_ponteiro = (self.i2c_ponteiro + 1) % I2C_TAMANHO_MAPA
        return bytes(lidos)

    def _executar_uart(self, cmd, payload):
        """Espelho do Exec (tamanhos) e do UartBus (faixas, buffer, configuracao)."""
        if cmd == CMD_UART_CONFIG:
            if len(payload) != 4:
                return ERRO_PARAMETRO_INVALIDO, b""
            baud = int.from_bytes(payload, "little")
            if not UART_BAUD_MIN <= baud <= UART_BAUD_MAX:
                return ERRO_PARAMETRO_INVALIDO, b""
            self.uart_baud, self.uart_rx = baud, bytearray()
            return 0, b""
        if cmd == CMD_UART_WRITE:
            if len(payload) < 1 or len(payload) != 1 + payload[0] or payload[0] == 0:
                return ERRO_PARAMETRO_INVALIDO, b""
            if self.uart_baud is None:
                return ERRO_NAO_CONFIGURADO, b""
            if self.dut is not None:
                if self.uart_baud == DUT_BAUD:
                    self.recepcao_uart(self.dut.receber_uart(payload[1:]))
            elif self.loopback:
                self.recepcao_uart(payload[1:])
            return 0, b""
        if len(payload) != 1:
            return ERRO_PARAMETRO_INVALIDO, b""
        if self.uart_baud is None:
            return ERRO_NAO_CONFIGURADO, b""
        quantidade = min(payload[0], UART_TAMANHO_MAX, len(self.uart_rx))
        lidos, self.uart_rx = bytes(self.uart_rx[:quantidade]), self.uart_rx[quantidade:]
        return 0, bytes([quantidade]) + lidos

    def recepcao_uart(self, recebidos):
        """Bytes que chegam do DUT no RX: entram no fim do buffer; com ele cheio, sao descartados."""
        livre = UART_TAMANHO_RX - len(self.uart_rx)
        self.uart_rx += bytes(recebidos[:livre])

    def _concluir_pendente(self):
        """Equivale ao CommTask_VerificaResultado + Comm_BuildResult."""
        if self.pendente is None:
            return b""
        quadro, self.pendente = self.pendente, None
        erro, dados = self._executar(quadro)
        if erro == 0:
            resposta = montar_ack(quadro.seq, quadro.cmd, dados)
        else:
            resposta = montar_nack(quadro.seq, quadro.cmd, erro, dados)
        self._guardar(quadro.seq, resposta)
        # RUN_ROUTINE aceito: o ACK ja saiu; a TestExecTask executa a rotina
        if quadro.cmd == CMD_RUN_ROUTINE and erro == 0:
            self._rodar_rotina()
        return resposta

    def processar(self, dados):
        """Retorna (resultados do parser diferentes de LENDO, bytes que a jiga responde)."""
        resultados, respostas = [], b""
        for inicio in range(0, len(dados), TAMANHO_BLOCO_RX):
            for resultado, quadro in self.receptor.processar(dados[inicio:inicio + TAMANHO_BLOCO_RX]):
                resultados.append(resultado)
                if resultado == QUADRO_OK:
                    respostas += self._responder(quadro)
            respostas += self._concluir_pendente()
        return resultados, respostas

    def timeout(self):
        return self.receptor.timeout()


# --- casos de teste para enviar a placa --------------------------------------
#
# Cada passo e ("enviar", bytes) ou ("esperar", segundos). "esperado" e a lista de
# resultados que o parser da placa deve produzir, na ordem; a CommTask loga cada um
# na UART de debug (ST-Link). "respostas" sao os quadros que a jiga deve devolver pelo
# USB, na ordem (lista vazia = nenhuma resposta). Esperar mais que dCOMM_RX_TIMEOUT_MS
# (20 ms) dispara o timeout de pacote.
#
# Cada caso usa SEQs proprios: como a jiga reenvia a resposta guardada quando o SEQ se
# repete, dois casos com o mesmo SEQ interfeririam um no outro.

ESPERA_TIMEOUT = 0.25
TIMEOUT_PACOTE_MS = 20


def _ping(seq):
    return montar_quadro(seq, CMD_PING)


def _crc_trocado(quadro):
    return quadro[:-1] + bytes([quadro[-1] ^ 0xFF])


@dataclass
class CasoTeste:
    """Caso de teste enviado a placa.

    'respostas' sao os quadros nominais (os que a jiga simulada devolve). Quando a resposta da placa
    varia (entrada solta, leitura do ADC), 'conferir' decide se ela esta certa: recebe os bytes do
    USB e devolve None (certo) ou o motivo da falha. Sem 'conferir', a comparacao e exata.
    """

    nome: str
    descricao: str
    passos: list
    esperado: list
    respostas: list
    conferir: object = None
    requer_loopback: bool = False   # so passa com os jumpers do CT10/CT11/CT13
    requer_dut: bool = False        # so passa com o DUT de referencia (ESP32-S3) ligado na jiga (CT20)
    resumo_esperado: str = ""       # texto do esperado quando a resposta varia ou tem muitos quadros

    def bytes_enviados(self):
        return b"".join(dados for tipo, dados in self.passos if tipo == "enviar")

    def resposta_esperada(self):
        return b"".join(self.respostas)

    def conferir_resposta(self, dados):
        """None se a resposta do USB esta certa; senao, o motivo."""
        if self.conferir is not None:
            return self.conferir(bytes(dados))
        if bytes(dados) == self.resposta_esperada():
            return None
        return "resposta diferente da esperada"

    def descrever_esperado(self):
        return self.resumo_esperado or descrever_resposta(self.resposta_esperada())


# --- verificacao de respostas que variam -------------------------------------

def conferir_quadros(verificadores):
    """Monta o 'conferir' de um caso: um verificador por quadro esperado, na ordem.

    Cada verificador recebe o Quadro e devolve None (certo) ou o motivo.
    """
    def conferir(dados):
        receptor = ReceptorQuadros()
        eventos = receptor.processar(dados)
        if any(resultado != QUADRO_OK for resultado, _ in eventos) or receptor.estado != "AGUARDA_SOF":
            return "quadro invalido ou incompleto na resposta"
        if len(eventos) != len(verificadores):
            return f"{len(eventos)} resposta(s); esperado {len(verificadores)}"
        for i, ((_, quadro), verificador) in enumerate(zip(eventos, verificadores), 1):
            motivo = verificador(quadro)
            if motivo:
                return f"resposta {i}: {motivo}"
        return None
    return conferir


def _igual(esperado):
    """Verificador de um quadro exato."""
    def verificar(quadro):
        obtido = montar_quadro(quadro.seq, quadro.cmd, quadro.payload)
        if obtido != esperado:
            return f"{descrever_resposta(obtido)}; esperado {descrever_resposta(esperado)}"
        return None
    return verificar


def _motivo_ack(quadro, seq, cmd, tamanho):
    """Motivo se o quadro nao for o ACK de cmd, com o SEQ e o tamanho de dados certos."""
    try:
        resposta = interpretar_resposta(quadro)
    except ValueError as e:
        return str(e)
    if not resposta.ack or resposta.cmd != cmd or resposta.seq != seq:
        return f"{resposta.descrever()}; esperado ACK {nome_comando(cmd)} SEQ={seq}"
    if len(resposta.dados) != tamanho:
        return f"ACK com {len(resposta.dados)} B de dados; esperado {tamanho} B"
    return None


def _ack_gpio_read(seq, mascara=None):
    """ACK do GPIO_READ com bits 4 a 7 em 0; com 'mascara', o valor tambem e conferido."""
    def verificar(quadro):
        motivo = _motivo_ack(quadro, seq, CMD_GPIO_READ, 1)
        if motivo:
            return motivo
        obtida = quadro.payload[0]
        if obtida & ~GPIO_MASCARA_ENTRADAS:
            return f"mascara 0x{obtida:02X} com bits 4 a 7 ligados"
        if mascara is not None and obtida != mascara:
            return f"mascara 0x{obtida:02X}; esperado 0x{mascara:02X}"
        return None
    return verificar


def _ack_adc_read(seq, canal, alvo=None):
    """ACK do ADC_READ com o canal certo e 12 bits; com 'alvo', o valor fica a TOLERANCIA_ADC dele."""
    def verificar(quadro):
        motivo = _motivo_ack(quadro, seq, CMD_ADC_READ, 3)
        if motivo:
            return motivo
        canal_obtido, valor = quadro.payload[0], int.from_bytes(quadro.payload[1:3], "little")
        if canal_obtido != canal:
            return f"canal {canal_obtido}; esperado {canal}"
        if valor > ADC_VALOR_MAX:
            return f"valor {valor} passa de 12 bits"
        if alvo is not None and abs(valor - alvo) > TOLERANCIA_ADC:
            return f"A{canal} leu {valor}; esperado {alvo} +/- {TOLERANCIA_ADC} (diferenca {valor - alvo:+d})"
        return None
    return verificar


# --- casos com varios comandos (CT10 e CT11) ---------------------------------
#
# Um comando por vez: o proximo so sai depois de PAUSA_COMANDO, quando a resposta do anterior ja
# voltou (senao ele recebe OCUPADO). A pausa passa do timeout entre bytes, mas nao ha quadro pela
# metade, entao a CommTask nao loga timeout.

PAUSA_COMANDO = 0.05


def _sequencia(comandos):
    """[(quadro, resposta nominal, verificador) ou segundos] -> (passos, esperado, respostas, conferir).

    Um numero no lugar do comando e uma espera a mais (rotinas: deixar o tempo passar antes do proximo).
    """
    passos, respostas, verificadores, enviados = [], [], [], 0
    for item in comandos:
        if isinstance(item, (int, float)):
            passos.append(("esperar", item))
            continue
        quadro, resposta, verificador = item
        if enviados:
            passos.append(("esperar", PAUSA_COMANDO))
        passos.append(("enviar", quadro))
        enviados += 1
        respostas.append(resposta)
        verificadores.append(verificador)
    return passos, [QUADRO_OK] * enviados, respostas, conferir_quadros(verificadores)


def pontos_ct10(quantidade=11):
    """Valores do DAC na varredura do CT10: igualmente espacados na faixa util."""
    inicio, fim = DAC_FAIXA_UTIL
    return [round(inicio + i * (fim - inicio) / (quantidade - 1)) for i in range(quantidade)]


def _caso_ct10(canal_dac, seq):
    """CT10: DAC_SET em cada ponto e ADC_READ no canal ligado a ele pelo jumper."""
    canal_adc = LOOPBACK_DAC_ADC[canal_dac]
    comandos = []
    for valor in pontos_ct10():
        ack_dac = montar_ack(seq, CMD_DAC_SET)
        comandos.append((montar_dac_set(seq, canal_dac, valor), ack_dac, _igual(ack_dac)))
        ack_adc = montar_ack(seq + 1, CMD_ADC_READ, bytes([canal_adc]) + valor.to_bytes(2, "little"))
        comandos.append((montar_adc_read(seq + 1, canal_adc), ack_adc, _ack_adc_read(seq + 1, canal_adc, valor)))
        seq += 2
    passos, esperado, respostas, conferir = _sequencia(comandos)
    inicio, fim = DAC_FAIXA_UTIL
    return CasoTeste(
        f"CT10 DAC{canal_dac} -> A{canal_adc}",
        f"loopback DAC{canal_dac} -> A{canal_adc}: {len(pontos_ct10())} pontos de {inicio} a {fim}",
        passos, esperado, respostas, conferir=conferir, requer_loopback=True,
        resumo_esperado=f"{len(comandos)} ACKs; cada leitura a +/- {TOLERANCIA_ADC} codigos do DAC")


def _caso_ct11(seq):
    """CT11: as 16 combinacoes em GPIO1-4 e a mascara exata em GPIO5-8."""
    comandos = []
    for mascara in range(16):
        for pino in GPIO_SAIDAS:
            ack = montar_ack(seq, CMD_GPIO_SET)
            comandos.append((montar_gpio_set(seq, pino, (mascara >> (pino - 1)) & 1), ack, _igual(ack)))
            seq += 1
        ack = montar_ack(seq, CMD_GPIO_READ, bytes([mascara]))
        comandos.append((montar_gpio_read(seq), ack, _ack_gpio_read(seq, mascara)))
        seq += 1
    passos, esperado, respostas, conferir = _sequencia(comandos)
    return CasoTeste(
        "CT11 GPIO1-4 -> GPIO5-8", "loopback dos GPIOs: as 16 combinacoes, mascara exata",
        passos, esperado, respostas, conferir=conferir, requer_loopback=True,
        resumo_esperado=f"{len(comandos)} ACKs; mascara do GPIO_READ igual a combinacao aplicada")


def _padrao(quantidade, semente):
    """Bytes de teste que passam por 0x00, 0x7E e 0xFF e mudam de um envio para o outro."""
    return bytes((semente + 37 * i) & 0xFF for i in range(quantidade))


# CT13 SPI: os 4 modos a 1 MHz com 16 bytes e o buffer inteiro (239 bytes) a 54 MHz / 8 = 6,75 MHz
CT13_SPI = [(modo, 1_000_000, 16) for modo in range(SPI_MODO_MAX + 1)] + [(0, 6_750_000, SPI_TAMANHO_BUFFER)]

# CT13 UART: (baud, bytes). A 9600 baud, 16 bytes levam ~17 ms, menos que a PAUSA_COMANDO: o READ seguinte
# nao recebe OCUPADO e ja encontra os bytes no buffer
CT13_UART = [(9600, 16), (115_200, 64), (UART_BAUD_MAX, UART_TAMANHO_MAX)]


def _caso_ct13_spi(seq):
    """CT13 SPI: como mestre, com o jumper MOSI -> MISO, cada transferencia le de volta o que enviou."""
    comandos = []
    for modo, frequencia, quantidade in CT13_SPI:
        ack = montar_ack(seq, CMD_SPI_CONFIG)
        comandos.append((montar_spi_config(seq, SPI_PAPEL_MESTRE, modo, frequencia), ack, _igual(ack)))
        dados = _padrao(quantidade, seq)
        ack = montar_ack(seq + 1, CMD_SPI_MASTER_XFER, dados)
        comandos.append((montar_spi_xfer(seq + 1, dados), ack, _igual(ack)))
        seq += 2
    passos, esperado, respostas, conferir = _sequencia(comandos)
    return CasoTeste(
        "CT13 SPI MOSI -> MISO", "loopback do SPI mestre: modos 0 a 3 a 1 MHz e 239 bytes a 6,75 MHz",
        passos, esperado, respostas, conferir=conferir, requer_loopback=True,
        resumo_esperado=f"{len(comandos)} ACKs; cada SPI_MASTER_XFER devolve os bytes enviados")


def _caso_ct13_uart(seq):
    """CT13 UART: com o jumper TX -> RX, o UART_READ devolve o que o UART_WRITE enviou, em cada baud."""
    comandos = []
    for baud, quantidade in CT13_UART:
        dados = _padrao(quantidade, seq)
        for quadro, ack in ((montar_uart_config(seq, baud), montar_ack(seq, CMD_UART_CONFIG)),
                            (montar_uart_write(seq + 1, dados), montar_ack(seq + 1, CMD_UART_WRITE)),
                            (montar_uart_read(seq + 2), montar_ack(seq + 2, CMD_UART_READ,
                                                                  bytes([quantidade]) + dados))):
            comandos.append((quadro, ack, _igual(ack)))
        seq += 3
    passos, esperado, respostas, conferir = _sequencia(comandos)
    return CasoTeste(
        "CT13 UART TX -> RX", "loopback da UART4: 9600, 115200 e 1 000 000 baud",
        passos, esperado, respostas, conferir=conferir, requer_loopback=True,
        resumo_esperado=f"{len(comandos)} ACKs; cada UART_READ devolve os bytes do UART_WRITE")


def _comando(quadro, resposta):
    """(quadro, resposta nominal, verificador exato) para o _sequencia."""
    return quadro, resposta, _igual(resposta)


def _dut(seq, linha, resposta):
    """Comando ao DUT pela UART da jiga: UART_WRITE da linha e UART_READ da resposta (dois SEQ)."""
    return [_comando(montar_uart_write(seq, linha), montar_ack(seq, CMD_UART_WRITE)),
            _comando(montar_uart_read(seq + 1), montar_ack(seq + 1, CMD_UART_READ, bytes([len(resposta)]) + resposta))]


def _caso_ct20(nome, descricao, comandos, resumo):
    passos, esperado, respostas, conferir = _sequencia(comandos)
    return CasoTeste(nome, descricao, passos, esperado, respostas, conferir=conferir, requer_dut=True,
                     resumo_esperado=resumo)


def _casos_ct20(seq):
    """CT20 com o DUT de referencia (ESP32-S3) ligado na jiga; cada caso liga a UART e deixa o DUT livre (OFF)."""
    uart = lambda s: _comando(montar_uart_config(s, DUT_BAUD), montar_ack(s, CMD_UART_CONFIG))
    casos = []

    s = seq
    casos.append(_caso_ct20(
        "CT20 DUT PING", "UART4 a 115200 baud: o DUT responde OFF e PING pela UART",
        [uart(s)] + _dut(s + 1, linha_dut("OFF"), resposta_dut()) +
        _dut(s + 3, linha_dut("PING"), (DUT_IDENTIFICACAO + "\n").encode()),
        f"5 ACKs; o DUT responde \"{DUT_IDENTIFICACAO}\""))

    s += 5
    mapa = bytes(range(1, 9))
    casos.append(_caso_ct20(
        "CT20 I2C jiga escrava", "DUT mestre le a jiga escrava 0x50 (com e sem ponteiro) e escreve nela",
        [_comando(montar_i2c_load(s, 0x10, mapa), montar_ack(s, CMD_I2C_SLAVE_LOAD)),
         _comando(montar_i2c_config(s + 1, I2C_PAPEL_ESCRAVO, 0x50, 0), montar_ack(s + 1, CMD_I2C_CONFIG)),
         uart(s + 2)] +
        _dut(s + 3, linha_dut("I2CM", "50", b"\x10", 4), resposta_dut(mapa[:4])) +
        _dut(s + 5, linha_dut("I2CM", "50", b"", 2), resposta_dut(mapa[4:6])) +
        _dut(s + 7, linha_dut("I2CM", "50", b"\x20\xa1\xa2\xa3", 0), resposta_dut()) +
        [_comando(montar_i2c_dump(s + 9, 0x20, 3), montar_ack(s + 9, CMD_I2C_SLAVE_DUMP, b"\xa1\xa2\xa3"))],
        "o DUT le 01 02 03 04, depois 05 06 (o ponteiro continua) e grava A1 A2 A3 em 0x20"))

    s += 10
    casos.append(_caso_ct20(
        "CT20 I2C jiga mestra", "jiga mestra escreve e le o DUT escravo 0x28 (repeated start)",
        [uart(s)] + _dut(s + 1, linha_dut("I2CS", "28"), resposta_dut()) +
        [_comando(montar_i2c_config(s + 3, I2C_PAPEL_MESTRE, 0, 0), montar_ack(s + 3, CMD_I2C_CONFIG)),
         _comando(montar_i2c_xfer(s + 4, 0x28, b"\x40\xb1\xb2\xb3"), montar_ack(s + 4, CMD_I2C_MASTER_XFER)),
         _comando(montar_i2c_xfer(s + 5, 0x28, b"\x40", 3), montar_ack(s + 5, CMD_I2C_MASTER_XFER, b"\xb1\xb2\xb3"))] +
        _dut(s + 6, linha_dut("I2CS_DUMP", "40", 3), resposta_dut(b"\xb1\xb2\xb3")) +
        _dut(s + 8, linha_dut("OFF"), resposta_dut()),
        "a jiga le B1 B2 B3 do DUT e o DUT confirma o que recebeu"))

    s += 10
    carga = b"\xc0\xff\xee"
    casos.append(_caso_ct20(
        "CT20 SPI jiga escrava", "DUT mestre (modo 0, 1 MHz) faz duas transacoes com a jiga escrava",
        [_comando(montar_spi_config(s, SPI_PAPEL_ESCRAVO, 0), montar_ack(s, CMD_SPI_CONFIG)),
         _comando(montar_spi_load(s + 1, carga), montar_ack(s + 1, CMD_SPI_SLAVE_LOAD)),
         uart(s + 2)] +
        _dut(s + 3, linha_dut("SPIM", 0, 1_000_000, bytes(range(1, 6))), resposta_dut(carga + b"\xff\xff")) +
        _dut(s + 5, linha_dut("SPIM", 0, 1_000_000, b"\xaa\xbb"), resposta_dut(carga[:2])) +
        [_comando(montar_spi_read(s + 7),
                  montar_ack(s + 7, CMD_SPI_SLAVE_READ, b"\x07" + bytes(range(1, 6)) + b"\xaa\xbb"))],
        "o DUT recebe C0 FF EE FF FF e C0 FF (do byte 0 a cada transacao); a jiga recebe os 7 bytes"))

    s += 8
    casos.append(_caso_ct20(
        "CT20 SPI jiga mestra", "jiga mestra (modo 0, 1 MHz) transfere com o DUT escravo",
        [uart(s)] + _dut(s + 1, linha_dut("SPIS_LOAD", b"\x12\x34"), resposta_dut()) +
        _dut(s + 3, linha_dut("SPIS", 0), resposta_dut()) +
        [_comando(montar_spi_config(s + 5, SPI_PAPEL_MESTRE, 0, 1_000_000), montar_ack(s + 5, CMD_SPI_CONFIG)),
         _comando(montar_spi_xfer(s + 6, b"\xaa\xbb\xcc"), montar_ack(s + 6, CMD_SPI_MASTER_XFER, b"\x12\x34\xff"))] +
        _dut(s + 7, linha_dut("SPIS_READ"), resposta_dut(b"\xaa\xbb\xcc")) +
        _dut(s + 9, linha_dut("OFF"), resposta_dut()),
        "a jiga recebe 12 34 FF e o DUT recebe AA BB CC"))
    return casos


# --- rotinas (CT14 a CT19) --------------------------------------------------------------------------------
#
# A rotina roda na jiga enquanto o PC manda os proximos comandos: as esperas dos casos ("esperar") deixam a
# rotina chegar no ponto que o caso confere. A jiga simulada anda o relogio dela nas mesmas esperas.

# Criterio de tempo das rotinas (Quadro de criterios da monografia): |t_jiga - t_nominal| <= 50 us
TOLERANCIA_TEMPO_US = 50


def _status(seq, estado, passos, identificador, erro=0):
    return _comando(montar_routine_status(seq),
                    montar_ack(seq, CMD_ROUTINE_STATUS, StatusRotina(estado, passos, identificador, erro).em_bytes()))


def _ack_resultados(seq, identificador, resposta_nominal, conferir_registros):
    """(quadro, nominal, verificador) do READ_RESULT offset 0: confere o cabecalho e os registros com tolerancia.

    conferir_registros recebe a lista de Registro e devolve None ou o motivo.
    """
    def verificar(quadro):
        try:
            resposta = interpretar_resposta(quadro)
        except ValueError as e:
            return str(e)
        if not resposta.ack or resposta.cmd != CMD_READ_RESULT or resposta.seq != seq or len(resposta.dados) < 4:
            return f"{resposta.descrever()}; esperado ACK READ_RESULT SEQ={seq}"
        dados = resposta.dados
        if int.from_bytes(dados[0:2], "little") != identificador or int.from_bytes(dados[2:4], "little") != len(dados) - 4:
            return f"cabecalho {dados[:4].hex(' ')}: rotina {identificador} com todos os resultados no quadro"
        try:
            return conferir_registros(interpretar_resultados(dados[4:]))
        except ValueError as e:
            return str(e)
    return montar_read_result(seq), resposta_nominal, verificar


def _conferir_tempos(esperados):
    """Verificador dos registros: esperados = [(indice, conferir(registro, anteriores) -> motivo ou None)]."""
    def conferir(registros):
        if [r.indice for r in registros] != [indice for indice, _ in esperados]:
            return f"registros dos passos {[r.indice for r in registros]}; esperado {[i for i, _ in esperados]}"
        for i, (registro, (_, conferir_um)) in enumerate(zip(registros, esperados)):
            motivo = conferir_um(registro, registros[:i])
            if motivo:
                return f"passo {registro.indice}: {motivo}"
        return None
    return conferir


def _intervalo(nominal_us):
    """O registro vem nominal_us depois do anterior (+/- TOLERANCIA_TEMPO_US)."""
    def conferir(registro, anteriores):
        if not anteriores:
            return None
        intervalo = registro.instante_us - anteriores[-1].instante_us
        if abs(intervalo - nominal_us) > TOLERANCIA_TEMPO_US:
            return f"{intervalo} us depois do anterior; esperado {nominal_us} +/- {TOLERANCIA_TEMPO_US} us"
        return None
    return conferir


def _espera_gpio(resultado, decorrido_us, mascara=None):
    """Registro do ESPERA_GPIO: resultado exato e tempo decorrido (+/- TOLERANCIA_TEMPO_US)."""
    def conferir(registro, _):
        if len(registro.dados) != 5:
            return f"{len(registro.dados)} B; esperado 5"
        obtido, decorrido = registro.dados[0], _le32(registro.dados, 1)
        if obtido != resultado:
            return f"resultado {obtido}; esperado {resultado} ({'ocorreu' if resultado == GPIO_OCORREU else 'esgotou'})"
        if abs(decorrido - decorrido_us) > TOLERANCIA_TEMPO_US:
            return f"{decorrido} us decorridos; esperado {decorrido_us} +/- {TOLERANCIA_TEMPO_US} us"
        return None
    return conferir


def _mascara(esperada=None, bits=GPIO_MASCARA_ENTRADAS):
    """Registro do GPIO_READ: 1 byte com os bits 4 a 7 em 0 (e, se dado, o valor nos 'bits' conferidos)."""
    def conferir(registro, _):
        if len(registro.dados) != 1 or registro.dados[0] & ~GPIO_MASCARA_ENTRADAS:
            return f"GPIO_READ {registro.dados.hex(' ')}; esperado 1 B com bits 4 a 7 em 0"
        if esperada is not None and registro.dados[0] & bits != esperada:
            return f"mascara 0x{registro.dados[0]:02X}; esperado 0x{esperada:02X} nos bits 0x{bits:02X}"
        return None
    return conferir


def _e(*verificadores):
    """Junta verificadores de um registro: o primeiro motivo encontrado."""
    def conferir(registro, anteriores):
        for v in verificadores:
            motivo = v(registro, anteriores)
            if motivo:
                return motivo
        return None
    return conferir


def _resultado_nominal(jiga_passos, identificador, tempo_ms, seq_read, loopback=False):
    """READ_RESULT que a jiga simulada devolve para a rotina (instantes sem a latencia da placa)."""
    jiga = JigaSimulada(loopback=loopback)
    jiga.processar(montar_run_routine(0, identificador, tempo_ms, jiga_passos))
    jiga.avancar(tempo_ms / 1000)
    return jiga.processar(montar_read_result(seq_read))[1]


def _caso_rotina(nome, descricao, comandos, resumo, loopback=False):
    passos, esperado, respostas, conferir = _sequencia(comandos)
    return CasoTeste(nome, descricao, passos, esperado, respostas, conferir=conferir, requer_loopback=loopback,
                     resumo_esperado=resumo)


def _casos_rotina(seq):
    """CT14 a CT18 e o CT19 sem fios (so a base de tempo): rodam sem jumpers e sem DUT."""
    casos = []
    gpio_alto = passo(montar_gpio_set(0, 1, 1))

    s = seq
    casos.append(_caso_rotina(
        "CT14 rotina invalida", "passo 0x55 no meio, rotina sem passos e DAC 4096: recusadas sem executar",
        [_comando(montar_quadro(s, CMD_RESET_ALL), montar_ack(s, CMD_RESET_ALL)),
         _comando(montar_run_routine(s + 1, 14, 1000, [gpio_alto, b"\x55", passo(montar_gpio_read(0))]),
                  montar_nack(s + 1, CMD_RUN_ROUTINE, ERRO_ROTINA_INVALIDA, b"\x01")),
         _comando(montar_run_routine(s + 2, 14, 1000, []),
                  montar_nack(s + 2, CMD_RUN_ROUTINE, ERRO_ROTINA_INVALIDA, bytes([ROTINA_INDICE_CABECALHO]))),
         _comando(montar_run_routine(s + 3, 14, 1000, [passo(montar_dac_set(0, 1, 4096))]),
                  montar_nack(s + 3, CMD_RUN_ROUTINE, ERRO_ROTINA_INVALIDA, b"\x00")),
         _status(s + 4, ESTADO_OCIOSA, 0, 0),
         _comando(montar_read_result(s + 5), montar_nack(s + 5, CMD_READ_RESULT, ERRO_SEM_RESULTADO))],
        "NACK ROTINA_INVALIDA nos passos 1, cabecalho e 0; estado OCIOSA; READ_RESULT SEM_RESULTADO"))

    s += 6
    casos.append(_caso_rotina(
        "CT15 comandos durante a rotina", "ESPERA de 1 s: GPIO_SET, RUN_ROUTINE e READ_RESULT recebem OCUPADO",
        [_comando(montar_run_routine(s, 15, 2000, [passo_espera(1_000_000)]), montar_ack(s, CMD_RUN_ROUTINE)),
         _comando(montar_gpio_set(s + 1, 1, 1), montar_nack(s + 1, CMD_GPIO_SET, ERRO_OCUPADO)),
         _comando(montar_run_routine(s + 2, 99, 1000, [gpio_alto]), montar_nack(s + 2, CMD_RUN_ROUTINE, ERRO_OCUPADO)),
         _comando(montar_read_result(s + 3), montar_nack(s + 3, CMD_READ_RESULT, ERRO_OCUPADO)),
         _status(s + 4, ESTADO_EM_EXECUCAO, 0, 15),
         _comando(_ping(s + 5), montar_ack_ping(s + 5)),
         _comando(montar_abort(s + 6), montar_ack(s + 6, CMD_ABORT)),
         _status(s + 7, ESTADO_ABORTADA, 0, 15)],
        "3x OCUPADO; ROUTINE_STATUS e PING atendidos durante a rotina; ABORT encerra"))

    s += 8
    casos.append(_caso_rotina(
        "CT16 ABORT na espera", "GPIO1 alto e ESPERA de 10 s: ABORT depois de 0,3 s",
        [_comando(montar_run_routine(s, 16, 20000, [gpio_alto, passo_espera(10_000_000)]),
                  montar_ack(s, CMD_RUN_ROUTINE)),
         0.3,
         _status(s + 1, ESTADO_EM_EXECUCAO, 1, 16),
         _comando(montar_abort(s + 2), montar_ack(s + 2, CMD_ABORT)),
         _status(s + 3, ESTADO_ABORTADA, 1, 16),
         _comando(montar_read_result(s + 4), montar_ack(s + 4, CMD_READ_RESULT, (16).to_bytes(2, "little") + bytes(2)))],
        "EM_EXECUCAO com 1 passo; ABORT; ABORTADA (estado seguro aplicado), sem registros"))

    s += 5
    casos.append(_caso_rotina(
        "CT17 tempo maximo", "ESPERA de 500 ms em uma rotina de no maximo 100 ms",
        [_comando(montar_run_routine(s, 17, 100, [passo_espera(500_000), passo(montar_gpio_read(0))]),
                  montar_ack(s, CMD_RUN_ROUTINE)),
         0.3,
         _status(s + 1, ESTADO_TEMPO_ESGOTADO, 0, 17),
         _comando(montar_read_result(s + 2), montar_ack(s + 2, CMD_READ_RESULT, (17).to_bytes(2, "little") + bytes(2)))],
        "TEMPO_ESGOTADO sem passos concluidos e sem registros"))

    s += 3
    run = montar_run_routine(s, 18, 2000, [passo_espera(600_000)])
    casos.append(_caso_rotina(
        "CT18 RUN_ROUTINE retransmitido", "ESPERA de 600 ms; o mesmo RUN_ROUTINE (mesmo SEQ) de novo em 0,3 s",
        [_comando(run, montar_ack(s, CMD_RUN_ROUTINE)),
         0.25,
         _comando(run, montar_ack(s, CMD_RUN_ROUTINE)),
         _status(s + 1, ESTADO_EM_EXECUCAO, 0, 18),
         0.4,
         _status(s + 2, ESTADO_CONCLUIDA, 1, 18)],
        "ACK reenviado; a rotina nao recomeca (CONCLUIDA em ~0,6 s; recomecada so terminaria em ~0,9 s)"))

    s += 3
    esperas = [passo(montar_gpio_read(0)), passo_espera(1000), passo(montar_gpio_read(0)), passo_espera(10_000),
               passo(montar_gpio_read(0)), passo_espera(100_000), passo(montar_gpio_read(0))]
    casos.append(_caso_rotina(
        "CT19 base de tempo", "GPIO_READ com ESPERA de 1, 10 e 100 ms entre eles (instantes medidos pela jiga)",
        [_comando(montar_run_routine(s, 19, 1000, esperas), montar_ack(s, CMD_RUN_ROUTINE)),
         0.2,
         _status(s + 1, ESTADO_CONCLUIDA, 7, 19),
         _ack_resultados(s + 2, 19, _resultado_nominal(esperas, 19, 1000, s + 2),
                         _conferir_tempos([(0, _mascara()), (2, _e(_mascara(), _intervalo(1000))),
                                           (4, _e(_mascara(), _intervalo(10_000))),
                                           (6, _e(_mascara(), _intervalo(100_000)))]))],
        f"CONCLUIDA; registros a 1000, 10 000 e 100 000 us um do outro (+/- {TOLERANCIA_TEMPO_US} us)"))
    return casos


def _caso_ct19_loopback(seq):
    """CT19 com o jumper GPIO1 -> GPIO5: tempo de reacao da entrada e tempo limite do ESPERA_GPIO."""
    passos = [passo(montar_gpio_set(0, 1, 0)), passo(montar_gpio_set(0, 2, 0)), passo_espera(1000),
              passo(montar_gpio_set(0, 1, 1)),
              passo_espera_gpio(5, 1, 10_000), passo(montar_gpio_set(0, 1, 0)), passo_espera_gpio(5, 0, 10_000),
              passo_espera_gpio(6, 1, 1000), passo(montar_gpio_set(0, 2, 1)), passo_espera(5000),
              passo(montar_gpio_read(0))]
    return _caso_rotina(
        "CT19 GPIO1 -> GPIO5", "ESPERA_GPIO na subida e na descida do GPIO5, e no GPIO6 sem estimulo (esgota)",
        [_comando(montar_run_routine(seq, 191, 1000, passos), montar_ack(seq, CMD_RUN_ROUTINE)),
         0.2,
         _status(seq + 1, ESTADO_CONCLUIDA, 11, 191),
         _ack_resultados(seq + 2, 191, _resultado_nominal(passos, 191, 1000, seq + 2, loopback=True),
                         _conferir_tempos([(4, _espera_gpio(GPIO_OCORREU, 0)), (6, _espera_gpio(GPIO_OCORREU, 0)),
                                           (7, _espera_gpio(GPIO_ESGOTOU, 1000)), (10, _mascara(0x02, bits=0x03))]))],
        f"GPIO5 reage em ate {TOLERANCIA_TEMPO_US} us nas duas bordas; GPIO6 esgota em 1000 us; GPIO5=0 e GPIO6=1",
        loopback=True)


def _caso_variavel(nome, descricao, quadro, resposta_nominal, verificador, resumo):
    """Caso de um comando so cuja resposta varia na placa."""
    return CasoTeste(nome, descricao, [("enviar", quadro)], [QUADRO_OK], [resposta_nominal],
                     conferir=conferir_quadros([verificador]), resumo_esperado=resumo)


CASOS_TESTE = [
    CasoTeste("PING valido", "PING sem payload: ACK com as versoes",
              [("enviar", _ping(1))], [QUADRO_OK], [montar_ack_ping(1)]),
    CasoTeste("Payload com 0x7E", "LEN=4 com 0x7E no meio do payload (enquadramento por LEN)",
              [("enviar", montar_quadro(2, CMD_INEXISTENTE, bytes([0x11, 0x7E, 0x22, 0x7E])))],
              [QUADRO_OK], [montar_nack(2, CMD_INEXISTENTE, ERRO_CMD_DESCONHECIDO)]),
    CasoTeste("Payload maximo", "LEN=240",
              [("enviar", montar_quadro(3, CMD_INEXISTENTE, bytes(range(PAYLOAD_MAX))))],
              [QUADRO_OK], [montar_nack(3, CMD_INEXISTENTE, ERRO_CMD_DESCONHECIDO)]),
    CasoTeste("CRC errado", "PING com o ultimo byte do CRC invertido: sem resposta",
              [("enviar", _crc_trocado(_ping(4)))], [ERRO_CRC], []),
    CasoTeste("LEN invalido", "LEN=0xF1 (> 240): descartado logo no 2o byte, sem resposta",
              [("enviar", bytes([SOF, 0xF1, 0x05, 0x01]))], [ERRO_LEN], []),
    CasoTeste("Lixo antes do SOF", "bytes soltos e depois um PING",
              [("enviar", bytes([0xAA, 0xBB, 0x00]) + _ping(6))], [QUADRO_OK], [montar_ack_ping(6)]),
    CasoTeste("Dois quadros juntos", "dois PINGs no mesmo envio: duas respostas",
              [("enviar", _ping(7) + _ping(8))], [QUADRO_OK, QUADRO_OK],
              [montar_ack_ping(7), montar_ack_ping(8)]),
    CasoTeste("Quadro pela metade", "meio quadro, silencio > 20 ms, depois um PING",
              [("enviar", _ping(9)[:3]), ("esperar", ESPERA_TIMEOUT), ("enviar", _ping(10))],
              [TIMEOUT, QUADRO_OK], [montar_ack_ping(10)]),
    CasoTeste("Comando desconhecido", "CMD 0x55 nao existe: NACK CMD_DESCONHECIDO",
              [("enviar", montar_quadro(11, CMD_INEXISTENTE))],
              [QUADRO_OK], [montar_nack(11, CMD_INEXISTENTE, ERRO_CMD_DESCONHECIDO)]),
    CasoTeste("SEQ repetido", "PING e depois outro CMD com o mesmo SEQ: reenvia o ACK, sem executar",
              [("enviar", _ping(12)), ("esperar", 0.1), ("enviar", montar_quadro(12, CMD_INEXISTENTE))],
              [QUADRO_OK, QUADRO_OK], [montar_ack_ping(12), montar_ack_ping(12)]),
    # Comandos executados pela TestExecTask (v0.4.0). Os invalidos fecham o CT09 junto com o
    # "Comando desconhecido": NACK com o erro certo e nenhuma saida alterada
    CasoTeste("RESET_ALL", "leva as saidas ao estado seguro: ACK",
              [("enviar", montar_quadro(13, CMD_RESET_ALL))],
              [QUADRO_OK], [montar_ack(13, CMD_RESET_ALL)]),
    CasoTeste("DAC_SET valido", "DAC1 = 2048 (~1,65 V no PA4): ACK",
              [("enviar", montar_dac_set(14, 1, 2048))],
              [QUADRO_OK], [montar_ack(14, CMD_DAC_SET)]),
    CasoTeste("DAC_SET canal invalido", "canal 3 nao existe: NACK PARAMETRO_INVALIDO",
              [("enviar", montar_dac_set(15, 3, 2048))],
              [QUADRO_OK], [montar_nack(15, CMD_DAC_SET, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("DAC_SET valor invalido", "4096 passa de 12 bits: NACK PARAMETRO_INVALIDO",
              [("enviar", montar_dac_set(16, 1, DAC_VALOR_MAX + 1))],
              [QUADRO_OK], [montar_nack(16, CMD_DAC_SET, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("DAC_SET tamanho errado", "payload de 2 bytes (esperado 3): NACK PARAMETRO_INVALIDO",
              [("enviar", montar_quadro(17, CMD_DAC_SET, bytes([0x01, 0x00])))],
              [QUADRO_OK], [montar_nack(17, CMD_DAC_SET, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("Comando durante execucao", "DAC_SET e RESET_ALL no mesmo envio: o 2o recebe OCUPADO (antes do ACK)",
              [("enviar", montar_dac_set(18, 1, 1024) + montar_quadro(19, CMD_RESET_ALL))],
              [QUADRO_OK, QUADRO_OK],
              [montar_nack(19, CMD_RESET_ALL, ERRO_OCUPADO), montar_ack(18, CMD_DAC_SET)]),
    # GPIO e ADC sem fio (v0.5.0): as entradas nao tem pull, entao o valor lido e indefinido e so o
    # formato da resposta e conferido; os valores sao conferidos no CT10 e no CT11, com loopback
    CasoTeste("GPIO_SET valido", "GPIO1 (PC8) em nivel alto: ACK",
              [("enviar", montar_gpio_set(21, 1, 1))],
              [QUADRO_OK], [montar_ack(21, CMD_GPIO_SET)]),
    CasoTeste("GPIO_SET pino invalido", "GPIO5 e entrada: NACK PARAMETRO_INVALIDO",
              [("enviar", montar_gpio_set(22, 5, 1))],
              [QUADRO_OK], [montar_nack(22, CMD_GPIO_SET, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("GPIO_SET nivel invalido", "nivel 2: NACK PARAMETRO_INVALIDO",
              [("enviar", montar_gpio_set(23, 1, 2))],
              [QUADRO_OK], [montar_nack(23, CMD_GPIO_SET, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("GPIO_SET tamanho errado", "payload de 1 byte (esperado 2): NACK PARAMETRO_INVALIDO",
              [("enviar", montar_quadro(24, CMD_GPIO_SET, bytes([0x01])))],
              [QUADRO_OK], [montar_nack(24, CMD_GPIO_SET, ERRO_PARAMETRO_INVALIDO)]),
    _caso_variavel("GPIO_READ", "le GPIO5-8: ACK com 1 byte (entradas soltas: so o formato e conferido)",
                   montar_gpio_read(25), montar_ack(25, CMD_GPIO_READ, bytes([0])), _ack_gpio_read(25),
                   "ACK GPIO_READ SEQ=25 com 1 byte, bits 4 a 7 em 0"),
    CasoTeste("GPIO_READ com payload", "GPIO_READ nao tem payload: NACK PARAMETRO_INVALIDO",
              [("enviar", montar_quadro(26, CMD_GPIO_READ, bytes([0x00])))],
              [QUADRO_OK], [montar_nack(26, CMD_GPIO_READ, ERRO_PARAMETRO_INVALIDO)]),
    _caso_variavel("ADC_READ", "le o A0 (PA3): ACK com canal e valor (pino solto: so o formato e conferido)",
                   montar_adc_read(27, 0), montar_ack(27, CMD_ADC_READ, bytes([0, 0, 0])), _ack_adc_read(27, 0),
                   "ACK ADC_READ SEQ=27 com canal 0 e valor de 0 a 4095"),
    CasoTeste("ADC_READ canal invalido", "canal 3 nao existe: NACK PARAMETRO_INVALIDO",
              [("enviar", montar_adc_read(28, 3))],
              [QUADRO_OK], [montar_nack(28, CMD_ADC_READ, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("ADC_READ sem payload", "falta o canal: NACK PARAMETRO_INVALIDO",
              [("enviar", montar_quadro(29, CMD_ADC_READ))],
              [QUADRO_OK], [montar_nack(29, CMD_ADC_READ, ERRO_PARAMETRO_INVALIDO)]),
    # PWM (v0.6.0): a forma de onda e conferida no CT12 (multimetro ou osciloscopio no pino)
    CasoTeste("PWM_SET valido", "PWM1 (PE9) em 1 kHz e 50 %: ACK",
              [("enviar", montar_pwm_set(30, 1, 1000, 5000))],
              [QUADRO_OK], [montar_ack(30, CMD_PWM_SET)]),
    CasoTeste("PWM_SET canal invalido", "canal 4 nao existe: NACK PARAMETRO_INVALIDO",
              [("enviar", montar_pwm_set(31, 4, 1000, 5000))],
              [QUADRO_OK], [montar_nack(31, CMD_PWM_SET, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("PWM_SET frequencia 0", "0 Hz (minimo 1 Hz): NACK PARAMETRO_INVALIDO",
              [("enviar", montar_pwm_set(32, 1, 0, 5000))],
              [QUADRO_OK], [montar_nack(32, CMD_PWM_SET, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("PWM_SET frequencia alta", "100001 Hz (maximo 100 kHz): NACK PARAMETRO_INVALIDO",
              [("enviar", montar_pwm_set(33, 1, PWM_FREQUENCIA_MAX + 1, 5000))],
              [QUADRO_OK], [montar_nack(33, CMD_PWM_SET, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("PWM_SET razao invalida", "razao 10001 (maximo 10000 = 100 %): NACK PARAMETRO_INVALIDO",
              [("enviar", montar_pwm_set(34, 1, 1000, PWM_RAZAO_MAX + 1))],
              [QUADRO_OK], [montar_nack(34, CMD_PWM_SET, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("PWM_SET tamanho errado", "payload de 3 bytes (esperado 7): NACK PARAMETRO_INVALIDO",
              [("enviar", montar_quadro(35, CMD_PWM_SET, bytes([0x01, 0xE8, 0x03])))],
              [QUADRO_OK], [montar_nack(35, CMD_PWM_SET, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("PWM_STOP valido", "para o PWM1 (razao 0, PE9 em nivel baixo): ACK",
              [("enviar", montar_pwm_stop(36, 1))],
              [QUADRO_OK], [montar_ack(36, CMD_PWM_STOP)]),
    CasoTeste("PWM_STOP canal invalido", "canal 0 nao existe: NACK PARAMETRO_INVALIDO",
              [("enviar", montar_pwm_stop(37, 0))],
              [QUADRO_OK], [montar_nack(37, CMD_PWM_STOP, ERRO_PARAMETRO_INVALIDO)]),
    # I2C (v0.7.0), sem DUT: so os comandos e o mapa; o barramento e conferido no CT20, com o DUT de referencia
    CasoTeste("I2C_CONFIG escravo", "jiga escrava no 0x50 a 100 kHz: ACK",
              [("enviar", montar_i2c_config(40, I2C_PAPEL_ESCRAVO, 0x50, 0))],
              [QUADRO_OK], [montar_ack(40, CMD_I2C_CONFIG)]),
    CasoTeste("I2C_CONFIG endereco reservado", "endereco 0x78 (reservado): NACK PARAMETRO_INVALIDO",
              [("enviar", montar_i2c_config(41, I2C_PAPEL_ESCRAVO, 0x78, 0))],
              [QUADRO_OK], [montar_nack(41, CMD_I2C_CONFIG, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("I2C_CONFIG tamanho errado", "payload de 2 bytes (esperado 3): NACK PARAMETRO_INVALIDO",
              [("enviar", montar_quadro(42, CMD_I2C_CONFIG, bytes([0x00, 0x50])))],
              [QUADRO_OK], [montar_nack(42, CMD_I2C_CONFIG, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("I2C LOAD e DUMP", "carrega DE AD BE EF em 0x10 e le de volta: ACK com os mesmos bytes",
              [("enviar", montar_i2c_load(43, 0x10, bytes.fromhex("DEADBEEF"))), ("esperar", PAUSA_COMANDO),
               ("enviar", montar_i2c_dump(44, 0x10, 4))],
              [QUADRO_OK, QUADRO_OK],
              [montar_ack(43, CMD_I2C_SLAVE_LOAD), montar_ack(44, CMD_I2C_SLAVE_DUMP, bytes.fromhex("DEADBEEF"))]),
    CasoTeste("I2C_SLAVE_LOAD alem do mapa", "2 bytes a partir de 0xFF: NACK PARAMETRO_INVALIDO",
              [("enviar", montar_i2c_load(45, 0xFF, b"\x01\x02"))],
              [QUADRO_OK], [montar_nack(45, CMD_I2C_SLAVE_LOAD, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("I2C_SLAVE_LOAD tamanho errado", "quantidade 3 com 2 bytes de dados: NACK PARAMETRO_INVALIDO",
              [("enviar", montar_quadro(46, CMD_I2C_SLAVE_LOAD, bytes([0x00, 0x03, 0xAA, 0xBB])))],
              [QUADRO_OK], [montar_nack(46, CMD_I2C_SLAVE_LOAD, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("I2C_SLAVE_DUMP grande demais", "241 bytes nao cabem na resposta: NACK PARAMETRO_INVALIDO",
              [("enviar", montar_i2c_dump(47, 0x00, PAYLOAD_MAX + 1))],
              [QUADRO_OK], [montar_nack(47, CMD_I2C_SLAVE_DUMP, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("I2C_MASTER_XFER sem mestre", "transferencia sem I2C_CONFIG de mestre: NACK NAO_CONFIGURADO",
              [("enviar", montar_i2c_xfer(48, 0x68, b"\x0f", 2))],
              [QUADRO_OK], [montar_nack(48, CMD_I2C_MASTER_XFER, ERRO_NAO_CONFIGURADO)]),
    CasoTeste("RESET_ALL zera o mapa I2C", "LOAD em 0x00, RESET_ALL e DUMP: o registrador volta a 0",
              [("enviar", montar_i2c_load(49, 0x00, b"\x5a")), ("esperar", PAUSA_COMANDO),
               ("enviar", montar_quadro(50, CMD_RESET_ALL)), ("esperar", PAUSA_COMANDO),
               ("enviar", montar_i2c_dump(51, 0x00, 1))],
              [QUADRO_OK] * 3,
              [montar_ack(49, CMD_I2C_SLAVE_LOAD), montar_ack(50, CMD_RESET_ALL),
               montar_ack(51, CMD_I2C_SLAVE_DUMP, b"\x00")]),
    # SPI (v0.8.0), sem DUT: so os comandos e os buffers; a transferencia como mestre e conferida no CT13, com o
    # jumper MOSI -> MISO, e a escrava no CT20, com o DUT de referencia
    CasoTeste("SPI_CONFIG mestre", "jiga mestre no modo 0 a 1 MHz (54 MHz / 64 = 843 750 Hz): ACK",
              [("enviar", montar_spi_config(52, SPI_PAPEL_MESTRE, 0, 1_000_000))],
              [QUADRO_OK], [montar_ack(52, CMD_SPI_CONFIG)]),
    CasoTeste("SPI_CONFIG modo invalido", "modo 4 (so existem 0 a 3): NACK PARAMETRO_INVALIDO",
              [("enviar", montar_spi_config(53, SPI_PAPEL_ESCRAVO, 4))],
              [QUADRO_OK], [montar_nack(53, CMD_SPI_CONFIG, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("SPI_CONFIG mestre lento", "mestre a 100 kHz (minimo 210 938 Hz = 54 MHz / 256): NACK PARAMETRO_INVALIDO",
              [("enviar", montar_spi_config(54, SPI_PAPEL_MESTRE, 0, 100_000))],
              [QUADRO_OK], [montar_nack(54, CMD_SPI_CONFIG, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("SPI_CONFIG tamanho errado", "payload de 2 bytes (esperado 6): NACK PARAMETRO_INVALIDO",
              [("enviar", montar_quadro(55, CMD_SPI_CONFIG, bytes([SPI_PAPEL_ESCRAVO, 0])))],
              [QUADRO_OK], [montar_nack(55, CMD_SPI_CONFIG, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("SPI_CONFIG escrava", "jiga escrava no modo 0 (CS no PD7 vira entrada): ACK",
              [("enviar", montar_spi_config(56, SPI_PAPEL_ESCRAVO, 0))],
              [QUADRO_OK], [montar_ack(56, CMD_SPI_CONFIG)]),
    CasoTeste("SPI_MASTER_XFER sem mestre", "transferencia com a jiga escrava: NACK NAO_CONFIGURADO",
              [("enviar", montar_spi_xfer(57, b"\x9f\x00"))],
              [QUADRO_OK], [montar_nack(57, CMD_SPI_MASTER_XFER, ERRO_NAO_CONFIGURADO)]),
    CasoTeste("SPI_MASTER_XFER vazio", "quantidade 0: NACK PARAMETRO_INVALIDO",
              [("enviar", montar_spi_xfer(58, b""))],
              [QUADRO_OK], [montar_nack(58, CMD_SPI_MASTER_XFER, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("SPI LOAD e READ sem DUT", "carrega 3 bytes e le a recepcao: ACK e [0] (nenhuma transacao do DUT)",
              [("enviar", montar_spi_load(59, b"\x11\x22\x33")), ("esperar", PAUSA_COMANDO),
               ("enviar", montar_spi_read(60))],
              [QUADRO_OK] * 2, [montar_ack(59, CMD_SPI_SLAVE_LOAD), montar_ack(60, CMD_SPI_SLAVE_READ, b"\x00")]),
    CasoTeste("SPI_SLAVE_LOAD tamanho errado", "quantidade 3 com 2 bytes de dados: NACK PARAMETRO_INVALIDO",
              [("enviar", montar_quadro(61, CMD_SPI_SLAVE_LOAD, bytes([0x03, 0xAA, 0xBB])))],
              [QUADRO_OK], [montar_nack(61, CMD_SPI_SLAVE_LOAD, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("SPI_SLAVE_READ com payload", "READ nao tem payload: NACK PARAMETRO_INVALIDO",
              [("enviar", montar_quadro(62, CMD_SPI_SLAVE_READ, b"\x00"))],
              [QUADRO_OK], [montar_nack(62, CMD_SPI_SLAVE_READ, ERRO_PARAMETRO_INVALIDO)]),
    # UART (v0.8.0), sem fio: so os comandos. Sem o jumper o RX (PD0) fica solto e pode receber lixo, entao
    # nenhum caso daqui le a UART configurada; a recepcao e conferida no CT13
    CasoTeste("UART_WRITE sem config", "escrita antes do UART_CONFIG: NACK NAO_CONFIGURADO",
              [("enviar", montar_uart_write(63, b"OK"))],
              [QUADRO_OK], [montar_nack(63, CMD_UART_WRITE, ERRO_NAO_CONFIGURADO)]),
    CasoTeste("UART_READ sem config", "leitura antes do UART_CONFIG: NACK NAO_CONFIGURADO",
              [("enviar", montar_uart_read(64))],
              [QUADRO_OK], [montar_nack(64, CMD_UART_READ, ERRO_NAO_CONFIGURADO)]),
    CasoTeste("UART_CONFIG baud baixo", "1199 baud (minimo 1200): NACK PARAMETRO_INVALIDO",
              [("enviar", montar_uart_config(65, UART_BAUD_MIN - 1))],
              [QUADRO_OK], [montar_nack(65, CMD_UART_CONFIG, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("UART_CONFIG baud alto", "1 000 001 baud (maximo 1 000 000): NACK PARAMETRO_INVALIDO",
              [("enviar", montar_uart_config(66, UART_BAUD_MAX + 1))],
              [QUADRO_OK], [montar_nack(66, CMD_UART_CONFIG, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("UART_CONFIG tamanho errado", "payload de 3 bytes (esperado 4): NACK PARAMETRO_INVALIDO",
              [("enviar", montar_quadro(67, CMD_UART_CONFIG, bytes([0x00, 0xC2, 0x01])))],
              [QUADRO_OK], [montar_nack(67, CMD_UART_CONFIG, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("UART_CONFIG e WRITE", "115200 baud e envia 'OK\\n' (sem DUT, o TX so sai no PD1): dois ACKs",
              [("enviar", montar_uart_config(68, 115_200)), ("esperar", PAUSA_COMANDO),
               ("enviar", montar_uart_write(69, b"OK\n"))],
              [QUADRO_OK] * 2, [montar_ack(68, CMD_UART_CONFIG), montar_ack(69, CMD_UART_WRITE)]),
    CasoTeste("UART_WRITE vazio", "quantidade 0: NACK PARAMETRO_INVALIDO",
              [("enviar", montar_uart_write(70, b""))],
              [QUADRO_OK], [montar_nack(70, CMD_UART_WRITE, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("UART_READ tamanho errado", "READ sem o maximo: NACK PARAMETRO_INVALIDO",
              [("enviar", montar_quadro(71, CMD_UART_READ))],
              [QUADRO_OK], [montar_nack(71, CMD_UART_READ, ERRO_PARAMETRO_INVALIDO)]),
    CasoTeste("RESET_ALL desliga a UART", "RESET_ALL e READ: a UART volta a pedir UART_CONFIG",
              [("enviar", montar_quadro(72, CMD_RESET_ALL)), ("esperar", PAUSA_COMANDO),
               ("enviar", montar_uart_read(73))],
              [QUADRO_OK] * 2, [montar_ack(72, CMD_RESET_ALL), montar_nack(73, CMD_UART_READ, ERRO_NAO_CONFIGURADO)]),
    # Com os jumpers (opcao de loopback): CT10, CT11 e CT13 da monografia
    _caso_ct10(1, 100),
    _caso_ct10(2, 122),
    _caso_ct11(150),
    _caso_ct13_spi(230),
    _caso_ct13_uart(240),
    _caso_ct19_loopback(250),
    # Com o DUT de referencia (ESP32-S3) ligado na jiga, sem os jumpers do CT13: CT20 da monografia
    *_casos_ct20(74),
    # Rotinas (sem jumpers e sem DUT): CT14 a CT18 e a base de tempo do CT19
    *_casos_rotina(186),
    CasoTeste("RESET_ALL no fim", "deixa a jiga no estado seguro depois da bateria: ACK",
              [("enviar", montar_quadro(20, CMD_RESET_ALL))],
              [QUADRO_OK], [montar_ack(20, CMD_RESET_ALL)]),
]


def marca_caso(caso):
    """Marca do caso nas listas: precisa dos jumpers ou do DUT ligado."""
    return " [loopback]" if caso.requer_loopback else " [DUT]" if caso.requer_dut else ""


def casos_a_executar(loopback=False, dut=False):
    """Casos do 'executar todos': os de loopback so entram com os jumpers ligados e os do CT20 com o DUT."""
    return [caso for caso in CASOS_TESTE
            if (loopback or not caso.requer_loopback) and (dut or not caso.requer_dut)]


# Inicio das linhas que a CommTask escreve no DebugLog para cada resultado
LOG_FIRMWARE = {
    QUADRO_OK: "[TASK -> COMM] Quadro OK",
    ERRO_CRC: "[TASK -> COMM] Erro de CRC",
    ERRO_LEN: "[TASK -> COMM] LEN maior que",
    TIMEOUT: "[TASK -> COMM] Timeout",
}


def resultado_do_log(linha):
    """Converte uma linha do DebugLog no resultado do parser, ou None se nao for do parser."""
    for resultado, prefixo in LOG_FIRMWARE.items():
        if linha.strip().startswith(prefixo):
            return resultado
    return None


def simular(caso, jiga=None):
    """Roda o caso na jiga simulada; retorna (resultados do parser, bytes respondidos).

    Sem 'jiga', usa uma nova, com loopback se o caso pedir.
    """
    jiga = jiga or JigaSimulada(loopback=caso.requer_loopback, dut=DutSimulado() if caso.requer_dut else None)
    resultados, respostas = [], b""
    for tipo, valor in caso.passos:
        if tipo == "enviar":
            novos, resposta = jiga.processar(valor)
            resultados += novos
            respostas += resposta
        else:
            if valor * 1000 > TIMEOUT_PACOTE_MS and jiga.timeout() == TIMEOUT:
                resultados.append(TIMEOUT)
            jiga.avancar(valor)
    return resultados, respostas


def simular_caso(caso, receptor=None):
    """Resultados do parser para o caso (deve bater com 'esperado')."""
    jiga = JigaSimulada(receptor, loopback=caso.requer_loopback,
                        dut=DutSimulado() if caso.requer_dut else None) if receptor else None
    return simular(caso, jiga)[0]


@dataclass
class Avaliacao:
    """Veredito de um caso executado na placa."""

    log_obtido: list        # resultados do parser lidos no DebugLog
    linhas_log: list        # linhas do DebugLog que geraram esses resultados
    resposta_obtida: bytes  # bytes recebidos pelo USB durante o caso
    log_ok: bool
    resposta_ok: bool
    motivo: str = ""        # por que a resposta do USB foi reprovada

    @property
    def aprovado(self):
        return self.log_ok and self.resposta_ok


def avaliar_caso(caso, linhas_log, resposta_usb):
    """Compara o DebugLog e a resposta do USB com o esperado do caso."""
    do_parser = [linha for linha in linhas_log if resultado_do_log(linha)]
    obtido = [resultado_do_log(linha) for linha in do_parser]
    motivo = caso.conferir_resposta(resposta_usb)
    return Avaliacao(log_obtido=obtido, linhas_log=do_parser, resposta_obtida=bytes(resposta_usb),
                     log_ok=obtido == caso.esperado, resposta_ok=motivo is None, motivo=motivo or "")


def descrever_log(resultados):
    """Resultados do parser como no DebugLog; repeticoes seguidas viram 'Nx ...' (casos longos)."""
    grupos = []
    for resultado in resultados:
        if grupos and grupos[-1][0] == resultado:
            grupos[-1][1] += 1
        else:
            grupos.append([resultado, 1])
    return ", ".join(LOG_FIRMWARE[r] if n == 1 else f"{n}x {LOG_FIRMWARE[r]}" for r, n in grupos) or "(nada)"


def descrever_resposta(dados, maximo=None):
    """Descreve as respostas; com 'maximo', mostra so as primeiras e quantas faltam."""
    descricoes = descrever_respostas(dados)
    if maximo is not None and len(descricoes) > maximo:
        descricoes = descricoes[:maximo] + [f"... (+{len(descricoes) - maximo} respostas)"]
    return "; ".join(descricoes) or "(nenhuma)"
