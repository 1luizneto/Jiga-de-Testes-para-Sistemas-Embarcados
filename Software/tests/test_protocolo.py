"""Testes do protocolo do lado do PC (CRC, quadros, receptor, respostas e casos de teste)."""

import binascii
import random

import pytest

from protocolo import (
    CASOS_TESTE, CMD_ADC_READ, CMD_DAC_SET, CMD_GPIO_READ, CMD_GPIO_SET, CMD_INEXISTENTE, CMD_PING,
    CMD_RESET_ALL, DAC_FAIXA_UTIL, DAC_VALOR_MAX, ERRO_CMD_DESCONHECIDO, ERRO_CRC, ERRO_LEN,
    ERRO_OCUPADO, ERRO_PARAMETRO_INVALIDO, LENDO, LOG_FIRMWARE, OCIOSO, PAYLOAD_MAX, QUADRO_OK, SOF,
    TAMANHO_QUADRO_MAX, TIMEOUT, TOLERANCIA_ADC, JigaSimulada, Quadro, ReceptorQuadros, avaliar_caso,
    casos_a_executar, crc16_ccitt, descrever_log, descrever_resposta, descrever_respostas,
    interpretar_resposta, montar_ack, montar_ack_ping, montar_adc_read, montar_dac_set,
    montar_gpio_read, montar_gpio_set, montar_nack, montar_quadro, pontos_ct10, resultado_do_log,
    simular, simular_caso,
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
