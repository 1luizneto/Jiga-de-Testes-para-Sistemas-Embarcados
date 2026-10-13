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
CMD_NACK = 0xFF
FLAG_ACK = 0x80

# Codigo que nenhum comando do Apendice A usa: a jiga sempre responde NACK CMD_DESCONHECIDO.
# Os casos de teste usam ele para nao mudar de resposta quando os comandos forem implementados.
CMD_INEXISTENTE = 0x55

NOMES_COMANDO = {CMD_PING: "PING", CMD_RESET_ALL: "RESET_ALL", CMD_DAC_SET: "DAC_SET"}

# Versao do protocolo informada no ACK do PING (dCOMM_PROTOCOL_VERSION no firmware)
VERSAO_PROTOCOLO = 1

# DAC (Apendice A): canal 1 = DAC1 (PA4), 2 = DAC2 (PA5); 12 bits
DAC_CANAIS = (1, 2)
DAC_VALOR_MAX = 4095

# A CommTask le o USB em blocos de ate 64 bytes (rxBuffer) e so confere o resultado da TestExecTask
# depois de processar o bloco inteiro: um comando que chega no mesmo bloco de outro pendente recebe
# OCUPADO antes de o pendente ser respondido
TAMANHO_BLOCO_RX = 64

# Codigos de erro do NACK (Apendice A, commError_t no firmware)
ERRO_CMD_DESCONHECIDO = 0x01
ERRO_PARAMETRO_INVALIDO = 0x02
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
    - demais -> executados: RESET_ALL, DAC_SET; codigo desconhecido -> NACK CMD_DESCONHECIDO.

    Como no firmware, o resultado do pendente so e respondido depois do bloco de bytes inteiro
    (TAMANHO_BLOCO_RX): por isso um comando que chega junto recebe OCUPADO antes.
    """

    receptor: ReceptorQuadros = field(default_factory=ReceptorQuadros)
    ultimo_seq: int = None
    ultima_resposta: bytes = b""
    pendente: Quadro = None
    dac: dict = field(default_factory=lambda: {canal: 0 for canal in DAC_CANAIS})

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

    def _executar(self, quadro):
        """Equivale ao Exec_Process: 0 (ACK) ou codigo de erro (NACK). Comando invalido nao mexe no DAC."""
        if quadro.cmd == CMD_RESET_ALL:
            if quadro.payload:
                return ERRO_PARAMETRO_INVALIDO
            self.dac = {canal: 0 for canal in DAC_CANAIS}
            return 0
        if quadro.cmd == CMD_DAC_SET:
            if len(quadro.payload) != 3:
                return ERRO_PARAMETRO_INVALIDO
            canal, valor = quadro.payload[0], int.from_bytes(quadro.payload[1:3], "little")
            if canal not in DAC_CANAIS or valor > DAC_VALOR_MAX:
                return ERRO_PARAMETRO_INVALIDO
            self.dac[canal] = valor
            return 0
        return ERRO_CMD_DESCONHECIDO

    def _concluir_pendente(self):
        """Equivale ao CommTask_VerificaResultado + Comm_BuildResult."""
        if self.pendente is None:
            return b""
        quadro, self.pendente = self.pendente, None
        erro = self._executar(quadro)
        resposta = montar_ack(quadro.seq, quadro.cmd) if erro == 0 else montar_nack(quadro.seq, quadro.cmd, erro)
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
    nome: str
    descricao: str
    passos: list
    esperado: list
    respostas: list

    def bytes_enviados(self):
        return b"".join(dados for tipo, dados in self.passos if tipo == "enviar")

    def resposta_esperada(self):
        return b"".join(self.respostas)


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
    CasoTeste("RESET_ALL no fim", "deixa a jiga no estado seguro depois da bateria: ACK",
              [("enviar", montar_quadro(20, CMD_RESET_ALL))],
              [QUADRO_OK], [montar_ack(20, CMD_RESET_ALL)]),
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


def simular(caso, jiga=None):
    """Roda o caso na jiga simulada; retorna (resultados do parser, bytes respondidos)."""
    jiga = jiga or JigaSimulada()
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
    return simular(caso, JigaSimulada(receptor) if receptor else None)[0]


@dataclass
class Avaliacao:
    """Veredito de um caso executado na placa."""

    log_obtido: list        # resultados do parser lidos no DebugLog
    linhas_log: list        # linhas do DebugLog que geraram esses resultados
    resposta_obtida: bytes  # bytes recebidos pelo USB durante o caso
    log_ok: bool
    resposta_ok: bool

    @property
    def aprovado(self):
        return self.log_ok and self.resposta_ok


def avaliar_caso(caso, linhas_log, resposta_usb):
    """Compara o DebugLog e a resposta do USB com o esperado do caso."""
    do_parser = [linha for linha in linhas_log if resultado_do_log(linha)]
    obtido = [resultado_do_log(linha) for linha in do_parser]
    return Avaliacao(log_obtido=obtido, linhas_log=do_parser, resposta_obtida=bytes(resposta_usb),
                     log_ok=obtido == caso.esperado,
                     resposta_ok=bytes(resposta_usb) == caso.resposta_esperada())


def descrever_log(resultados):
    return ", ".join(LOG_FIRMWARE[r] for r in resultados) or "(nada)"


def descrever_resposta(dados):
    return "; ".join(descrever_respostas(dados)) or "(nenhuma)"
