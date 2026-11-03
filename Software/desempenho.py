"""
Testes de desempenho da comunicacao PC <-> jiga (secao 3.7.5 da monografia).

- CT23: 1000 PING seguidos, medindo o tempo de ida e volta no PC (time.perf_counter). Criterio: maximo < 50 ms;
  registra media, mediana, percentil 95 e maximo (o escalonamento do sistema operacional do PC causa atrasos
  ocasionais que a media esconderia).
- CT24: 10 000 comandos seguidos, alternando escrita (DAC_SET) e leitura (ADC_READ). Criterio: nenhum erro nao
  recuperado (sem resposta depois das 3 tentativas, ou NACK); registra a taxa de retransmissao.

Uso:
    python desempenho.py                     # CT23 e CT24 na jiga (porta detectada)
    python desempenho.py --ct 23 --n 1000 --porta COM14
    python desempenho.py --simulada          # confere o script sem a placa
Os tempos de cada comando vao para medicoes/ct23_ping.csv e medicoes/ct24_escrita_leitura.csv.
"""

import argparse
import csv
import math
import statistics
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

from cliente import ClienteJiga, ErroComunicacao, ErroNack, TransporteSerial, TransporteSimulado
from protocolo import DAC_FAIXA_UTIL, JigaSimulada

LATENCIA_MAX_MS = 50            # criterio do CT23 (metade do tempo limite de 100 ms)
PASTA_MEDICOES = Path(__file__).resolve().parent / "medicoes"


@dataclass
class Medicao:
    nome: str
    tempos_ms: list = field(default_factory=list)
    erros: list = field(default_factory=list)
    retransmissoes: int = 0
    comandos: int = 0

    def percentil(self, p):
        ordenados = sorted(self.tempos_ms)
        if not ordenados:
            return 0.0
        k = max(0, min(len(ordenados) - 1, math.ceil(p * len(ordenados) / 100) - 1))   # posto mais proximo
        return ordenados[k]

    def resumo(self):
        if not self.tempos_ms:
            return {"comandos": self.comandos, "erros": len(self.erros)}
        return {
            "comandos": self.comandos,
            "media_ms": statistics.fmean(self.tempos_ms),
            "mediana_ms": statistics.median(self.tempos_ms),
            "p95_ms": self.percentil(95),
            "p99_ms": self.percentil(99),
            "min_ms": min(self.tempos_ms),
            "max_ms": max(self.tempos_ms),
            "erros": len(self.erros),
            "retransmissoes": self.retransmissoes,
            "taxa_retransmissao": self.retransmissoes / self.comandos if self.comandos else 0.0,
        }

    def texto(self):
        r = self.resumo()
        if "media_ms" not in r:
            return f"{self.nome}: nenhum comando respondido ({r['erros']} erros)"
        return (f"{self.nome}: {r['comandos']} comandos; media {r['media_ms']:.3f} ms, mediana "
                f"{r['mediana_ms']:.3f} ms, p95 {r['p95_ms']:.3f} ms, p99 {r['p99_ms']:.3f} ms, "
                f"min {r['min_ms']:.3f} ms, max {r['max_ms']:.3f} ms; {r['erros']} erro(s) nao recuperado(s), "
                f"{r['retransmissoes']} retransmissao(oes) ({100 * r['taxa_retransmissao']:.3f} %)")

    def salvar_csv(self, arquivo):
        with open(arquivo, "w", encoding="utf-8", newline="") as f:
            escritor = csv.writer(f)
            escritor.writerow(["comando", "tempo_ms"])
            for n, tempo in enumerate(self.tempos_ms):
                escritor.writerow([n, f"{tempo:.4f}"])


def _medir(cliente, nome, n, operacao):
    medicao = Medicao(nome)
    retransmissoes = cliente.retransmissoes
    for i in range(n):
        inicio = time.perf_counter()
        try:
            operacao(i)
        except (ErroComunicacao, ErroNack) as e:
            medicao.erros.append(f"{i}: {e}")
            continue
        finally:
            medicao.comandos += 1
        medicao.tempos_ms.append((time.perf_counter() - inicio) * 1000)
    medicao.retransmissoes = cliente.retransmissoes - retransmissoes
    return medicao


def medir_ping(cliente, n=1000):
    """CT23: n PING; o tempo de cada um inclui as retransmissoes, se houver."""
    return _medir(cliente, "CT23 PING", n, lambda i: cliente.ping())


def medir_escrita_leitura(cliente, n=10_000):
    """CT24: n comandos alternando DAC_SET (valores variando na faixa util) e ADC_READ."""
    baixo, alto = DAC_FAIXA_UTIL

    def operacao(i):
        if i % 2 == 0:
            cliente.dac_set(1, baixo + (i * 37) % (alto - baixo))
        else:
            cliente.adc_read(0)
    return _medir(cliente, "CT24 escrita/leitura", n, operacao)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Testes de desempenho (CT23 e CT24).")
    parser.add_argument("--ct", type=int, choices=(23, 24), help="so um dos casos (padrao: os dois)")
    parser.add_argument("--n", type=int, help="numero de comandos (padrao: 1000 no CT23, 10000 no CT24)")
    parser.add_argument("--porta", help="porta do USB-CDC da jiga (padrao: detectada)")
    parser.add_argument("--simulada", action="store_true", help="jiga simulada (confere o script)")
    parser.add_argument("--saida", default=str(PASTA_MEDICOES), help="pasta dos CSV (padrao: medicoes)")
    args = parser.parse_args(argv)

    transporte = TransporteSimulado(JigaSimulada()) if args.simulada else TransporteSerial(args.porta)
    cliente = ClienteJiga(transporte)
    try:
        cliente.abrir()
        print(f"Jiga: protocolo {cliente.ping()[0]}, firmware {cliente.ping()[1]}")
        cliente.reset_all()
    except (ErroComunicacao, ErroNack) as e:
        print(f"Jiga: {e}")
        return 2
    Path(args.saida).mkdir(parents=True, exist_ok=True)
    falhou = False
    try:
        if args.ct in (None, 23):
            medicao = medir_ping(cliente, args.n or 1000)
            print(medicao.texto())
            medicao.salvar_csv(Path(args.saida) / "ct23_ping.csv")
            maximo = medicao.resumo().get("max_ms", float("inf"))
            ok = not medicao.erros and maximo < LATENCIA_MAX_MS
            print(f"CT23: {'PASSOU' if ok else 'FALHOU'} (maximo {maximo:.3f} ms; criterio < {LATENCIA_MAX_MS} ms)")
            falhou |= not ok
        if args.ct in (None, 24):
            medicao = medir_escrita_leitura(cliente, args.n or 10_000)
            print(medicao.texto())
            medicao.salvar_csv(Path(args.saida) / "ct24_escrita_leitura.csv")
            ok = not medicao.erros
            print(f"CT24: {'PASSOU' if ok else 'FALHOU'} ({len(medicao.erros)} erro(s) nao recuperado(s))")
            for erro in medicao.erros[:5]:
                print(f"  {erro}")
            falhou |= not ok
    finally:
        try:
            cliente.reset_all()
        except (ErroComunicacao, ErroNack):
            pass
        cliente.fechar()
    return 1 if falhou else 0


if __name__ == "__main__":
    sys.exit(main())
