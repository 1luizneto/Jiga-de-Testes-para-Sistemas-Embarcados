"""Testes do protocolo do lado do PC (CRC, quadros, receptor, respostas e casos de teste)."""

import binascii
import random

import pytest

from protocolo import (
    CASOS_TESTE, CMD_ADC_READ, CMD_DAC_SET, CMD_GPIO_READ, CMD_GPIO_SET, CMD_INEXISTENTE, CMD_PING,
    CMD_I2C_CONFIG, CMD_I2C_MASTER_XFER, CMD_I2C_SLAVE_DUMP, CMD_I2C_SLAVE_LOAD, CMD_PWM_SET, CMD_PWM_STOP,
    CMD_RESET_ALL, CMD_SPI_CONFIG, CMD_SPI_MASTER_XFER, CMD_SPI_SLAVE_LOAD, CMD_SPI_SLAVE_READ, DAC_FAIXA_UTIL,
    DAC_VALOR_MAX, ERRO_CMD_DESCONHECIDO,
    ERRO_CRC, ERRO_LEN, ERRO_NAO_CONFIGURADO, ERRO_OCUPADO, ERRO_PERIFERICO, I2C_PAPEL_ESCRAVO,
    I2C_PAPEL_MESTRE, ERRO_PARAMETRO_INVALIDO, LENDO, LOG_FIRMWARE, OCIOSO, PAYLOAD_MAX,
    PWM_FREQUENCIA_MAX, PWM_FREQUENCIA_PADRAO, PWM_RAZAO_MAX, QUADRO_OK, SOF, SPI_FREQUENCIA_MIN, SPI_PAPEL_ESCRAVO,
    SPI_PAPEL_MESTRE, SPI_TAMANHO_BUFFER, TAMANHO_QUADRO_MAX, TIMEOUT,
    TOLERANCIA_ADC, JigaSimulada, Quadro, ReceptorQuadros, avaliar_caso, calcular_pwm, casos_a_executar,
    crc16_ccitt, descrever_log, descrever_pwm, descrever_resposta, descrever_respostas, interpretar_resposta,
    montar_ack, montar_ack_ping, montar_adc_read, montar_dac_set, montar_gpio_read, montar_gpio_set,
    montar_i2c_config, montar_i2c_dump, montar_i2c_load, montar_i2c_xfer, montar_nack,
    montar_pwm_set, montar_pwm_stop, montar_quadro, montar_spi_config, montar_spi_load, montar_spi_read,
    montar_spi_xfer, pontos_ct10, porcento_para_razao, pwm_gerado, resultado_do_log, simular, simular_caso,
    descrever_spi, spi_divisor,
)
from versao import __version__


# --- CRC ---------------------------------------------------------------------

def test_crc_vetor_de_referencia():
    assert crc16_ccitt(b"123456789") == 0x29B1


# Mesmos vetores dos testes Unity do periferico CRC (projeto/App/Test/Suites/Test_Crc16.c):
# se o PC e a placa discordarem do CRC, nenhum quadro passa.
@pytest.mark.parametrize("dados, inicial, esperado", [
    (b"123456789", 0xFFFF, 0x29B1),
    (b"123456789", 0x0000, 0x31C3),
    (b"", 0xFFFF, 0xFFFF),
    (b"A", 0xFFFF, 0xB915),
    (b"Jiga", 0xFFFF, 0x6937),
    (bytes([0x00]), 0xFFFF, 0xE1F0),
    (bytes([0xFF]), 0xFFFF, 0xFF00),
    (bytes([0x01, 0x02, 0x03, 0x04]), 0xFFFF, 0x89C3),
    (bytes(4), 0xFFFF, 0x84C0),
    (b"STM32F767", 0xFFFF, 0x8348),
    ((b"0123456789" * 25)[:243], 0xFFFF, 0xB504),
])
def test_crc_igual_ao_firmware(dados, inicial, esperado):
    assert crc16_ccitt(dados, inicial) == esperado


def test_crc_igual_ao_binascii():
    gerador = random.Random(2026)
    for _ in range(200):
        dados = bytes(gerador.randrange(256) for _ in range(gerador.randrange(250)))
        assert crc16_ccitt(dados) == binascii.crc_hqx(dados, 0xFFFF)


# --- montagem do quadro ------------------------------------------------------

def test_quadro_ping():
    # Mesmo quadro usado em Test_Comm.c
    assert montar_quadro(1, CMD_PING) == bytes([0x7E, 0x00, 0x01, 0x01, 0x8C, 0xEF])


def test_quadro_campos_e_crc_little_endian():
    payload = bytes([0x11, 0x7E, 0x22])
    quadro = montar_quadro(9, 0x10, payload)
    assert quadro[0] == SOF
    assert quadro[1:4] == bytes([len(payload), 9, 0x10])
    assert quadro[4:-2] == payload
    assert int.from_bytes(quadro[-2:], "little") == crc16_ccitt(quadro[1:-2])


def test_quadro_tamanho_maximo():
    assert len(montar_quadro(0, 0x10, bytes(PAYLOAD_MAX))) == TAMANHO_QUADRO_MAX == 246


def test_quadro_payload_grande_demais():
    with pytest.raises(ValueError):
        montar_quadro(0, 0x10, bytes(PAYLOAD_MAX + 1))


@pytest.mark.parametrize("seq, cmd", [(256, 1), (-1, 1), (0, 256)])
def test_quadro_seq_cmd_fora_de_1_byte(seq, cmd):
    with pytest.raises(ValueError):
        montar_quadro(seq, cmd)


# --- receptor (espelho do parser do firmware) --------------------------------

def test_receptor_ping_lendo_ate_o_ultimo_byte():
    receptor = ReceptorQuadros()
    quadro = montar_quadro(1, CMD_PING)
    resultados = [receptor.processar_byte(b)[0] for b in quadro]
    assert resultados == [LENDO] * 5 + [QUADRO_OK]


def test_receptor_devolve_campos():
    receptor = ReceptorQuadros()
    [(resultado, quadro)] = receptor.processar(montar_quadro(42, 0x33, b"\x7e\x01\x02"))
    assert resultado == QUADRO_OK
    assert (quadro.seq, quadro.cmd, quadro.payload, quadro.tamanho) == (42, 0x33, b"\x7e\x01\x02", 3)


def test_receptor_crc_errado():
    quadro = bytearray(montar_quadro(1, CMD_PING))
    quadro[-1] ^= 0xFF
    assert ReceptorQuadros().processar(quadro) == [(ERRO_CRC, None)]


def test_receptor_len_invalido_no_segundo_byte():
    receptor = ReceptorQuadros()
    assert receptor.processar_byte(SOF)[0] == LENDO
    assert receptor.processar_byte(PAYLOAD_MAX + 1)[0] == ERRO_LEN
    assert receptor.estado == "AGUARDA_SOF"


def test_receptor_ignora_lixo_antes_do_sof():
    eventos = ReceptorQuadros().processar(b"\xaa\xbb\x00" + montar_quadro(1, CMD_PING))
    assert [r for r, _ in eventos] == [QUADRO_OK]


def test_receptor_timeout():
    receptor = ReceptorQuadros()
    assert receptor.timeout() == OCIOSO
    receptor.processar(montar_quadro(1, CMD_PING)[:3])
    assert receptor.timeout() == TIMEOUT
    assert receptor.timeout() == OCIOSO


def test_receptor_quadros_aleatorios_em_sequencia():
    gerador = random.Random(7)
    quadros = [(gerador.randrange(256), gerador.randrange(256),
                bytes(gerador.randrange(256) for _ in range(gerador.randrange(PAYLOAD_MAX + 1))))
               for _ in range(50)]
    fluxo = b"".join(montar_quadro(*q) for q in quadros)
    eventos = ReceptorQuadros().processar(fluxo)
    assert [(q.seq, q.cmd, q.payload) for _, q in eventos] == quadros


# --- respostas da jiga -------------------------------------------------------

# Exemplos do Apendice A da monografia: os mesmos vetores dos testes Unity do Comm_BuildFrame
def test_ack_ping_igual_ao_apendice_a():
    assert montar_ack_ping(1, "0.1.0") == bytes.fromhex("7E 04 01 81 01 00 01 00 AC B8")


def test_nack_igual_ao_apendice_a():
    assert montar_nack(2, 0x10, 0x02) == bytes.fromhex("7E 02 02 FF 10 02 B5 54")


def test_ack_ping_usa_a_versao_do_software():
    maior, menor, correcao = (int(p) for p in __version__.split("."))
    assert montar_ack_ping(5)[4:8] == bytes([1, maior, menor, correcao])


def test_interpretar_ack_do_ping():
    [(_, quadro)] = ReceptorQuadros().processar(montar_ack_ping(1, "0.2.0"))
    resposta = interpretar_resposta(quadro)
    assert (resposta.ack, resposta.seq, resposta.cmd, resposta.dados) == (True, 1, CMD_PING, b"\x01\x00\x02\x00")
    assert resposta.descrever() == "ACK PING SEQ=1 (protocolo 1, firmware 0.2.0)"


def test_interpretar_nack():
    [(_, quadro)] = ReceptorQuadros().processar(montar_nack(7, CMD_INEXISTENTE, ERRO_CMD_DESCONHECIDO))
    resposta = interpretar_resposta(quadro)
    assert (resposta.ack, resposta.seq, resposta.cmd, resposta.erro) == (False, 7, 0x55, ERRO_CMD_DESCONHECIDO)
    assert resposta.descrever() == "NACK 0x55 SEQ=7: CMD_DESCONHECIDO"


def test_descrever_com_nome_do_comando():
    [(_, ack)] = ReceptorQuadros().processar(montar_ack(4, CMD_DAC_SET))
    [(_, nack)] = ReceptorQuadros().processar(montar_nack(5, CMD_RESET_ALL, ERRO_OCUPADO))
    assert interpretar_resposta(ack).descrever() == "ACK DAC_SET SEQ=4"
    assert interpretar_resposta(nack).descrever() == "NACK RESET_ALL SEQ=5: OCUPADO"


# --- DAC_SET -----------------------------------------------------------------

def test_dac_set_igual_ao_validado_na_placa():
    # Quadro enviado no teste manual do OCUPADO (v0.4.0, passo 5): a placa aceitou e respondeu ACK
    assert montar_dac_set(20, 1, 2048) == bytes.fromhex("7E 03 14 10 01 00 08 33 E0")


def test_dac_set_valor_little_endian():
    assert montar_dac_set(1, 2, 0x0FFF)[4:7] == bytes([0x02, 0xFF, 0x0F])


@pytest.mark.parametrize("canal, valor", [(256, 0), (-1, 0), (1, 0x10000), (1, -1)])
def test_dac_set_fora_de_u8_u16(canal, valor):
    with pytest.raises(ValueError):
        montar_dac_set(1, canal, valor)


# --- GPIO_SET, GPIO_READ e ADC_READ ------------------------------------------

def test_ack_adc_read_igual_ao_apendice_a():
    # Exemplo do Apendice A: ACK do ADC_READ, SEQ=3, canal 0, valor 2047 (o mesmo do Test_Comm_ResultComDados)
    assert montar_ack(3, CMD_ADC_READ, bytes([0x00, 0xFF, 0x07])) == bytes.fromhex("7E 03 03 C0 00 FF 07 9E EE")


def test_montar_gpio_e_adc():
    assert montar_gpio_set(1, 2, 1)[1:6] == bytes([2, 1, CMD_GPIO_SET, 2, 1])
    assert montar_gpio_read(1)[1:4] == bytes([0, 1, CMD_GPIO_READ])
    assert montar_adc_read(1, 2)[1:5] == bytes([1, 1, CMD_ADC_READ, 2])


@pytest.mark.parametrize("montar", [lambda: montar_gpio_set(1, 256, 0), lambda: montar_gpio_set(1, 1, -1),
                                    lambda: montar_adc_read(1, 256)])
def test_gpio_e_adc_fora_de_u8(montar):
    with pytest.raises(ValueError):
        montar()


def test_descrever_gpio_read_e_adc_read():
    [(_, gpio)] = ReceptorQuadros().processar(montar_ack(4, CMD_GPIO_READ, bytes([0x05])))
    [(_, adc)] = ReceptorQuadros().processar(montar_ack(3, CMD_ADC_READ, bytes([0x00, 0xFF, 0x07])))
    assert interpretar_resposta(gpio).descrever() == \
        "ACK GPIO_READ SEQ=4 mascara=0x05 (GPIO5=1 GPIO6=0 GPIO7=1 GPIO8=0)"
    assert interpretar_resposta(adc).descrever() == "ACK ADC_READ SEQ=3 canal 0 = 2047 (1,650 V)"


def test_pwm_set_igual_ao_apendice_a():
    """Canal 1, 1 kHz, 50 %: o quadro do Apendice A, validado na placa (ACK e 1,65 V no PE9)."""
    assert montar_pwm_set(4, 1, 1000, 5000) == bytes.fromhex("7E 07 04 30 01 E8 03 00 00 88 13 5A 47")


def test_pwm_stop_igual_ao_validado_na_placa():
    assert montar_pwm_stop(5, 1) == bytes.fromhex("7E 01 05 31 01 01 3F")


def test_pwm_set_frequencia_u32_little_endian():
    assert montar_pwm_set(1, 2, 0x0001_86A0, 0x1234)[4:11] == bytes([0x02, 0xA0, 0x86, 0x01, 0x00, 0x34, 0x12])


@pytest.mark.parametrize("montar", [
    lambda: montar_pwm_set(1, 256, 1000, 0), lambda: montar_pwm_set(1, 1, 1 << 32, 0),
    lambda: montar_pwm_set(1, 1, -1, 0), lambda: montar_pwm_set(1, 1, 1000, 0x10000),
    lambda: montar_pwm_stop(1, 256), lambda: montar_pwm_stop(1, -1),
], ids=["canal 256", "freq 2^32", "freq -1", "razao 2^16", "stop canal 256", "stop canal -1"])
def test_pwm_fora_de_u8_u32_u16(montar):
    with pytest.raises(ValueError):
        montar()


# Mesmos vetores dos testes Unity do Pwm_Calcula (projeto/App/Test/Suites/Test_Pwm.c): canal 1 = TIM1
# (216 MHz, 16 bits), 2 = TIM2 (108 MHz, 32 bits), 3 = TIM3 (108 MHz, 16 bits)
@pytest.mark.parametrize("canal, frequencia, razao, esperado", [
    (1, 1000, 5000, (3, 53999, 27000)),
    (3, 100_000, 2500, (0, 1079, 270)),
    (1, 100_000, 9000, (0, 2159, 1944)),
    (1, 1, 5000, (3295, 65533, 32767)),
    (2, 1, 5000, (0, 107999999, 54000000)),
    (3, 1, 5000, (1647, 65533, 32767)),
    (3, 50, 5000, (32, 65454, 32728)),
    (2, 20_000, 1234, (0, 5399, 666)),
    (1, 1000, 0, (3, 53999, 0)),
    (1, 1000, 1, (3, 53999, 5)),
    (2, 1000, 0, (0, 107999, 0)),
    (3, 1000, 0, (1, 53999, 0)),
])
def test_calcular_pwm_igual_ao_firmware(canal, frequencia, razao, esperado):
    assert calcular_pwm(canal, frequencia, razao) == esperado


def test_calcular_pwm_100_porcento_fica_sempre_alto():
    """CCR = ARR + 1: o comparador nunca dispara e a saida fica em nivel alto."""
    _, arr, ccr = calcular_pwm(1, 1000, PWM_RAZAO_MAX)
    assert ccr == arr + 1


@pytest.mark.parametrize("canal, frequencia, razao", [
    (0, 1000, 5000), (4, 1000, 5000), (1, 0, 5000), (1, PWM_FREQUENCIA_MAX + 1, 5000), (1, 1000, PWM_RAZAO_MAX + 1),
], ids=["canal 0", "canal 4", "0 Hz", "100001 Hz", "razao 10001"])
def test_calcular_pwm_fora_da_faixa(canal, frequencia, razao):
    with pytest.raises(ValueError):
        calcular_pwm(canal, frequencia, razao)


@pytest.mark.parametrize("porcento, razao", [(50, 5000), ("12.34", 1234), (0.01, 1), (100, 10000)])
def test_porcento_para_razao(porcento, razao):
    assert porcento_para_razao(porcento) == razao


def test_pwm_gerado_mostra_o_arredondamento_dos_registradores():
    """O que o CT12 mede: 50 Hz no TIM3 sai 49,9997 Hz e 12,34 % a 100 kHz sai 12,31 % (CCR = 133 de 1080)."""
    assert pwm_gerado(1, 1000, 5000) == (pytest.approx(1000), pytest.approx(50))
    assert pwm_gerado(3, 50, 5000)[0] == pytest.approx(49.9997, abs=1e-4)
    assert pwm_gerado(3, 100_000, 1234)[1] == pytest.approx(100 * 133 / 1080)


def test_descrever_pwm():
    assert descrever_pwm(1, 1000, 5000) == "PWM1 (PE9): PSC=3 ARR=53999 CCR=27000 -> 1000,0000 Hz, 50,00 %"


def test_interpretar_quadro_que_nao_e_resposta():
    with pytest.raises(ValueError):
        interpretar_resposta(Quadro(seq=1, cmd=CMD_PING))


def test_interpretar_nack_sem_codigo_de_erro():
    with pytest.raises(ValueError):
        interpretar_resposta(Quadro(seq=1, cmd=0xFF, payload=b"\x10"))


def test_descrever_respostas_em_fluxo_picado():
    fluxo = montar_ack_ping(1) + montar_nack(2, CMD_INEXISTENTE, ERRO_CMD_DESCONHECIDO)
    assert len(descrever_respostas(fluxo)) == 2
    assert descrever_respostas(fluxo[:-1])[-1] == "quadro incompleto"
    assert descrever_respostas(b"") == []


# --- jiga simulada (espelho do Comm_ProcessFrame) ----------------------------

def test_jiga_responde_ping_e_nack():
    jiga = JigaSimulada()
    assert jiga.processar(montar_quadro(1, CMD_PING)) == ([QUADRO_OK], montar_ack_ping(1))
    resposta = montar_nack(2, CMD_INEXISTENTE, ERRO_CMD_DESCONHECIDO)
    assert jiga.processar(montar_quadro(2, CMD_INEXISTENTE)) == ([QUADRO_OK], resposta)


def test_jiga_nao_responde_quadro_corrompido():
    quadro = bytearray(montar_quadro(1, CMD_PING))
    quadro[-1] ^= 0xFF
    assert JigaSimulada().processar(quadro) == ([ERRO_CRC], b"")


def test_jiga_seq_repetido_reenvia_sem_executar():
    jiga = JigaSimulada()
    _, primeira = jiga.processar(montar_quadro(3, CMD_PING))
    _, segunda = jiga.processar(montar_quadro(3, CMD_INEXISTENTE))
    assert segunda == primeira == montar_ack_ping(3)


def test_jiga_sessao_nova_esquece_o_seq():
    jiga = JigaSimulada()
    jiga.processar(montar_quadro(3, CMD_PING))
    jiga.nova_sessao()
    _, resposta = jiga.processar(montar_quadro(3, CMD_INEXISTENTE))
    assert resposta == montar_nack(3, CMD_INEXISTENTE, ERRO_CMD_DESCONHECIDO)


# --- jiga simulada: execucao (espelho do Exec e da TestExecTask) -------------

def test_jiga_dac_set_e_reset_all():
    jiga = JigaSimulada()
    assert jiga.processar(montar_dac_set(1, 1, 2048))[1] == montar_ack(1, CMD_DAC_SET)
    assert jiga.processar(montar_dac_set(2, 2, DAC_VALOR_MAX))[1] == montar_ack(2, CMD_DAC_SET)
    assert jiga.dac == {1: 2048, 2: DAC_VALOR_MAX}
    assert jiga.processar(montar_quadro(3, CMD_RESET_ALL))[1] == montar_ack(3, CMD_RESET_ALL)
    assert jiga.dac == {1: 0, 2: 0}


@pytest.mark.parametrize("quadro", [
    montar_dac_set(1, 3, 100),                          # canal inexistente
    montar_dac_set(1, 0, 100),                          # canal 0
    montar_dac_set(1, 1, DAC_VALOR_MAX + 1),            # valor acima de 12 bits
    montar_quadro(1, CMD_DAC_SET, bytes([0x01, 0x00])),  # payload curto
    montar_quadro(1, CMD_RESET_ALL, bytes([0x00])),      # RESET_ALL com payload
], ids=["canal 3", "canal 0", "valor 4096", "tamanho 2", "reset com payload"])
def test_jiga_parametro_invalido_nao_altera_saida(quadro):
    """CT09: NACK PARAMETRO_INVALIDO e nenhuma saida alterada."""
    jiga = JigaSimulada()
    jiga.processar(montar_dac_set(9, 1, 1234))
    _, resposta = jiga.processar(quadro)
    [(_, nack)] = ReceptorQuadros().processar(resposta)
    assert interpretar_resposta(nack).erro == ERRO_PARAMETRO_INVALIDO
    assert jiga.dac == {1: 1234, 2: 0}


def test_jiga_gpio_set_read_e_reset_all():
    jiga = JigaSimulada()
    assert jiga.processar(montar_gpio_set(1, 3, 1))[1] == montar_ack(1, CMD_GPIO_SET)
    assert jiga.gpio == {1: 0, 2: 0, 3: 1, 4: 0}
    jiga.entradas = 0x0A
    assert jiga.processar(montar_gpio_read(2))[1] == montar_ack(2, CMD_GPIO_READ, bytes([0x0A]))
    assert jiga.processar(montar_quadro(3, CMD_RESET_ALL))[1] == montar_ack(3, CMD_RESET_ALL)
    assert jiga.gpio == {1: 0, 2: 0, 3: 0, 4: 0}


def test_jiga_adc_read_sem_loopback():
    jiga = JigaSimulada()
    jiga.adc[2] = 0x0ABC
    assert jiga.processar(montar_adc_read(1, 2))[1] == montar_ack(1, CMD_ADC_READ, bytes([2, 0xBC, 0x0A]))


def test_jiga_loopback_liga_saidas_nas_entradas():
    """Jumpers do CT10/CT11: GPIO1-4 -> GPIO5-8, DAC1 -> A0 e DAC2 -> A1 (A2 fica solto)."""
    jiga = JigaSimulada(loopback=True)
    jiga.processar(montar_gpio_set(1, 1, 1))
    jiga.processar(montar_gpio_set(2, 4, 1))
    assert jiga.processar(montar_gpio_read(3))[1] == montar_ack(3, CMD_GPIO_READ, bytes([0x09]))
    jiga.processar(montar_dac_set(4, 2, 3000))
    assert jiga.processar(montar_adc_read(5, 1))[1] == montar_ack(5, CMD_ADC_READ, bytes([1]) + (3000).to_bytes(2, "little"))
    assert jiga.processar(montar_adc_read(6, 2))[1] == montar_ack(6, CMD_ADC_READ, bytes([2, 0, 0]))


@pytest.mark.parametrize("quadro", [
    montar_gpio_set(1, 0, 1),                            # pino 0
    montar_gpio_set(1, 5, 1),                            # GPIO5 e entrada
    montar_gpio_set(1, 2, 2),                            # nivel 2
    montar_quadro(1, CMD_GPIO_SET, bytes([0x02])),       # payload curto
    montar_quadro(1, CMD_GPIO_READ, bytes([0x00])),      # GPIO_READ com payload
    montar_adc_read(1, 3),                               # canal inexistente
    montar_quadro(1, CMD_ADC_READ),                      # ADC_READ sem canal
    montar_quadro(1, CMD_ADC_READ, bytes([0, 0])),       # ADC_READ com 2 bytes
], ids=["pino 0", "pino 5", "nivel 2", "gpio tamanho 1", "read com payload", "canal 3", "adc sem canal",
        "adc tamanho 2"])
def test_jiga_gpio_adc_parametro_invalido_nao_altera_saida(quadro):
    """CT09 com os comandos novos: NACK PARAMETRO_INVALIDO, sem dados e nenhuma saida alterada."""
    jiga = JigaSimulada()
    jiga.processar(montar_gpio_set(9, 2, 1))
    _, resposta = jiga.processar(quadro)
    [(_, nack)] = ReceptorQuadros().processar(resposta)
    assert interpretar_resposta(nack).erro == ERRO_PARAMETRO_INVALIDO
    assert jiga.gpio == {1: 0, 2: 1, 3: 0, 4: 0}


def test_jiga_pwm_set_stop_e_reset_all():
    """PWM_STOP e RESET_ALL zeram a razao e mantem a frequencia (espelho do Pwm_Stop/Pwm_SafeState)."""
    jiga = JigaSimulada()
    padrao = (PWM_FREQUENCIA_PADRAO, 0)
    assert jiga.pwm == {1: padrao, 2: padrao, 3: padrao}
    assert jiga.processar(montar_pwm_set(1, 1, 1000, 5000))[1] == montar_ack(1, CMD_PWM_SET)
    assert jiga.processar(montar_pwm_set(2, 3, 50, 7500))[1] == montar_ack(2, CMD_PWM_SET)
    assert jiga.pwm == {1: (1000, 5000), 2: padrao, 3: (50, 7500)}
    assert jiga.processar(montar_pwm_stop(3, 1))[1] == montar_ack(3, CMD_PWM_STOP)
    assert jiga.pwm == {1: (1000, 0), 2: padrao, 3: (50, 7500)}
    assert jiga.processar(montar_quadro(4, CMD_RESET_ALL))[1] == montar_ack(4, CMD_RESET_ALL)
    assert jiga.pwm == {1: (1000, 0), 2: padrao, 3: (50, 0)}


@pytest.mark.parametrize("quadro", [
    montar_pwm_set(1, 0, 1000, 5000),                       # canal 0
    montar_pwm_set(1, 4, 1000, 5000),                       # canal inexistente
    montar_pwm_set(1, 2, 0, 5000),                          # 0 Hz
    montar_pwm_set(1, 2, PWM_FREQUENCIA_MAX + 1, 5000),     # acima de 100 kHz
    montar_pwm_set(1, 2, 1000, PWM_RAZAO_MAX + 1),          # acima de 100 %
    montar_quadro(1, CMD_PWM_SET, bytes([0x02, 0xE8, 0x03])),  # payload curto
    montar_pwm_stop(1, 4),                                  # canal inexistente
    montar_quadro(1, CMD_PWM_STOP),                         # PWM_STOP sem canal
], ids=["canal 0", "canal 4", "0 Hz", "100001 Hz", "razao 10001", "set tamanho 3", "stop canal 4",
        "stop sem canal"])
def test_jiga_pwm_parametro_invalido_nao_altera_saida(quadro):
    """CT09 com o PWM: NACK PARAMETRO_INVALIDO e nenhuma saida alterada."""
    jiga = JigaSimulada()
    jiga.processar(montar_pwm_set(9, 2, 20_000, 1234))
    _, resposta = jiga.processar(quadro)
    [(_, nack)] = ReceptorQuadros().processar(resposta)
    assert interpretar_resposta(nack).erro == ERRO_PARAMETRO_INVALIDO
    assert jiga.pwm == {1: (PWM_FREQUENCIA_PADRAO, 0), 2: (20_000, 1234), 3: (PWM_FREQUENCIA_PADRAO, 0)}


# --- I2C ----------------------------------------------------------------------

def test_quadros_i2c():
    assert montar_i2c_config(1, I2C_PAPEL_ESCRAVO, 0x50, 1)[1:7] == bytes([3, 1, CMD_I2C_CONFIG, 0x00, 0x50, 0x01])
    assert montar_i2c_load(2, 0x10, b"\xde\xad")[1:8] == bytes([4, 2, CMD_I2C_SLAVE_LOAD, 0x10, 2, 0xDE, 0xAD])
    assert montar_i2c_dump(3, 0xFE, 2)[1:6] == bytes([2, 3, CMD_I2C_SLAVE_DUMP, 0xFE, 2])
    assert montar_i2c_xfer(4, 0x68, b"\x0f", 6)[1:8] == bytes([4, 4, CMD_I2C_MASTER_XFER, 0x68, 1, 6, 0x0F])


@pytest.mark.parametrize("montar", [
    lambda: montar_i2c_config(1, 256, 0x50, 0), lambda: montar_i2c_load(1, 0, bytes(256)),
    lambda: montar_i2c_dump(1, -1, 1), lambda: montar_i2c_xfer(1, 0x68, b"", 256),
], ids=["papel 256", "load 256 bytes", "dump offset -1", "xfer nr 256"])
def test_i2c_fora_de_u8(montar):
    with pytest.raises(ValueError):
        montar()


def _resposta(jiga, quadro):
    _, resposta = jiga.processar(quadro)
    [(_, recebido)] = ReceptorQuadros().processar(resposta)
    return interpretar_resposta(recebido)


def test_jiga_i2c_load_dump_sem_config_e_reset_all():
    """O mapa funciona com o barramento desligado; o RESET_ALL desliga e zera."""
    jiga = JigaSimulada()
    assert jiga.processar(montar_i2c_load(1, 0xFC, b"\x01\x02\x03\x04"))[1] == montar_ack(1, CMD_I2C_SLAVE_LOAD)
    assert _resposta(jiga, montar_i2c_dump(2, 0xFB, 5)).dados == b"\x00\x01\x02\x03\x04"
    jiga.processar(montar_i2c_config(3, I2C_PAPEL_ESCRAVO, 0x50, 0))
    assert jiga.i2c_papel == I2C_PAPEL_ESCRAVO
    jiga.processar(montar_quadro(4, CMD_RESET_ALL))
    assert jiga.i2c_papel is None
    assert jiga.i2c_mapa == bytearray(256)


@pytest.mark.parametrize("quadro", [
    montar_i2c_config(1, 2, 0x50, 0),                               # papel 2
    montar_i2c_config(1, I2C_PAPEL_ESCRAVO, 0x07, 0),               # endereco reservado
    montar_i2c_config(1, I2C_PAPEL_ESCRAVO, 0x50, 2),               # velocidade 2
    montar_quadro(1, CMD_I2C_CONFIG, bytes([0, 0x50])),             # tamanho 2
    montar_i2c_load(1, 0xFF, b"\x01\x02"),                          # alem do mapa
    montar_quadro(1, CMD_I2C_SLAVE_LOAD, bytes([0, 3, 0xAA])),      # quantidade nao bate
    montar_quadro(1, CMD_I2C_SLAVE_LOAD, bytes([0, 0])),            # quantidade 0
    montar_i2c_dump(1, 0, 241),                                     # nao cabe na resposta
    montar_i2c_dump(1, 0xFF, 2),                                    # alem do mapa
    montar_quadro(1, CMD_I2C_MASTER_XFER, bytes([0x68, 2, 0, 1])),  # nw nao bate
    montar_i2c_xfer(1, 0x68, b"", 0),                               # nw + nr = 0
], ids=["papel 2", "endereco 0x07", "velocidade 2", "config tamanho 2", "load alem do mapa", "load qtd errada",
        "load qtd 0", "dump 241", "dump alem do mapa", "xfer nw errado", "xfer vazio"])
def test_jiga_i2c_parametro_invalido_nao_altera(quadro):
    """CT09 com o I2C: NACK PARAMETRO_INVALIDO, mapa e papel intactos."""
    jiga = JigaSimulada()
    jiga.processar(montar_i2c_load(9, 0x00, b"\x77"))
    assert _resposta(jiga, quadro).erro == ERRO_PARAMETRO_INVALIDO
    assert jiga.i2c_mapa[0] == 0x77 and jiga.i2c_papel is None


def test_jiga_i2c_mestre():
    """Sem mestre: NAO_CONFIGURADO; endereco sem dispositivo: falta de ACK; com dispositivo: le e escreve."""
    jiga = JigaSimulada()
    assert _resposta(jiga, montar_i2c_xfer(1, 0x68, b"\x0f", 1)).erro == ERRO_NAO_CONFIGURADO
    jiga.processar(montar_i2c_config(2, I2C_PAPEL_MESTRE, 0, 1))
    assert _resposta(jiga, montar_i2c_xfer(3, 0x68, b"\x0f", 1)).erro == ERRO_PERIFERICO
    jiga.i2c_dispositivos[0x68] = bytearray(range(256))
    assert _resposta(jiga, montar_i2c_xfer(4, 0x68, b"\x0f", 2)).dados == b"\x0f\x10"
    assert _resposta(jiga, montar_i2c_xfer(5, 0x68, b"\x20\xaa\xbb")).dados == b""
    assert jiga.i2c_dispositivos[0x68][0x20:0x22] == b"\xaa\xbb"


# --- SPI ----------------------------------------------------------------------

def test_quadros_spi():
    assert montar_spi_config(1, SPI_PAPEL_MESTRE, 3, 1_000_000)[1:10] == bytes(
        [6, 1, CMD_SPI_CONFIG, 0x01, 0x03, 0x40, 0x42, 0x0F, 0x00])
    assert montar_spi_load(2, b"\x11\x22")[1:7] == bytes([3, 2, CMD_SPI_SLAVE_LOAD, 2, 0x11, 0x22])
    assert montar_spi_load(3, b"")[1:5] == bytes([1, 3, CMD_SPI_SLAVE_LOAD, 0])
    assert montar_spi_read(4)[1:4] == bytes([0, 4, CMD_SPI_SLAVE_READ])
    assert montar_spi_xfer(5, b"\x9f\x00")[1:7] == bytes([3, 5, CMD_SPI_MASTER_XFER, 2, 0x9F, 0x00])


@pytest.mark.parametrize("montar", [
    lambda: montar_spi_config(1, 256, 0), lambda: montar_spi_config(1, 0, 0, 2 ** 32),
    lambda: montar_spi_load(1, bytes(256)), lambda: montar_spi_xfer(1, bytes(256)),
], ids=["papel 256", "frequencia 2^32", "load 256 bytes", "xfer 256 bytes"])
def test_spi_fora_do_tamanho(montar):
    with pytest.raises(ValueError):
        montar()


@pytest.mark.parametrize("frequencia, divisor", [
    (27_000_000, 2), (30_000_000, 2), (26_999_999, 4), (1_000_000, 64), (843_750, 64), (843_749, 128),
    (SPI_FREQUENCIA_MIN, 256),
])
def test_spi_divisor_igual_ao_firmware(frequencia, divisor):
    """Os pontos do Test_SpiBus.c e mais alguns: o mestre nunca fica mais rapido que o pedido."""
    assert spi_divisor(frequencia) == divisor


@pytest.mark.parametrize("frequencia", [0, SPI_FREQUENCIA_MIN - 1])
def test_spi_divisor_abaixo_do_minimo(frequencia):
    with pytest.raises(ValueError):
        spi_divisor(frequencia)


def test_descrever_spi():
    assert descrever_spi(1_000_000) == "SPI mestre: 54 MHz / 64 = 843750 Hz"


def test_jiga_spi_escrava():
    """LOAD/READ sem SPI_CONFIG; a transmissao recomeca do byte 0 a cada transacao; o READ esvazia."""
    jiga = JigaSimulada()
    assert jiga.processar(montar_spi_load(1, b"\x11\x22"))[1] == montar_ack(1, CMD_SPI_SLAVE_LOAD)
    assert jiga.transacao_spi(b"\xa1\xa2\xa3") == b"\x11\x22\xff"
    assert jiga.transacao_spi(b"\xb1") == b"\x11"
    resposta = _resposta(jiga, montar_spi_read(2))
    assert resposta.dados == b"\x04\xa1\xa2\xa3\xb1"
    assert resposta.descrever() == "ACK SPI_SLAVE_READ SEQ=2 4 B do DUT: A1 A2 A3 B1"
    assert _resposta(jiga, montar_spi_read(3)).dados == b"\x00"


def test_jiga_spi_recepcao_cheia_descarta_excedente():
    jiga = JigaSimulada()
    jiga.transacao_spi(bytes(200))
    jiga.transacao_spi(bytes([0x55]) * 100)
    dados = _resposta(jiga, montar_spi_read(1)).dados
    assert len(dados) == 1 + SPI_TAMANHO_BUFFER and dados[0] == SPI_TAMANHO_BUFFER
    assert dados[-1] == 0x55


def test_jiga_spi_mestre():
    """Sem mestre: NAO_CONFIGURADO; com loopback o MISO le o MOSI; sem ele, 0xFF (nominal)."""
    jiga = JigaSimulada(loopback=True)
    assert _resposta(jiga, montar_spi_xfer(1, b"\x9f")).erro == ERRO_NAO_CONFIGURADO
    jiga.processar(montar_spi_config(2, SPI_PAPEL_MESTRE, 0, 1_000_000))
    assert _resposta(jiga, montar_spi_xfer(3, b"\x9f\x00\x5a")).dados == b"\x9f\x00\x5a"
    jiga.loopback = False
    assert _resposta(jiga, montar_spi_xfer(4, b"\x9f\x00")).dados == b"\xff\xff"


def test_jiga_spi_reset_all():
    jiga = JigaSimulada()
    jiga.processar(montar_spi_config(1, SPI_PAPEL_MESTRE, 0, 1_000_000))
    jiga.processar(montar_spi_load(2, b"\x11"))
    jiga.transacao_spi(b"\x22")
    jiga.processar(montar_quadro(3, CMD_RESET_ALL))
    assert (jiga.spi_papel, jiga.spi_tx, jiga.spi_rx) == (None, b"", bytearray())


@pytest.mark.parametrize("quadro", [
    montar_spi_config(1, 2, 0),                                            # papel 2
    montar_spi_config(1, SPI_PAPEL_ESCRAVO, 4),                            # modo 4
    montar_spi_config(1, SPI_PAPEL_MESTRE, 0, SPI_FREQUENCIA_MIN - 1),     # mestre lento
    montar_spi_config(1, SPI_PAPEL_MESTRE, 0, 0),                          # mestre sem frequencia
    montar_quadro(1, CMD_SPI_CONFIG, bytes([0, 0])),                       # tamanho 2
    montar_quadro(1, CMD_SPI_SLAVE_LOAD, bytes([3, 0xAA])),                # quantidade nao bate
    montar_quadro(1, CMD_SPI_SLAVE_LOAD, b""),                             # sem quantidade
    montar_quadro(1, CMD_SPI_SLAVE_READ, b"\x00"),                         # read com payload
    montar_spi_xfer(1, b""),                                               # xfer vazio
    montar_quadro(1, CMD_SPI_MASTER_XFER, bytes([2, 0x9F])),               # xfer quantidade nao bate
], ids=["papel 2", "modo 4", "mestre lento", "mestre 0 Hz", "config tamanho 2", "load qtd errada", "load vazio",
        "read com payload", "xfer vazio", "xfer qtd errada"])
def test_jiga_spi_parametro_invalido_nao_altera(quadro):
    """CT09 com o SPI: NACK PARAMETRO_INVALIDO, buffers e papel intactos."""
    jiga = JigaSimulada()
    jiga.processar(montar_spi_load(9, b"\x77"))
    jiga.transacao_spi(b"\x66")
    assert _resposta(jiga, quadro).erro == ERRO_PARAMETRO_INVALIDO
    assert (jiga.spi_papel, jiga.spi_tx, jiga.spi_rx) == (None, b"\x77", bytearray(b"\x66"))


def test_jiga_ocupado_no_mesmo_bloco():
    """Dois comandos no mesmo envio: o 2o recebe OCUPADO antes do ACK do 1o, e nao e executado."""
    jiga = JigaSimulada()
    resultados, respostas = jiga.processar(montar_dac_set(1, 1, 1024) + montar_quadro(2, CMD_RESET_ALL))
    assert resultados == [QUADRO_OK, QUADRO_OK]
    assert respostas == montar_nack(2, CMD_RESET_ALL, ERRO_OCUPADO) + montar_ack(1, CMD_DAC_SET)
    assert jiga.dac[1] == 1024


def test_jiga_ocupado_nao_fica_no_cache():
    """Depois do pendente, o mesmo SEQ que recebeu OCUPADO e executado de verdade."""
    jiga = JigaSimulada()
    jiga.processar(montar_dac_set(1, 1, 1024) + montar_quadro(2, CMD_RESET_ALL))
    assert jiga.processar(montar_quadro(2, CMD_RESET_ALL))[1] == montar_ack(2, CMD_RESET_ALL)
    assert jiga.dac[1] == 0


def test_jiga_retransmissao_do_pendente_e_ignorada():
    """O mesmo comando duas vezes no mesmo envio: uma unica resposta, executado uma vez."""
    jiga = JigaSimulada()
    comando = montar_dac_set(1, 1, 500)
    _, respostas = jiga.processar(comando + comando)
    assert respostas == montar_ack(1, CMD_DAC_SET)


def test_jiga_ping_atendido_com_comando_pendente():
    jiga = JigaSimulada()
    _, respostas = jiga.processar(montar_dac_set(1, 1, 500) + montar_quadro(2, CMD_PING))
    assert respostas == montar_ack_ping(2) + montar_ack(1, CMD_DAC_SET)


def test_jiga_sessao_nova_esquece_o_pendente():
    jiga = JigaSimulada()
    jiga.pendente = Quadro(seq=1, cmd=CMD_DAC_SET, payload=bytes([1, 0, 1]))
    jiga.nova_sessao()
    assert jiga.pendente is None


def test_jiga_quadro_grande_em_blocos_de_64():
    """Payload maximo chega em 4 blocos (64+64+64+54): a resposta sai uma vez, no fim."""
    jiga = JigaSimulada()
    resultados, respostas = jiga.processar(montar_quadro(1, CMD_INEXISTENTE, bytes(PAYLOAD_MAX)))
    assert resultados == [QUADRO_OK]
    assert respostas == montar_nack(1, CMD_INEXISTENTE, ERRO_CMD_DESCONHECIDO)


# --- casos de teste enviados para a placa ------------------------------------

@pytest.mark.parametrize("caso", CASOS_TESTE, ids=lambda c: c.nome)
def test_casos_de_teste_coerentes(caso):
    """O log e a resposta nominal de cada caso batem com a jiga simulada (com loopback se o caso pedir),
    e o 'conferir' aceita essa resposta."""
    assert simular_caso(caso) == caso.esperado
    _, resposta = simular(caso)
    assert resposta == caso.resposta_esperada()
    assert caso.conferir_resposta(resposta) is None


def test_casos_em_sequencia_nao_interferem():
    """Na placa os casos rodam um atras do outro, com o cache de SEQ ligado: cada caso usa SEQs proprios."""
    jiga = JigaSimulada()
    for caso in casos_a_executar(loopback=False):
        assert simular(caso, jiga) == (caso.esperado, caso.resposta_esperada()), caso.nome


def test_casos_em_sequencia_com_loopback():
    """Com os jumpers, os casos sem fio leem o que os anteriores aplicaram: so o 'conferir' vale."""
    jiga = JigaSimulada(loopback=True)
    for caso in casos_a_executar(loopback=True):
        resultados, resposta = simular(caso, jiga)
        assert resultados == caso.esperado, caso.nome
        assert caso.conferir_resposta(resposta) is None, caso.nome


def test_bateria_termina_no_reset_all():
    assert [caso.nome for caso in casos_a_executar()][-1] == "RESET_ALL no fim"
    assert [caso.nome for caso in casos_a_executar(loopback=True)][-1] == "RESET_ALL no fim"
    assert not any(caso.requer_loopback for caso in casos_a_executar())


def test_casos_de_loopback():
    nomes = [caso.nome for caso in CASOS_TESTE if caso.requer_loopback]
    assert nomes == ["CT10 DAC1 -> A0", "CT10 DAC2 -> A1", "CT11 GPIO1-4 -> GPIO5-8"]


def test_pontos_do_ct10():
    pontos = pontos_ct10()
    assert len(pontos) == 11
    assert (pontos[0], pontos[-1]) == DAC_FAIXA_UTIL
    assert pontos[5] == 2048


def _caso(nome):
    return next(caso for caso in CASOS_TESTE if caso.nome == nome)


def _resposta_ct10_com(desvio, ponto=3):
    """Resposta nominal do CT10 do DAC1 com a leitura de um ponto deslocada."""
    caso = _caso("CT10 DAC1 -> A0")
    respostas = list(caso.respostas)
    indice = 2 * ponto + 1  # respostas: DAC_SET, ADC_READ, DAC_SET, ADC_READ...
    [(_, quadro)] = ReceptorQuadros().processar(respostas[indice])
    valor = int.from_bytes(quadro.payload[1:3], "little") + desvio
    respostas[indice] = montar_ack(quadro.seq, CMD_ADC_READ, bytes([0]) + valor.to_bytes(2, "little"))
    return caso, b"".join(respostas)


@pytest.mark.parametrize("desvio", [TOLERANCIA_ADC, -TOLERANCIA_ADC])
def test_ct10_aceita_no_limite_da_tolerancia(desvio):
    caso, resposta = _resposta_ct10_com(desvio)
    assert caso.conferir_resposta(resposta) is None


@pytest.mark.parametrize("desvio", [TOLERANCIA_ADC + 1, -(TOLERANCIA_ADC + 1)])
def test_ct10_reprova_fora_da_tolerancia(desvio):
    caso, resposta = _resposta_ct10_com(desvio)
    motivo = caso.conferir_resposta(resposta)
    assert motivo.startswith("resposta 8: A0 leu")
    assert f"+/- {TOLERANCIA_ADC}" in motivo


def test_ct10_reprova_canal_solto():
    """Sem o jumper, o A0 le o pino solto (aqui, 0): reprova logo no primeiro ponto."""
    caso = _caso("CT10 DAC1 -> A0")
    _, resposta = simular(caso, JigaSimulada())
    assert caso.conferir_resposta(resposta).startswith("resposta 2: A0 leu 0; esperado 124")


def test_ct11_reprova_entrada_trocada():
    caso = _caso("CT11 GPIO1-4 -> GPIO5-8")
    jiga = JigaSimulada()
    jiga.entradas = 0x01
    _, resposta = simular(caso, jiga)
    assert "mascara 0x01; esperado 0x00" in caso.conferir_resposta(resposta)


def test_gpio_read_sem_fio_aceita_qualquer_mascara_de_4_bits():
    caso = _caso("GPIO_READ")
    for mascara in (0x00, 0x0F, 0x06):
        assert caso.conferir_resposta(montar_ack(25, CMD_GPIO_READ, bytes([mascara]))) is None
    assert "bits 4 a 7" in caso.conferir_resposta(montar_ack(25, CMD_GPIO_READ, bytes([0x10])))
    assert "B de dados" in caso.conferir_resposta(montar_ack(25, CMD_GPIO_READ, bytes([0, 0])))


def test_adc_read_sem_fio_confere_canal_e_12_bits():
    caso = _caso("ADC_READ")
    assert caso.conferir_resposta(montar_ack(27, CMD_ADC_READ, bytes([0, 0xFF, 0x0F]))) is None
    assert "canal 1" in caso.conferir_resposta(montar_ack(27, CMD_ADC_READ, bytes([1, 0, 0])))
    assert "12 bits" in caso.conferir_resposta(montar_ack(27, CMD_ADC_READ, bytes([0, 0x00, 0x10])))
    assert "NACK" in caso.conferir_resposta(montar_nack(27, CMD_ADC_READ, ERRO_PARAMETRO_INVALIDO))


def test_conferir_reprova_falta_de_resposta():
    caso = _caso("CT11 GPIO1-4 -> GPIO5-8")
    resposta = caso.resposta_esperada()
    assert caso.conferir_resposta(b"") == "0 resposta(s); esperado 80"
    assert caso.conferir_resposta(resposta[:-1]) == "quadro invalido ou incompleto na resposta"


def test_avaliacao_traz_o_motivo():
    caso = _caso("ADC_READ")
    avaliacao = avaliar_caso(caso, [LOG_FIRMWARE[QUADRO_OK]], b"")
    assert not avaliacao.resposta_ok
    assert avaliacao.motivo == "0 resposta(s); esperado 1"


def test_descrever_log_agrupa_repeticoes():
    assert descrever_log([QUADRO_OK] * 3 + [TIMEOUT]) == \
        f"3x {LOG_FIRMWARE[QUADRO_OK]}, {LOG_FIRMWARE[TIMEOUT]}"


def test_descrever_resposta_com_maximo():
    fluxo = b"".join(montar_ack_ping(seq) for seq in range(6))
    texto = descrever_resposta(fluxo, maximo=2)
    assert texto.count("ACK PING") == 2
    assert texto.endswith("... (+4 respostas)")


def test_avaliar_caso():
    caso = CASOS_TESTE[0]
    linhas = ["[TASK -> COMM] Sessao nova", LOG_FIRMWARE[QUADRO_OK] + ": LEN=0 SEQ=1 CMD=0x01"]
    assert avaliar_caso(caso, linhas, caso.resposta_esperada()).aprovado
    sem_resposta = avaliar_caso(caso, linhas, b"")
    assert sem_resposta.log_ok and not sem_resposta.resposta_ok and not sem_resposta.aprovado
    assert not avaliar_caso(caso, [], caso.resposta_esperada()).log_ok


@pytest.mark.parametrize("resultado", [QUADRO_OK, ERRO_CRC, ERRO_LEN, TIMEOUT])
def test_log_do_firmware_mapeia_resultado(resultado):
    linha = LOG_FIRMWARE[resultado] + ": detalhe qualquer\r"
    assert resultado_do_log(linha) == resultado


def test_log_que_nao_e_do_parser():
    assert resultado_do_log("[TASK -> COMM] Inicio da Task") is None
