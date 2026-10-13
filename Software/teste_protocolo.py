"""
Teste automatico do protocolo com a placa (hardware-in-the-loop).

Envia os casos de teste (protocolo.CASOS_TESTE) pelo USB-CDC (CN13) e confere duas
coisas: na UART de debug do ST-Link, se a CommTask logou o resultado esperado do
parser; e no proprio USB-CDC, se a jiga devolveu a resposta (ACK/NACK) esperada.

Precisa das duas portas: o CN13 (USB-CDC) e o USB do ST-Link (CN1).

Uso:
    python teste_protocolo.py                          # detecta as duas portas
    python teste_protocolo.py --porta COM14 --porta-log COM8
    python teste_protocolo.py --caso 1                 # so um caso (numero da lista)
    python teste_protocolo.py --listar                 # lista os casos e sai
"""

import argparse
import sys
import time

import serial
from serial.tools import list_ports

from protocolo import CASOS_TESTE, avaliar_caso, descrever_log, descrever_resposta
from terminal_usb import detectar_porta
from versao import __version__

# PIDs do ST-LINK (V2-1 da NUCLEO-F767ZI e variantes V3)
STLINK_VID = 0x0483
STLINK_PIDS = {0x374B, 0x374E, 0x374F, 0x3752, 0x3753}

BAUD_LOG = 1_000_000   # huart3 (DebugLog)
ESPERA_LOG = 0.3       # tempo para a CommTask logar e responder depois do ultimo passo (s)


def detectar_porta_log():
    for p in list_ports.comports():
        if p.vid == STLINK_VID and p.pid in STLINK_PIDS:
            return p.device
    return None


def ler_linhas(porta_log):
    dados = porta_log.read(porta_log.in_waiting or 1)
    return dados.decode("utf-8", errors="replace").splitlines()


def executar_caso(caso, porta_usb, porta_log):
    """Envia o caso e devolve a avaliacao (DebugLog + resposta do USB)."""
    porta_log.reset_input_buffer()
    porta_usb.reset_input_buffer()
    for tipo, valor in caso.passos:
        if tipo == "enviar":
            porta_usb.write(valor)
            porta_usb.flush()
        else:
            time.sleep(valor)
    time.sleep(ESPERA_LOG)

    resposta = porta_usb.read(porta_usb.in_waiting)
    return avaliar_caso(caso, ler_linhas(porta_log), resposta)


def main():
    parser = argparse.ArgumentParser(description="Teste automatico do protocolo com a placa.")
    parser.add_argument("--porta", help="porta do USB-CDC (CN13). Padrao: detecta")
    parser.add_argument("--porta-log", help="porta do ST-Link (DebugLog). Padrao: detecta")
    parser.add_argument("--caso", type=int, help="numero do caso (ver --listar)")
    parser.add_argument("--listar", action="store_true", help="lista os casos e sai")
    args = parser.parse_args()

    if args.listar:
        for i, caso in enumerate(CASOS_TESTE, 1):
            print(f"{i:2d}. {caso.nome:22s} {caso.descricao}")
        return 0

    casos = CASOS_TESTE
    if args.caso is not None:
        if not 1 <= args.caso <= len(CASOS_TESTE):
            print(f"caso invalido; use 1 a {len(CASOS_TESTE)}")
            return 2
        casos = [CASOS_TESTE[args.caso - 1]]

    nome_usb = args.porta or detectar_porta()
    nome_log = args.porta_log or detectar_porta_log()
    if not nome_usb or not nome_log:
        print(f"portas nao encontradas (USB-CDC: {nome_usb}, ST-Link: {nome_log}).")
        print("Confira os dois cabos ou informe --porta e --porta-log.")
        return 2

    print(f"Teste do protocolo v{__version__} | USB-CDC {nome_usb} | DebugLog {nome_log}\n")
    falhas = 0
    with serial.Serial(nome_usb, timeout=0.1) as porta_usb, \
            serial.Serial(nome_log, BAUD_LOG, timeout=0.1) as porta_log:
        for caso in casos:
            avaliacao = executar_caso(caso, porta_usb, porta_log)
            falhas += not avaliacao.aprovado
            print(f"[{'PASS' if avaliacao.aprovado else 'FALHA'}] {caso.nome}")
            if not avaliacao.log_ok:
                print(f"        log esperado:      {descrever_log(caso.esperado)}")
                print(f"        log obtido:        {avaliacao.linhas_log or '(nada)'}")
            if not avaliacao.resposta_ok:
                print(f"        resposta esperada: {descrever_resposta(caso.resposta_esperada())}")
                print(f"        resposta obtida:   {descrever_resposta(avaliacao.resposta_obtida)}")
                print(f"                           {avaliacao.resposta_obtida.hex(' ').upper() or '(sem bytes)'}")

    print(f"\n{len(casos) - falhas}/{len(casos)} casos passaram")
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
