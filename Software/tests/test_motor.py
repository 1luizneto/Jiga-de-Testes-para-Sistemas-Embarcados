"""Motor de testes contra a jiga simulada (CT07): vereditos passou, falhou e inconclusivo, e o relatorio."""

import csv
import json
from pathlib import Path

import pytest

from bateria import CarregadorDeBaterias
from cliente import ClienteJiga, TransporteSimulado
from motor import MotorDeTestes
from protocolo import JigaSimulada
from relatorio import FALHOU, INCONCLUSIVO, PASSOU
from versao import __version__

BATERIAS = Path(__file__).resolve().parent.parent / "baterias"


def _motor(jiga=None, **kwargs):
    transporte = TransporteSimulado(jiga if jiga is not None else JigaSimulada(), **kwargs)
    cliente = ClienteJiga(transporte)
    cliente.abrir()
    return MotorDeTestes(cliente), transporte


def _bateria(texto):
    return CarregadorDeBaterias().carregar_texto(texto)


DOIS_CASOS = """
bateria: dois
casos:
  - nome: passa
    estimulos: [{dac: {canal: 1, tensao: 1.0}}]
    verificar: [{adc: {canal: 0, esperado: 1.0}}]
  - nome: falha
    estimulos: [{gpio_out: {pino: 2, nivel: 1}}]
    verificar: [{gpio_in: {pino: 6, esperado: 0}}, {gpio_in: {pino: 5, esperado: 0}}]
"""


# 1. A bateria de loopback passa inteira na jiga simulada com loopback (os jumpers do CT10/CT11/CT13)
def test_loopback_passa():
    motor, _ = _motor(JigaSimulada(loopback=True))
    relatorio = motor.executar(CarregadorDeBaterias().carregar(BATERIAS / "loopback.yaml"))
    assert [c.veredito for c in relatorio.casos] == [PASSOU] * 6, relatorio.texto()
    assert relatorio.veredito == PASSOU and relatorio.firmware == __version__ and not relatorio.interrompida


# 2. A bateria sem fios passa na jiga simulada sem loopback
def test_sem_fios_passa():
    motor, _ = _motor()
    relatorio = motor.executar(CarregadorDeBaterias().carregar(BATERIAS / "sem_fios.yaml"))
    assert relatorio.veredito == PASSOU, relatorio.texto()


# 3. Medicao fora do esperado: o caso falha, a verificacao errada fica marcada e a bateria continua
def test_falhou_e_continua():
    motor, _ = _motor(JigaSimulada(loopback=True))
    relatorio = motor.executar(_bateria(DOIS_CASOS))
    passa, falha = relatorio.casos
    assert passa.veredito == PASSOU and passa.verificacoes[0].obtido.startswith("1.000 V")
    assert falha.veredito == FALHOU and [v.passou for v in falha.verificacoes] == [False, True]
    assert falha.verificacoes[0].obtido == "1" and relatorio.veredito == FALHOU


# 4. Rotina em ERRO (o DUT nao respondeu no I2C): falhou com o codigo do erro, e a bateria continua
def test_erro_do_dut_e_falha():
    motor, _ = _motor()
    relatorio = motor.executar(_bateria("""
bateria: i2c
casos:
  - nome: sem DUT no barramento
    estimulos: [{i2c_config: {papel: mestre}}]
    verificar: [{i2c_xfer: {endereco: 0x68, escrita: "0F", esperado: "33"}}]
  - nome: depois
    estimulos: [{espera_ms: 1}]
"""))
    erro, depois = relatorio.casos
    assert erro.veredito == FALHOU and erro.estado == "ERRO" and "ERRO_PERIFERICO" in erro.motivo
    assert erro.verificacoes[0].obtido == "sem medicao"
    assert depois.veredito == PASSOU


# 5. Tempo maximo esgotado: inconclusivo e a bateria para (o caso seguinte nao roda)
def test_tempo_esgotado_e_inconclusivo():
    motor, transporte = _motor()
    relatorio = motor.executar(_bateria("""
bateria: t
casos:
  - nome: espera maior que o tempo
    tempo_max_ms: 10
    estimulos: [{espera_ms: 50}]
  - nome: nao roda
    estimulos: [{espera_ms: 1}]
"""))
    [caso] = relatorio.casos
    assert caso.veredito == INCONCLUSIVO and caso.estado == "TEMPO_ESGOTADO"
    assert relatorio.interrompida and relatorio.veredito == INCONCLUSIVO
    assert transporte.jiga.dac == {1: 0, 2: 0}             # RESET_ALL no fim


# 6. Jiga para de responder no meio: inconclusivo depois das retransmissoes, sem travar
def test_comunicacao_perdida_e_inconclusivo():
    motor, transporte = _motor(JigaSimulada(loopback=True), descartar=1000, descartar_a_partir=6)
    relatorio = motor.executar(_bateria(DOIS_CASOS))
    assert relatorio.interrompida and relatorio.casos[-1].veredito == INCONCLUSIVO
    assert "sem resposta" in relatorio.motivo
    assert relatorio.retransmissoes >= 2


# 7. Interromper pela interface: ABORT na jiga, caso inconclusivo e os seguintes nao rodam
def test_interromper_envia_abort():
    motor, transporte = _motor()
    bateria = _bateria("""
bateria: longa
casos:
  - nome: rapido
    estimulos: [{espera_ms: 1}]
  - nome: longo
    estimulos: [{espera_ms: 5000}]
  - nome: nao roda
    estimulos: [{espera_ms: 1}]
""")
    relatorio = motor.executar(bateria, ao_iniciar_caso=lambda caso, n, total: n == 2 and motor.interromper())
    assert [c.veredito for c in relatorio.casos] == [PASSOU, INCONCLUSIVO]
    assert relatorio.casos[1].estado == "ABORTADA" and "usuario" in relatorio.casos[1].motivo
    assert transporte.jiga.relogio_us < 1_000_000


# 8. Respostas perdidas de vez em quando nao mudam os vereditos (retransmissao) e entram no relatorio
def test_retransmissoes_no_relatorio():
    motor, _ = _motor(JigaSimulada(loopback=True), descartar=1, descartar_a_partir=3)
    relatorio = motor.executar(_bateria(DOIS_CASOS))
    assert [c.veredito for c in relatorio.casos] == [PASSOU, FALHOU] and relatorio.retransmissoes == 1


# 9. ESPERA_GPIO que nao ocorre: a verificacao falha com o tempo esgotado
def test_reacao_que_nao_ocorre():
    motor, _ = _motor()
    relatorio = motor.executar(_bateria("""
bateria: r
casos:
  - nome: sem reacao
    verificar: [{reacao_gpio: {pino: 5, nivel: 1, max_ms: 20}}]
"""))
    [caso] = relatorio.casos
    assert caso.veredito == FALHOU and caso.verificacoes[0].obtido == "nao mudou em 20.000 ms"


# 10. Relatorio em CSV (uma linha por verificacao) e JSON (com os dados brutos)
def test_relatorio_csv_e_json(tmp_path):
    motor, _ = _motor(JigaSimulada(loopback=True))
    relatorio = motor.executar(_bateria(DOIS_CASOS))
    relatorio.salvar_csv(tmp_path / "r.csv")
    relatorio.salvar_json(tmp_path / "r.json")
    with open(tmp_path / "r.csv", encoding="utf-8-sig", newline="") as f:
        linhas = list(csv.DictReader(f, delimiter=";"))
    assert [(l["nome"], l["resultado"]) for l in linhas] == [("passa", PASSOU), ("falha", FALHOU), ("falha", PASSOU)]
    assert linhas[0]["estimulos"] == "DAC1 = 1 V (codigo 1241)"
    dados = json.loads((tmp_path / "r.json").read_text(encoding="utf-8"))
    assert dados["veredito"] == FALHOU and dados["contagem"] == {PASSOU: 1, FALHOU: 1, INCONCLUSIVO: 0}
    assert dados["casos"][0]["resultados_hex"].startswith("01 00 00 00 00 03 00")
    assert "Resultado: FALHOU (1 passou, 1 falhou" in relatorio.texto()


# 11. Mesma bateria executada varias vezes: mesmos estimulos e mesmos vereditos (base do CT21)
def test_execucoes_repetidas_iguais():
    motor, transporte = _motor(JigaSimulada(loopback=True))
    bateria = CarregadorDeBaterias().carregar(BATERIAS / "loopback.yaml")
    relatorios = [motor.executar(bateria) for _ in range(3)]
    assert len({tuple(c.veredito for c in r.casos) for r in relatorios}) == 1
    assert len({tuple(c.resultados_hex for c in r.casos) for r in relatorios}) == 1


# 12. Versao de protocolo diferente: a bateria nem comeca
def test_protocolo_incompativel(monkeypatch):
    motor, _ = _motor()
    monkeypatch.setattr(motor.cliente, "ping", lambda: (2, "9.9.9"))
    relatorio = motor.executar(_bateria(DOIS_CASOS))
    assert relatorio.interrompida and not relatorio.casos and "protocolo 2" in relatorio.motivo
