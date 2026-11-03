"""Testes do DUT de referencia simulado (espelho do firmware DUT_ESP32S3) e dos casos do CT20."""

import pytest

from dut import DUT_IDENTIFICACAO, DutSimulado, linha_dut, resposta_dut
from protocolo import (
    CASOS_TESTE, CMD_I2C_MASTER_XFER, CMD_RESET_ALL, CMD_SPI_MASTER_XFER, CMD_UART_READ, I2C_PAPEL_ESCRAVO,
    I2C_PAPEL_MESTRE, ERRO_PERIFERICO, ReceptorQuadros, JigaSimulada, SPI_PAPEL_ESCRAVO, SPI_PAPEL_MESTRE,
    casos_a_executar, interpretar_resposta, montar_i2c_config, montar_i2c_load, montar_i2c_xfer, montar_quadro,
    montar_spi_config, montar_spi_load, montar_spi_read, montar_spi_xfer, montar_uart_config, montar_uart_read,
    montar_uart_write, simular,
)
from versao import __version__


def _executa(dut, linha):
    return dut.executar(linha).decode()


# --- interpretador (mesmos vetores do DUT_ESP32S3/test_host/test_dut.c) ------------------------------------

@pytest.mark.parametrize("linha, esperado", [
    ("PING", f"OK DUT ESP32-S3 {__version__}\n"),
    ("PING 1", "ERR ARG\n"),
    ("RESET", "ERR CMD\n"),
    ("ping", "ERR CMD\n"),
    ("OFF", "OK\n"),
    ("I2CM 78 10 1", "ERR ARG\n"),
    ("I2CM 07 10 1", "ERR ARG\n"),
    ("I2CM 50 1 1", "ERR ARG\n"),
    ("I2CM 50 1G 1", "ERR ARG\n"),
    ("I2CM 50 10 240", "ERR ARG\n"),
    ("I2CM 50 - 0", "ERR ARG\n"),
    ("I2CM 50 10", "ERR ARG\n"),
    ("I2CS 80", "ERR ARG\n"),
    ("I2CS_LOAD FF 0102", "ERR ARG\n"),
    ("I2CS_LOAD 00 -", "ERR ARG\n"),
    ("I2CS_DUMP FF 2", "ERR ARG\n"),
    ("I2CS_DUMP 00 0", "ERR ARG\n"),
    ("SPIM 4 1000000 9F", "ERR ARG\n"),
    ("SPIM 0 999 9F", "ERR ARG\n"),
    ("SPIM 0 10000001 9F", "ERR ARG\n"),
    ("SPIM 0 1000000 -", "ERR ARG\n"),
    ("SPIM 0 1k 9F", "ERR ARG\n"),
    ("I2CM 50 10 4 9 9", "ERR ARG\n"),
    ("A" * 513, "ERR ARG\n"),
])
def test_interpretador(linha, esperado):
    assert _executa(DutSimulado(), linha) == esperado


def test_linha_vazia_nao_tem_resposta():
    assert DutSimulado().executar("  ") == b""


def test_uart_junta_linhas_e_ignora_cr():
    dut = DutSimulado()
    assert dut.receber_uart(b"PI") == b""
    assert dut.receber_uart(b"NG\r\nOFF\n") == (DUT_IDENTIFICACAO + "\nOK\n").encode()


def test_i2cs_load_e_dump():
    dut = DutSimulado()
    assert _executa(dut, "I2CS_LOAD FE CAFE") == "OK\n"
    assert _executa(dut, "I2CS_DUMP FD 3") == "OK 00CAFE\n"


def test_spis_carga_vale_na_proxima_transacao():
    """Como no firmware: a carga do SPIS_LOAD entra na transacao que a task arma depois dele."""
    dut = DutSimulado()
    _executa(dut, "SPIS 0")
    _executa(dut, "SPIS_LOAD 1234")
    assert dut.spi_jiga_mestre(b"\xaa\xbb") == b"\xff\xff"   # transacao ja armada sem carga
    assert dut.spi_jiga_mestre(b"\xcc") == b"\x12"
    assert _executa(dut, "SPIS_READ") == "OK AABBCC\n"
    assert _executa(dut, "SPIS_READ") == "OK\n"


def test_estado_ocupado_e_off():
    dut = DutSimulado()
    _executa(dut, "I2CS 28")
    assert _executa(dut, "I2CM 50 10 1") == "ERR ESTADO\n"
    _executa(dut, "SPIS 0")
    assert _executa(dut, "SPIM 0 1000000 9F") == "ERR ESTADO\n"
    _executa(dut, "OFF")
    assert dut.i2c_endereco is None and dut.spi_modo is None


def test_sem_jiga_nack_e_miso_solto():
    dut = DutSimulado()
    assert _executa(dut, "I2CM 50 10 4") == "ERR NACK\n"
    assert _executa(dut, "SPIM 0 1000000 9F00") == "OK 0000\n"


def test_linha_e_resposta():
    assert linha_dut("I2CM", "50", b"\x10", 4) == b"I2CM 50 10 4\n"
    assert linha_dut("I2CM", "50", b"", 2) == b"I2CM 50 - 2\n"
    assert resposta_dut(b"\xde\xad") == b"OK DEAD\n"
    assert resposta_dut() == b"OK\n"
    assert resposta_dut(erro="NACK") == b"ERR NACK\n"


# --- DUT ligado na jiga simulada --------------------------------------------------------------------------

def _resposta(jiga, quadro):
    _, resposta = jiga.processar(quadro)
    [(_, recebido)] = ReceptorQuadros().processar(resposta)
    return interpretar_resposta(recebido)


def _dut(jiga, seq, linha):
    """UART_WRITE da linha e UART_READ: texto que o DUT respondeu."""
    jiga.processar(montar_uart_write(seq, linha.encode() + b"\n"))
    dados = _resposta(jiga, montar_uart_read(seq + 1)).dados
    return dados[1:].decode()


def test_dut_mestre_i2c_le_a_jiga_escrava_e_o_ponteiro_continua():
    """O ponteiro da jiga escrava avanca na leitura: uma leitura sem ponteiro continua de onde parou."""
    jiga = JigaSimulada(dut=DutSimulado())
    jiga.processar(montar_uart_config(1, 115200))
    jiga.processar(montar_i2c_load(2, 0x10, bytes(range(1, 9))))
    assert _dut(jiga, 3, "I2CM 50 10 4") == "ERR NACK\n"   # jiga ainda nao e escrava
    jiga.processar(montar_i2c_config(5, I2C_PAPEL_ESCRAVO, 0x50, 0))
    assert _dut(jiga, 6, "I2CM 51 10 4") == "ERR NACK\n"   # outro endereco
    assert _dut(jiga, 8, "I2CM 50 10 4") == "OK 01020304\n"
    assert _dut(jiga, 10, "I2CM 50 - 2") == "OK 0506\n"


def test_jiga_mestra_i2c_le_o_dut_sem_mover_o_ponteiro():
    jiga = JigaSimulada(dut=DutSimulado())
    jiga.processar(montar_uart_config(1, 115200))
    jiga.processar(montar_i2c_config(2, I2C_PAPEL_MESTRE, 0, 0))
    assert _resposta(jiga, montar_i2c_xfer(3, 0x28, b"\x00", 1)).erro == ERRO_PERIFERICO   # DUT nao e escravo
    _dut(jiga, 4, "I2CS 28")
    jiga.processar(montar_i2c_xfer(6, 0x28, b"\x40\xb1\xb2"))   # o ponteiro fica em 0x42, depois dos dados
    jiga.processar(montar_i2c_xfer(7, 0x28, b"\x40"))              # so o ponteiro
    assert _resposta(jiga, montar_i2c_xfer(8, 0x28, b"", 2)).dados == b"\xb1\xb2"
    assert _resposta(jiga, montar_i2c_xfer(9, 0x28, b"", 1)).dados == b"\xb1"   # de novo do ponteiro


def test_spi_nos_dois_sentidos():
    jiga = JigaSimulada(dut=DutSimulado())
    jiga.processar(montar_uart_config(1, 115200))
    jiga.processar(montar_spi_config(2, SPI_PAPEL_ESCRAVO, 0))
    jiga.processar(montar_spi_load(3, b"\xc0"))
    assert _dut(jiga, 4, "SPIM 0 1000000 0102") == "OK C0FF\n"
    assert _resposta(jiga, montar_spi_read(6)).dados == b"\x02\x01\x02"

    _dut(jiga, 7, "SPIS 0")
    jiga.processar(montar_spi_config(9, SPI_PAPEL_MESTRE, 0, 1_000_000))
    assert _resposta(jiga, montar_spi_xfer(10, b"\x55")).dados == b"\xff"
    assert _dut(jiga, 11, "SPIS_READ") == "OK 55\n"


def test_uart_em_outro_baud_o_dut_nao_entende():
    jiga = JigaSimulada(dut=DutSimulado())
    jiga.processar(montar_uart_config(1, 9600))
    assert _dut(jiga, 2, "PING") == ""


def test_reset_all_nao_mexe_no_dut():
    """O RESET_ALL desliga a jiga; o DUT continua como estava (so o OFF libera os barramentos dele)."""
    jiga = JigaSimulada(dut=DutSimulado())
    jiga.processar(montar_uart_config(1, 115200))
    _dut(jiga, 2, "I2CS 28")
    jiga.processar(montar_quadro(4, CMD_RESET_ALL))
    assert jiga.dut.i2c_endereco == 0x28 and jiga.i2c_endereco is None


# --- casos do CT20 ----------------------------------------------------------------------------------------

def test_casos_do_ct20():
    nomes = [caso.nome for caso in CASOS_TESTE if caso.requer_dut]
    assert nomes == ["CT20 DUT PING", "CT20 I2C jiga escrava", "CT20 I2C jiga mestra", "CT20 SPI jiga escrava",
                     "CT20 SPI jiga mestra"]
    assert not any(caso.requer_dut and caso.requer_loopback for caso in CASOS_TESTE)


def test_ct20_so_entra_com_o_dut():
    assert not any(caso.requer_dut for caso in casos_a_executar())
    assert [caso.nome for caso in casos_a_executar(dut=True)][-1] == "RESET_ALL no fim"


def test_casos_com_o_dut_em_sequencia_nao_interferem():
    """Na placa os casos rodam um atras do outro com o mesmo DUT: cada caso deixa o DUT livre (OFF)."""
    jiga = JigaSimulada(dut=DutSimulado())
    for caso in casos_a_executar(dut=True):
        assert simular(caso, jiga) == (caso.esperado, caso.resposta_esperada()), caso.nome
    assert jiga.dut.i2c_endereco is None and jiga.dut.spi_modo is None


def test_ct20_termina_com_o_dut_livre():
    for caso in CASOS_TESTE:
        if caso.requer_dut and caso.nome in ("CT20 I2C jiga mestra", "CT20 SPI jiga mestra"):
            ultimo = [dados for tipo, dados in caso.passos if tipo == "enviar"][-2]
            assert b"OFF\n" in ultimo, caso.nome
    assert CMD_UART_READ and CMD_I2C_MASTER_XFER and CMD_SPI_MASTER_XFER
