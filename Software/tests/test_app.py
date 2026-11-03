"""Janela do app (app_jiga.py) com a jiga simulada, sem mostrar na tela. Pulado sem o PySide6 (como no CI)."""

import os
import time
from pathlib import Path

import pytest

pytest.importorskip("PySide6")
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtWidgets import QApplication  # noqa: E402

import app_jiga  # noqa: E402

BATERIAS = Path(__file__).resolve().parent.parent / "baterias"


@pytest.fixture(scope="module")
def app():
    return QApplication.instance() or QApplication([])


def _esperar(app, janela, segundos=20):
    fim = time.time() + segundos
    while janela.execucao is not None and time.time() < fim:
        app.processEvents()
        time.sleep(0.01)
    app.processEvents()


# 1. Abre a bateria, executa duas vezes na jiga simulada e mostra uma linha por verificacao; grava os relatorios
def test_executa_na_jiga_simulada(app, tmp_path):
    janela = app_jiga.JanelaPrincipal(str(BATERIAS / "loopback.yaml"))
    janela.pasta = str(tmp_path)
    janela.combo_porta.setCurrentIndex(janela.combo_porta.findData(app_jiga.SIMULADA))
    janela.repeticoes.setValue(2)
    janela.executar()
    _esperar(app, janela)
    assert janela.tabela.rowCount() == 14
    assert janela.rotulo_resumo.text().startswith("PASSOU: 2 execução(ões), vereditos iguais")
    assert len(list(tmp_path.glob("*.csv"))) == 2 and len(list(tmp_path.glob("*.json"))) == 2
    janela.close()


# 2. Bateria invalida: os erros aparecem e o botao Executar fica desabilitado
def test_bateria_invalida(app, tmp_path):
    arquivo = tmp_path / "ruim.yaml"
    arquivo.write_text("bateria: ruim\ncasos:\n  - nome: x\n    estimulos: [{dac: {canal: 9, tensao: 1}}]\n",
                       encoding="utf-8")
    janela = app_jiga.JanelaPrincipal(str(arquivo))
    assert not janela.botao_executar.isEnabled()
    assert "canal do DAC 9" in janela.texto.toPlainText()
    janela.close()
