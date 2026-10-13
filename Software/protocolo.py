"""
Protocolo binario PC <-> jiga (lado do PC).

Quadro: SOF(0x7E) | LEN | SEQ | CMD | PAYLOAD(0 a 240) | CRC-16 (little-endian)

- CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, sem reflexao, sem XOR final)
  calculado sobre LEN + SEQ + CMD + PAYLOAD.
- O 0x7E so marca inicio de quadro quando o receptor esta procurando o SOF;
  dentro do quadro ele e um byte comum (enquadramento por comprimento).

O ReceptorQuadros espelha o parser do firmware (projeto/App/Comm/Comm.c):
os testes do software e os casos de teste enviados para a placa usam ele
para saber o resultado esperado.
"""

from dataclasses import dataclass, field

SOF = 0x7E
TAMANHO_CABECALHO = 3            # LEN + SEQ + CMD
PAYLOAD_MAX = 240
TAMANHO_QUADRO_MAX = 1 + TAMANHO_CABECALHO + PAYLOAD_MAX + 2
CRC_INICIAL = 0xFFFF
CRC_POLINOMIO = 0x1021

# Comandos ja definidos na monografia (os demais entram com o Apendice A)
CMD_PING = 0x01
CMD_NACK = 0xFF
FLAG_ACK = 0x80

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


# --- casos de teste para enviar a placa --------------------------------------
#
# Cada passo e ("enviar", bytes) ou ("esperar", segundos). "esperado" e a lista de
# resultados que o parser da placa deve produzir, na ordem; a CommTask loga cada um
# na UART de debug (ST-Link). Esperar mais que dCOMM_RX_TIMEOUT_MS (100 ms) dispara
# o timeout de pacote.

ESPERA_TIMEOUT = 0.25


def _ping(seq):
    return montar_quadro(seq, CMD_PING)


def _crc_trocado(quadro):
    return quadro[:-1] + bytes([quadro[-1] ^ 0xFF])


@dataclass
class CasoTeste:
    nome: str
    descricao: str
    passos: list
    esperado: list

    def bytes_enviados(self):
        return b"".join(dados for tipo, dados in self.passos if tipo == "enviar")


CASOS_TESTE = [
    CasoTeste("PING valido", "PING sem payload",
              [("enviar", _ping(1))], [QUADRO_OK]),
    CasoTeste("Payload com 0x7E", "LEN=4 com 0x7E no meio do payload (enquadramento por LEN)",
              [("enviar", montar_quadro(2, 0x10, bytes([0x11, 0x7E, 0x22, 0x7E])))], [QUADRO_OK]),
    CasoTeste("Payload maximo", "LEN=240",
              [("enviar", montar_quadro(3, 0x10, bytes(range(PAYLOAD_MAX))))], [QUADRO_OK]),
    CasoTeste("CRC errado", "PING com o ultimo byte do CRC invertido",
              [("enviar", _crc_trocado(_ping(4)))], [ERRO_CRC]),
    CasoTeste("LEN invalido", "LEN=0xF1 (> 240): descartado logo no 2o byte",
              [("enviar", bytes([SOF, 0xF1, 0x05, 0x01]))], [ERRO_LEN]),
    CasoTeste("Lixo antes do SOF", "bytes soltos e depois um PING",
              [("enviar", bytes([0xAA, 0xBB, 0x00]) + _ping(6))], [QUADRO_OK]),
    CasoTeste("Dois quadros juntos", "dois PINGs no mesmo envio",
              [("enviar", _ping(7) + _ping(8))], [QUADRO_OK, QUADRO_OK]),
    CasoTeste("Quadro pela metade", "meio quadro, silencio > 100 ms, depois um PING",
              [("enviar", _ping(9)[:3]), ("esperar", ESPERA_TIMEOUT), ("enviar", _ping(10))],
              [TIMEOUT, QUADRO_OK]),
]


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


def simular_caso(caso, receptor=None):
    """Roda o caso no receptor do PC e devolve os resultados (deve bater com 'esperado')."""
    receptor = receptor or ReceptorQuadros()
    resultados = []
    for tipo, valor in caso.passos:
        if tipo == "enviar":
            resultados += [r for r, _ in receptor.processar(valor)]
        elif valor * 1000 > 100:
            resultado = receptor.timeout()
            if resultado == TIMEOUT:
                resultados.append(TIMEOUT)
    return resultados
