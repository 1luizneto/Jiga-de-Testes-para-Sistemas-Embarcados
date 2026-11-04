"""
DUT de referencia (ESP32-S3) do lado do PC: os comandos de texto e o espelho do firmware.

O firmware fica em DUT_ESP32S3/ (comandos.c, mapa_i2c.c, barramentos.c). A UART1 do ESP32 vai na UART4 da
jiga: o PC manda um comando com UART_WRITE ("I2CM 50 10 4\\n") e le a resposta com UART_READ
("OK 01020304\\n"). Assim o CT20 roda inteiro pela jiga, sem outro cabo.

O DutSimulado espelha o interpretador e os barramentos do ESP32 e se liga a JigaSimulada (protocolo.py):
o pytest confere os casos do CT20 sem placa, como ja faz com os outros casos.
"""

import re

from sensores import bme_calibracao_de_bytes, bme_le, mpu_le
from versao import __version__

DUT_BAUD = 115200                 # UART1 do ESP32, 8N1
DUT_DADOS_MAX = 239               # o mesmo limite dos buffers da jiga
DUT_LINHA_MAX = 512
DUT_I2C_ENDERECO_MIN = 0x08
DUT_I2C_ENDERECO_MAX = 0x77
DUT_I2C_BLOCO_LEITURA = 128       # bytes postos na FIFO do escravo I2C a cada leitura do mestre
DUT_SPI_MODO_MAX = 3
DUT_SPI_FREQUENCIA_MIN = 1000
DUT_SPI_FREQUENCIA_MAX = 10_000_000
DUT_IDENTIFICACAO = f"OK DUT ESP32-S3 {__version__}"
DUT_MPU_ENDERECO, DUT_BME_ENDERECO = 0x68, 0x76
DUT_TENTATIVAS = 10               # leituras do bit de reset antes de desistir (drivers_sensores.c)

# Ligacao jiga <-> ESP32-S3 (GND comum; pull-ups de 4,7 kOhm para o 3V3 no SDA e no SCL)
DUT_LIGACAO = [
    ("PD0 (UART4 RX)", "GPIO17 (UART1 TX)"),
    ("PD1 (UART4 TX)", "GPIO18 (UART1 RX)"),
    ("PF0 (I2C2 SDA)", "GPIO8 (SDA)"),
    ("PF1 (I2C2 SCL)", "GPIO9 (SCL)"),
    ("PD7 (CS)", "GPIO10 (CS)"),
    ("PB15 (MOSI)", "GPIO11 (MOSI)"),
    ("PB10 (SCK)", "GPIO12 (SCK)"),
    ("PC2 (MISO)", "GPIO13 (MISO)"),
    ("GND", "GND"),
]

_HEX = re.compile(r"^[0-9A-Fa-f]*$")


def linha_dut(comando, *argumentos):
    """Monta a linha de um comando do DUT, ex.: linha_dut("I2CM", "50", b"\\x10", 4) -> b"I2CM 50 10 4\\n".

    bytes viram hex sem separador ("-" se vazio); int vira decimal; str passa como esta.
    """
    campos = [comando]
    for argumento in argumentos:
        if isinstance(argumento, (bytes, bytearray)):
            campos.append(bytes(argumento).hex().upper() or "-")
        else:
            campos.append(str(argumento))
    return (" ".join(campos) + "\n").encode()


def resposta_dut(dados=None, erro=None):
    """Texto que o DUT responde: "OK", "OK <hex>" ou "ERR <motivo>", com o '\\n'."""
    if erro:
        return f"ERR {erro}\n".encode()
    if dados:
        return f"OK {bytes(dados).hex().upper()}\n".encode()
    return b"OK\n"


class _Erro(Exception):
    """Resposta de erro do interpretador ("ARG", "NACK", "ESTADO")."""


def _hex(texto):
    if texto == "-":
        return b""
    if not texto or len(texto) % 2 or len(texto) // 2 > DUT_DADOS_MAX or not _HEX.match(texto):
        raise _Erro("ARG")
    return bytes.fromhex(texto)


def _byte_hex(texto):
    if not 1 <= len(texto) <= 2 or not _HEX.match(texto):
        raise _Erro("ARG")
    return int(texto, 16)


def _decimal(texto, maximo):
    if not texto.isdigit() or not texto.isascii() or int(texto) > maximo:
        raise _Erro("ARG")
    return int(texto)


def _endereco(texto):
    endereco = _byte_hex(texto)
    if not DUT_I2C_ENDERECO_MIN <= endereco <= DUT_I2C_ENDERECO_MAX:
        raise _Erro("ARG")
    return endereco


class DutSimulado:
    """Espelho do firmware do DUT: interpretador (comandos.c), mapa I2C (mapa_i2c.c) e barramentos.

    A 'jiga' (JigaSimulada) e o outro lado dos barramentos: com o DUT mestre, ele le e escreve a jiga escrava;
    com a jiga mestra, ela chama i2c_jiga_mestre() e spi_jiga_mestre().
    """

    def __init__(self):
        self.jiga = None
        self.linha = bytearray()
        self.desligar()
        self.i2c_mapa = bytearray(256)
        self.i2c_ponteiro = 0
        self.spi_carga = b""
        self.spi_recebido = bytearray()

    def desligar(self):
        """OFF: escravos desligados (o mapa e a carga do SPI ficam)."""
        self.i2c_endereco = None
        self.spi_modo = None
        self.spi_tx_armado = b""

    # --- UART -------------------------------------------------------------------

    def receber_uart(self, dados):
        """Bytes que chegam na UART1: junta em linhas, executa cada uma e devolve o que o DUT responde."""
        respostas = b""
        for byte in dados:
            if byte == 0x0D:
                continue
            if byte != 0x0A:
                self.linha.append(byte)
                continue
            linha, self.linha = self.linha.decode(errors="replace"), bytearray()
            respostas += self.executar(linha)
        return respostas

    def executar(self, linha):
        """Uma linha de comando -> resposta (b"" para linha vazia)."""
        if len(linha) > DUT_LINHA_MAX:
            return resposta_dut(erro="ARG")
        campos = linha.split()
        if not campos:
            return b""
        nome, argumentos = campos[0], campos[1:]
        if nome == "PING":
            return resposta_dut(erro="ARG") if argumentos else (DUT_IDENTIFICACAO + "\n").encode()
        if nome in ("MPU", "BME", "MPUS", "BMES"):
            try:
                if len(argumentos) > 1:
                    raise _Erro("ARG")
                if nome.endswith("S"):
                    # Por SPI: o argumento e a frequencia; o endereco do sensor nao existe
                    if argumentos:
                        _decimal(argumentos[0], DUT_SPI_FREQUENCIA_MAX)
                        if int(argumentos[0]) < DUT_SPI_FREQUENCIA_MIN:
                            raise _Erro("ARG")
                    endereco = None
                else:
                    padrao = DUT_MPU_ENDERECO if nome == "MPU" else DUT_BME_ENDERECO
                    endereco = _endereco(argumentos[0]) if argumentos else padrao
                return (self._mpu(endereco) if nome.startswith("MPU") else self._bme(endereco)).encode()
            except _Erro as erro:
                return resposta_dut(erro=str(erro))
        tabela = {
            "OFF": (0, self._off), "I2CM": (3, self._i2cm), "I2CS": (1, self._i2cs),
            "I2CS_LOAD": (2, self._i2cs_load), "I2CS_DUMP": (2, self._i2cs_dump), "SPIM": (3, self._spim),
            "SPIS": (1, self._spis), "SPIS_LOAD": (1, self._spis_load), "SPIS_READ": (0, self._spis_read),
        }
        if nome not in tabela:
            return b"ERR CMD\n"
        quantidade, funcao = tabela[nome]
        try:
            if len(argumentos) != quantidade:
                raise _Erro("ARG")
            return resposta_dut(funcao(*argumentos))
        except _Erro as erro:
            return resposta_dut(erro=str(erro))

    # --- comandos ---------------------------------------------------------------

    def _off(self):
        self.desligar()

    def _i2cm(self, endereco, escrita, nr):
        endereco, escrita, nr = _endereco(endereco), _hex(escrita), _decimal(nr, DUT_DADOS_MAX)
        if not escrita and nr == 0:
            raise _Erro("ARG")
        if self.i2c_endereco is not None:
            raise _Erro("ESTADO")
        lidos = self.jiga.i2c_mestre_externo(endereco, escrita, nr) if self.jiga else None
        if lidos is None:
            raise _Erro("NACK")
        return lidos

    # --- sensores (drivers_sensores.c) ----------------------------------------------

    def _mestre(self, endereco, escrita, nr=0):
        """Acesso do driver a um registrador: I2C (endereco) ou SPI (endereco None: bit 7 do 1o byte = leitura, com 7
        bits de endereco; sem a jiga no modo registradores, o MISO solto le 0xFF)."""
        if endereco is None:
            if self.spi_modo is not None:
                raise _Erro("ESTADO")
            leitura = nr > 0
            primeiro = (escrita[0] | 0x80) if leitura else (escrita[0] & 0x7F)
            dados = bytes(escrita[1:]) if not leitura else bytes(nr)
            lidos = self.jiga.spi_registro_externo(primeiro, dados, nr if leitura else len(dados)) if self.jiga else None
            return lidos if lidos is not None else bytes([0xFF] * nr)
        if self.i2c_endereco is not None:
            raise _Erro("ESTADO")
        lidos = self.jiga.i2c_mestre_externo(endereco, bytes(escrita), nr) if self.jiga else None
        if lidos is None:
            raise _Erro("NACK")
        return lidos

    def _mpu(self, endereco):
        """Reset (espera o bit 7 do PWR_MGMT_1 voltar a 0), WHO_AM_I, 2 g/250 graus/s e os 14 bytes do 0x3B."""
        self._mestre(endereco, [0x6B, 0x80])
        for _ in range(DUT_TENTATIVAS):
            if not self._mestre(endereco, [0x6B], 1)[0] & 0x80:
                break
        else:
            raise _Erro("PERIFERICO")
        self._mestre(endereco, [0x6B, 0x01])
        if self._mestre(endereco, [0x75], 1)[0] != 0x70:
            raise _Erro("PERIFERICO")
        self._mestre(endereco, [0x1B, 0x00])
        self._mestre(endereco, [0x1C, 0x00])
        aceleracao, temperatura, giro = mpu_le(self._mestre(endereco, [0x3B], 14))
        return ("OK " + " ".join(f"{v:.4f}" for v in aceleracao) + " " + " ".join(f"{v:.3f}" for v in giro)
                + f" {temperatura:.2f}\n")

    def _bme(self, endereco):
        """chip_id, soft reset (espera o im_update), calibracao, modo normal e os 8 bytes do 0xF7 compensados."""
        if self._mestre(endereco, [0xD0], 1)[0] != 0x60:
            raise _Erro("PERIFERICO")
        self._mestre(endereco, [0xE0, 0xB6])
        for _ in range(DUT_TENTATIVAS):
            if not self._mestre(endereco, [0xF3], 1)[0] & 0x01:
                break
        else:
            raise _Erro("PERIFERICO")
        calibracao = bme_calibracao_de_bytes(self._mestre(endereco, [0x88], 26), self._mestre(endereco, [0xE1], 7))
        self._mestre(endereco, [0xF2, 0x01])
        self._mestre(endereco, [0xF4, 0x27])
        temperatura, pressao, umidade = bme_le(self._mestre(endereco, [0xF7], 8), calibracao)
        return f"OK {temperatura:.2f} {pressao:.2f} {umidade:.2f}\n"

    def _i2cs(self, endereco):
        self.i2c_endereco = _endereco(endereco)

    def _i2cs_load(self, offset, dados):
        offset, dados = _byte_hex(offset), _hex(dados)
        if not dados or offset + len(dados) > 256:
            raise _Erro("ARG")
        self.i2c_mapa[offset:offset + len(dados)] = dados

    def _i2cs_dump(self, offset, quantidade):
        offset, quantidade = _byte_hex(offset), _decimal(quantidade, DUT_DADOS_MAX)
        if quantidade == 0 or offset + quantidade > 256:
            raise _Erro("ARG")
        return bytes(self.i2c_mapa[offset:offset + quantidade])

    def _spim(self, modo, frequencia, dados):
        _decimal(modo, DUT_SPI_MODO_MAX)
        if _decimal(frequencia, DUT_SPI_FREQUENCIA_MAX) < DUT_SPI_FREQUENCIA_MIN:
            raise _Erro("ARG")
        dados = _hex(dados)
        if not dados:
            raise _Erro("ARG")
        if self.spi_modo is not None:
            raise _Erro("ESTADO")
        lidos = self.jiga.spi_mestre_externo(dados) if self.jiga else None
        return lidos if lidos is not None else bytes(len(dados))   # sem escravo, o MISO solto leu 00

    def _spis(self, modo):
        self.spi_modo = _decimal(modo, DUT_SPI_MODO_MAX)
        self.spi_recebido = bytearray()
        self.spi_tx_armado = self.spi_carga

    def _spis_load(self, dados):
        self.spi_carga = _hex(dados)

    def _spis_read(self):
        recebido, self.spi_recebido = bytes(self.spi_recebido), bytearray()
        return recebido

    # --- a jiga como mestra -----------------------------------------------------

    def i2c_jiga_mestre(self, endereco, escrita, nr):
        """Transacao da jiga mestra: None se o DUT nao responde no endereco (falta de ACK).

        Escrita: o 1o byte e o ponteiro e os demais vao para o mapa (o ponteiro fica depois do ultimo). Leitura:
        a partir do ponteiro, sem move-lo
        (o driver do ESP32 nao informa quantos bytes o mestre leu; ver mapa_i2c.c).
        """
        if self.i2c_endereco != endereco:
            return None
        if escrita:
            self.i2c_ponteiro = escrita[0]
            for byte in escrita[1:]:
                self.i2c_mapa[self.i2c_ponteiro] = byte
                self.i2c_ponteiro = (self.i2c_ponteiro + 1) % 256
        bloco = bytes(self.i2c_mapa[(self.i2c_ponteiro + i) % 256] for i in range(DUT_I2C_BLOCO_LEITURA))
        return bloco[:nr] + bytes([0xFF]) * max(0, nr - DUT_I2C_BLOCO_LEITURA)

    def spi_jiga_mestre(self, enviados):
        """Transacao da jiga mestra: None se o escravo SPI do DUT estiver desligado.

        O DUT envia a carga armada (do byte 0, completada com FF) e guarda o que recebeu; a carga nova
        (SPIS_LOAD) vale a partir da proxima transacao, como no firmware.
        """
        if self.spi_modo is None:
            return None
        livre = DUT_DADOS_MAX - len(self.spi_recebido)
        self.spi_recebido += bytes(enviados[:livre])
        tx = (self.spi_tx_armado + bytes([0xFF]) * DUT_DADOS_MAX)[:len(enviados)]
        self.spi_tx_armado = self.spi_carga
        return tx
