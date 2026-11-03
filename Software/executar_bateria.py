"""
Executa uma bateria de testes pela linha de comando (testes de regressao automaticos, secao 3.6.2).

Uso:
    python executar_bateria.py baterias/sem_fios.yaml                 # jiga no USB (porta detectada)
    python executar_bateria.py baterias/loopback.yaml --repetir 10     # CT21: dez execucoes seguidas
    python executar_bateria.py baterias/loopback.yaml --simulada --loopback
    python executar_bateria.py baterias/x.yaml --porta COM14 --relatorios relatorios

Cada execucao gera um relatorio CSV e um JSON na pasta de relatorios. Codigo de saida: 0 = todos os casos
passaram, 1 = algum falhou, 2 = inconclusivo (comunicacao, tempo esgotado, interrompida) ou bateria invalida.
Com --repetir, confere tambem se os vereditos foram os mesmos em todas as execucoes.
"""

import argparse
import sys
from datetime import datetime
from pathlib import Path

from bateria import CarregadorDeBaterias, ErroBateria
from cliente import ClienteJiga, ErroComunicacao, TransporteSerial, TransporteSimulado
from motor import MotorDeTestes
from protocolo import JigaSimulada
from relatorio import FALHOU, PASSOU

SAIDA = {PASSOU: 0, FALHOU: 1}


def nome_relatorio(bateria, pasta, repeticao=None):
    carimbo = datetime.now().strftime("%Y%m%d-%H%M%S")
    sufixo = f"-{repeticao:02d}" if repeticao is not None else ""
    nome = "".join(c if c.isalnum() or c in "-_" else "_" for c in bateria.nome)
    return Path(pasta) / f"{nome}-{carimbo}{sufixo}"


def main(argv=None):
    parser = argparse.ArgumentParser(description="Executa uma bateria de testes YAML na jiga.")
    parser.add_argument("bateria", help="arquivo YAML da bateria")
    parser.add_argument("--porta", help="porta do USB-CDC da jiga (padrao: detectada pelo VID/PID)")
    parser.add_argument("--repetir", type=int, default=1, help="quantas vezes executar a bateria (CT21: 10)")
    parser.add_argument("--relatorios", default="relatorios", help="pasta dos relatorios (padrao: relatorios)")
    parser.add_argument("--sem-relatorio", action="store_true", help="nao grava os arquivos de relatorio")
    parser.add_argument("--simulada", action="store_true", help="usa a jiga simulada em vez da placa")
    parser.add_argument("--loopback", action="store_true", help="jiga simulada com os jumpers de loopback")
    args = parser.parse_args(argv)
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")      # console sem UTF-8 nao derruba o script por um acento

    try:
        bateria = CarregadorDeBaterias().carregar(args.bateria)
    except (OSError, ErroBateria) as e:
        print(f"Bateria invalida:\n{e}")
        return 2
    print(f"Bateria '{bateria.nome}': {len(bateria.casos)} caso(s), validada e compilada")

    if args.simulada:
        transporte = TransporteSimulado(JigaSimulada(loopback=args.loopback))
    else:
        transporte = TransporteSerial(args.porta)
    cliente = ClienteJiga(transporte)
    try:
        cliente.abrir()
    except ErroComunicacao as e:
        print(f"Jiga: {e}")
        return 2
    motor = MotorDeTestes(cliente)

    relatorios = []
    try:
        for n in range(1, args.repetir + 1):
            if args.repetir > 1:
                print(f"\n--- execucao {n} de {args.repetir} ---")
            relatorio = motor.executar(
                bateria,
                ao_terminar_caso=lambda r: print(f"  [{r.veredito.upper():12s}] {r.identificador}. {r.nome}"
                                                 + (f" -- {r.motivo}" if r.motivo else "")))
            relatorios.append(relatorio)
            print(relatorio.texto().splitlines()[-1] if not relatorio.motivo else relatorio.texto())
            if not args.sem_relatorio:
                Path(args.relatorios).mkdir(parents=True, exist_ok=True)
                base = nome_relatorio(bateria, args.relatorios, n if args.repetir > 1 else None)
                relatorio.salvar_csv(base.with_suffix(".csv"))
                relatorio.salvar_json(base.with_suffix(".json"))
                print(f"  relatorio: {base}.csv / .json")
            if relatorio.interrompida:
                break
    except KeyboardInterrupt:
        motor.interromper()
        print("Interrompido")
        return 2
    finally:
        cliente.fechar()

    if args.repetir > 1:
        vereditos = {tuple(c.veredito for c in r.casos) for r in relatorios}
        iguais = len(vereditos) == 1 and len(relatorios) == args.repetir
        print(f"\n{len(relatorios)} execucao(oes): vereditos {'iguais' if iguais else 'DIFERENTES'} em todas; "
              f"{sum(r.retransmissoes for r in relatorios)} retransmissao(oes) em "
              f"{sum(r.comandos for r in relatorios)} comandos")
        if not iguais:
            return 2
    return max(SAIDA.get(r.veredito, 2) for r in relatorios)


if __name__ == "__main__":
    sys.exit(main())
