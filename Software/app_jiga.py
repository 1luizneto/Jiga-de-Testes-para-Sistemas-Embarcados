"""
Aplicacao desktop da jiga (secao 3.6 da monografia): abre uma bateria YAML, executa na jiga e mostra os vereditos.

A JanelaPrincipal so cuida da interface: a execucao e do MotorDeTestes (motor.py), numa thread propria, e a
mesma bateria roda tambem pela linha de comando (executar_bateria.py). Cada execucao gera o relatorio CSV e JSON
na pasta 'relatorios' (ou onde o usuario escolher).

Uso:
    python app_jiga.py [bateria.yaml]
"""

import sys
from pathlib import Path

from PySide6.QtCore import QSettings, QThread, Signal
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (
    QAbstractItemView, QApplication, QComboBox, QFileDialog, QHBoxLayout, QHeaderView, QLabel, QMainWindow,
    QMessageBox, QPlainTextEdit, QPushButton, QSpinBox, QSplitter, QTableWidget, QTableWidgetItem, QVBoxLayout,
    QWidget,
)
from PySide6.QtCore import Qt
from serial.tools import list_ports

from bateria import CarregadorDeBaterias, ErroBateria
from cliente import STM32_CDC_PID, STM32_VID, ClienteJiga, ErroComunicacao, TransporteSerial, TransporteSimulado
from executar_bateria import nome_relatorio
from motor import MotorDeTestes
from protocolo import JigaSimulada
from relatorio import FALHOU, INCONCLUSIVO, PASSOU
from versao import __version__

SIMULADA = "Jiga simulada (loopback)"
CORES = {PASSOU: QColor("#1b7f3b"), FALHOU: QColor("#b3261e"), INCONCLUSIVO: QColor("#9a6700")}
COLUNAS = ["Caso", "Veredito", "Verificação", "Esperado", "Obtido", "Instante (µs)"]


class Execucao(QThread):
    """Roda o MotorDeTestes fora da thread da interface; os sinais levam o progresso para a janela."""

    caso_iniciado = Signal(int, int, str)
    caso_terminado = Signal(object)
    terminou = Signal(object, str)           # (lista de relatorios, erro)

    def __init__(self, bateria, porta, repeticoes, pasta):
        super().__init__()
        self.bateria, self.porta, self.repeticoes, self.pasta = bateria, porta, repeticoes, pasta
        self.motor = None
        self._interromper = False

    def interromper(self):
        self._interromper = True
        if self.motor is not None:
            self.motor.interromper()

    def run(self):
        if self.porta == SIMULADA:
            transporte = TransporteSimulado(JigaSimulada(loopback=True))
        else:
            transporte = TransporteSerial(self.porta)
        cliente = ClienteJiga(transporte)
        relatorios = []
        try:
            cliente.abrir()
        except ErroComunicacao as e:
            self.terminou.emit(relatorios, str(e))
            return
        self.motor = MotorDeTestes(cliente)
        try:
            for n in range(1, self.repeticoes + 1):
                if self._interromper:
                    break
                relatorio = self.motor.executar(
                    self.bateria,
                    ao_iniciar_caso=lambda caso, i, total: self.caso_iniciado.emit(i, total, caso.nome),
                    ao_terminar_caso=self.caso_terminado.emit)
                relatorio.porta = self.porta
                relatorios.append(relatorio)
                if self.pasta:
                    Path(self.pasta).mkdir(parents=True, exist_ok=True)
                    base = nome_relatorio(self.bateria, self.pasta, n if self.repeticoes > 1 else None)
                    relatorio.salvar_csv(base.with_suffix(".csv"))
                    relatorio.salvar_json(base.with_suffix(".json"))
                if relatorio.interrompida:
                    break
        finally:
            cliente.fechar()
        self.terminou.emit(relatorios, "")


class JanelaPrincipal(QMainWindow):
    def __init__(self, arquivo=None):
        super().__init__()
        self.setWindowTitle(f"Jiga de testes {__version__}")
        self.resize(1100, 700)
        self.config = QSettings("TCC-Jiga", "app_jiga")
        self.bateria = None
        self.execucao = None
        self._montar()
        self.atualizar_portas()
        self.pasta = self.config.value("pasta_relatorios", str(Path(__file__).resolve().parent / "relatorios"))
        self.rotulo_pasta.setText(f"Relatórios em: {self.pasta}")
        if arquivo:
            self.abrir_bateria(arquivo)

    def _montar(self):
        central = QWidget()
        layout = QVBoxLayout(central)

        linha = QHBoxLayout()
        self.botao_abrir = QPushButton("Abrir bateria...")
        self.botao_abrir.clicked.connect(lambda: self.abrir_bateria())
        self.rotulo_bateria = QLabel("Nenhuma bateria aberta")
        linha.addWidget(self.botao_abrir)
        linha.addWidget(self.rotulo_bateria, 1)
        layout.addLayout(linha)

        linha = QHBoxLayout()
        linha.addWidget(QLabel("Jiga:"))
        self.combo_porta = QComboBox()
        self.combo_porta.setMinimumWidth(260)
        botao_portas = QPushButton("Atualizar")
        botao_portas.clicked.connect(self.atualizar_portas)
        linha.addWidget(self.combo_porta)
        linha.addWidget(botao_portas)
        linha.addSpacing(20)
        linha.addWidget(QLabel("Repetições:"))
        self.repeticoes = QSpinBox()
        self.repeticoes.setRange(1, 100)
        linha.addWidget(self.repeticoes)
        linha.addSpacing(20)
        self.botao_executar = QPushButton("Executar")
        self.botao_executar.setEnabled(False)
        self.botao_executar.clicked.connect(self.executar)
        self.botao_interromper = QPushButton("Interromper")
        self.botao_interromper.setEnabled(False)
        self.botao_interromper.clicked.connect(self.interromper)
        botao_pasta = QPushButton("Pasta dos relatórios...")
        botao_pasta.clicked.connect(self.escolher_pasta)
        linha.addWidget(self.botao_executar)
        linha.addWidget(self.botao_interromper)
        linha.addStretch(1)
        linha.addWidget(botao_pasta)
        layout.addLayout(linha)

        self.tabela = QTableWidget(0, len(COLUNAS))
        self.tabela.setHorizontalHeaderLabels(COLUNAS)
        self.tabela.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.tabela.setSelectionBehavior(QAbstractItemView.SelectRows)
        cabecalho = self.tabela.horizontalHeader()
        cabecalho.setSectionResizeMode(QHeaderView.ResizeToContents)
        cabecalho.setSectionResizeMode(2, QHeaderView.Stretch)
        self.texto = QPlainTextEdit()
        self.texto.setReadOnly(True)
        divisor = QSplitter(Qt.Vertical)
        divisor.addWidget(self.tabela)
        divisor.addWidget(self.texto)
        divisor.setSizes([450, 200])
        layout.addWidget(divisor, 1)

        self.rotulo_resumo = QLabel("")
        self.rotulo_pasta = QLabel("")
        layout.addWidget(self.rotulo_resumo)
        layout.addWidget(self.rotulo_pasta)
        self.setCentralWidget(central)

    def atualizar_portas(self):
        atual = self.combo_porta.currentText() or self.config.value("porta", "")
        self.combo_porta.clear()
        jiga = None
        for p in list_ports.comports():
            self.combo_porta.addItem(f"{p.device} - {p.description}", p.device)
            if p.vid == STM32_VID and p.pid == STM32_CDC_PID:
                jiga = self.combo_porta.count() - 1
        self.combo_porta.addItem(SIMULADA, SIMULADA)
        indice = self.combo_porta.findText(atual)
        self.combo_porta.setCurrentIndex(indice if indice >= 0 else (jiga if jiga is not None else 0))

    def abrir_bateria(self, arquivo=None):
        if arquivo is None:
            inicio = self.config.value("ultima_pasta", str(Path(__file__).resolve().parent / "baterias"))
            arquivo, _ = QFileDialog.getOpenFileName(self, "Abrir bateria", inicio, "Baterias (*.yaml *.yml)")
            if not arquivo:
                return
        try:
            self.bateria = CarregadorDeBaterias().carregar(arquivo)
        except (OSError, ErroBateria) as e:
            self.bateria = None
            self.botao_executar.setEnabled(False)
            self.rotulo_bateria.setText(f"{Path(arquivo).name}: inválida")
            self.texto.setPlainText(f"Bateria inválida ({arquivo}):\n{e}")
            return
        self.config.setValue("ultima_pasta", str(Path(arquivo).parent))
        dut = f", DUT {self.bateria.dut}" if self.bateria.dut else ""
        self.rotulo_bateria.setText(f"{Path(arquivo).name}: '{self.bateria.nome}'{dut}, "
                                    f"{len(self.bateria.casos)} caso(s)")
        linhas = [f"Bateria '{self.bateria.nome}' validada e compilada:"]
        for caso in self.bateria.casos:
            linhas.append(f"  {caso.identificador}. {caso.nome} ({len(caso.passos)} passos, "
                          f"{len(caso.payload)} B, tempo máximo {caso.tempo_max_ms} ms)")
        self.texto.setPlainText("\n".join(linhas))
        self.tabela.setRowCount(0)
        self.botao_executar.setEnabled(True)

    def escolher_pasta(self):
        pasta = QFileDialog.getExistingDirectory(self, "Pasta dos relatórios", self.pasta)
        if pasta:
            self.pasta = pasta
            self.config.setValue("pasta_relatorios", pasta)
            self.rotulo_pasta.setText(f"Relatórios em: {pasta}")

    def executar(self):
        if self.bateria is None or self.execucao is not None:
            return
        porta = self.combo_porta.currentData()
        self.config.setValue("porta", self.combo_porta.currentText())
        self.tabela.setRowCount(0)
        self.texto.clear()
        self.execucao = Execucao(self.bateria, porta, self.repeticoes.value(), self.pasta)
        self.execucao.caso_iniciado.connect(self._caso_iniciado)
        self.execucao.caso_terminado.connect(self.exibir_resultado)
        self.execucao.terminou.connect(self._terminou)
        self._travar(True)
        self.execucao.start()

    def interromper(self):
        if self.execucao is not None:
            self.execucao.interromper()
            self.rotulo_resumo.setText("Interrompendo (ABORT)...")

    def _travar(self, executando):
        self.botao_executar.setEnabled(not executando)
        self.botao_abrir.setEnabled(not executando)
        self.combo_porta.setEnabled(not executando)
        self.repeticoes.setEnabled(not executando)
        self.botao_interromper.setEnabled(executando)

    def _caso_iniciado(self, n, total, nome):
        self.rotulo_resumo.setText(f"Executando caso {n} de {total}: {nome}")

    def _celula(self, linha, coluna, texto, cor=None):
        item = QTableWidgetItem(str(texto))
        if cor is not None:
            item.setForeground(cor)
        self.tabela.setItem(linha, coluna, item)

    def exibir_resultado(self, resultado):
        """Uma linha por verificacao (ou uma so, para o caso sem verificacoes ou que nao chegou a medir)."""
        cor = CORES.get(resultado.veredito)
        linhas = resultado.verificacoes or [None]
        for v in linhas:
            linha = self.tabela.rowCount()
            self.tabela.insertRow(linha)
            self._celula(linha, 0, f"{resultado.identificador}. {resultado.nome}")
            self._celula(linha, 1, resultado.veredito.upper(), cor)
            if v is None:
                self._celula(linha, 2, resultado.motivo or resultado.estado)
                continue
            self._celula(linha, 2, v.descricao)
            self._celula(linha, 3, v.esperado)
            self._celula(linha, 4, v.obtido, CORES[PASSOU] if v.passou else CORES[FALHOU])
            self._celula(linha, 5, "" if v.instante_us is None else v.instante_us)
        self.tabela.scrollToBottom()

    def _terminou(self, relatorios, erro):
        self.execucao = None
        self._travar(False)
        if erro:
            self.rotulo_resumo.setText(f"Sem conexão com a jiga: {erro}")
            QMessageBox.warning(self, "Jiga", erro)
            return
        self.texto.setPlainText("\n\n".join(r.texto() for r in relatorios))
        if relatorios:
            vereditos = {tuple(c.veredito for c in r.casos) for r in relatorios}
            ultimo = relatorios[-1]
            texto = f"{ultimo.veredito.upper()}: {len(relatorios)} execução(ões)"
            if len(relatorios) > 1:
                texto += ", vereditos " + ("iguais" if len(vereditos) == 1 else "DIFERENTES") + " em todas"
            self.rotulo_resumo.setText(texto + f"; relatórios em {self.pasta}")

    def closeEvent(self, evento):
        if self.execucao is not None:
            self.execucao.interromper()
            self.execucao.wait(5000)
        super().closeEvent(evento)


def main():
    app = QApplication(sys.argv)
    janela = JanelaPrincipal(sys.argv[1] if len(sys.argv) > 1 else None)
    janela.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
