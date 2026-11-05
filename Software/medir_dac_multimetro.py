"""
Conferencia do DAC com multimetro nos 11 pontos do CT10 (instrumento independente da jiga).

Para cada ponto da faixa util (codigos 124 a 3971), o script escreve o DAC, espera o usuario medir o pino com o
multimetro e digitar a leitura em volts, e guarda o codigo, a tensao nominal, a medida e o erro. O criterio e o do
CT10: 1 % do fundo de escala (33 mV). Ligacao: ponta vermelha no pino do DAC, preta no GND; sem carga.

Uso:
    python medir_dac_multimetro.py                    # DAC1 (PA4)
    python medir_dac_multimetro.py --canal 2          # DAC2 (PA5)
    python medir_dac_multimetro.py --saida medicoes/medicao_dac2_multimetro.csv --canal 2
"""

import argparse
import csv
import sys

from cliente import ClienteJiga, TransporteSerial
from protocolo import DAC_VALOR_MAX, TENSAO_REFERENCIA, pontos_ct10

TOLERANCIA_V = 0.033
PINOS = {1: "PA4", 2: "PA5"}


def ler_tensao(texto):
    """Le a medida digitada ('1,234' ou '1.234', em V); None para pular o ponto."""
    while True:
        bruto = input(texto).strip().replace(",", ".")
        if bruto == "":
            return None
        try:
            return float(bruto)
        except ValueError:
            print("  digite a tensao em volts (ex.: 1,652) ou Enter para pular")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--canal", type=int, choices=(1, 2), default=1)
    parser.add_argument("--porta", default=None, help="porta do USB-CDC (padrao: detectada pelo VID/PID)")
    parser.add_argument("--saida", default=None, help="CSV (padrao: medicoes/medicao_dac<canal>_multimetro.csv)")
    args = parser.parse_args()
    saida = args.saida or f"medicoes/medicao_dac{args.canal}_multimetro.csv"

    cliente = ClienteJiga(TransporteSerial())
    cliente.abrir(args.porta)
    linhas = []
    try:
        print(f"DAC{args.canal} no pino {PINOS[args.canal]}: meca com o multimetro e digite a leitura em V.")
        for codigo in pontos_ct10():
            nominal = TENSAO_REFERENCIA * codigo / DAC_VALOR_MAX
            cliente.dac_set(args.canal, codigo)
            medida = ler_tensao(f"codigo {codigo:4d} (nominal {nominal:.4f} V): ")
            if medida is None:
                continue
            erro_mv = (medida - nominal) * 1000
            dentro = abs(medida - nominal) <= TOLERANCIA_V
            linhas.append([args.canal, codigo, f"{nominal:.4f}", f"{medida:.4f}", f"{erro_mv:.1f}", int(dentro)])
            print(f"  erro {erro_mv:+.1f} mV {'ok' if dentro else 'FORA da tolerancia'}")
    finally:
        cliente.reset_all()
        cliente.fechar()

    with open(saida, "w", newline="", encoding="utf-8") as f:
        escritor = csv.writer(f)
        escritor.writerow(["canal_dac", "codigo_dac", "tensao_nominal_v", "tensao_medida_v", "erro_mv",
                           "dentro_tolerancia"])
        escritor.writerows(linhas)
    if linhas:
        pior = max(abs(float(l[4])) for l in linhas)
        print(f"{len(linhas)} pontos, pior erro {pior:.1f} mV (tolerancia {TOLERANCIA_V * 1000:.0f} mV). Salvo em {saida}")
    return 0 if all(l[5] for l in linhas) else 1


if __name__ == "__main__":
    sys.exit(main())
