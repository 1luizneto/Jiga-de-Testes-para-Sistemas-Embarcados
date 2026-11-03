"""Garante que firmware, software e CHANGELOG estao na mesma versao (SemVer)."""

import re
from pathlib import Path

from versao import __version__

RAIZ = Path(__file__).resolve().parents[2]
VERSION_H = RAIZ / "projeto" / "App" / "System" / "Inc" / "Version.h"
CHANGELOG = RAIZ / "CHANGELOG.md"
VERSAO_DUT = RAIZ / "DUT_ESP32S3" / "main" / "versao_dut.h"

SEMVER = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")


def _define(nome):
    texto = VERSION_H.read_text(encoding="utf-8")
    return re.search(rf"#define\s+{nome}\s+\(?\"?([^\")\s]+?)U?\"?\)?\s*$", texto, re.M).group(1)


def test_versao_e_semver():
    assert SEMVER.match(__version__)


def test_firmware_e_software_na_mesma_versao():
    assert _define("dFW_VERSION_STRING") == __version__


def test_major_minor_patch_batem_com_a_string():
    numeros = tuple(_define(f"dFW_VERSION_{campo}") for campo in ("MAJOR", "MINOR", "PATCH"))
    assert ".".join(numeros) == _define("dFW_VERSION_STRING")


def test_changelog_tem_a_versao_atual():
    assert f"## [{__version__}]" in CHANGELOG.read_text(encoding="utf-8")


def test_dut_na_mesma_versao():
    """O firmware do DUT de referencia (ESP32-S3) sai junto com a jiga: mesma versao."""
    texto = VERSAO_DUT.read_text(encoding="utf-8")
    assert re.search(r'#define\s+dDUT_VERSAO\s+"([^"]+)"', texto).group(1) == __version__
