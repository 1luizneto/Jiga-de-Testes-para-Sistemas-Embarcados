"""Casos de teste de unidade da monografia (secao 3.7.4): CT02 (codificar e decodificar os quadros dos Quadros de
exemplo do Apendice A), CT03 (bit alterado) e CT04 (bytes espurios e quadro truncado)."""

import pytest

from cliente import ClienteJiga, TransporteSimulado
from protocolo import (
    CMD_ABORT, CMD_ADC_READ, CMD_DAC_SET, CMD_PING, CMD_PWM_SET, CMD_READ_RESULT, CMD_ROUTINE_STATUS,
    CMD_RUN_ROUTINE, ERRO_CRC, ERRO_OCUPADO, ERRO_PARAMETRO_INVALIDO, QUADRO_OK, JigaSimulada, ReceptorQuadros,
    Registro, StatusRotina, TIMEOUT, montar_ack, montar_nack, montar_quadro, montar_rotina, passo_espera_gpio,
)

# (descricao, quadro do Apendice A, seq, cmd, payload)
EXEMPLOS = [
    ("PING", "7E 00 01 01 8C EF", 1, CMD_PING, ""),
    ("ACK do PING 0.1.0", "7E 04 01 81 01 00 01 00 AC B8", 1, 0x81, "01 00 01 00"),
    ("DAC_SET", "7E 03 02 10 01 00 08 EC 29", 2, CMD_DAC_SET, "01 00 08"),
    ("ACK do DAC_SET", "7E 00 02 90 47 29", 2, 0x90, ""),
    ("NACK do DAC_SET", "7E 02 02 FF 10 02 B5 54", 2, 0xFF, "10 02"),
    ("ADC_READ", "7E 01 03 40 00 E8 A6", 3, CMD_ADC_READ, "00"),
    ("ACK do ADC_READ", "7E 03 03 C0 00 FF 07 9E EE", 3, 0xC0, "00 FF 07"),
    ("PWM_SET", "7E 07 04 30 01 E8 03 00 00 88 13 5A 47", 4, CMD_PWM_SET, "01 E8 03 00 00 88 13"),
    ("RUN_ROUTINE", "7E 14 05 08 07 00 E8 03 00 00 03 10 01 B2 09 79 05 01 20 A1 07 00 40 00 FB CD", 5,
     CMD_RUN_ROUTINE, "07 00 E8 03 00 00 03 10 01 B2 09 79 05 01 20 A1 07 00 40 00"),
    ("ACK do RUN_ROUTINE", "7E 00 05 88 E9 23", 5, 0x88, ""),
    ("ROUTINE_STATUS", "7E 00 06 09 13 F7", 6, CMD_ROUTINE_STATUS, ""),
    ("ACK do ROUTINE_STATUS em execucao", "7E 05 06 89 01 01 07 00 00 12 1D", 6, 0x89, "01 01 07 00 00"),
    ("ACK do ROUTINE_STATUS concluida", "7E 05 07 89 02 03 07 00 00 C9 A6", 7, 0x89, "02 03 07 00 00"),
    ("READ_RESULT", "7E 02 08 0A 00 00 8D 17", 8, CMD_READ_RESULT, "00 00"),
    ("ABORT", "7E 00 09 0B 6F C7", 9, CMD_ABORT, ""),
    ("NACK OCUPADO", "7E 02 09 FF 08 07 D5 90", 9, 0xFF, "08 07"),
]

# ACK do READ_RESULT da rotina do exemplo, com o registro do ADC_READ em 3 B ([canal][valor]), como na v0.9.0:
# ESPERA_GPIO (passo 1) em 42 350 us, reacao em 42 338 us; ADC_READ (passo 2) 21 us depois, canal 0 = 2047
ACK_READ_RESULT = "7E 18 08 8A 07 00 14 00 01 6E A5 00 00 05 00 62 A5 00 00 02 83 A5 00 00 03 00 FF 07 51 36"


def _hex(texto):
    return bytes.fromhex(texto)


# 1. CT02: cada quadro dos exemplos e montado byte a byte e decodificado nos mesmos campos
@pytest.mark.parametrize("descricao, quadro, seq, cmd, payload", EXEMPLOS, ids=[e[0] for e in EXEMPLOS])
def test_ct02_codificar_e_decodificar(descricao, quadro, seq, cmd, payload):
    assert montar_quadro(seq, cmd, _hex(payload)) == _hex(quadro)
    [(resultado, q)] = ReceptorQuadros().processar(_hex(quadro))
    assert resultado == QUADRO_OK and (q.seq, q.cmd, q.payload) == (seq, cmd, _hex(payload))


# 2. CT02: o payload do RUN_ROUTINE do exemplo sai da montagem da rotina, e o ACK do READ_RESULT tem os registros
def test_ct02_rotina_do_exemplo():
    passos = [_hex("10 01 B2 09"), passo_espera_gpio(5, 1, 500_000), _hex("40 00")]
    assert montar_rotina(7, 1000, passos) == _hex(EXEMPLOS[8][4])
    assert StatusRotina.de_bytes(_hex("02 03 07 00 00")) == StatusRotina(2, 3, 7, 0)
    registros = Registro(1, 42_350, _hex("00") + (42_338).to_bytes(4, "little")).em_bytes() + \
        Registro(2, 42_371, _hex("00 FF 07")).em_bytes()
    ack = montar_ack(8, CMD_READ_RESULT, _hex("07 00") + len(registros).to_bytes(2, "little") + registros)
    assert ack == _hex(ACK_READ_RESULT)
    assert montar_nack(2, CMD_DAC_SET, ERRO_PARAMETRO_INVALIDO) == _hex(EXEMPLOS[4][1])
    assert montar_nack(9, CMD_RUN_ROUTINE, ERRO_OCUPADO) == _hex(EXEMPLOS[15][1])


# 3. CT03: qualquer bit alterado (fora do SOF e do LEN) faz o quadro ser descartado, sem resposta da jiga;
# o cliente retransmite por tempo limite e recupera com o quadro integro
@pytest.mark.parametrize("bit", range(8, 8 * 9))
def test_ct03_bit_alterado_descartado(bit):
    quadro = bytearray(_hex(EXEMPLOS[2][1]))
    quadro[bit // 8] ^= 1 << (bit % 8)
    resultados = [r for r, _ in ReceptorQuadros().processar(bytes(quadro))]
    assert QUADRO_OK not in resultados
    jiga = JigaSimulada()
    _, resposta = jiga.processar(bytes(quadro))
    assert resposta == b"" and jiga.dac[1] == 0


def test_ct03_crc_errado_e_retransmissao():
    quadro = bytearray(_hex(EXEMPLOS[2][1]))
    quadro[5] ^= 0x01
    assert [r for r, _ in ReceptorQuadros().processar(bytes(quadro))] == [ERRO_CRC]

    class Corrompe(TransporteSimulado):
        """Corrompe a primeira transmissao, como um erro na linha."""

        def escrever(self, dados):
            if not self.enviados:
                self.enviados.append(bytes(dados))
                dados = bytes(dados[:-1]) + bytes([dados[-1] ^ 0xFF])
                self.jiga.processar(dados)
                return
            super().escrever(dados)

    cliente = ClienteJiga(Corrompe())
    cliente.dac_set(1, 2048)
    assert cliente.retransmissoes == 1 and cliente.transporte.jiga.dac[1] == 2048


# 4. CT04: lixo antes do SOF e um quadro truncado; o receptor ressincroniza no SOF seguinte (depois do timeout)
def test_ct04_lixo_e_quadro_truncado():
    receptor = ReceptorQuadros()
    lixo = _hex("00 FF 13 37")
    resultados = receptor.processar(lixo + _hex(EXEMPLOS[0][1]))
    assert [r for r, _ in resultados] == [QUADRO_OK]
    truncado = _hex(EXEMPLOS[2][1])[:5]
    assert receptor.processar(truncado) == []
    assert receptor.timeout() == TIMEOUT
    [(resultado, quadro)] = receptor.processar(_hex(EXEMPLOS[5][1]))
    assert resultado == QUADRO_OK and quadro.cmd == CMD_ADC_READ
