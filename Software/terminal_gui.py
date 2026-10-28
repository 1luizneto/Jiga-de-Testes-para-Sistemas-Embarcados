"""
Terminal serial grafico da jiga de testes (no estilo do Termite).

Mostra o que chega pela porta serial do STM32 (USB-CDC ou a UART do ST-Link)
e permite enviar texto, bytes em hexadecimal ou quadros do protocolo, alem de
executar os casos de teste do parser com a placa (hardware-in-the-loop): os
quadros saem pelo USB-CDC (CN13) e o veredito vem do DebugLog lido na porta do
ST-Link, como no teste_protocolo.py.

Uso:
    python terminal_gui.py
"""

import codecs
import sys
import threading
import time
from datetime import datetime

import serial
from serial.tools import list_ports
from PySide6.QtCore import QSettings, Qt, QThread, QTimer, Signal
from PySide6.QtGui import QColor, QFont, QKeySequence, QShortcut, QTextCharFormat, QTextCursor
from PySide6.QtWidgets import (
    QAbstractItemView, QApplication, QCheckBox, QComboBox, QDoubleSpinBox, QFileDialog, QGroupBox,
    QHBoxLayout, QHeaderView, QLabel, QLineEdit, QMainWindow, QMessageBox, QPlainTextEdit, QPushButton, QSpinBox,
    QTableWidget, QTableWidgetItem, QVBoxLayout, QWidget,
)

from protocolo import (
    CASOS_TESTE, I2C_PAPEL_ESCRAVO, I2C_PAPEL_MESTRE, I2C_VELOCIDADES, PAYLOAD_MAX, PWM_CANAIS, PWM_FREQUENCIA_MAX,
    PWM_FREQUENCIA_MIN, PWM_FREQUENCIA_PADRAO, PWM_PINOS, PWM_RAZAO_MAX, QUADRO_OK, ReceptorQuadros, avaliar_caso, casos_a_executar, descrever_log,
    descrever_pwm, descrever_resposta, interpretar_resposta, montar_i2c_config, montar_i2c_dump, montar_i2c_load,
    montar_pwm_set, montar_pwm_stop, montar_quadro, porcento_para_razao,
)
from terminal_usb import (
    PAUSA_ENTRE_CASOS, SILENCIO_FIM_RAJADA, TEMPO_MAX_RAJADA, detectar_porta, formatar_hex,
    interpretar_hex,
)
from teste_protocolo import BAUD_LOG, ESPERA_LOG, MAXIMO_RESPOSTAS, detectar_porta_log
from versao import __version__

TAXAS_BAUD = ["9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"]
FINAIS_DE_LINHA = {"Nenhum": b"", "LF (\\n)": b"\n", "CR (\\r)": b"\r", "CR+LF": b"\r\n"}
MAX_LINHAS = 20000

COR_RX = QColor("#1a7f37")
COR_TX = QColor("#0b4fd6")
COR_INFO = QColor("#8a8a8a")
COR_ERRO = QColor("#c62828")
COR_LOG = QColor("#7a3e9d")
COR_PASS = QColor("#d7f5dd")
COR_FALHA = QColor("#fbd5d5")


class LeitorSerial(QThread):
    """Le a porta em segundo plano e reconecta sozinho se o USB cair."""

    recebido = Signal(bytes)
    estado = Signal(str, bool)  # mensagem, conectado

    def __init__(self, porta, baud):
        super().__init__()
        self.porta = porta
        self.baud = baud
        self.serial = None
        self.rodando = True
        self.trava = threading.Lock()

    def parar(self):
        self.rodando = False
        self.wait(2000)

    def escrever(self, dados):
        with self.trava:
            if self.serial is None:
                raise serial.SerialException("porta nao conectada")
            self.serial.write(dados)

    def _abrir(self):
        try:
            s = serial.Serial(self.porta, self.baud, timeout=0.05)
        except serial.SerialException:
            return False
        with self.trava:
            self.serial = s
        self.estado.emit(f"Conectado em {self.porta} ({self.baud} baud)", True)
        return True

    def _fechar(self):
        with self.trava:
            s, self.serial = self.serial, None
        if s:
            try:
                s.close()
            except serial.SerialException:
                pass

    def _ler_rajada(self):
        dados = bytearray(self.serial.read(1))
        inicio = time.monotonic()
        while dados and self.rodando and time.monotonic() - inicio < TEMPO_MAX_RAJADA:
            time.sleep(SILENCIO_FIM_RAJADA)
            pendentes = self.serial.in_waiting
            if not pendentes:
                break
            dados += self.serial.read(pendentes)
        return bytes(dados)

    def run(self):
        aguardando = False
        while self.rodando:
            if self.serial is None:
                if not self._abrir():
                    if not aguardando:
                        self.estado.emit(f"Aguardando {self.porta}...", False)
                        aguardando = True
                    time.sleep(0.5)
                    continue
                aguardando = False
            try:
                dados = self._ler_rajada()
            except (serial.SerialException, OSError):
                self._fechar()
                self.estado.emit(f"Conexao perdida em {self.porta}", False)
                continue
            if dados:
                self.recebido.emit(dados)
        self._fechar()


class CampoEnvio(QLineEdit):
    """Linha de envio com historico nas setas para cima/baixo, como no Termite."""

    def __init__(self):
        super().__init__()
        self.historico = []
        self.posicao = 0

    def guardar(self, texto):
        if texto and (not self.historico or self.historico[-1] != texto):
            self.historico.append(texto)
        self.posicao = len(self.historico)

    def keyPressEvent(self, evento):
        if evento.key() == Qt.Key_Up and self.historico:
            self.posicao = max(0, self.posicao - 1)
            self.setText(self.historico[self.posicao])
        elif evento.key() == Qt.Key_Down and self.historico:
            self.posicao = min(len(self.historico), self.posicao + 1)
            self.setText(self.historico[self.posicao] if self.posicao < len(self.historico) else "")
        else:
            super().keyPressEvent(evento)


class JanelaTerminal(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle(f"Terminal da Jiga v{__version__}")
        self.resize(1000, 800)
        self.leitor = None
        self.inicio_linha = True
        self.decodificador = codecs.getincrementaldecoder("utf-8")(errors="replace")
        self.fila_teste = []
        self.executando = False
        # DebugLog (UART do ST-Link): segunda porta, usada para o veredito dos casos
        self.leitor_log = None
        self.decodificador_log = codecs.getincrementaldecoder("utf-8")(errors="replace")
        self.resto_log = ""        # linha do log ainda sem '\n'
        self.linhas_caso = None    # linhas do log capturadas durante o caso atual
        self.rx_caso = None        # bytes recebidos pelo USB durante o caso atual
        self.receptor_rx = ReceptorQuadros()  # interpreta as respostas da jiga na tela
        self.aprovados = 0
        self.avaliados = 0
        self.config = QSettings("TCC-Jiga", "TerminalSerial")
        self._montar_interface()
        self._carregar_config()
        self.atualizar_portas()

        # Atualiza a lista de portas enquanto desconectado (placa ligada/desligada)
        self.timer_portas = QTimer(self, interval=1500, timeout=self._atualizar_portas_auto)
        self.timer_portas.start()

    # --- interface --------------------------------------------------------

    def _montar_interface(self):
        central = QWidget()
        layout = QVBoxLayout(central)

        # Linha 1: porta e conexao
        linha1 = QHBoxLayout()
        self.combo_porta = QComboBox(minimumWidth=320)
        self.bt_atualizar = QPushButton("Atualizar", clicked=self.atualizar_portas)
        self.combo_baud = QComboBox(editable=True)
        self.combo_baud.addItems(TAXAS_BAUD)
        self.combo_baud.setToolTip("Ignorado pelo USB-CDC; vale para a UART do ST-Link")
        self.bt_conectar = QPushButton("Conectar", checkable=True, minimumWidth=110)
        self.bt_conectar.toggled.connect(self.alternar_conexao)
        self.lb_estado = QLabel("Desconectado")
        linha1.addWidget(QLabel("Porta:"))
        linha1.addWidget(self.combo_porta)
        linha1.addWidget(self.bt_atualizar)
        linha1.addSpacing(12)
        linha1.addWidget(QLabel("Baud:"))
        linha1.addWidget(self.combo_baud)
        linha1.addSpacing(12)
        linha1.addWidget(self.bt_conectar)
        linha1.addWidget(self.lb_estado, 1)
        layout.addLayout(linha1)

        # Linha 2: opcoes de exibicao
        linha2 = QHBoxLayout()
        self.combo_modo = QComboBox()
        self.combo_modo.addItems(["Texto", "Hex"])
        self.combo_modo.currentTextChanged.connect(self._mudou_modo)
        self.ck_horario = QCheckBox("Horário", checked=True)
        self.ck_eco = QCheckBox("Mostrar enviados", checked=True)
        self.ck_rolagem = QCheckBox("Rolagem automática", checked=True)
        self.ck_interpretar = QCheckBox("Interpretar respostas", checked=True)
        self.ck_interpretar.setToolTip("Mostra cada quadro recebido da jiga já decodificado (ACK/NACK)")
        bt_limpar = QPushButton("Limpar", clicked=self.limpar)
        bt_salvar = QPushButton("Salvar...", clicked=self.salvar)
        linha2.addWidget(QLabel("Exibição:"))
        linha2.addWidget(self.combo_modo)
        linha2.addWidget(self.ck_horario)
        linha2.addWidget(self.ck_eco)
        linha2.addWidget(self.ck_rolagem)
        linha2.addWidget(self.ck_interpretar)
        linha2.addStretch(1)
        linha2.addWidget(bt_limpar)
        linha2.addWidget(bt_salvar)
        layout.addLayout(linha2)

        # Area de recepcao
        self.tela = QPlainTextEdit(readOnly=True)
        self.tela.setMaximumBlockCount(MAX_LINHAS)
        self.tela.setFont(QFont("Consolas", 10))
        self.tela.setStyleSheet("QPlainTextEdit { background: white; color: black; }")
        self.tela.setLineWrapMode(QPlainTextEdit.NoWrap)
        layout.addWidget(self.tela, 1)

        # Linha de envio
        linha3 = QHBoxLayout()
        self.campo = CampoEnvio()
        self.campo.setFont(QFont("Consolas", 10))
        self.campo.setPlaceholderText("Texto a enviar (Enter envia; setas recuperam o histórico)")
        self.campo.returnPressed.connect(self.enviar)
        self.ck_hex = QCheckBox("Hex")
        self.ck_hex.setToolTip("Envia o campo como bytes, ex.: 7E 01 02 0A")
        self.combo_fim = QComboBox()
        self.combo_fim.addItems(FINAIS_DE_LINHA)
        bt_enviar = QPushButton("Enviar", clicked=self.enviar)
        linha3.addWidget(self.campo, 1)
        linha3.addWidget(self.ck_hex)
        linha3.addWidget(QLabel("Final:"))
        linha3.addWidget(self.combo_fim)
        linha3.addWidget(bt_enviar)
        layout.addLayout(linha3)

        layout.addWidget(self._montar_protocolo())

        self.setCentralWidget(central)
        QShortcut(QKeySequence("Ctrl+L"), self, activated=self.limpar)

    def _montar_protocolo(self):
        grupo = QGroupBox("Protocolo (quadros e casos de teste do parser)")
        layout = QVBoxLayout(grupo)

        # Quadro montado a mao: SOF, LEN e CRC sao calculados
        linha_quadro = QHBoxLayout()
        self.spin_seq = QSpinBox(minimum=0, maximum=255)
        self.ck_seq_auto = QCheckBox("auto", checked=True)
        self.ck_seq_auto.setToolTip("Incrementa o SEQ a cada quadro enviado")
        self.campo_cmd = QLineEdit("01", maximumWidth=50)
        self.campo_cmd.setFont(QFont("Consolas", 10))
        self.campo_payload = QLineEdit()
        self.campo_payload.setFont(QFont("Consolas", 10))
        self.campo_payload.setPlaceholderText(f"Payload em hex (0 a {PAYLOAD_MAX} B), ex.: AA BB 7E")
        self.campo_payload.returnPressed.connect(self.enviar_quadro)
        bt_quadro = QPushButton("Enviar quadro", clicked=self.enviar_quadro)
        linha_quadro.addWidget(QLabel("SEQ:"))
        linha_quadro.addWidget(self.spin_seq)
        linha_quadro.addWidget(self.ck_seq_auto)
        linha_quadro.addSpacing(8)
        linha_quadro.addWidget(QLabel("CMD:"))
        linha_quadro.addWidget(self.campo_cmd)
        linha_quadro.addWidget(QLabel("Payload:"))
        linha_quadro.addWidget(self.campo_payload, 1)
        linha_quadro.addWidget(bt_quadro)
        layout.addLayout(linha_quadro)

        # PWM: monta o PWM_SET/PWM_STOP e mostra o que o timer vai gerar (referencia para o CT12)
        linha_pwm = QHBoxLayout()
        self.combo_pwm = QComboBox()
        for canal in PWM_CANAIS:
            self.combo_pwm.addItem(f"PWM{canal} ({PWM_PINOS[canal]})", canal)
        self.spin_freq = QSpinBox(suffix=" Hz", minimumWidth=110)
        self.spin_freq.setRange(PWM_FREQUENCIA_MIN, PWM_FREQUENCIA_MAX)
        self.spin_freq.setValue(PWM_FREQUENCIA_PADRAO)
        self.spin_razao = QDoubleSpinBox(suffix=" %", decimals=2)
        self.spin_razao.setRange(0, PWM_RAZAO_MAX / 100)
        self.spin_razao.setValue(50)
        self.lb_pwm = QLabel()
        self.lb_pwm.setStyleSheet("color: #555")
        for sinal in (self.combo_pwm.currentIndexChanged, self.spin_freq.valueChanged, self.spin_razao.valueChanged):
            sinal.connect(self._mudou_pwm)
        linha_pwm.addWidget(QLabel("PWM:"))
        linha_pwm.addWidget(self.combo_pwm)
        linha_pwm.addWidget(self.spin_freq)
        linha_pwm.addWidget(self.spin_razao)
        linha_pwm.addWidget(self.lb_pwm, 1)
        linha_pwm.addWidget(QPushButton("Aplicar PWM", clicked=self.enviar_pwm))
        linha_pwm.addWidget(QPushButton("Parar PWM", clicked=self.parar_pwm))
        layout.addLayout(linha_pwm)

        # I2C: papel da jiga no barramento e o mapa de registradores da jiga escrava (I2C_SLAVE_LOAD/DUMP)
        linha_i2c = QHBoxLayout()
        self.combo_i2c_papel = QComboBox()
        self.combo_i2c_papel.addItem("Escrava", I2C_PAPEL_ESCRAVO)
        self.combo_i2c_papel.addItem("Mestre", I2C_PAPEL_MESTRE)
        self.campo_i2c_endereco = QLineEdit("50", maximumWidth=40)
        self.campo_i2c_endereco.setToolTip("Endereço de 7 bits da jiga escrava, em hex (08 a 77)")
        self.combo_i2c_velocidade = QComboBox()
        for velocidade, hz in I2C_VELOCIDADES.items():
            self.combo_i2c_velocidade.addItem(f"{hz // 1000} kHz", velocidade)
        self.campo_i2c_offset = QLineEdit("00", maximumWidth=40)
        self.campo_i2c_offset.setToolTip("Primeiro registrador do mapa, em hex")
        self.campo_i2c_dados = QLineEdit()
        self.campo_i2c_dados.setFont(QFont("Consolas", 10))
        self.campo_i2c_dados.setPlaceholderText("Dados para carregar, em hex, ex.: DE AD BE EF")
        self.spin_i2c_qtd = QSpinBox(minimum=1, maximum=PAYLOAD_MAX, value=16)
        self.spin_i2c_qtd.setToolTip("Quantidade de registradores lidos pelo 'Ler mapa'")
        linha_i2c.addWidget(QLabel("I2C:"))
        linha_i2c.addWidget(self.combo_i2c_papel)
        linha_i2c.addWidget(QLabel("0x"))
        linha_i2c.addWidget(self.campo_i2c_endereco)
        linha_i2c.addWidget(self.combo_i2c_velocidade)
        linha_i2c.addWidget(QPushButton("Configurar", clicked=self.configurar_i2c))
        linha_i2c.addSpacing(12)
        linha_i2c.addWidget(QLabel("Reg. 0x"))
        linha_i2c.addWidget(self.campo_i2c_offset)
        linha_i2c.addWidget(self.campo_i2c_dados, 1)
        linha_i2c.addWidget(QPushButton("Carregar", clicked=self.carregar_i2c))
        linha_i2c.addWidget(self.spin_i2c_qtd)
        linha_i2c.addWidget(QPushButton("Ler mapa", clicked=self.ler_i2c))
        layout.addLayout(linha_i2c)

        # Casos de teste prontos
        linha_teste = QHBoxLayout()
        self.combo_caso = QComboBox()
        for i, caso in enumerate(CASOS_TESTE, 1):
            marca = " [loopback]" if caso.requer_loopback else ""
            self.combo_caso.addItem(f"{i}. {caso.nome}{marca}", caso)
        self.combo_caso.currentIndexChanged.connect(self._mudou_caso)
        self.lb_caso = QLabel()
        self.lb_caso.setStyleSheet("color: #555")
        self.bt_caso = QPushButton("Executar caso", clicked=lambda: self.executar_casos([self.combo_caso.currentData()]))
        self.bt_todos = QPushButton("Executar todos", clicked=lambda: self.executar_casos(
            casos_a_executar(self.ck_loopback.isChecked())))
        self.ck_loopback = QCheckBox("Incluir loopback")
        self.ck_loopback.setToolTip("Inclui no 'Executar todos' o CT10 e o CT11, que precisam dos jumpers:\n"
                                    "DAC1 (PA4) → A0 (PA3), DAC2 (PA5) → A1 (PC0)\n"
                                    "GPIO1-4 (PC8-PC11) → GPIO5-8 (PC12, PD2, PG2, PG3)")
        linha_teste.addWidget(QLabel("Caso de teste:"))
        linha_teste.addWidget(self.combo_caso)
        linha_teste.addWidget(self.lb_caso, 1)
        linha_teste.addWidget(self.bt_caso)
        linha_teste.addWidget(self.bt_todos)
        linha_teste.addWidget(self.ck_loopback)
        layout.addLayout(linha_teste)

        # Porta do DebugLog: e dela que sai o veredito de cada caso
        linha_log = QHBoxLayout()
        self.combo_log = QComboBox(minimumWidth=320)
        self.combo_log.setToolTip(f"Porta do ST-Link (USART3, {BAUD_LOG} baud)")
        self.bt_log = QPushButton("Conectar log", checkable=True, minimumWidth=110)
        self.bt_log.toggled.connect(self.alternar_log)
        self.ck_log_tela = QCheckBox("Mostrar log na tela", checked=True)
        self.lb_log = QLabel("Desconectado")
        linha_log.addWidget(QLabel("DebugLog:"))
        linha_log.addWidget(self.combo_log)
        linha_log.addWidget(self.bt_log)
        linha_log.addWidget(self.ck_log_tela)
        linha_log.addWidget(self.lb_log, 1)
        layout.addLayout(linha_log)

        # Resultado dos casos executados
        self.tabela = QTableWidget(0, 4)
        self.tabela.setHorizontalHeaderLabels(["Caso", "Esperado (DebugLog e USB)", "Obtido", "Resultado"])
        self.tabela.verticalHeader().setVisible(False)
        self.tabela.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.tabela.setSelectionBehavior(QAbstractItemView.SelectRows)
        cabecalho = self.tabela.horizontalHeader()
        cabecalho.setSectionResizeMode(0, QHeaderView.ResizeToContents)
        cabecalho.setSectionResizeMode(1, QHeaderView.Stretch)
        cabecalho.setSectionResizeMode(2, QHeaderView.Stretch)
        cabecalho.setSectionResizeMode(3, QHeaderView.ResizeToContents)
        self.tabela.setWordWrap(True)
        self.tabela.setMaximumHeight(360)
        layout.addWidget(self.tabela)

        self.lb_resumo = QLabel("Conecte o USB-CDC (acima) e o DebugLog: o veredito confere o log e a resposta do USB.")
        self.lb_resumo.setStyleSheet("color: #8a8a8a")
        layout.addWidget(self.lb_resumo)

        self._mudou_caso()
        self._mudou_pwm()
        return grupo

    def _carregar_config(self):
        self.combo_baud.setCurrentText(self.config.value("baud", "115200"))
        self.combo_modo.setCurrentText(self.config.value("modo", "Texto"))
        self.combo_fim.setCurrentText(self.config.value("fim", "LF (\\n)"))
        self.ck_hex.setChecked(self.config.value("enviar_hex", False, type=bool))
        self.ck_horario.setChecked(self.config.value("horario", True, type=bool))
        self.ck_log_tela.setChecked(self.config.value("mostrar_log", True, type=bool))
        self.ck_interpretar.setChecked(self.config.value("interpretar", True, type=bool))
        self.ck_loopback.setChecked(self.config.value("loopback", False, type=bool))

    def _salvar_config(self):
        self.config.setValue("loopback", self.ck_loopback.isChecked())
        self.config.setValue("porta", self.porta_selecionada() or "")
        self.config.setValue("porta_log", self.combo_log.currentData() or "")
        self.config.setValue("mostrar_log", self.ck_log_tela.isChecked())
        self.config.setValue("interpretar", self.ck_interpretar.isChecked())
        self.config.setValue("baud", self.combo_baud.currentText())
        self.config.setValue("modo", self.combo_modo.currentText())
        self.config.setValue("fim", self.combo_fim.currentText())
        self.config.setValue("enviar_hex", self.ck_hex.isChecked())
        self.config.setValue("horario", self.ck_horario.isChecked())

    # --- portas -----------------------------------------------------------

    def porta_selecionada(self):
        return self.combo_porta.currentData()

    @staticmethod
    def _preencher_portas(combo, portas, alvos):
        """Lista as portas no combo e seleciona o primeiro alvo encontrado."""
        combo.blockSignals(True)
        combo.clear()
        for p in portas:
            combo.addItem(f"{p.device} - {p.description}", p.device)
        combo.blockSignals(False)
        for alvo in alvos:
            indice = combo.findData(alvo) if alvo else -1
            if indice >= 0:
                combo.setCurrentIndex(indice)
                break

    def atualizar_portas(self):
        portas = list_ports.comports()
        # Prioridade: a porta que estava escolhida; senao, a detectada pelo VID/PID.
        # Combo de porta conectada nao e mexido.
        if self.leitor is None:
            anterior = self.porta_selecionada() or self.config.value("porta", "")
            self._preencher_portas(self.combo_porta, portas, (anterior, detectar_porta()))
        if self.leitor_log is None:
            anterior = self.combo_log.currentData() or self.config.value("porta_log", "")
            self._preencher_portas(self.combo_log, portas, (anterior, detectar_porta_log()))

    def _atualizar_portas_auto(self):
        if self.leitor is not None and self.leitor_log is not None:
            return
        if self.combo_porta.view().isVisible() or self.combo_log.view().isVisible():
            return
        combo = self.combo_porta if self.leitor is None else self.combo_log
        atuais = [combo.itemData(i) for i in range(combo.count())]
        if atuais != [p.device for p in list_ports.comports()]:
            self.atualizar_portas()

    # --- conexao ----------------------------------------------------------

    def alternar_conexao(self, conectar):
        if conectar:
            porta = self.porta_selecionada()
            try:
                baud = int(self.combo_baud.currentText())
            except ValueError:
                baud = 0
            if not porta or baud <= 0:
                QMessageBox.warning(self, "Terminal", "Escolha uma porta e uma taxa de baud válidas.")
                self.bt_conectar.setChecked(False)
                return
            self.receptor_rx.reiniciar()
            self.leitor = LeitorSerial(porta, baud)
            self.leitor.recebido.connect(self.mostrar_recebido)
            self.leitor.estado.connect(self.mostrar_estado)
            self.leitor.start()
            self.bt_conectar.setText("Desconectar")
            self._travar_config(True)
        else:
            if self.leitor:
                self.leitor.parar()
                self.leitor = None
            self.bt_conectar.setText("Conectar")
            self._travar_config(False)
            self.mostrar_estado("Desconectado", False)

    def _travar_config(self, travar):
        for w in (self.combo_porta, self.bt_atualizar, self.combo_baud):
            w.setEnabled(not travar)

    def mostrar_estado(self, mensagem, conectado):
        cor = "#1a7f37" if conectado else ("#c62828" if self.leitor else "#555")
        self.lb_estado.setText(f"<span style='color:{cor}'>● {mensagem}</span>")
        if self.leitor:
            self._escrever_info(mensagem)

    def alternar_log(self, conectar):
        if conectar:
            porta = self.combo_log.currentData()
            if not porta:
                QMessageBox.warning(self, "Terminal", "Escolha a porta do ST-Link para o DebugLog.")
                self.bt_log.setChecked(False)
                return
            if self.leitor is not None and porta == self.leitor.porta:
                QMessageBox.warning(self, "Terminal",
                                    "A porta do DebugLog é a mesma da conexão principal.\n"
                                    "Use o USB-CDC (CN13) acima e o ST-Link aqui.")
                self.bt_log.setChecked(False)
                return
            self.resto_log = ""
            self.decodificador_log.reset()
            self.leitor_log = LeitorSerial(porta, BAUD_LOG)
            self.leitor_log.recebido.connect(self.receber_log)
            self.leitor_log.estado.connect(self.mostrar_estado_log)
            self.leitor_log.start()
            self.bt_log.setText("Desconectar log")
            self.combo_log.setEnabled(False)
        else:
            if self.leitor_log:
                self.leitor_log.parar()
                self.leitor_log = None
            self.bt_log.setText("Conectar log")
            self.combo_log.setEnabled(True)
            self.mostrar_estado_log("Desconectado", False)

    def mostrar_estado_log(self, mensagem, conectado):
        cor = "#1a7f37" if conectado else ("#c62828" if self.leitor_log else "#555")
        self.lb_log.setText(f"<span style='color:{cor}'>● {mensagem}</span>")

    def receber_log(self, dados):
        """Monta as linhas do DebugLog: guarda para o caso em execucao e mostra na tela."""
        self.resto_log += self.decodificador_log.decode(dados)
        *linhas, self.resto_log = self.resto_log.replace("\r", "").split("\n")
        for linha in linhas:
            if self.linhas_caso is not None:
                self.linhas_caso.append(linha)
            if self.ck_log_tela.isChecked():
                self._quebrar_linha()
                self._escrever_texto(linha + "\n", COR_LOG, "LOG")

    # --- exibicao ---------------------------------------------------------

    def _formato(self, cor):
        f = QTextCharFormat()
        f.setForeground(cor)
        return f

    def _inserir(self, texto, cor):
        cursor = QTextCursor(self.tela.document())
        cursor.movePosition(QTextCursor.End)
        cursor.insertText(texto, self._formato(cor))
        if self.ck_rolagem.isChecked():
            barra = self.tela.verticalScrollBar()
            barra.setValue(barra.maximum())

    def _quebrar_linha(self):
        if not self.inicio_linha:
            self._inserir("\n", COR_INFO)
            self.inicio_linha = True

    def _horario(self):
        return datetime.now().strftime("%H:%M:%S.%f")[:-3]

    def _escrever_info(self, mensagem):
        self._quebrar_linha()
        self._inserir(f"*** {mensagem} ***\n", COR_INFO)

    def _escrever_texto(self, texto, cor, prefixo):
        """Escreve texto continuo, pondo o horario no inicio de cada linha."""
        texto = texto.replace("\r\n", "\n").replace("\r", "\n")
        for pedaco in texto.splitlines(keepends=True):
            if self.inicio_linha and self.ck_horario.isChecked():
                self._inserir(f"[{self._horario()}] {prefixo} ", COR_INFO)
            self._inserir(pedaco, cor)
            self.inicio_linha = pedaco.endswith("\n")

    def _escrever_hex(self, dados, cor, prefixo):
        self._quebrar_linha()
        cabecalho = f"[{self._horario()}] " if self.ck_horario.isChecked() else ""
        self._inserir(f"{cabecalho}{prefixo} {len(dados)} B\n", COR_INFO)
        self._inserir(formatar_hex(dados) + "\n", cor)

    def mostrar_recebido(self, dados):
        if self.rx_caso is not None:
            self.rx_caso += dados
        if self.combo_modo.currentText() == "Hex":
            self._escrever_hex(dados, COR_RX, "RX")
        else:
            self._escrever_texto(self.decodificador.decode(dados), COR_RX, "RX")
        self._interpretar_recebido(dados)

    def _interpretar_recebido(self, dados):
        """Mostra cada quadro completo vindo da jiga ja decodificado (o quadro pode vir picado)."""
        for resultado, quadro in self.receptor_rx.processar(dados):
            if not self.ck_interpretar.isChecked():
                continue
            if resultado != QUADRO_OK:
                texto = f"quadro inválido ({resultado})"
            else:
                try:
                    texto = interpretar_resposta(quadro).descrever()
                except ValueError as e:
                    texto = f"resposta inválida: {e}"
            self._quebrar_linha()
            self._inserir(f"  => {texto}\n", COR_RX)

    def _mudou_modo(self, _):
        self.decodificador.reset()
        self._quebrar_linha()

    # --- envio ------------------------------------------------------------

    def _transmitir(self, dados):
        """Escreve na porta; retorna False (e avisa na tela) se nao deu."""
        if self.leitor is None:
            self._escrever_info("Não conectado; nada enviado")
            return False
        try:
            self.leitor.escrever(dados)
        except (serial.SerialException, OSError) as e:
            self._escrever_info(f"Erro ao enviar: {e}")
            return False
        return True

    def _enviar_bytes(self, dados, rotulo="TX"):
        """Envia bytes binarios e mostra em hex (quadros do protocolo)."""
        if self._transmitir(dados) and self.ck_eco.isChecked():
            self._escrever_hex(dados, COR_TX, rotulo)

    def enviar_quadro(self):
        try:
            cmd = interpretar_hex(self.campo_cmd.text())
            if len(cmd) != 1:
                raise ValueError("CMD precisa ser 1 byte, ex.: 01")
            quadro = montar_quadro(self.spin_seq.value(), cmd[0], interpretar_hex(self.campo_payload.text()))
        except ValueError as e:
            self._escrever_info(f"Quadro inválido: {e}")
            return
        self._enviar_com_seq(quadro, f"CMD=0x{cmd[0]:02X}")

    def _enviar_com_seq(self, quadro, rotulo):
        """Envia um quadro montado com o SEQ do campo e avanca o SEQ (se 'auto')."""
        if self.leitor is None:
            self._escrever_info("Não conectado; nada enviado")
            return
        self._enviar_bytes(quadro, f"TX quadro SEQ={self.spin_seq.value()} {rotulo}")
        if self.ck_seq_auto.isChecked():
            self.spin_seq.setValue((self.spin_seq.value() + 1) % 256)

    def _pwm_escolhido(self):
        """(canal, frequencia em Hz, razao em centesimos de %) dos campos de PWM."""
        return (self.combo_pwm.currentData(), self.spin_freq.value(),
                porcento_para_razao(self.spin_razao.value()))

    def _mudou_pwm(self, *_):
        self.lb_pwm.setText(descrever_pwm(*self._pwm_escolhido()))

    def enviar_pwm(self):
        canal, frequencia, razao = self._pwm_escolhido()
        self._enviar_com_seq(montar_pwm_set(self.spin_seq.value(), canal, frequencia, razao),
                             f"PWM_SET {descrever_pwm(canal, frequencia, razao)}")

    def parar_pwm(self):
        canal = self.combo_pwm.currentData()
        self._enviar_com_seq(montar_pwm_stop(self.spin_seq.value(), canal), f"PWM_STOP PWM{canal}")

    def configurar_i2c(self):
        try:
            endereco = int(self.campo_i2c_endereco.text(), 16)
            quadro = montar_i2c_config(self.spin_seq.value(), self.combo_i2c_papel.currentData(), endereco,
                                       self.combo_i2c_velocidade.currentData())
        except ValueError as e:
            self._escrever_info(f"I2C_CONFIG inválido: {e}")
            return
        self._enviar_com_seq(quadro, f"I2C_CONFIG {self.combo_i2c_papel.currentText()} 0x{endereco:02X} "
                                     f"{self.combo_i2c_velocidade.currentText()}")

    def carregar_i2c(self):
        try:
            offset = int(self.campo_i2c_offset.text(), 16)
            dados = interpretar_hex(self.campo_i2c_dados.text())
            quadro = montar_i2c_load(self.spin_seq.value(), offset, dados)
        except ValueError as e:
            self._escrever_info(f"I2C_SLAVE_LOAD inválido: {e}")
            return
        self._enviar_com_seq(quadro, f"I2C_SLAVE_LOAD {len(dados)} B em 0x{offset:02X}")

    def ler_i2c(self):
        try:
            offset = int(self.campo_i2c_offset.text(), 16)
            quadro = montar_i2c_dump(self.spin_seq.value(), offset, self.spin_i2c_qtd.value())
        except ValueError as e:
            self._escrever_info(f"I2C_SLAVE_DUMP inválido: {e}")
            return
        self._enviar_com_seq(quadro, f"I2C_SLAVE_DUMP {self.spin_i2c_qtd.value()} B de 0x{offset:02X}")

    def _mudou_caso(self, *_):
        caso = self.combo_caso.currentData()
        self.lb_caso.setText(f"{caso.descricao}  →  USB: {caso.descrever_esperado()}")
        self.lb_caso.setToolTip(f"DebugLog: {descrever_log(caso.esperado)}")

    def executar_casos(self, casos):
        """Enfileira os passos dos casos; as esperas usam QTimer para nao travar a janela.

        Para cada caso: comeca a capturar o DebugLog e o RX do USB, envia os passos, espera a
        CommTask logar e responder (ESPERA_LOG) e compara os dois com o esperado.
        """
        if self.executando:
            return
        if self.leitor is None:
            self._escrever_info("Não conectado; nada enviado")
            return
        if self.leitor_log is None:
            self._escrever_info("DebugLog não conectado: só a resposta do USB será avaliada")
        for caso in casos:
            self.fila_teste.append(("caso", caso))
            self.fila_teste += list(caso.passos)
            self.fila_teste.append(("esperar", ESPERA_LOG))
            self.fila_teste.append(("avaliar", caso))
            self.fila_teste.append(("esperar", PAUSA_ENTRE_CASOS))
        self.tabela.setRowCount(0)
        self.aprovados = self.avaliados = 0
        self.lb_resumo.setText(f"Executando {len(casos)} caso(s)...")
        self._marcar_execucao(True)
        self._proximo_passo()

    def _marcar_execucao(self, executando):
        self.executando = executando
        self.bt_caso.setEnabled(not executando)
        self.bt_todos.setEnabled(not executando)
        self.bt_conectar.setEnabled(not executando)
        self.bt_log.setEnabled(not executando)

    def _proximo_passo(self):
        while self.fila_teste:
            tipo, valor = self.fila_teste.pop(0)
            if tipo == "esperar":
                QTimer.singleShot(int(valor * 1000), self._proximo_passo)
                return
            if tipo == "caso":
                self._escrever_info(f"Caso '{valor.nome}' | esperado: log {descrever_log(valor.esperado)}"
                                    f" | USB {valor.descrever_esperado()}")
                self.linhas_caso = []
                self.rx_caso = bytearray()
            elif tipo == "avaliar":
                self._avaliar(valor)
            else:
                self._enviar_bytes(valor)
        self.linhas_caso = None
        self.rx_caso = None
        self._marcar_execucao(False)
        self._mostrar_resumo()

    def _avaliar(self, caso):
        """Compara o DebugLog e a resposta do USB do caso com o esperado e poe na tabela."""
        linhas = self.linhas_caso or []
        resposta = bytes(self.rx_caso or b"")
        self.linhas_caso = None
        self.rx_caso = None
        avaliacao = avaliar_caso(caso, linhas, resposta)
        # Sem o DebugLog conectado, so a resposta do USB entra no veredito
        com_log = self.leitor_log is not None
        aprovado = avaliacao.aprovado if com_log else avaliacao.resposta_ok
        self.avaliados += 1
        self.aprovados += aprovado

        esperado = (f"log: {descrever_log(caso.esperado)}\n"
                    f"USB: {caso.descrever_esperado()}")
        log_obtido = descrever_log(avaliacao.log_obtido) if com_log else "(DebugLog não conectado)"
        obtido = f"log: {log_obtido}\nUSB: {descrever_resposta(resposta, MAXIMO_RESPOSTAS)}"
        if avaliacao.motivo:
            obtido += f"\nmotivo: {avaliacao.motivo}"
        detalhe = ("Linhas do DebugLog durante o caso:\n"
                   + ("\n".join(linhas) or "(nenhuma)")
                   + f"\n\nBytes recebidos pelo USB:\n{resposta.hex(' ').upper() or '(nenhum)'}")
        self._adicionar_linha(caso.nome, esperado, obtido,
                              "PASS" if aprovado else "FALHA", COR_PASS if aprovado else COR_FALHA, detalhe)
        self._escrever_info(f"[{'PASS' if aprovado else 'FALHA'}] {caso.nome}")

    def _adicionar_linha(self, nome, esperado, obtido, resultado, cor, detalhe):
        linha = self.tabela.rowCount()
        self.tabela.insertRow(linha)
        for coluna, texto in enumerate((nome, esperado, obtido, resultado)):
            item = QTableWidgetItem(texto)
            item.setToolTip(detalhe)
            if cor is not None:
                item.setBackground(cor)
            self.tabela.setItem(linha, coluna, item)
        self.tabela.resizeRowToContents(linha)
        self.tabela.scrollToBottom()

    def _mostrar_resumo(self):
        if not self.avaliados:
            self.lb_resumo.setText("Nenhum caso avaliado.")
            self.lb_resumo.setStyleSheet("color: #8a8a8a")
            return
        tudo_ok = self.aprovados == self.avaliados
        self.lb_resumo.setText(f"{self.aprovados}/{self.avaliados} casos passaram"
                               "  (passe o mouse numa linha para ver o log e os bytes do caso)")
        self.lb_resumo.setStyleSheet(f"color: {'#1a7f37' if tudo_ok else '#c62828'}; font-weight: bold")
        self._escrever_info(f"{self.aprovados}/{self.avaliados} casos passaram")

    def enviar(self):
        texto = self.campo.text()
        if self.leitor is None:
            self._escrever_info("Não conectado; nada enviado")
            return
        try:
            if self.ck_hex.isChecked():
                dados = interpretar_hex(texto)
            else:
                dados = texto.encode("utf-8") + FINAIS_DE_LINHA[self.combo_fim.currentText()]
        except ValueError:
            self._escrever_info("Hexadecimal inválido; ex.: 7E 01 02 0A")
            return
        if not dados or not self._transmitir(dados):
            return
        self.campo.guardar(texto)
        self.campo.clear()
        if self.ck_eco.isChecked():
            if self.combo_modo.currentText() == "Hex":
                self._escrever_hex(dados, COR_TX, "TX")
            else:
                self._quebrar_linha()
                self._escrever_texto(dados.decode("utf-8", errors="replace"), COR_TX, "TX")
                self._quebrar_linha()

    # --- outros -----------------------------------------------------------

    def limpar(self):
        self.tela.clear()
        self.inicio_linha = True

    def salvar(self):
        nome, _ = QFileDialog.getSaveFileName(
            self, "Salvar registro", f"terminal_{datetime.now():%Y%m%d_%H%M%S}.txt",
            "Texto (*.txt);;Todos (*)")
        if nome:
            with open(nome, "w", encoding="utf-8") as arquivo:
                arquivo.write(self.tela.toPlainText())

    def closeEvent(self, evento):
        self._salvar_config()
        for leitor in (self.leitor, self.leitor_log):
            if leitor:
                leitor.parar()
        super().closeEvent(evento)


def main():
    app = QApplication(sys.argv)
    janela = JanelaTerminal()
    janela.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
