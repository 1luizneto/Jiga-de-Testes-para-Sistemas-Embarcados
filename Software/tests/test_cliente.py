"""ClienteJiga com o transporte simulado: SEQ, retransmissao (CT05), NACK e os comandos de rotina."""

import pytest

from cliente import ClienteJiga, ErroComunicacao, ErroNack, TransporteSimulado
from protocolo import (
    CMD_DAC_SET, ERRO_OCUPADO, ERRO_PARAMETRO_INVALIDO, ERRO_ROTINA_INVALIDA, ESTADO_ABORTADA, ESTADO_CONCLUIDA,
    ESTADO_EM_EXECUCAO, JigaSimulada, ReceptorQuadros, interpretar_resultados, montar_ack, montar_adc_read, montar_dac_set,
    montar_gpio_read, passo, passo_espera,
)
from versao import __version__


def _cliente(**kwargs):
    transporte = TransporteSimulado(**kwargs)
    cliente = ClienteJiga(transporte)
    cliente.abrir()
    return cliente, transporte


def _seqs(enviados):
    return [q.seq for dados in enviados for _, q in ReceptorQuadros().processar(dados)]


# 1. PING devolve a versao do protocolo e a do firmware
def test_ping_devolve_versoes():
    cliente, _ = _cliente()
    assert cliente.ping() == (1, __version__)


# 2. Cada comando novo usa o SEQ seguinte (e volta de 255 para 0)
def test_seq_incrementa_e_da_a_volta():
    cliente, transporte = _cliente()
    cliente.seq = 254
    for _ in range(3):
        cliente.ping()
    assert _seqs(transporte.enviados) == [254, 255, 0]


# 3. CT05: o transporte descarta a primeira resposta; o cliente retransmite com o mesmo SEQ e o comando
# executa uma unica vez (a jiga reenvia a resposta guardada)
def test_ct05_retransmite_com_o_mesmo_seq_e_executa_uma_vez():
    jiga = JigaSimulada()
    cliente, transporte = _cliente(jiga=jiga, descartar=1)
    execucoes = []
    original = jiga._executar
    jiga._executar = lambda quadro: execucoes.append(quadro.cmd) or original(quadro)
    cliente.dac_set(1, 2048)
    assert _seqs(transporte.enviados) == [0, 0]
    assert execucoes == [CMD_DAC_SET]
    assert cliente.retransmissoes == 1 and transporte.descartadas == 1
    assert jiga.dac[1] == 2048


# 4. Sem resposta em nenhuma das 3 tentativas: ErroComunicacao (nada de perda silenciosa)
def test_sem_resposta_depois_das_tentativas():
    cliente, transporte = _cliente(descartar=3)
    with pytest.raises(ErroComunicacao, match="3 tentativas"):
        cliente.ping()
    assert len(transporte.enviados) == 3 and cliente.retransmissoes == 2


# 5. Duas respostas perdidas ainda se recuperam na terceira tentativa
def test_recupera_na_terceira_tentativa():
    cliente, _ = _cliente(descartar=2)
    assert cliente.ping()[0] == 1
    assert cliente.retransmissoes == 2


# 6. NACK vira ErroNack com o codigo do erro
def test_nack_vira_erro_nack():
    cliente, _ = _cliente()
    with pytest.raises(ErroNack) as erro:
        cliente.dac_set(3, 0)
    assert erro.value.erro == ERRO_PARAMETRO_INVALIDO


# 7. Comandos diretos com os dados ja interpretados
def test_comandos_diretos_interpretados():
    jiga = JigaSimulada(loopback=True)
    cliente, _ = _cliente(jiga=jiga)
    cliente.reset_all()
    cliente.dac_set(1, 1234)
    assert cliente.adc_read(0) == 1234
    cliente.gpio_set(2, 1)
    assert cliente.gpio_read() == 0b0010
    cliente.i2c_config(0, 0x50, 0)
    cliente.i2c_slave_load(0x10, b"\xDE\xAD")
    assert cliente.i2c_slave_dump(0x10, 2) == b"\xDE\xAD"
    cliente.uart_config(115200)
    cliente.uart_write(b"ola")
    assert cliente.uart_read() == b"ola"
    cliente.spi_config(0, 0)
    cliente.spi_slave_load(b"\x01\x02")
    assert cliente.spi_slave_read() == b""


# 8. Rotina inteira pelo cliente: RUN_ROUTINE, STATUS ate concluir (as esperas andam o relogio simulado) e
# READ_RESULT com os registros
def test_rotina_pelo_cliente():
    jiga = JigaSimulada(loopback=True)
    cliente, _ = _cliente(jiga=jiga)
    passos = [passo(montar_dac_set(0, 1, 2000)), passo_espera(50_000), passo(montar_adc_read(0, 0))]
    cliente.run_routine(9, 1000, passos)
    assert cliente.status().estado == ESTADO_EM_EXECUCAO
    cliente.aguardar(0.06)
    status = cliente.status()
    assert status.estado == ESTADO_CONCLUIDA and status.passos == 3 and status.identificador == 9
    identificador, dados = cliente.read_result()
    [registro] = interpretar_resultados(dados)
    assert identificador == 9 and registro.indice == 2 and registro.instante_us == 50_000
    assert registro.dados == bytes([0]) + (2000).to_bytes(2, "little")


# 9. Rotina invalida: ErroNack ROTINA_INVALIDA com o indice do passo
def test_rotina_invalida_traz_o_indice():
    cliente, _ = _cliente()
    with pytest.raises(ErroNack) as erro:
        cliente.run_routine(1, 1000, [passo_espera(10), bytes([CMD_DAC_SET, 9, 0, 0])])
    assert erro.value.erro == ERRO_ROTINA_INVALIDA and erro.value.resposta.dados == bytes([1])


# 10. Durante a rotina os comandos diretos recebem OCUPADO e o ABORT interrompe
def test_ocupado_e_abort():
    cliente, _ = _cliente()
    cliente.run_routine(2, 10_000, [passo_espera(5_000_000)])
    with pytest.raises(ErroNack) as erro:
        cliente.gpio_read()
    assert erro.value.erro == ERRO_OCUPADO
    cliente.abort()
    assert cliente.status().estado == ESTADO_ABORTADA


# 11. READ_RESULT em varias partes: a area inteira chega junta (mais de 236 B)
def test_read_result_em_partes():
    jiga = JigaSimulada()
    cliente, _ = _cliente(jiga=jiga)
    passos = [passo(montar_gpio_read(0))] * 60
    cliente.run_routine(3, 1000, passos)
    _, dados = cliente.read_result()
    registros = interpretar_resultados(dados)
    assert len(dados) == 60 * 7 and [r.indice for r in registros] == list(range(60))


# 12. Resposta com SEQ diferente (atrasada, de outro comando) e ignorada
def test_resposta_de_outro_seq_e_ignorada():
    cliente, transporte = _cliente()
    transporte._buffer = montar_dac_set(77, 1, 0)[:0] + bytes.fromhex("7E 00 4D 81") + b"\x00\x00"
    assert cliente.ping()[0] == 1
