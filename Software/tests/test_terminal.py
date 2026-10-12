"""Testes das funcoes auxiliares do terminal (sem porta serial)."""

import pytest

from terminal_usb import formatar_hex, interpretar_hex


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
