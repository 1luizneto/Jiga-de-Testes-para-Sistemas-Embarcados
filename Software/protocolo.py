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
dados; NACK = mesmo SEQ, CMD 0xFF, payload [comando recusado, codigo do erro].
Quadro com CRC errado ou LEN invalido nao tem resposta.
"""

from dataclasses import dataclass, field

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
    CMD_PING: "PING", CMD_RESET_ALL: "RESET_ALL", CMD_DAC_SET: "DAC_SET",
    CMD_GPIO_SET: "GPIO_SET", CMD_GPIO_READ: "GPIO_READ", CMD_PWM_SET: "PWM_SET",
    CMD_PWM_STOP: "PWM_STOP", CMD_ADC_READ: "ADC_READ", CMD_I2C_CONFIG: "I2C_CONFIG",
    CMD_I2C_SLAVE_LOAD: "I2C_SLAVE_LOAD", CMD_I2C_SLAVE_DUMP: "I2C_SLAVE_DUMP",
    CMD_I2C_MASTER_XFER: "I2C_MASTER_XFER", CMD_SPI_CONFIG: "SPI_CONFIG", CMD_SPI_SLAVE_LOAD: "SPI_SLAVE_LOAD",
    CMD_SPI_SLAVE_READ: "SPI_SLAVE_READ", CMD_SPI_MASTER_XFER: "SPI_MASTER_XFER", CMD_UART_CONFIG: "UART_CONFIG",
    CMD_UART_WRITE: "UART_WRITE", CMD_UART_READ: "UART_READ",
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


def montar_nack(seq, cmd, erro):
    return montar_quadro(seq, CMD_NACK, bytes([cmd, erro]))


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
    dados: bytes = b""  # payload do ACK
    erro: int = 0       # codigo do erro, no NACK

    def descrever(self):
        if not self.ack:
            nome = CODIGOS_ERRO.get(self.erro, f"0x{self.erro:02X}")
            return f"NACK {nome_comando(self.cmd)} SEQ={self.seq}: {nome}"
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
        if len(quadro.payload) != 2:
            raise ValueError(f"NACK com {len(quadro.payload)} B de payload; esperado [cmd, erro]")
        return Resposta(seq=quadro.seq, ack=False, cmd=quadro.payload[0], erro=quadro.payload[1])
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
    i2c_mapa: bytearray = field(default_factory=lambda: bytearray(I2C_TAMANHO_MAPA))
    i2c_dispositivos: dict = field(default_factory=dict)
    spi_papel: int = None
    spi_tx: bytes = b""
    spi_rx: bytearray = field(default_factory=bytearray)
    uart_baud: int = None
    uart_rx: bytearray = field(default_factory=bytearray)

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
        if self.pendente is not None:
            return montar_nack(quadro.seq, quadro.cmd, ERRO_OCUPADO)
        self.pendente = quadro
        return b""

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
            self.dac = {canal: 0 for canal in DAC_CANAIS}
            self.gpio = {pino: 0 for pino in GPIO_SAIDAS}
            self.pwm = {canal: (frequencia, 0) for canal, (frequencia, _) in self.pwm.items()}
            self.i2c_papel = None
            self.i2c_mapa = bytearray(I2C_TAMANHO_MAPA)
            self.spi_papel, self.spi_tx, self.spi_rx = None, b"", bytearray()
            self.uart_baud, self.uart_rx = None, bytearray()
            return 0, b""
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
        if CMD_I2C_CONFIG <= quadro.cmd <= CMD_I2C_MASTER_XFER:
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
        return 0, bytes(dados) if self.loopback else bytes([SPI_BYTE_VAZIO]) * len(dados)

    def transacao_spi(self, enviados):
        """Uma transacao do DUT com a jiga escrava (CS baixo, bytes, CS alto): retorna o que a jiga enviou.

        Como no SpiBus, a transmissao recomeca do byte 0 e o que passa do buffer de recepcao e descartado.
        """
        livre = SPI_TAMANHO_BUFFER - len(self.spi_rx)
        self.spi_rx += bytes(enviados[:livre])
        tx = self.spi_tx + bytes([SPI_BYTE_VAZIO]) * SPI_TAMANHO_BUFFER
        return tx[:len(enviados)]

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
            if self.loopback:
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
            resposta = montar_nack(quadro.seq, quadro.cmd, erro)
        return self._guardar(quadro.seq, resposta)

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
    requer_loopback: bool = False   # so passa com os jumpers do CT10/CT11
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
    """[(quadro, resposta nominal, verificador)] -> (passos, esperado, respostas, conferir)."""
    passos = []
    for i, (quadro, _, _) in enumerate(comandos):
        if i:
            passos.append(("esperar", PAUSA_COMANDO))
        passos.append(("enviar", quadro))
    return (passos, [QUADRO_OK] * len(comandos), [resposta for _, resposta, _ in comandos],
            conferir_quadros([verificador for _, _, verificador in comandos]))


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
    CasoTeste("RESET_ALL no fim", "deixa a jiga no estado seguro depois da bateria: ACK",
              [("enviar", montar_quadro(20, CMD_RESET_ALL))],
              [QUADRO_OK], [montar_ack(20, CMD_RESET_ALL)]),
]


def casos_a_executar(loopback=False):
    """Casos do 'executar todos': os de loopback so entram com os jumpers ligados."""
    return [caso for caso in CASOS_TESTE if loopback or not caso.requer_loopback]


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
    jiga = jiga or JigaSimulada(loopback=caso.requer_loopback)
    resultados, respostas = [], b""
    for tipo, valor in caso.passos:
        if tipo == "enviar":
            novos, resposta = jiga.processar(valor)
            resultados += novos
            respostas += resposta
        elif valor * 1000 > TIMEOUT_PACOTE_MS:
            if jiga.timeout() == TIMEOUT:
                resultados.append(TIMEOUT)
    return resultados, respostas


def simular_caso(caso, receptor=None):
    """Resultados do parser para o caso (deve bater com 'esperado')."""
    jiga = JigaSimulada(receptor, loopback=caso.requer_loopback) if receptor else None
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
