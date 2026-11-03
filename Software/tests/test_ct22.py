"""Bateria do CT22 (ventilador com histerese) contra DUTs simulados: o comportamento conhecido passa inteiro e um
firmware sem histerese falha exatamente nos casos da histerese (a bateria distingue os dois comportamentos)."""

from pathlib import Path

from bateria import CarregadorDeBaterias
from cliente import ClienteJiga, TransporteSimulado
from motor import MotorDeTestes
from protocolo import JigaSimulada, codigo_para_tensao
from relatorio import FALHOU, PASSOU

BATERIA = Path(__file__).resolve().parent.parent / "baterias" / "ct22_ventilador.yaml"


class JigaComVentilador(JigaSimulada):
    """DAC1 -> sensor do DUT; saida do DUT -> GPIO5. 'liga'/'desliga' em mV (iguais = sem histerese)."""

    def __init__(self, liga=1800, desliga=1500):
        super().__init__()
        self.liga, self.desliga, self.ligado = liga, desliga, False

    def ler_entradas(self):
        tensao = codigo_para_tensao(self.dac[1]) * 1000
        if tensao > self.liga:
            self.ligado = True
        elif tensao < self.desliga:
            self.ligado = False
        return int(self.ligado)

    def _executar(self, quadro):
        resultado = super()._executar(quadro)
        self.ler_entradas()            # o DUT reage a cada mudanca do DAC (le o sensor o tempo todo)
        return resultado


def _executar(jiga):
    cliente = ClienteJiga(TransporteSimulado(jiga))
    cliente.abrir()
    return MotorDeTestes(cliente).executar(CarregadorDeBaterias().carregar(BATERIA))


# 1. O comportamento conhecido passa nos 5 casos
def test_ct22_comportamento_conhecido_passa():
    relatorio = _executar(JigaComVentilador())
    assert [c.veredito for c in relatorio.casos] == [PASSOU] * 5, relatorio.texto()


# 2. Firmware sem histerese (limiar unico de 1,65 V): falham os dois casos de histerese, os outros passam
def test_ct22_sem_histerese_falha_na_histerese():
    relatorio = _executar(JigaComVentilador(liga=1650, desliga=1650))
    assert [c.veredito for c in relatorio.casos] == [PASSOU, PASSOU, FALHOU, PASSOU, FALHOU]


# 3. DUT parado (saida sempre em 0): falham os casos que esperam a saida ligada
def test_ct22_dut_parado():
    relatorio = _executar(JigaSimulada())
    assert [c.veredito for c in relatorio.casos] == [PASSOU, FALHOU, FALHOU, PASSOU, PASSOU]
