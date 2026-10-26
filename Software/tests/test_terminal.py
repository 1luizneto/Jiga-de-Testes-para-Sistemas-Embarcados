"""Testes das funcoes auxiliares do terminal (sem porta serial)."""

import pytest

from protocolo import montar_i2c_config, montar_i2c_dump, montar_i2c_load, montar_i2c_xfer
from terminal_usb import formatar_hex, interpretar_hex, interpretar_pwm, montar_comando_i2c


@pytest.mark.parametrize("texto, esperado", [
    ("7E 01 0a", b"\x7e\x01\x0a"),
    ("7E010A", b"\x7e\x01\x0a"),
    ("0x7E, 0x01", b"\x7e\x01"),
    ("", b""),
])
def test_interpretar_hex(texto, esperado):
    assert interpretar_hex(texto) == esperado


def test_interpretar_hex_invalido():
    with pytest.raises(ValueError):
        interpretar_hex("7G")


def test_formatar_hex_quebra_em_16_bytes():
    linhas = formatar_hex(bytes(range(20))).splitlines()
    assert len(linhas) == 2
    assert linhas[0].startswith("  0000  00 01 02")
    assert linhas[1].startswith("  0010  10 11 12 13")


def test_formatar_hex_ascii():
    assert formatar_hex(b"Jiga\x00").endswith("|Jiga.|")


@pytest.mark.parametrize("texto, esperado", [
    ("1 1000 50", (1, 1000, 5000)),
    ("3 20000 12,34", (3, 20000, 1234)),
    ("2 1 0.5", (2, 1, 50)),
    ("4 0 200", (4, 0, 20000)),   # fora da faixa: vai para a jiga, que responde NACK
])
def test_interpretar_pwm(texto, esperado):
    assert interpretar_pwm(texto) == esperado


@pytest.mark.parametrize("texto", ["", "1 1000", "1 1000 50 9", "um 1000 50", "1 1k 50"])
def test_interpretar_pwm_invalido(texto):
    with pytest.raises(ValueError):
        interpretar_pwm(texto)


@pytest.mark.parametrize("comando, argumentos, esperado", [
    ("config", "0 50 1", montar_i2c_config(7, 0, 0x50, 1)),
    ("load", "10 DE AD BE", montar_i2c_load(7, 0x10, bytes.fromhex("DEADBE"))),
    ("dump", "FF 1", montar_i2c_dump(7, 0xFF, 1)),
    ("xfer", "68 2 0F", montar_i2c_xfer(7, 0x68, b"\x0f", 2)),
    ("xfer", "68 0 20 AA", montar_i2c_xfer(7, 0x68, b"\x20\xaa", 0)),
])
def test_montar_comando_i2c(comando, argumentos, esperado):
    assert montar_comando_i2c(7, comando, argumentos) == esperado


@pytest.mark.parametrize("comando, argumentos", [
    ("config", "0 50"), ("load", "10"), ("dump", "10"), ("xfer", "68"), ("ler", "10 4"), ("config", "0 5G 0"),
])
def test_montar_comando_i2c_invalido(comando, argumentos):
    with pytest.raises(ValueError):
        montar_comando_i2c(7, comando, argumentos)
