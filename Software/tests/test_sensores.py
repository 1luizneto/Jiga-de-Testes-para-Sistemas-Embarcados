"""Emulacao de sensores (CT25): modelos do MPU-6500 e do BME280, bateria com a secao 'sensores' e o DUT simulado
lendo a jiga com os mesmos drivers do firmware do ESP32-S3 (drivers_sensores.c)."""

from pathlib import Path

import pytest

from bateria import CarregadorDeBaterias, ErroBateria
from cliente import ClienteJiga, TransporteSimulado
from dut import DutSimulado
from motor import MotorDeTestes
from protocolo import (
    CMD_I2C_CONFIG, CMD_I2C_SLAVE_ADDR2, CMD_I2C_SLAVE_LOAD, CMD_I2C_SLAVE_MASK, CMD_SPI_SLAVE_REG, JigaSimulada,
    montar_i2c_addr2, montar_i2c_config, montar_i2c_load, montar_i2c_mask, montar_spi_reg,
)
from relatorio import FALHOU, PASSOU
from sensores import (
    BME_CALIBRACAO, bme_brutos, bme_calibracao_bytes, bme_calibracao_de_bytes, bme_dados, bme_le, bme_pressao,
    bme_t_fine, bme_temperatura, mpu_aceleracao, mpu_giro, mpu_le, mpu_temperatura,
)

BATERIAS = Path(__file__).resolve().parent.parent / "baterias"


def _jiga_com_dut():
    return JigaSimulada(dut=DutSimulado())


def _executar(texto_ou_arquivo, jiga=None):
    carregador = CarregadorDeBaterias()
    bateria = carregador.carregar(texto_ou_arquivo) if isinstance(texto_ou_arquivo, Path) \
        else carregador.carregar_texto(texto_ou_arquivo)
    cliente = ClienteJiga(TransporteSimulado(jiga if jiga is not None else _jiga_com_dut()))
    cliente.abrir()
    return MotorDeTestes(cliente).executar(bateria)


# 1. BME280: compensacao igual ao exemplo da folha de dados (25,08 C e 100 653,27 Pa)
def test_bme_folha_de_dados():
    assert bme_temperatura(519888) == 25.08
    assert bme_pressao(415148, bme_t_fine(519888)) == pytest.approx(1006.5327, abs=0.001)


# 2. BME280: a inversa acha o valor bruto que, compensado, da o pedido (em toda a faixa do sensor)
@pytest.mark.parametrize("t, p, h", [(25, 1013.25, 50), (-40, 300, 0), (85, 1100, 100), (-5, 780, 20),
                                     (35.2, 1005.8, 82.5)])
def test_bme_inversa(t, p, h):
    temperatura, pressao, umidade = bme_le(bme_dados(t, p, h))
    assert temperatura == pytest.approx(t, abs=0.006)
    assert pressao == pytest.approx(p, abs=0.003)
    assert umidade == pytest.approx(h, abs=0.006)


# 3. BME280: os bytes de calibracao seguem o formato da folha de dados (H4/H5 de 12 bits em 3 bytes)
def test_bme_calibracao_bytes():
    tp, h = bme_calibracao_bytes()
    assert len(tp) == 26 and len(h) == 7
    assert bme_calibracao_de_bytes(tp, h) == BME_CALIBRACAO
    assert tp[0:2] == (27504).to_bytes(2, "little") and h[3:6] == bytes([0x13, 0x29, 0x03])
    adc_t, adc_p, _ = bme_brutos(25.08, 1006.5327, 50)
    assert bme_temperatura(adc_t) == 25.08 and bme_pressao(adc_p, bme_t_fine(adc_t)) == pytest.approx(1006.5327, abs=0.002)


# 4. MPU-6500: big-endian com sinal, fundo de escala e saturacao no int16
def test_mpu_codificacao():
    assert mpu_aceleracao((0.5, -0.25, 1.0)) == bytes.fromhex("2000F0004000")
    assert mpu_aceleracao((1.0, 0, 0), escala_g=16) == bytes.fromhex("080000000000")
    assert mpu_giro((250, -250, 0)) == bytes.fromhex("7FFF80000000")
    assert mpu_temperatura(21) == b"\x00\x00"
    aceleracao, temperatura, giro = mpu_le(mpu_aceleracao((0.1, 0.2, -0.3)) + mpu_temperatura(36.6)
                                           + mpu_giro((1, 2, 3)))
    assert aceleracao == pytest.approx((0.1, 0.2, -0.3), abs=1e-4) and temperatura == pytest.approx(36.6, abs=0.002)
    assert giro == pytest.approx((1, 2, 3), abs=0.008)


# 5. Jiga simulada: segundo endereco e mascara (escrita do DUT so nos bits gravaveis, como o I2cBus)
def test_jiga_dois_enderecos_e_mascara():
    jiga = JigaSimulada()
    for quadro in (montar_i2c_addr2(1, 0x76), montar_i2c_load(2, 0x6B, b"\x01"), montar_i2c_mask(3, 0x6B, b"\x7f"),
                   montar_i2c_config(4, 0, 0x68, 1)):
        jiga.processar(quadro)
    assert jiga.i2c_mestre_externo(0x68, b"\x6b\x80", 0) == b""
    # o bit 7 (reset) nao e gravavel; os outros recebem o que o DUT escreveu (0): 0x00, lido pelo outro endereco
    assert jiga.i2c_mestre_externo(0x76, b"\x6b", 1) == b"\x00"
    assert jiga.i2c_mestre_externo(0x69, b"\x6b", 1) is None
    jiga.processar(montar_i2c_addr2(5, 0x55 + 0x21))                   # 0x76 de novo, sem I2C_CONFIG: nada muda
    jiga._estado_seguro()
    assert jiga.i2c_mascara == bytearray([0xFF] * 256) and jiga.i2c_endereco2_ativo is None


# 6. CT25: a bateria inteira passa com o DUT simulado lendo a jiga pelos drivers
def test_ct25_passa_com_o_dut_simulado():
    relatorio = _executar(BATERIAS / "ct25_sensores.yaml")
    assert [c.veredito for c in relatorio.casos] == [PASSOU] * 5, relatorio.texto()
    assert relatorio.casos[2].verificacoes[0].obtido.startswith("[35.2, 1005.8, 82.5]")


# 7. Preparo do caso: segundo endereco, carga e mascara de cada sensor e o I2C_CONFIG por ultimo
def test_preparo_dos_sensores():
    bateria = CarregadorDeBaterias().carregar(BATERIAS / "ct25_sensores.yaml")
    preparo = bateria.casos[0].preparo
    assert preparo[0] == (CMD_I2C_SLAVE_ADDR2, bytes([0x76]))
    assert preparo[-1] == (CMD_I2C_CONFIG, bytes([0, 0x68, 1]))
    assert {cmd for cmd, _ in preparo[1:-1]} == {CMD_I2C_SLAVE_LOAD, CMD_I2C_SLAVE_MASK}
    assert all(len(payload) <= 240 for _, payload in preparo)
    estimulo = bateria.casos[1].passos[0]
    assert estimulo[0:3] == bytes([CMD_I2C_SLAVE_LOAD, 0x3B, 14])


# 8. Um so sensor: sem segundo endereco; o DUT le o BME280 no 0x77
def test_um_sensor_no_endereco_alternativo():
    relatorio = _executar("""
bateria: um
sensores:
  bme280: {endereco: 0x77}
casos:
  - nome: le
    estimulos: [{bme280: {pressao: 900}}]
    verificar: [{uart_valores: {comando: "BME 77\\n", esperado: [25, 900, 50], tolerancia: 0.02}}]
""")
    assert relatorio.casos[0].veredito == PASSOU, relatorio.texto()


# 9. DUT que le errado (outro valor) falha; sem o sensor no endereco o DUT responde ERR NACK e o caso falha
def test_valores_errados_falham():
    relatorio = _executar("""
bateria: errado
sensores:
  mpu6500: {}
casos:
  - nome: espera 2 g e recebe 1 g
    verificar: [{uart_valores: {comando: "MPU\\n", esperado: [0, 0, 2, 0, 0, 0, 25], tolerancia: 0.01}}]
  - nome: BME nao emulado
    verificar: [{uart_valores: {comando: "BME\\n", esperado: [25, 1013.25, 50], tolerancia: 0.02}}]
""")
    assert [c.veredito for c in relatorio.casos] == [FALHOU, FALHOU]
    assert "ERR NACK" in relatorio.casos[1].verificacoes[0].obtido


# 10. Erros de descricao: sensor desconhecido, endereco invalido, estimulo de sensor nao declarado, fora da faixa
@pytest.mark.parametrize("texto, trecho", [
    ("sensores: {bmp180: {}}\ncasos: [{nome: a, estimulos: [{espera_ms: 1}]}]", "sensor desconhecido"),
    ("sensores: {mpu6500: {endereco: 0x50}}\ncasos: [{nome: a, estimulos: [{espera_ms: 1}]}]", "'endereco'"),
    ("casos: [{nome: a, estimulos: [{bme280: {temperatura: 20}}]}]", "nao esta na secao 'sensores'"),
    ("sensores: {bme280: {}}\ncasos: [{nome: a, estimulos: [{bme280: {pressao: 1200}}]}]", "'pressao' = 1200"),
    ("sensores: {mpu6500: {}}\ncasos: [{nome: a, estimulos: [{mpu6500: {aceleracao: [0, 0, 3]}}]}]",
     "fundo de escala"),
    ("sensores: {mpu6500: {endereco: 0x76}, bme280: {endereco: 0x76}}\ncasos: [{nome: a, estimulos: [{espera_ms: 1}]}]",
     None),
])
def test_erros_de_descricao(texto, trecho):
    with pytest.raises(ErroBateria) as erro:
        CarregadorDeBaterias().carregar_texto("bateria: x\n" + texto)
    assert trecho is None or any(trecho in e for e in erro.value.erros), erro.value.erros


# 11. CT26: BME280 na escrava SPI (modo registradores, 'ou' 0x80) e MPU-6500 no I2C ao mesmo tempo, lidos pelo DUT
def test_ct26_sensor_spi_com_o_dut_simulado():
    relatorio = _executar(BATERIAS / "ct26_sensores_spi.yaml")
    assert [c.veredito for c in relatorio.casos] == [PASSOU] * 3, relatorio.texto()
    preparo = CarregadorDeBaterias().carregar(BATERIAS / "ct26_sensores_spi.yaml").casos[0].preparo
    assert preparo[-1] == (CMD_SPI_SLAVE_REG, bytes([1, 0x80]))


# 12. Sem o modo registradores, o MISO solto le 0xFF: o driver do DUT nao reconhece o sensor (ERR PERIFERICO)
def test_spi_sem_modo_registradores():
    jiga = _jiga_com_dut()
    assert jiga.dut.executar("BMES") == b"ERR PERIFERICO\n"
    jiga.processar(montar_spi_reg(1, 1, 0x80))
    assert jiga.spi_registros == 1 and jiga.spi_registro_externo(0xD0, b"", 1) is None   # sem SPI_CONFIG escrava


# 13. Dois sensores no SPI: recusado (um CS so)
def test_dois_sensores_no_spi():
    with pytest.raises(ErroBateria) as erro:
        CarregadorDeBaterias().carregar_texto(
            "bateria: x\nsensores: {bme280: {barramento: spi}, mpu6500: {barramento: spi}}\n"
            "casos: [{nome: a, estimulos: [{espera_ms: 1}]}]")
    assert any("so um sensor no SPI" in e for e in erro.value.erros)
