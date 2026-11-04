"""Gerador de ondas: o mesmo calculo do Onda.c (vetores do Test_Onda.c), as formas, a bateria e a execucao na jiga
simulada (DAC lido pelo ADC em loopback e o DUT lendo um sensor com uma onda)."""

import math

import pytest

from bateria import CarregadorDeBaterias, ErroBateria
from cliente import ClienteJiga, TransporteSimulado
from dut import DutSimulado
from motor import MotorDeTestes
from ondas import ErroOnda, tabela
from protocolo import (
    ONDA_DAC, ONDA_DEGRAU, ONDA_I2C_S16, ONDA_INTERPOLA, ONDA_PWM, JigaSimulada, onda_ultimo, onda_valida, onda_valor,
    passo_onda, tamanho_passo,
)
from relatorio import PASSOU


def _executar(texto, jiga):
    cliente = ClienteJiga(TransporteSimulado(jiga))
    cliente.abrir()
    return MotorDeTestes(cliente).executar(CarregadorDeBaterias().carregar_texto(texto))


# 1. Mesmos vetores do Test_Onda.c: triangular de 100 a 1100 (interpolacao, periodo) e degrau
def test_valor_igual_ao_firmware():
    p = passo_onda(0, ONDA_DAC, 1, 1000, 1000, [0, 65535, 0], 100, 1000)[1:]
    assert [onda_valor(p, t) for t in (0, 250, 500, 750, 1250)] == [100, 600, 1100, 600, 600]
    p = passo_onda(0, ONDA_DAC, 1, 1000, 1000, [0, 65535, 0], 100, 1000, modo=ONDA_DEGRAU)[1:]
    assert (onda_valor(p, 499), onda_valor(p, 500)) == (100, 1100)
    rampa = passo_onda(0, ONDA_DAC, 1, 1000, 1_000_000, [0, 65535], 0, 4000, ciclos=1)[1:]
    assert (onda_valor(rampa, 1000), onda_valor(rampa, 500_000), onda_ultimo(rampa)) == (4, 2000, 4000)
    dma = passo_onda(0, ONDA_DAC, 2, 10, 1000, [0, 65535, 0], 0, 4000)[1:]
    assert onda_valor(dma, 10) == 80


# 2. Validacao igual ao Onda_Valida (e o tamanho do passo pelo numero de pontos)
@pytest.mark.parametrize("args, valido", [
    ((0, ONDA_DAC, 1, 1000, 1_000_000, [0, 65535], 0, 4000), True),
    ((6, ONDA_DAC, 1, 1000, 1_000_000, [0, 65535], 0, 4000), False),           # gerador 6
    ((0, ONDA_DAC, 3, 1000, 1_000_000, [0, 65535], 0, 4000), False),           # DAC3
    ((0, ONDA_DAC, 1, 1000, 1_000_000, [0, 65535], 0, 4096), False),           # passa de 4095
    ((0, ONDA_PWM, 1, 99, 1_000_000, [0, 65535], 0, 10000), False),            # PWM abaixo de 100 us
    ((0, ONDA_DAC, 1, 10, 20_000, [0, 65535], 0, 4095), False),                # 2000 amostras no DMA
    ((0, ONDA_DAC, 1, 10, 10_000, [0, 65535], 0, 4095), True),
    ((0, ONDA_I2C_S16, 0x3F, 1000, 500, [0, 65535], -100, 200), False),        # periodo < passo
    ((0, ONDA_I2C_S16, 0x3F, 1000, 1000, [0], -100, 200), False),              # 1 ponto
])
def test_validacao(args, valido):
    passo = passo_onda(*args)
    assert onda_valida(passo[1:]) == valido
    assert (tamanho_passo(passo, 0) is not None) == valido


# 3. Formas: tabela, modo e ciclos padrao
def test_formas():
    assert tabela("rampa", {"de": 1, "ate": 2, "duracao_ms": 100}) == (100_000, [1, 2], ONDA_INTERPOLA, 1)
    assert tabela("triangular", {"min": 0, "max": 1, "periodo_ms": 10}) == (10_000, [0, 1, 0], ONDA_INTERPOLA, 0)
    assert tabela("dente_de_serra", {"min": 0, "max": 1, "periodo_ms": 10})[1] == [0, 1]
    _, v, modo, _ = tabela("quadrada", {"min": 0, "max": 1, "periodo_ms": 10, "razao": 30})
    assert modo == ONDA_DEGRAU and v[-1] == 1 and v[:-1].count(1) / (len(v) - 1) == pytest.approx(0.3)
    _, v, _, ciclos = tabela("exponencial", {"de": 20, "ate": 60, "tau_ms": 100})
    assert ciclos == 1 and v[0] == 20 and v[-1] == pytest.approx(60 - 40 * math.exp(-5))
    periodo, v, modo, _ = tabela("pontos", {"pontos": [[0, 40], [1000, 80], [3000, 40]], "interpolar": False})
    assert (periodo, v, modo) == (3_000_000, [40, 80, 80, 40], ONDA_DEGRAU)


# 4. Senoide de 32 pontos: a interpolacao linear fica a menos de 0,5 % da amplitude da senoide verdadeira
def test_senoide_precisao():
    periodo, v, _, _ = tabela("senoide", {"min": 0, "max": 2, "periodo_ms": 1})
    p = passo_onda(0, ONDA_DAC, 1, 1000, periodo, [round(x / 2 * 65535) for x in v], 0, 4000)[1:]
    erro = max(abs(onda_valor(p, t) - (2000 + 2000 * math.sin(2 * math.pi * t / periodo))) for t in range(0, 1000, 7))
    assert erro < 0.005 * 4000


# 5. Erros de descricao das ondas
@pytest.mark.parametrize("onda, trecho", [
    ("{alvo: dac3, forma: rampa, de: 0, ate: 1, duracao_ms: 10}", "alvo 'dac3'"),
    ("{alvo: dac1, forma: zigue, min: 0, max: 1, periodo_ms: 10}", "forma desconhecida"),
    ("{alvo: pwm1.razao, forma: senoide, min: 0, max: 50, periodo_ms: 10, passo_us: 10}", "so no DAC"),
    ("{alvo: dac1, forma: senoide, min: 0, max: 1, periodo_ms: 100, passo_us: 10}", "1024 amostras"),
    ("{alvo: dac1, forma: rampa, de: 0, ate: 5, duracao_ms: 10}", "fora de"),
    ("{alvo: mpu6500.aceleracao_x, forma: rampa, de: 0, ate: 1, duracao_ms: 10}", "alvo 'mpu6500"),
    ("{alvo: dac1, forma: rampa, de: 0, ate: 1}", "falta 'duracao_ms'"),
])
def test_erros(onda, trecho):
    with pytest.raises(ErroBateria) as erro:
        CarregadorDeBaterias().carregar_texto(f"bateria: x\ncasos: [{{nome: a, estimulos: [{{onda: {onda}}}]}}]")
    assert any(trecho in e for e in erro.value.erros), erro.value.erros


# 6. Rampa no DAC1 lida pelo A0 (loopback) em varios instantes: a onda roda enquanto a rotina continua
def test_rampa_no_dac_lida_pelo_adc():
    relatorio = _executar("""
bateria: rampa
casos:
  - nome: rampa de 0,5 a 2,5 V em 1 s
    estimulos:
      - onda: {alvo: dac1, forma: rampa, de: 0.5, ate: 2.5, duracao_ms: 1000}
    verificar:
      - adc: {canal: 0, esperado: 0.5, tolerancia: 0.01}
      - espera_ms: 250
      - adc: {canal: 0, esperado: 1.0, tolerancia: 0.01}
      - espera_ms: 500
      - adc: {canal: 0, esperado: 2.0, tolerancia: 0.01}
      - espera_ms: 500
      - adc: {canal: 0, esperado: 2.5, tolerancia: 0.01}
""", JigaSimulada(loopback=True))
    assert relatorio.casos[0].veredito == PASSOU, relatorio.texto()


# 7. Temperatura do BME280 em rampa: o DUT le a temperatura do instante e a pressao e a umidade nao mudam
# (as duas ondas acopladas corrigem a compensacao)
def test_rampa_de_temperatura_do_bme_com_o_dut():
    relatorio = _executar("""
bateria: aquecimento
sensores: {bme280: {}}
casos:
  - nome: 20 a 40 graus em 1 s
    estimulos:
      - bme280: {pressao: 950, umidade: 70}
      - onda: {alvo: bme280.temperatura, forma: rampa, de: 20, ate: 40, duracao_ms: 1000}
      - espera_ms: 500
    verificar:
      - uart_valores: {comando: "BME\\n", esperado: [30, 950, 70], tolerancia: [0.1, 0.05, 0.05], espera_ms: 0}
""", JigaSimulada(dut=DutSimulado()))
    assert relatorio.casos[0].veredito == PASSOU, relatorio.texto()


# 8. Quadrada na aceleracao do MPU-6500: o DUT le o valor alto ou o baixo conforme o instante
def test_quadrada_no_mpu_com_o_dut():
    relatorio = _executar("""
bateria: vibracao
sensores: {mpu6500: {}}
casos:
  - nome: z alterna entre 0,5 e 1,5 g a cada 100 ms
    estimulos:
      - onda: {alvo: mpu6500.aceleracao_z, forma: quadrada, min: 0.5, max: 1.5, periodo_ms: 200}
      - espera_ms: 50
    verificar:
      - uart_valores: {comando: "MPU\\n", esperado: [0, 0, 1.5, 0, 0, 0, 25], tolerancia: 0.002, espera_ms: 0}
      - espera_ms: 100
      - uart_valores: {comando: "MPU\\n", esperado: [0, 0, 0.5, 0, 0, 0, 25], tolerancia: 0.002, espera_ms: 0}
""", JigaSimulada(dut=DutSimulado()))
    assert relatorio.casos[0].veredito == PASSOU, relatorio.texto()


# 9. O estado seguro do fim da rotina para as ondas: o caso seguinte comeca sem onda
def test_fim_da_rotina_para_as_ondas():
    jiga = JigaSimulada(loopback=True)
    relatorio = _executar("""
bateria: para
casos:
  - nome: onda sem fim
    estimulos: [{onda: {alvo: dac1, forma: triangular, min: 1, max: 2, periodo_ms: 10}}, {espera_ms: 5}]
  - nome: depois
    estimulos: [{espera_ms: 20}]
    verificar: [{adc: {canal: 0, esperado: 0, tolerancia: 0.001}}]
""", jiga)
    assert [c.veredito for c in relatorio.casos] == [PASSOU, PASSOU] and not jiga.ondas


# 10. Mais de 6 ondas no caso: recusado na validacao
def test_geradores_demais():
    ondas = ", ".join("{onda: {alvo: dac1, forma: rampa, de: 0, ate: 1, duracao_ms: 10}}" for _ in range(7))
    with pytest.raises((ErroBateria, ErroOnda)) as erro:
        CarregadorDeBaterias().carregar_texto(f"bateria: x\ncasos: [{{nome: a, estimulos: [{ondas}]}}]")
    assert "6 ondas" in str(erro.value) or "nao cabe" in str(erro.value)
