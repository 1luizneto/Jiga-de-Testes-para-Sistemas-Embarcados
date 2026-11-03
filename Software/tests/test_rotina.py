"""Rotinas: montagem (passos, RUN_ROUTINE), validacao (espelho do Rotina_Carrega), interpretacao e a execucao na
jiga simulada (espelho do Rotina_Executa), com os mesmos vetores dos testes Unity (Test_Exec.c, 60 a 76)."""

import pytest

from protocolo import (
    CMD_ABORT, CMD_GPIO_SET, CMD_READ_RESULT, CMD_ROUTINE_STATUS, CMD_RUN_ROUTINE, ERRO_OCUPADO,
    ERRO_PARAMETRO_INVALIDO, ERRO_ROTINA_INVALIDA, ERRO_SEM_RESULTADO, ESTADO_ABORTADA, ESTADO_CONCLUIDA,
    ESTADO_EM_EXECUCAO, ESTADO_ERRO, ESTADO_OCIOSA, ESTADO_TEMPO_ESGOTADO, GPIO_ESGOTOU, GPIO_OCORREU,
    ROTINA_INDICE_CABECALHO, ROTINA_LEITURA_MAX, ROTINA_TEMPO_MAX_MS, JigaSimulada, ReceptorQuadros, Registro,
    StatusRotina, interpretar_resposta, interpretar_resultados, montar_abort, montar_adc_read, montar_dac_set,
    montar_gpio_read, montar_gpio_set, montar_i2c_dump, montar_i2c_load, montar_i2c_xfer, montar_nack,
    montar_pwm_set, montar_quadro, montar_read_result, montar_rotina, montar_routine_status, montar_run_routine,
    montar_uart_config, passo, passo_espera, passo_espera_gpio, tamanho_passo, validar_rotina,
)


def _respostas(jiga, quadro):
    _, dados = jiga.processar(quadro)
    return [interpretar_resposta(q) for _, q in ReceptorQuadros().processar(dados)]


def _uma(jiga, quadro):
    [resposta] = _respostas(jiga, quadro)
    return resposta


def _status(jiga, seq=200):
    resposta = _uma(jiga, montar_routine_status(seq))
    assert resposta.ack and resposta.cmd == CMD_ROUTINE_STATUS
    return StatusRotina.de_bytes(resposta.dados)


def _resultados(jiga, seq=201, offset=0):
    resposta = _uma(jiga, montar_read_result(seq, offset))
    assert resposta.ack, resposta.descrever()
    return resposta.dados


EXEMPLO = [passo(montar_dac_set(0, 1, 2482)), passo_espera_gpio(5, 1, 500_000), passo(montar_adc_read(0, 0))]


# --- montagem ---------------------------------------------------------------------------------------------

def test_quadro_do_exemplo_do_apendice():
    """RUN_ROUTINE do Quadro de exemplos de rotina (Apendice A): DAC1 2482, ESPERA_GPIO 5 ate 500 ms e ADC A0."""
    quadro = montar_run_routine(5, 7, 1000, EXEMPLO)
    assert quadro.hex(" ").upper().startswith("7E 14 05 08 07 00 E8 03 00 00 03 10 01 B2 09 79 05 01 20 A1 07 00 40 00")


def test_passo_tira_o_enquadramento():
    assert passo(montar_gpio_set(9, 1, 1)) == b"\x20\x01\x01"
    assert passo(montar_gpio_read(9)) == b"\x21"
    assert passo_espera(1000) == b"\x78\xe8\x03\x00\x00"
    assert passo_espera_gpio(8, 0, 1) == b"\x79\x08\x00\x01\x00\x00\x00"


def test_rotina_que_nao_cabe_no_quadro():
    with pytest.raises(ValueError):
        montar_rotina(1, 1000, [passo(montar_i2c_load(0, 0, bytes(230))), b"\x21\x21"])
    assert len(montar_rotina(1, 1000, [passo(montar_i2c_load(0, 0, bytes(230)))])) == 240


def test_comandos_sem_payload():
    assert montar_routine_status(6) == montar_quadro(6, CMD_ROUTINE_STATUS)
    assert montar_abort(9) == montar_quadro(9, CMD_ABORT)
    assert montar_read_result(8, 0x0123)[4:6] == b"\x23\x01"


# --- validacao --------------------------------------------------------------------------------------------

@pytest.mark.parametrize("passos, indice", [
    ([b"\x21", b"\x55", b"\x21"], 1),                                        # codigo inexistente
    ([b"\x21", b"\x10\x01"], 1),                                             # passo truncado
    ([passo(montar_dac_set(0, 1, 4096))], 0),
    ([passo(montar_gpio_set(0, 5, 1))], 0),
    ([passo_espera_gpio(4, 1, 10)], 0),
    ([passo(montar_adc_read(0, 3))], 0),
    ([passo(montar_pwm_set(0, 1, 100_001, 0))], 0),
    ([passo(montar_uart_config(0, 1199))], 0),
    ([passo(montar_i2c_xfer(0, 0x50, b"", 0))], 0),                          # nem escrita nem leitura
    ([b"\x51\x00\x03\xaa\xbb"], 0),                                          # quantidade maior que os dados
    ([passo(montar_i2c_dump(0, 0, 240))] * 9, 8),                            # passa dos 2048 B de resultados
])
def test_passo_invalido(passos, indice):
    assert validar_rotina(montar_rotina(1, 1000, passos)) == (indice, None)


@pytest.mark.parametrize("payload", [
    montar_rotina(1, 1000, [b"\x21"])[:6],                                  # cabecalho curto
    montar_rotina(1, 1000, []),                                              # sem passos
    montar_rotina(1, 0, [b"\x21"]),
    montar_rotina(1, ROTINA_TEMPO_MAX_MS + 1, [b"\x21"]),
    montar_rotina(1, 1000, [b"\x21"]) + b"\x21",                            # bytes sobrando
])
def test_cabecalho_invalido(payload):
    assert validar_rotina(payload) == (ROTINA_INDICE_CABECALHO, None)


def test_menos_passos_que_o_informado():
    payload = bytearray(montar_rotina(1, 1000, [b"\x21", b"\x21"]))
    payload[6] = 3
    assert validar_rotina(payload) == (2, None)


def test_rotina_valida_decodificada():
    indice, passos = validar_rotina(montar_rotina(7, 1000, EXEMPLO))
    assert indice is None
    assert passos == [(0x10, b"\x01\xb2\x09"), (0x79, b"\x05\x01\x20\xa1\x07\x00"), (0x40, b"\x00")]
    assert validar_rotina(montar_rotina(1, ROTINA_TEMPO_MAX_MS, [b"\x21"]))[0] is None
    assert validar_rotina(montar_rotina(1, 1000, [passo(montar_i2c_dump(0, 0, 240))] * 8))[0] is None


def test_tamanho_e_registro_dos_passos():
    assert tamanho_passo(passo(montar_i2c_xfer(0, 0x50, b"\x10\x11", 4)), 0) == (6, 4)
    assert tamanho_passo(b"\x62", 0) == (1, 240)
    assert tamanho_passo(b"\x72\x10", 0) == (2, 17)
    assert tamanho_passo(passo_espera_gpio(5, 1, 0), 0) == (7, 5)
    assert tamanho_passo(passo_espera(0), 0) == (5, 0)


# --- interpretacao ----------------------------------------------------------------------------------------

def test_status_e_registros():
    status = StatusRotina.de_bytes(b"\x05\x02\x07\x00\x03")
    assert (status.estado, status.passos, status.identificador, status.erro) == (ESTADO_ERRO, 2, 7, 3)
    assert status.finalizada and "NAO_CONFIGURADO" in status.descrever()
    assert not StatusRotina(ESTADO_EM_EXECUCAO, 0, 1, 0).finalizada
    registros = [Registro(1, 42350, b"\x00\x6e\xa5\x00\x00"), Registro(2, 42371, b"\x00\xff\x07")]
    assert interpretar_resultados(b"".join(r.em_bytes() for r in registros)) == registros
    with pytest.raises(ValueError):
        interpretar_resultados(registros[0].em_bytes()[:-1])


def test_nack_com_indice_do_passo():
    resposta = interpretar_resposta(ReceptorQuadros().processar(
        montar_nack(5, CMD_RUN_ROUTINE, ERRO_ROTINA_INVALIDA, b"\x01"))[0][1])
    assert (resposta.erro, resposta.dados) == (ERRO_ROTINA_INVALIDA, b"\x01")
    assert resposta.descrever().endswith("ROTINA_INVALIDA (passo 1)")


# --- execucao na jiga simulada ----------------------------------------------------------------------------

def test_exemplo_executa_e_registra():
    jiga = JigaSimulada(entradas=0x01, adc={0: 0x07FF, 1: 0, 2: 0})
    assert _uma(jiga, montar_run_routine(1, 7, 1000, EXEMPLO)).ack
    status = _status(jiga)
    assert (status.estado, status.passos, status.identificador) == (ESTADO_CONCLUIDA, 3, 7)
    assert jiga.dac[1] == 0                                                   # estado seguro no fim
    dados = _resultados(jiga)
    assert dados[:4] == b"\x07\x00\x14\x00"
    assert interpretar_resultados(dados[4:]) == [Registro(1, 0, bytes([GPIO_OCORREU]) + bytes(4)),
                                                  Registro(2, 0, b"\x00\xff\x07")]


def test_espera_gpio_esgota_e_o_tempo_anda_so_no_avancar():
    jiga = JigaSimulada()
    _uma(jiga, montar_run_routine(1, 4, 1000, [passo_espera_gpio(8, 1, 1000), passo(montar_gpio_read(0))]))
    assert _status(jiga, 200).estado == ESTADO_EM_EXECUCAO
    jiga.avancar(0.0009)
    assert _status(jiga, 201).estado == ESTADO_EM_EXECUCAO
    jiga.avancar(0.0001)
    assert _status(jiga, 202).estado == ESTADO_CONCLUIDA
    registros = interpretar_resultados(_resultados(jiga)[4:])
    assert registros == [Registro(0, 1000, bytes([GPIO_ESGOTOU]) + (1000).to_bytes(4, "little")),
                         Registro(1, 1000, b"\x00")]


def test_durante_a_rotina_so_ping_status_e_abort():
    jiga = JigaSimulada()
    _uma(jiga, montar_run_routine(1, 15, 2000, [passo_espera(1_000_000)]))
    for seq, quadro in enumerate([montar_gpio_set(2, 1, 1), montar_run_routine(3, 9, 1, [b"\x21"]),
                                  montar_read_result(4), montar_quadro(5, 0x02)], start=2):
        resposta = _uma(jiga, quadro)
        assert not resposta.ack and resposta.erro == ERRO_OCUPADO, seq
    assert jiga.gpio[1] == 0
    assert _uma(jiga, montar_quadro(6, 0x01)).ack
    assert _status(jiga, 7).estado == ESTADO_EM_EXECUCAO
    assert _uma(jiga, montar_abort(8)).ack
    assert _status(jiga, 9).estado == ESTADO_ABORTADA


def test_abort_aplica_o_estado_seguro_e_guarda_os_resultados():
    jiga = JigaSimulada()
    _uma(jiga, montar_run_routine(1, 16, 20000, [passo(montar_gpio_set(0, 1, 1)), b"\x21", passo_espera(10_000_000)]))
    jiga.avancar(0.3)
    assert jiga.gpio[1] == 1
    _uma(jiga, montar_abort(2))
    assert jiga.gpio[1] == 0
    status = _status(jiga)
    assert (status.estado, status.passos) == (ESTADO_ABORTADA, 2)
    assert len(interpretar_resultados(_resultados(jiga)[4:])) == 1


def test_tempo_maximo():
    jiga = JigaSimulada()
    _uma(jiga, montar_run_routine(1, 17, 100, [passo_espera(500_000), b"\x21"]))
    jiga.avancar(0.099)
    assert _status(jiga, 200).estado == ESTADO_EM_EXECUCAO
    jiga.avancar(0.001)
    status = _status(jiga, 201)
    assert (status.estado, status.passos) == (ESTADO_TEMPO_ESGOTADO, 0)


def test_erro_em_um_passo():
    jiga = JigaSimulada()
    _uma(jiga, montar_run_routine(1, 5, 1000, [b"\x21", passo(montar_i2c_xfer(0, 0x50, b"", 2)), b"\x21"]))
    status = _status(jiga)
    assert (status.estado, status.passos, status.erro) == (ESTADO_ERRO, 1, 3)


def test_retransmissao_do_run_routine_nao_recomeca():
    jiga = JigaSimulada()
    run = montar_run_routine(1, 18, 2000, [passo_espera(600_000)])
    _uma(jiga, run)
    jiga.avancar(0.3)
    assert _uma(jiga, run).ack
    assert _status(jiga, 200).estado == ESTADO_EM_EXECUCAO
    jiga.avancar(0.3)
    assert _status(jiga, 201).estado == ESTADO_CONCLUIDA


def test_read_result_em_partes_e_invalido():
    jiga = JigaSimulada()
    assert _uma(jiga, montar_read_result(1)).erro == ERRO_SEM_RESULTADO
    _uma(jiga, montar_run_routine(2, 8, 1000, [passo(montar_i2c_dump(0, 0, 240)), passo(montar_i2c_dump(0, 16, 240))]))
    total = int.from_bytes(_resultados(jiga, 3)[2:4], "little")
    assert total == 492
    partes = b"".join(_resultados(jiga, 4 + i, offset)[4:] for i, offset in enumerate(range(0, total, ROTINA_LEITURA_MAX)))
    assert [r.indice for r in interpretar_resultados(partes)] == [0, 1]
    assert len(_resultados(jiga, 10, total)) == 4
    assert _uma(jiga, montar_read_result(11, total + 1)).erro == ERRO_PARAMETRO_INVALIDO
    assert _uma(jiga, montar_quadro(12, CMD_READ_RESULT, b"\x00")).erro == ERRO_PARAMETRO_INVALIDO


def test_reset_all_volta_a_ociosa():
    jiga = JigaSimulada()
    _uma(jiga, montar_run_routine(1, 9, 1000, [b"\x21"]))
    _uma(jiga, montar_quadro(2, 0x02))
    status = _status(jiga)
    assert (status.estado, status.identificador) == (ESTADO_OCIOSA, 0)
    assert _uma(jiga, montar_read_result(3)).erro == ERRO_SEM_RESULTADO


def test_status_e_abort_com_payload():
    jiga = JigaSimulada()
    assert _uma(jiga, montar_quadro(1, CMD_ROUTINE_STATUS, b"\x00")).erro == ERRO_PARAMETRO_INVALIDO
    assert _uma(jiga, montar_quadro(2, CMD_ABORT, b"\x00")).erro == ERRO_PARAMETRO_INVALIDO
    assert _uma(jiga, montar_abort(3)).ack                                   # sem rotina: ACK, nada muda
    assert _status(jiga, 4).estado == ESTADO_OCIOSA


def test_rotina_invalida_nao_executa_nada():
    jiga = JigaSimulada()
    resposta = _uma(jiga, montar_run_routine(1, 14, 1000, [passo(montar_gpio_set(0, 1, 1)), b"\x55"]))
    assert (resposta.erro, resposta.dados) == (ERRO_ROTINA_INVALIDA, b"\x01")
    assert jiga.gpio[1] == 0 and _status(jiga).estado == ESTADO_OCIOSA


def test_loopback_espera_gpio_reage_ao_proprio_estimulo():
    jiga = JigaSimulada(loopback=True)
    _uma(jiga, montar_run_routine(1, 19, 1000, [passo(montar_gpio_set(0, 1, 1)), passo_espera_gpio(5, 1, 10_000)]))
    [registro] = interpretar_resultados(_resultados(jiga)[4:])
    assert registro.dados == bytes([GPIO_OCORREU]) + bytes(4)
