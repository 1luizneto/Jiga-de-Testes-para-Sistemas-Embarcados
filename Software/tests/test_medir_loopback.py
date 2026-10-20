"""Testes da medicao do loopback DAC -> ADC, com a jiga simulada no lugar da placa."""

import csv

import pytest

from medir_loopback import (
    COLUNAS, ErroJiga, Ponto, medir, pontos_varredura, resumo_canal, salvar_csv,
)
from protocolo import (
    DAC_FAIXA_UTIL, TOLERANCIA_ADC, JigaSimulada, ReceptorQuadros, interpretar_resposta, pontos_ct10,
)


def executor(jiga):
    """Faz a JigaSimulada responder como o ClienteSerial: um comando por vez, com SEQ proprio."""
    seq = 0

    def executar(montar, *argumentos):
        nonlocal seq
        seq += 1
        _, resposta = jiga.processar(montar(seq, *argumentos))
        [(_, quadro)] = ReceptorQuadros().processar(resposta)
        return interpretar_resposta(quadro)
    return executar


def _medir(jiga, pontos=(124, 2048, 3971), amostras=3):
    return medir(executor(jiga), list(pontos), amostras, esperar=lambda _: None)


def test_varredura_padrao_igual_ao_ct10():
    assert pontos_varredura(11, *DAC_FAIXA_UTIL) == pontos_ct10()


@pytest.mark.parametrize("quantidade, inicio, fim", [(1, 0, 10), (5, 10, 10), (5, -1, 10), (5, 0, 4096)])
def test_varredura_invalida(quantidade, inicio, fim):
    with pytest.raises(ValueError):
        pontos_varredura(quantidade, inicio, fim)


def test_loopback_ideal_sem_erro():
    medidos = _medir(JigaSimulada(loopback=True))
    assert [(p.canal_dac, p.canal_adc, p.codigo) for p in medidos] == [
        (1, 0, 124), (1, 0, 2048), (1, 0, 3971), (2, 1, 124), (2, 1, 2048), (2, 1, 3971)]
    assert all(p.leituras == [p.codigo] * 3 and p.erro == 0 and p.dentro_tolerancia for p in medidos)


def test_resumo_de_canal_ideal():
    pior, pior_mv, ganho, offset = resumo_canal(_medir(JigaSimulada(loopback=True))[:3])
    assert (pior, pior_mv) == (0, 0)
    assert ganho == pytest.approx(1)
    assert offset == pytest.approx(0, abs=1e-9)


def test_sem_jumper_fica_fora_da_tolerancia():
    """Sem o loopback, o ADC simulado le 0: o ponto de 2048 erra -2048 codigos."""
    medidos = _medir(JigaSimulada())
    assert [p.pior_erro for p in medidos[:3]] == [-124, -2048, -3971]
    assert not any(p.dentro_tolerancia for p in medidos)


def test_ponto_no_limite_da_tolerancia():
    no_limite = Ponto(1, 0, 1000, [1000 + TOLERANCIA_ADC, 1000 - 3])
    fora = Ponto(1, 0, 1000, [1000, 1000 - TOLERANCIA_ADC - 1])
    assert no_limite.dentro_tolerancia and no_limite.pior_erro == TOLERANCIA_ADC
    assert not fora.dentro_tolerancia and fora.pior_erro == -(TOLERANCIA_ADC + 1)
    assert no_limite.erro == pytest.approx((TOLERANCIA_ADC - 3) / 2)


def test_comando_recusado_interrompe_a_medicao():
    with pytest.raises(ErroJiga, match="NACK DAC_SET"):
        _medir(JigaSimulada(loopback=True), pontos=(4096,))


def test_csv(tmp_path):
    caminho = tmp_path / "medicao.csv"
    salvar_csv([Ponto(2, 1, 2048, [2050, 2046, 2051])], caminho)
    with open(caminho, encoding="utf-8") as arquivo:
        [linha] = list(csv.DictReader(arquivo))
    assert list(linha) == COLUNAS
    assert linha["codigo_dac"] == "2048"
    assert linha["tensao_nominal_v"] == "1.6504"
    assert (linha["leitura_min"], linha["leitura_max"], linha["pior_erro_codigos"]) == ("2046", "2051", "3")
    assert linha["dentro_tolerancia"] == "1"
