"""
Medicao do loopback DAC -> ADC (dados do CT10 para o Cap. 4 da monografia).

Faz a mesma varredura do CT10, mas guarda os valores: para cada ponto, escreve o DAC,
le o ADC ligado a ele varias vezes e registra media, minimo, maximo e erro. No fim mostra,
por canal, o maior erro e a reta ajustada (leitura = ganho x DAC + offset), e salva tudo
num CSV.

Precisa dos jumpers DAC1 (PA4) -> A0 (PA3) e DAC2 (PA5) -> A1 (PC0) e so do USB-CDC (CN13).
O erro medido junta o erro do DAC e o do ADC: para separar os dois, meca alguns pontos
do DAC no multimetro.

Uso:
    python medir_loopback.py                              # 11 pontos da faixa util, 8 leituras cada
    python medir_loopback.py --pontos 33 --amostras 16
    python medir_loopback.py --de 0 --ate 4095 --pontos 41  # inclui as pontas (saturacao do buffer)
    python medir_loopback.py --saida medicao.csv --porta COM14
"""

import argparse
import csv
import statistics
import sys
import time
from dataclasses import dataclass

import serial

from protocolo import (
    ADC_VALOR_MAX, CMD_RESET_ALL, DAC_CANAIS, DAC_FAIXA_UTIL, DAC_VALOR_MAX, LOOPBACK_DAC_ADC, QUADRO_OK,
    TOLERANCIA_ADC, ReceptorQuadros, codigo_para_tensao, interpretar_resposta, montar_adc_read,
    montar_dac_set, montar_quadro, nome_comando,
)
from terminal_usb import detectar_porta
from versao import __version__

TEMPO_RESPOSTA = 0.5      # s; a resposta normal chega em poucos ms
ACOMODACAO_DAC = 0.01     # s entre o DAC_SET e a primeira leitura (o DAC acomoda em microssegundos)
SAIDA_PADRAO = "medicao_loopback.csv"

COLUNAS = ["canal_dac", "canal_adc", "codigo_dac", "tensao_nominal_v", "leitura_media", "leitura_min",
           "leitura_max", "erro_medio_codigos", "erro_medio_mv", "pior_erro_codigos", "dentro_tolerancia"]


class ErroJiga(Exception):
    """A jiga nao respondeu ou recusou um comando da medicao."""


@dataclass
class Ponto:
    """Um ponto da varredura: o codigo escrito no DAC e as leituras do ADC ligado a ele."""

    canal_dac: int
    canal_adc: int
    codigo: int
    leituras: list

    @property
    def media(self):
        return statistics.fmean(self.leituras)

    @property
    def erro(self):
        """Erro medio, em codigos (leitura - DAC)."""
        return self.media - self.codigo

    @property
    def erro_mv(self):
        return codigo_para_tensao(self.erro) * 1000

    @property
    def pior_erro(self):
        """Leitura mais distante do DAC, em codigos (com sinal): e ela que o CT10 reprovaria."""
        return max((leitura - self.codigo for leitura in self.leituras), key=abs)

    @property
    def dentro_tolerancia(self):
        return abs(self.pior_erro) <= TOLERANCIA_ADC

    def linha_csv(self):
        return {
            "canal_dac": self.canal_dac, "canal_adc": self.canal_adc, "codigo_dac": self.codigo,
            "tensao_nominal_v": f"{codigo_para_tensao(self.codigo):.4f}",
            "leitura_media": f"{self.media:.2f}", "leitura_min": min(self.leituras),
            "leitura_max": max(self.leituras), "erro_medio_codigos": f"{self.erro:.2f}",
            "erro_medio_mv": f"{self.erro_mv:.2f}", "pior_erro_codigos": self.pior_erro,
            "dentro_tolerancia": int(self.dentro_tolerancia),
        }


def pontos_varredura(quantidade, inicio, fim):
    """Codigos do DAC igualmente espacados de inicio a fim (inclusive)."""
    if quantidade < 2:
        raise ValueError("a varredura precisa de pelo menos 2 pontos")
    if not 0 <= inicio < fim <= DAC_VALOR_MAX:
        raise ValueError(f"faixa precisa ficar entre 0 e {DAC_VALOR_MAX}, com inicio < fim")
    return [round(inicio + i * (fim - inicio) / (quantidade - 1)) for i in range(quantidade)]


def _exigir_ack(resposta):
    if not resposta.ack:
        raise ErroJiga(resposta.descrever())
    return resposta


def medir(executar, pontos, amostras, canais=DAC_CANAIS, esperar=time.sleep):
    """Faz a varredura e devolve a lista de Ponto.

    executar(montar, *argumentos) envia o quadro montado por montar(seq, *argumentos) e devolve a
    Resposta: na placa e o ClienteSerial; nos testes, a JigaSimulada com loopback.
    """
    medidos = []
    for canal_dac in canais:
        canal_adc = LOOPBACK_DAC_ADC[canal_dac]
        for codigo in pontos:
            _exigir_ack(executar(montar_dac_set, canal_dac, codigo))
            esperar(ACOMODACAO_DAC)
            leituras = []
            for _ in range(amostras):
                resposta = _exigir_ack(executar(montar_adc_read, canal_adc))
                leituras.append(int.from_bytes(resposta.dados[1:3], "little"))
            medidos.append(Ponto(canal_dac, canal_adc, codigo, leituras))
    return medidos


def resumo_canal(pontos):
    """(pior erro em codigos, pior erro em mV, ganho, offset em codigos) de um canal."""
    pior = max((p.pior_erro for p in pontos), key=abs)
    ganho, offset = statistics.linear_regression([p.codigo for p in pontos], [p.media for p in pontos])
    return pior, codigo_para_tensao(pior) * 1000, ganho, offset


def salvar_csv(pontos, caminho):
    with open(caminho, "w", newline="", encoding="utf-8") as arquivo:
        escritor = csv.DictWriter(arquivo, fieldnames=COLUNAS)
        escritor.writeheader()
        for ponto in pontos:
            escritor.writerow(ponto.linha_csv())


class ClienteSerial:
    """Envia um comando por vez e espera a resposta com o mesmo SEQ."""

    def __init__(self, porta):
        self.porta = porta
        self.seq = 0
        self.receptor = ReceptorQuadros()

    def __call__(self, montar, *argumentos):
        self.seq = self.seq % 0xFF + 1
        quadro = montar(self.seq, *argumentos)
        self.porta.write(quadro)
        limite = time.monotonic() + TEMPO_RESPOSTA
        while time.monotonic() < limite:
            for resultado, recebido in self.receptor.processar(self.porta.read(self.porta.in_waiting or 1)):
                if resultado == QUADRO_OK and recebido.seq == self.seq:
                    return interpretar_resposta(recebido)
        raise ErroJiga(f"sem resposta ao {nome_comando(quadro[3])} SEQ={self.seq}")


def _reset_all(seq):
    return montar_quadro(seq, CMD_RESET_ALL)


def imprimir(pontos):
    print(f"{'DAC':>4} {'ADC':>4} {'codigo':>7} {'nominal':>9} {'media':>8} {'min':>5} {'max':>5}"
          f" {'erro':>7} {'erro mV':>8}  ok")
    for p in pontos:
        print(f"{p.canal_dac:>4} {'A' + str(p.canal_adc):>4} {p.codigo:>7} {codigo_para_tensao(p.codigo):>8.3f}V"
              f" {p.media:>8.1f} {min(p.leituras):>5} {max(p.leituras):>5} {p.erro:>+7.1f} {p.erro_mv:>+8.1f}"
              f"  {'sim' if p.dentro_tolerancia else 'NAO'}")
    print()
    for canal in sorted({p.canal_dac for p in pontos}):
        do_canal = [p for p in pontos if p.canal_dac == canal]
        pior, pior_mv, ganho, offset = resumo_canal(do_canal)
        fora = sum(not p.dentro_tolerancia for p in do_canal)
        print(f"DAC{canal} -> A{do_canal[0].canal_adc}: pior erro {pior:+d} codigos ({pior_mv:+.1f} mV),"
              f" tolerancia +/- {TOLERANCIA_ADC}; ajuste: leitura = {ganho:.5f} x DAC {offset:+.2f};"
              f" {fora} ponto(s) fora")


def main():
    parser = argparse.ArgumentParser(description="Mede o loopback DAC -> ADC e salva em CSV.")
    parser.add_argument("--porta", help="porta do USB-CDC (CN13). Padrao: detecta")
    parser.add_argument("--pontos", type=int, default=11, help="pontos por canal (padrao: 11, como o CT10)")
    parser.add_argument("--amostras", type=int, default=8, help="leituras do ADC por ponto (padrao: 8)")
    parser.add_argument("--de", type=int, default=DAC_FAIXA_UTIL[0], help="primeiro codigo do DAC")
    parser.add_argument("--ate", type=int, default=DAC_FAIXA_UTIL[1], help="ultimo codigo do DAC")
    parser.add_argument("--saida", default=SAIDA_PADRAO, help=f"arquivo CSV (padrao: {SAIDA_PADRAO})")
    args = parser.parse_args()

    try:
        pontos = pontos_varredura(args.pontos, args.de, args.ate)
    except ValueError as e:
        print(e)
        return 2
    if args.amostras < 1:
        print("--amostras precisa ser pelo menos 1")
        return 2

    nome = args.porta or detectar_porta()
    if not nome:
        print("USB-CDC da jiga nao encontrado: confira o cabo no CN13 ou informe --porta.")
        return 2

    print(f"Medicao do loopback v{__version__} | {nome} | {len(pontos)} pontos x {args.amostras} leituras"
          f" | escala de {ADC_VALOR_MAX} = 3,3 V\n")
    with serial.Serial(nome, timeout=0.05) as porta:
        executar = ClienteSerial(porta)
        try:
            medidos = medir(executar, pontos, args.amostras)
        except ErroJiga as e:
            print(f"falha na medicao: {e}")
            return 1
        finally:
            try:
                executar(_reset_all)   # nunca deixa o DAC aplicado
            except ErroJiga:
                print("aviso: a jiga nao confirmou o RESET_ALL do fim")

    imprimir(medidos)
    salvar_csv(medidos, args.saida)
    print(f"\nCSV salvo em {args.saida}")
    return 0 if all(p.dentro_tolerancia for p in medidos) else 1


if __name__ == "__main__":
    sys.exit(main())
