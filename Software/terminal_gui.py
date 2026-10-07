"""
Terminal serial grafico da jiga de testes (no estilo do Termite).

Mostra o que chega pela porta serial do STM32 (USB-CDC ou a UART do ST-Link)
e permite enviar texto ou bytes em hexadecimal.

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
    QApplication, QCheckBox, QComboBox, QFileDialog, QHBoxLayout, QLabel, QLineEdit,
    QMainWindow, QMessageBox, QPlainTextEdit, QPushButton, QVBoxLayout, QWidget,
)

from terminal_usb import SILENCIO_FIM_RAJADA, detectar_porta, formatar_hex, interpretar_hex

TAXAS_BAUD = ["9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"]
FINAIS_DE_LINHA = {"Nenhum": b"", "LF (\\n)": b"\n", "CR (\\r)": b"\r", "CR+LF": b"\r\n"}
MAX_LINHAS = 20000

COR_RX = QColor("#1a7f37")
COR_TX = QColor("#0b4fd6")
COR_INFO = QColor("#8a8a8a")
COR_ERRO = QColor("#c62828")


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
        while dados and self.rodando:
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
        self.setWindowTitle("Terminal da Jiga")
        self.resize(900, 600)
        self.leitor = None
        self.inicio_linha = True
        self.decodificador = codecs.getincrementaldecoder("utf-8")(errors="replace")
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
        bt_limpar = QPushButton("Limpar", clicked=self.limpar)
        bt_salvar = QPushButton("Salvar...", clicked=self.salvar)
        linha2.addWidget(QLabel("Exibição:"))
        linha2.addWidget(self.combo_modo)
        linha2.addWidget(self.ck_horario)
        linha2.addWidget(self.ck_eco)
        linha2.addWidget(self.ck_rolagem)
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

        self.setCentralWidget(central)
        QShortcut(QKeySequence("Ctrl+L"), self, activated=self.limpar)

    def _carregar_config(self):
        self.combo_baud.setCurrentText(self.config.value("baud", "115200"))
        self.combo_modo.setCurrentText(self.config.value("modo", "Texto"))
        self.combo_fim.setCurrentText(self.config.value("fim", "LF (\\n)"))
        self.ck_hex.setChecked(self.config.value("enviar_hex", False, type=bool))
        self.ck_horario.setChecked(self.config.value("horario", True, type=bool))

    def _salvar_config(self):
        self.config.setValue("porta", self.porta_selecionada() or "")
        self.config.setValue("baud", self.combo_baud.currentText())
        self.config.setValue("modo", self.combo_modo.currentText())
        self.config.setValue("fim", self.combo_fim.currentText())
        self.config.setValue("enviar_hex", self.ck_hex.isChecked())
        self.config.setValue("horario", self.ck_horario.isChecked())

    # --- portas -----------------------------------------------------------

    def porta_selecionada(self):
        return self.combo_porta.currentData()

    def atualizar_portas(self):
        anterior = self.porta_selecionada() or self.config.value("porta", "")
        portas = list_ports.comports()
        self.combo_porta.blockSignals(True)
        self.combo_porta.clear()
        for p in portas:
            self.combo_porta.addItem(f"{p.device} - {p.description}", p.device)
        self.combo_porta.blockSignals(False)
        # Prioridade: a porta que estava escolhida; senao, o USB-CDC do STM32
        for alvo in (anterior, detectar_porta()):
            indice = self.combo_porta.findData(alvo) if alvo else -1
            if indice >= 0:
                self.combo_porta.setCurrentIndex(indice)
                break

    def _atualizar_portas_auto(self):
        if self.leitor is None and not self.combo_porta.view().isVisible():
            atuais = [self.combo_porta.itemData(i) for i in range(self.combo_porta.count())]
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
        if self.combo_modo.currentText() == "Hex":
            self._escrever_hex(dados, COR_RX, "RX")
        else:
            self._escrever_texto(self.decodificador.decode(dados), COR_RX, "RX")

    def _mudou_modo(self, _):
        self.decodificador.reset()
        self._quebrar_linha()

    # --- envio ------------------------------------------------------------

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
        if not dados:
            return
        try:
            self.leitor.escrever(dados)
        except (serial.SerialException, OSError) as e:
            self._escrever_info(f"Erro ao enviar: {e}")
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
        if self.leitor:
            self.leitor.parar()
        super().closeEvent(evento)


def main():
    app = QApplication(sys.argv)
    janela = JanelaTerminal()
    janela.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
