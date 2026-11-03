"""Script de desempenho (CT23 e CT24) com a jiga simulada: estatisticas, erros nao recuperados e retransmissoes."""

import pytest

from cliente import ClienteJiga, TransporteSimulado
from desempenho import Medicao, main, medir_escrita_leitura, medir_ping
from protocolo import JigaSimulada


def _cliente(**kwargs):
    cliente = ClienteJiga(TransporteSimulado(JigaSimulada(loopback=True), **kwargs))
    cliente.abrir()
    return cliente


# 1. CT23: um tempo por PING, sem erros
def test_ping():
    medicao = medir_ping(_cliente(), 50)
    assert len(medicao.tempos_ms) == 50 and medicao.comandos == 50 and not medicao.erros


# 2. CT24: alterna DAC_SET e ADC_READ (com loopback, a leitura segue a escrita)
def test_escrita_leitura_alterna():
    cliente = _cliente()
    medicao = medir_escrita_leitura(cliente, 20)
    assert medicao.comandos == 20 and not medicao.erros
    assert cliente.transporte.jiga.dac[1] == 124 + (18 * 37) % (3971 - 124)


# 3. Respostas perdidas: recuperadas por retransmissao (contadas), ou erro nao recuperado depois das 3 tentativas
def test_retransmissao_e_erro_nao_recuperado():
    medicao = medir_ping(_cliente(descartar=2, descartar_a_partir=5), 10)
    assert medicao.retransmissoes == 2 and not medicao.erros
    medicao = medir_ping(_cliente(descartar=3, descartar_a_partir=5), 10)
    assert len(medicao.erros) == 1 and len(medicao.tempos_ms) == 9 and medicao.comandos == 10


# 4. Percentil pelo posto mais proximo e o resumo
@pytest.mark.parametrize("p, esperado", [(50, 50), (95, 95), (99, 99), (100, 100)])
def test_percentil(p, esperado):
    medicao = Medicao("x", tempos_ms=[float(v) for v in range(100, 0, -1)])
    assert medicao.percentil(p) == esperado


def test_resumo():
    medicao = Medicao("x", tempos_ms=[1.0, 2.0, 3.0], retransmissoes=1, comandos=4)
    r = medicao.resumo()
    assert (r["media_ms"], r["mediana_ms"], r["max_ms"], r["taxa_retransmissao"]) == (2.0, 2.0, 3.0, 0.25)


# 5. Linha de comando com a jiga simulada: os dois casos passam e os CSV sao gravados
def test_main_simulada(tmp_path):
    assert main(["--simulada", "--n", "40", "--saida", str(tmp_path)]) == 0
    assert (tmp_path / "ct23_ping.csv").read_text(encoding="utf-8").count("\n") == 41
    assert (tmp_path / "ct24_escrita_leitura.csv").exists()
