"""Baterias YAML: carregamento, validacao e compilacao em rotinas (CT06)."""

from pathlib import Path

import pytest

from bateria import CarregadorDeBaterias, ErroBateria, tempo_maximo_ms, tensao_para_codigo
from protocolo import (
    CMD_ADC_READ, CMD_GPIO_READ, CMD_UART_READ, PASSO_ESPERA, PASSO_ESPERA_GPIO, montar_adc_read, montar_dac_set,
    passo, passo_espera, passo_espera_gpio, validar_rotina,
)

BATERIAS = Path(__file__).resolve().parent.parent / "baterias"


def _carregar(texto):
    return CarregadorDeBaterias().carregar_texto(texto)


def _erros(texto):
    with pytest.raises(ErroBateria) as erro:
        _carregar(texto)
    return erro.value.erros


# 1. CT06: a bateria da figura da monografia compila nos bytes esperados
def test_ct06_bateria_da_monografia():
    bateria = CarregadorDeBaterias().carregar(BATERIAS / "exemplo_monografia.yaml")
    assert bateria.nome == "controle-ventilador" and bateria.dut == "ESP32" and len(bateria.casos) == 2
    liga, desliga = bateria.casos
    assert liga.passos == [passo(montar_dac_set(0, 1, 2482)), passo_espera_gpio(5, 1, 100_000)]
    assert liga.tempo_max_ms == 210                     # 100 ms x 1,1 + 100 ms de margem
    assert desliga.passos == [passo(montar_dac_set(0, 1, 1241)), passo_espera(500_000), bytes([CMD_GPIO_READ]),
                              passo(montar_adc_read(0, 0))]
    assert [v.indice_passo for v in desliga.verificacoes] == [2, 3]
    assert desliga.verificacoes[1].parametros == {"canal": 0, "esperado": 1.65, "tolerancia": 0.05}
    for caso in bateria.casos:
        assert validar_rotina(caso.payload)[0] is None


# 2. CT06: um caso que nao cabe em um quadro e recusado na validacao, antes de falar com a jiga
def test_ct06_caso_grande_demais_recusado():
    estimulos = "\n".join("      - gpio_out: {pino: 1, nivel: 1}" for _ in range(80))
    erros = _erros(f"bateria: grande\ncasos:\n  - nome: enorme\n    estimulos:\n{estimulos}\n")
    assert len(erros) == 1 and "nao cabe em um quadro" in erros[0] and "enorme" in erros[0]


# 3. O mesmo exemplo de rotina do Apendice A (DAC1 2482, ESPERA_GPIO 5 ate 500 ms, ADC A0)
def test_rotina_do_apendice():
    bateria = _carregar("""
bateria: apendice
casos:
  - nome: exemplo
    tempo_max_ms: 1000
    estimulos: [{dac: {canal: 1, codigo: 2482}}]
    verificar:
      - reacao_gpio: {pino: 5, nivel: 1, max_ms: 500}
      - adc: {canal: 0, esperado: 2.0}
""")
    [caso] = bateria.casos
    assert b"".join(caso.passos).hex(" ").upper() == "10 01 B2 09 79 05 01 20 A1 07 00 40 00"
    assert caso.tempo_max_ms == 1000 and caso.verificacoes[1].parametros["tolerancia"] == 0.033


# 4. Conversao de tensao para codigo do DAC: round(4095 x V / 3,3)
@pytest.mark.parametrize("tensao, codigo", [(0, 0), (1.65, 2048), (2.0, 2482), (3.3, 4095)])
def test_tensao_para_codigo(tensao, codigo):
    assert tensao_para_codigo(tensao) == codigo


# 5. Tempo maximo: (esperas + limites) x 1,1 + 100 ms, limitado a 1 h
def test_tempo_maximo():
    assert tempo_maximo_ms(0) == 100
    assert tempo_maximo_ms(1_000_000) == 1200
    assert tempo_maximo_ms(10 ** 12) == 3_600_000


# 6. Todos os erros aparecem juntos, cada um com o caso e o item
def test_todos_os_erros_juntos():
    erros = _erros("""
bateria: com-erros
casos:
  - nome: dac errado
    estimulos:
      - dac: {canal: 3, tensao: 1.0}
      - gpio_out: {pino: 1, nivel: 1}
    verificar:
      - adc: {canal: 0, esperado: 5.0}
  - nome: tipo errado
    estimulos:
      - laser: {potencia: 9000}
  - estimulos: []
""")
    assert len(erros) == 4
    assert erros[0].startswith("caso 1 ('dac errado'), estimulos 1 (dac): canal do DAC 3")
    assert "verificar 1 (adc): 'esperado' = 5.0 fora de 0 a 3.3" in erros[1]
    assert "estimulo desconhecido 'laser'" in erros[2]
    assert erros[3] == "caso 3: falta 'nome'"


# 7. Campos desconhecidos e faltando tambem sao erros (evita erro de digitacao silencioso)
def test_campos_desconhecidos():
    erros = _erros("""
bateria: x
casos:
  - nome: a
    estimulo: []
    verificar:
      - gpio_in: {pino: 5, esperad: 1}
""")
    assert "campo(s) desconhecido(s): estimulo" in erros[0]
    assert "campo(s) desconhecido(s): esperad" in erros[1]


# 8. Bateria sem nome ou sem casos, e YAML quebrado
@pytest.mark.parametrize("texto, trecho", [
    ("casos: []", "falta o nome"),
    ("bateria: x", "falta a lista 'casos'"),
    ("bateria: [", "YAML invalido"),
    ("- 1\n- 2", "precisa ser um mapa"),
])
def test_bateria_invalida(texto, trecho):
    assert any(trecho in e for e in _erros(texto))


# 9. UART: o texto vira bytes, o tempo de transmissao entra no tempo maximo e uart_write exige uart_config
def test_uart_texto_e_tempo():
    bateria = _carregar("""
bateria: uart
casos:
  - nome: ping no dut
    estimulos:
      - uart_config: {baud: 1200}
      - uart_write: {texto: "PING\\n"}
    espera_ms: 50
    verificar:
      - uart_read: {esperado_texto: "OK\\n", max: 16}
""")
    [caso] = bateria.casos
    assert caso.passos[1] == bytes([0x71, 5]) + b"PING\n"
    assert caso.passos[3] == bytes([CMD_UART_READ, 16])
    assert caso.verificacoes[0].parametros["esperado"] == b"OK\n"
    assert caso.tempo_max_ms == tempo_maximo_ms(50_000 + 41_667)
    erros = _erros("bateria: u\ncasos:\n  - nome: sem config\n    estimulos:\n      - uart_write: {texto: a}\n")
    assert "sem uart_config" in erros[0]


# 10. I2C, SPI e PWM compilam nos passos dos comandos diretos
def test_barramentos_e_pwm():
    bateria = _carregar("""
bateria: barramentos
casos:
  - nome: tudo
    estimulos:
      - pwm: {canal: 1, frequencia: 1000, razao: 50}
      - i2c_config: {papel: escravo, endereco: 0x50, velocidade: 400}
      - i2c_load: {offset: 0x10, dados: "DE AD"}
      - spi_config: {papel: escrava, modo: 3}
      - spi_load: {dados: [1, 2, 3]}
      - espera_us: 250
      - pwm_stop: {canal: 1}
    verificar:
      - i2c_dump: {offset: 0x10, esperado: "DE AD"}
      - spi_read: {esperado: ""}
""")
    passos = [p.hex(" ").upper() for p in bateria.casos[0].passos]
    assert passos == ["30 01 E8 03 00 00 88 13", "50 00 50 01", "51 10 02 DE AD", "60 00 03 00 00 00 00",
                      "61 03 01 02 03", "78 FA 00 00 00", "31 01", "52 10 02", "62"]
    assert bateria.casos[0].estimulos[0] == "PWM1 = 1000 Hz, 50 %"


# 11. Resultados acima de 2048 B: a rotina e recusada pela mesma validacao do firmware
def test_resultados_demais():
    leituras = "\n".join("      - spi_read: {esperado: \"\"}" for _ in range(9))
    erros = _erros(f"bateria: r\ncasos:\n  - nome: leituras\n    verificar:\n{leituras}\n")
    assert "rotina recusada na validacao (passo 8" in erros[0]


# 12. Os passos de aquisicao sao os de leitura; ESPERA e ESPERA_GPIO nos codigos do Apendice A
def test_codigos_dos_passos():
    bateria = _carregar("""
bateria: c
casos:
  - nome: c
    estimulos: [{espera_ms: 1}]
    verificar: [{reacao_gpio: {pino: 8, nivel: 0, max_ms: 2}}, {adc: {canal: 2, esperado: 0}}]
""")
    assert [p[0] for p in bateria.casos[0].passos] == [PASSO_ESPERA, PASSO_ESPERA_GPIO, CMD_ADC_READ]
