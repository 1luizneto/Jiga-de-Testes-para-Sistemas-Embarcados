"""
Teste automatico do protocolo com a placa (hardware-in-the-loop).

Envia os casos de teste (protocolo.CASOS_TESTE) pelo USB-CDC (CN13) e confere duas
coisas: na UART de debug do ST-Link, se a CommTask logou o resultado esperado do
parser; e no proprio USB-CDC, se a jiga devolveu a resposta (ACK/NACK) esperada.

Precisa das duas portas: o CN13 (USB-CDC) e o USB do ST-Link (CN1).

Os casos de loopback (CT10 e CT11) so passam com os jumpers DAC1 (PA4) -> A0 (PA3),
DAC2 (PA5) -> A1 (PC0) e GPIO1-4 (PC8-PC11) -> GPIO5-8 (PC12, PD2, PG2, PG3): sem
--loopback eles sao pulados.

Uso:
    python teste_protocolo.py                          # detecta as duas portas
    python teste_protocolo.py --porta COM14 --porta-log COM8
    python teste_protocolo.py --loopback               # inclui o CT10 e o CT11 (jumpers ligados)
    python teste_protocolo.py --caso 1                 # so um caso (numero da lista)
    python teste_protocolo.py --listar                 # lista os casos e sai
"""

import argparse
import sys
import time

import serial
from serial.tools import list_ports

from protocolo import CASOS_TESTE, avaliar_caso, casos_a_executar, descrever_log, descrever_resposta
from terminal_usb import detectar_porta
from versao import __version__

# PIDs do ST-LINK (V2-1 da NUCLEO-F767ZI e variantes V3)
STLINK_VID = 0x0483
STLINK_PIDS = {0x374B, 0x374E, 0x374F, 0x3752, 0x3753}

BAUD_LOG = 1_000_000   # huart3 (DebugLog)
ESPERA_LOG = 0.3       # tempo para a CommTask logar e responder depois do ultimo passo (s)
BUFFER_RX = 1 << 16    # buffer de recepcao do driver: o CT11 gera ~80 linhas de log
MAXIMO_RESPOSTAS = 4   # respostas mostradas quando um caso falha (os casos longos tem dezenas)


def detectar_porta_log():
    for p in list_ports.comports():
        if p.vid == STLINK_VID and p.pid in STLINK_PIDS:
            return p.device
    return None


def ler_disponivel(porta):
    return porta.read(porta.in_waiting) if porta.in_waiting else b""


def aumentar_buffer(porta):
    # So existe no Windows; nos outros sistemas o buffer do driver ja e grande
    if hasattr(porta, "set_buffer_size"):
        porta.set_buffer_size(rx_size=BUFFER_RX)


def executar_caso(caso, porta_usb, porta_log):
    """Envia o caso e devolve a avaliacao (DebugLog + resposta do USB).

    O log e o USB sao lidos a cada passo, e nao so no fim: nos casos longos (CT10, CT11) o que
    chega durante o caso passaria do buffer da porta.
    """
    porta_log.reset_input_buffer()
    porta_usb.reset_input_buffer()
    log, resposta = bytearray(), bytearray()
    for tipo, valor in caso.passos:
        if tipo == "enviar":
            porta_usb.write(valor)
            porta_usb.flush()
        else:
            time.sleep(valor)
        log += ler_disponivel(porta_log)
        resposta += ler_disponivel(porta_usb)
    time.sleep(ESPERA_LOG)
    log += ler_disponivel(porta_log)
    resposta += ler_disponivel(porta_usb)

    linhas = log.decode("utf-8", errors="replace").splitlines()
    return avaliar_caso(caso, linhas, bytes(resposta))


def main():
    parser = argparse.ArgumentParser(description="Teste automatico do protocolo com a placa.")
    parser.add_argument("--porta", help="porta do USB-CDC (CN13). Padrao: detecta")
    parser.add_argument("--porta-log", help="porta do ST-Link (DebugLog). Padrao: detecta")
    parser.add_argument("--caso", type=int, help="numero do caso (ver --listar); roda mesmo se for de loopback")
    parser.add_argument("--loopback", action="store_true",
                        help="inclui os casos que precisam dos jumpers (CT10 e CT11)")
    parser.add_argument("--listar", action="store_true", help="lista os casos e sai")
    args = parser.parse_args()

    if args.listar:
        for i, caso in enumerate(CASOS_TESTE, 1):
            marca = " [loopback]" if caso.requer_loopback else ""
            print(f"{i:2d}. {caso.nome:24s} {caso.descricao}{marca}")
        return 0

    casos = casos_a_executar(args.loopback)
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
        aumentar_buffer(porta_usb)
        aumentar_buffer(porta_log)
        for caso in casos:
            avaliacao = executar_caso(caso, porta_usb, porta_log)
            falhas += not avaliacao.aprovado
            print(f"[{'PASS' if avaliacao.aprovado else 'FALHA'}] {caso.nome}")
            if not avaliacao.log_ok:
                print(f"        log esperado:      {descrever_log(caso.esperado)}")
                print(f"        log obtido:        {avaliacao.linhas_log or '(nada)'}")
            if not avaliacao.resposta_ok:
                obtida = avaliacao.resposta_obtida
                print(f"        motivo:            {avaliacao.motivo}")
                print(f"        resposta esperada: {caso.descrever_esperado()}")
                print(f"        resposta obtida:   {descrever_resposta(obtida, MAXIMO_RESPOSTAS)}")
                if len(obtida) <= 64:
                    print(f"                           {obtida.hex(' ').upper() or '(sem bytes)'}")

    pulados = [] if args.caso is not None or args.loopback else [c for c in CASOS_TESTE if c.requer_loopback]
    for caso in pulados:
        print(f"[PULADO] {caso.nome} (requer loopback: use --loopback com os jumpers ligados)")

    resumo = f"{len(casos) - falhas}/{len(casos)} casos passaram"
    print(f"\n{resumo}" + (f", {len(pulados)} pulados" if pulados else ""))
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
