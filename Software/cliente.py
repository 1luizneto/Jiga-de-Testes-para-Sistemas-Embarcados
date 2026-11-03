"""
Cliente da jiga (secao 3.6 da monografia): uma operacao por comando do protocolo.

- TransporteSerial: abre a porta do USB-CDC e troca bytes com a jiga (pyserial). Abrir a porta liga o DTR, que
  comeca uma sessao nova na jiga (o cache de SEQ e esquecido).
- TransporteSimulado: o mesmo papel com a JigaSimulada; pode descartar respostas de proposito (CT05) e anda o
  relogio da jiga simulada no tempo que o cliente espera.
- ClienteJiga: controla o SEQ, o tempo limite (100 ms) e as retransmissoes (3 tentativas, mesmo SEQ). A jiga
  reconhece a retransmissao pelo SEQ e responde de novo sem executar outra vez (Apendice A). Os metodos de cada
  comando devolvem os dados ja interpretados e levantam ErroNack quando a jiga recusa o comando.

O CodecProtocolo da monografia sao as funcoes de montagem e o ReceptorQuadros do protocolo.py.
"""

import time

import serial
from serial.tools import list_ports

from protocolo import (
    CMD_ABORT, CMD_ADC_READ, CMD_DAC_SET, CMD_GPIO_READ, CMD_GPIO_SET, CMD_I2C_CONFIG, CMD_I2C_MASTER_XFER,
    CMD_I2C_SLAVE_DUMP, CMD_I2C_SLAVE_LOAD, CMD_PING, CMD_PWM_SET, CMD_PWM_STOP, CMD_READ_RESULT, CMD_RESET_ALL,
    CMD_ROUTINE_STATUS, CMD_RUN_ROUTINE, CMD_SPI_CONFIG, CMD_SPI_MASTER_XFER, CMD_SPI_SLAVE_LOAD,
    CMD_SPI_SLAVE_READ, CMD_UART_CONFIG, CMD_UART_READ, CMD_UART_WRITE, CODIGOS_ERRO, QUADRO_OK, JigaSimulada,
    ReceptorQuadros, StatusRotina, interpretar_resposta, montar_quadro, montar_rotina, nome_comando,
)

STM32_VID = 0x0483
STM32_CDC_PID = 0x5740

TEMPO_LIMITE = 0.1          # s: tempo limite de resposta (Apendice A, Sequenciamento e Retransmissao)
TENTATIVAS = 3              # envio original + 2 retransmissoes
BITS_POR_BYTE_UART = 10     # 8N1


class ErroComunicacao(Exception):
    """A jiga nao respondeu depois de todas as tentativas (ou respondeu algo que nao e ACK/NACK)."""


class ErroNack(Exception):
    """A jiga recusou o comando (NACK)."""

    def __init__(self, resposta):
        self.resposta = resposta
        super().__init__(resposta.descrever())

    @property
    def erro(self):
        return self.resposta.erro


def detectar_porta():
    """Porta do USB-CDC do STM32, ou None se nao estiver conectado."""
    for p in list_ports.comports():
        if p.vid == STM32_VID and p.pid == STM32_CDC_PID:
            return p.device
    return None


class TransporteSerial:
    """Porta serial virtual (USB-CDC) da jiga."""

    def __init__(self, porta=None):
        self.porta = porta
        self.serial = None

    def abrir(self, porta=None):
        porta = porta or self.porta or detectar_porta()
        if porta is None:
            raise ErroComunicacao("jiga nao encontrada (USB-CDC 0483:5740)")
        try:
            self.serial = serial.Serial(porta, timeout=0)
        except serial.SerialException as e:
            raise ErroComunicacao(f"nao abriu {porta}: {e}") from e
        self.porta = porta
        self.serial.reset_input_buffer()

    def fechar(self):
        if self.serial is not None:
            try:
                self.serial.close()
            except serial.SerialException:
                pass
            self.serial = None

    def escrever(self, dados):
        try:
            self.serial.write(dados)
        except (serial.SerialException, OSError) as e:
            raise ErroComunicacao(f"falha ao escrever: {e}") from e

    def ler(self, tempo_limite):
        """Bytes recebidos ate o tempo limite (devolve assim que chegar algo; b"" so depois do tempo inteiro)."""
        fim = time.monotonic() + tempo_limite
        while True:
            try:
                pendentes = self.serial.in_waiting
                if pendentes:
                    return self.serial.read(pendentes)
            except (serial.SerialException, OSError) as e:
                raise ErroComunicacao(f"falha ao ler: {e}") from e
            if time.monotonic() >= fim:
                return b""
            time.sleep(0.0005)

    def aguardar(self, segundos):
        time.sleep(segundos)


class TransporteSimulado:
    """Transporte ligado a uma JigaSimulada, para os testes sem a placa.

    descartar: quantas respostas seguidas jogar fora (como se a USB perdesse o quadro), a partir de
    'descartar_a_partir' respostas recebidas; o cliente deve retransmitir com o mesmo SEQ (CT05).
    O tempo que o cliente espera anda o relogio da jiga simulada (as esperas das rotinas terminam).
    """

    def __init__(self, jiga=None, descartar=0, descartar_a_partir=0):
        self.jiga = jiga if jiga is not None else JigaSimulada()
        self.descartar = descartar
        self.descartar_a_partir = descartar_a_partir
        self.recebidas = 0
        self.descartadas = 0
        self.enviados = []
        self._buffer = b""

    def abrir(self, porta=None):
        self.jiga.nova_sessao()

    def fechar(self):
        pass

    def escrever(self, dados):
        self.enviados.append(bytes(dados))
        _, respostas = self.jiga.processar(dados)
        for _, quadro in ReceptorQuadros().processar(respostas):
            self.recebidas += 1
            if self.recebidas > self.descartar_a_partir and self.descartadas < self.descartar:
                self.descartadas += 1
                continue
            self._buffer += montar_quadro(quadro.seq, quadro.cmd, quadro.payload)

    def ler(self, tempo_limite):
        if self._buffer:
            dados, self._buffer = self._buffer, b""
            return dados
        self.jiga.avancar(tempo_limite)
        return b""

    def aguardar(self, segundos):
        self.jiga.avancar(segundos)


class ClienteJiga:
    """Uma operacao por comando do protocolo, com SEQ, tempo limite e retransmissao."""

    def __init__(self, transporte, tempo_limite=TEMPO_LIMITE, tentativas=TENTATIVAS):
        self.transporte = transporte
        self.tempo_limite = tempo_limite
        self.tentativas = tentativas
        self.seq = 0
        self.comandos = 0
        self.retransmissoes = 0
        self.uart_baud = None
        self._receptor = ReceptorQuadros()

    def abrir(self, porta=None):
        self.transporte.abrir(porta)
        self._receptor = ReceptorQuadros()

    def fechar(self):
        self.transporte.fechar()

    def aguardar(self, segundos):
        self.transporte.aguardar(segundos)

    def _proximo_seq(self):
        seq, self.seq = self.seq, (self.seq + 1) % 256
        return seq

    def _esperar_resposta(self, seq, tempo_limite):
        """Resposta com o SEQ do comando, ou None no tempo limite. Respostas de outro SEQ (atrasadas) ficam de fora."""
        fim = time.monotonic() + tempo_limite
        while True:
            restante = fim - time.monotonic()
            if restante <= 0:
                return None
            dados = self.transporte.ler(restante)
            if not dados:
                return None
            for resultado, quadro in self._receptor.processar(dados):
                if resultado != QUADRO_OK or quadro.seq != seq:
                    continue
                try:
                    return interpretar_resposta(quadro)
                except ValueError as e:
                    raise ErroComunicacao(f"resposta invalida: {e}") from e

    def comando(self, cmd, payload=b"", tempo_limite=None):
        """Envia um comando e devolve a Resposta (ACK ou NACK); ErroComunicacao depois das tentativas."""
        tempo_limite = self.tempo_limite if tempo_limite is None else tempo_limite
        seq = self._proximo_seq()
        quadro = montar_quadro(seq, cmd, payload)
        self.comandos += 1
        for tentativa in range(self.tentativas):
            if tentativa:
                self.retransmissoes += 1
            self.transporte.escrever(quadro)
            resposta = self._esperar_resposta(seq, tempo_limite)
            if resposta is not None:
                return resposta
        raise ErroComunicacao(f"{nome_comando(cmd)} SEQ={seq}: sem resposta em {self.tentativas} tentativas")

    def _ack(self, cmd, payload=b"", tempo_limite=None):
        resposta = self.comando(cmd, payload, tempo_limite)
        if not resposta.ack:
            raise ErroNack(resposta)
        if resposta.cmd != cmd:
            raise ErroComunicacao(f"ACK de {nome_comando(resposta.cmd)} para {nome_comando(cmd)}")
        return resposta.dados

    # --- comandos gerais ---------------------------------------------------------------------------------

    def ping(self):
        """(versao do protocolo, 'maior.menor.correcao' do firmware)."""
        dados = self._ack(CMD_PING)
        if len(dados) != 4:
            raise ErroComunicacao(f"ACK do PING com {len(dados)} B")
        return dados[0], f"{dados[1]}.{dados[2]}.{dados[3]}"

    def reset_all(self):
        self._ack(CMD_RESET_ALL)

    # --- comandos diretos --------------------------------------------------------------------------------

    def dac_set(self, canal, valor):
        self._ack(CMD_DAC_SET, bytes([canal]) + int(valor).to_bytes(2, "little"))

    def gpio_set(self, pino, nivel):
        self._ack(CMD_GPIO_SET, bytes([pino, nivel]))

    def gpio_read(self):
        """Mascara das entradas (bit 0 = GPIO5)."""
        return self._ack(CMD_GPIO_READ)[0]

    def adc_read(self, canal):
        dados = self._ack(CMD_ADC_READ, bytes([canal]))
        return int.from_bytes(dados[1:3], "little")

    def pwm_set(self, canal, frequencia, razao):
        self._ack(CMD_PWM_SET, bytes([canal]) + int(frequencia).to_bytes(4, "little") + int(razao).to_bytes(2, "little"))

    def pwm_stop(self, canal):
        self._ack(CMD_PWM_STOP, bytes([canal]))

    def i2c_config(self, papel, endereco, velocidade):
        self._ack(CMD_I2C_CONFIG, bytes([papel, endereco, velocidade]))

    def i2c_slave_load(self, offset, dados):
        self._ack(CMD_I2C_SLAVE_LOAD, bytes([offset, len(dados)]) + bytes(dados))

    def i2c_slave_dump(self, offset, quantidade):
        return self._ack(CMD_I2C_SLAVE_DUMP, bytes([offset, quantidade]))

    def i2c_master_xfer(self, endereco, escrita=b"", nr=0):
        return self._ack(CMD_I2C_MASTER_XFER, bytes([endereco, len(escrita), nr]) + bytes(escrita))

    def spi_config(self, papel, modo, frequencia=0):
        self._ack(CMD_SPI_CONFIG, bytes([papel, modo]) + int(frequencia).to_bytes(4, "little"))

    def spi_slave_load(self, dados):
        self._ack(CMD_SPI_SLAVE_LOAD, bytes([len(dados)]) + bytes(dados))

    def spi_slave_read(self):
        dados = self._ack(CMD_SPI_SLAVE_READ)
        return dados[1:1 + dados[0]]

    def spi_master_xfer(self, dados):
        return self._ack(CMD_SPI_MASTER_XFER, bytes([len(dados)]) + bytes(dados))

    def uart_config(self, baud):
        self.uart_baud = baud
        self._ack(CMD_UART_CONFIG, int(baud).to_bytes(4, "little"))

    def uart_write(self, dados):
        """O ACK so sai depois de enviar: o tempo limite cresce com o tempo de transmissao."""
        baud = self.uart_baud or 1200
        envio = len(dados) * BITS_POR_BYTE_UART / baud
        self._ack(CMD_UART_WRITE, bytes([len(dados)]) + bytes(dados), self.tempo_limite + envio)

    def uart_read(self, maximo=239):
        dados = self._ack(CMD_UART_READ, bytes([maximo]))
        return dados[1:1 + dados[0]]

    # --- rotinas -----------------------------------------------------------------------------------------

    def run_routine(self, identificador, tempo_max_ms, passos):
        """Envia a rotina; ErroNack com o indice do passo se a jiga recusar (ROTINA_INVALIDA)."""
        self._ack(CMD_RUN_ROUTINE, montar_rotina(identificador, tempo_max_ms, passos))

    def status(self):
        return StatusRotina.de_bytes(self._ack(CMD_ROUTINE_STATUS))

    def read_result(self):
        """Area de resultados inteira (varios READ_RESULT, de ate 236 B cada): (identificador, bytes)."""
        dados, total, identificador = b"", None, None
        while total is None or len(dados) < total:
            parte = self._ack(CMD_READ_RESULT, len(dados).to_bytes(2, "little"))
            identificador = int.from_bytes(parte[0:2], "little")
            total = int.from_bytes(parte[2:4], "little")
            if len(parte) == 4 and len(dados) < total:
                raise ErroComunicacao("READ_RESULT sem dados antes do fim da area")
            dados += parte[4:]
        return identificador, dados

    def abort(self):
        self._ack(CMD_ABORT)


def descrever_erro(codigo):
    return CODIGOS_ERRO.get(codigo, f"0x{codigo:02X}")
