"""Testes do protocolo do lado do PC (CRC, montagem de quadro e receptor)."""

import binascii
import random

import pytest

from protocolo import (
    CASOS_TESTE, CMD_PING, ERRO_CRC, ERRO_LEN, LENDO, LOG_FIRMWARE, OCIOSO, PAYLOAD_MAX,
    QUADRO_OK, SOF, TAMANHO_QUADRO_MAX, TIMEOUT, ReceptorQuadros, crc16_ccitt, montar_quadro,
    resultado_do_log, simular_caso,
)


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


# --- casos de teste enviados para a placa ------------------------------------

@pytest.mark.parametrize("caso", CASOS_TESTE, ids=lambda c: c.nome)
def test_casos_de_teste_coerentes(caso):
    """O resultado esperado de cada caso bate com o receptor do PC."""
    assert simular_caso(caso) == caso.esperado


@pytest.mark.parametrize("resultado", [QUADRO_OK, ERRO_CRC, ERRO_LEN, TIMEOUT])
def test_log_do_firmware_mapeia_resultado(resultado):
    linha = LOG_FIRMWARE[resultado] + ": detalhe qualquer\r"
    assert resultado_do_log(linha) == resultado


def test_log_que_nao_e_do_parser():
    assert resultado_do_log("[TASK -> COMM] Inicio da Task") is None
