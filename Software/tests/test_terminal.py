"""Testes das funcoes auxiliares do terminal (sem porta serial)."""

import pytest

from terminal_usb import formatar_hex, interpretar_hex, interpretar_pwm


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
