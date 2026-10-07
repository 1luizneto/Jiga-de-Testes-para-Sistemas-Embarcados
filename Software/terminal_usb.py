"""
Terminal USB-CDC da jiga de testes.

Primeira versao da aplicacao desktop: abre a porta serial virtual do STM32
(USB-CDC) e mostra tudo o que chega, em hexadecimal + ASCII ou como texto.
Tambem permite enviar texto ou bytes em hexadecimal.

Uso:
    python terminal_usb.py                 # detecta a porta do STM32 sozinho
    python terminal_usb.py --porta COM14   # porta especifica
    python terminal_usb.py --modo texto    # mostra como texto em vez de hex
    python terminal_usb.py --log saida.txt # tambem grava o que for exibido
    python terminal_usb.py --listar        # lista as portas e sai

Comandos digitados durante a execucao:
    texto qualquer   envia o texto (sem quebra de linha)
    /hex 7E 01 02    envia os bytes em hexadecimal
    /modo            alterna entre hex e texto
    /ajuda           mostra os comandos
    /sair            encerra (Ctrl+C tambem)
"""

import argparse
import sys
import threading
import time
from datetime import datetime

import serial
from serial.tools import list_ports

# VID/PID padrao do USB-CDC da STMicroelectronics (Virtual COM Port)
STM32_VID = 0x0483
STM32_CDC_PID = 0x5740

BYTES_POR_LINHA = 16

# Tempo sem receber bytes que marca o fim de uma rajada (s)
SILENCIO_FIM_RAJADA = 0.005


def listar_portas():
    for p in list_ports.comports():
        vid = f"{p.vid:04X}" if p.vid is not None else "----"
        pid = f"{p.pid:04X}" if p.pid is not None else "----"
        print(f"  {p.device:8s} VID={vid} PID={pid}  {p.description}")


def detectar_porta():
    """Retorna a porta do USB-CDC do STM32, ou None se nao estiver conectado."""
    for p in list_ports.comports():
        if p.vid == STM32_VID and p.pid == STM32_CDC_PID:
            return p.device
    return None


def formatar_hex(dados):
    """Formata os bytes em linhas de 16: deslocamento, hex e ASCII."""
    linhas = []
    for i in range(0, len(dados), BYTES_POR_LINHA):
        bloco = dados[i:i + BYTES_POR_LINHA]
        hexa = " ".join(f"{b:02X}" for b in bloco)
        ascii_ = "".join(chr(b) if 32 <= b < 127 else "." for b in bloco)
        linhas.append(f"  {i:04X}  {hexa:<{BYTES_POR_LINHA * 3}} |{ascii_}|")
    return "\n".join(linhas)


def interpretar_hex(texto):
    """Converte '7E 01 0a' ou '7E010A' em bytes."""
    return bytes.fromhex(texto.replace(",", " ").replace("0x", "").replace("0X", ""))


class Terminal:
    def __init__(self, porta, modo, log):
        self.porta_fixa = porta
        self.modo = modo
        self.log = log
        self.serial = None
        self.rodando = True
        self.trava_saida = threading.Lock()

    # --- saida ------------------------------------------------------------

    def exibir(self, texto):
        with self.trava_saida:
            print(texto, flush=True)
            if self.log:
                self.log.write(texto + "\n")
                self.log.flush()

    def exibir_recebido(self, dados):
        hora = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        if self.modo == "hex":
            self.exibir(f"[{hora}] RX {len(dados)} B\n{formatar_hex(dados)}")
        else:
            self.exibir(dados.decode("utf-8", errors="replace").rstrip("\r\n"))

    # --- conexao ----------------------------------------------------------

    def conectar(self):
        """Tenta abrir a porta ate conseguir (o USB some quando a placa reinicia)."""
        avisou = False
        while self.rodando:
            porta = self.porta_fixa or detectar_porta()
            if porta:
                try:
                    self.serial = serial.Serial(porta, timeout=0.05)
                    self.exibir(f"*** conectado em {porta} ***")
                    return True
                except serial.SerialException as e:
                    if not avisou:
                        self.exibir(f"*** nao foi possivel abrir {porta}: {e} ***")
            if not avisou:
                self.exibir("*** aguardando o STM32 (USB-CDC)... ***")
                avisou = True
            time.sleep(0.5)
        return False

    def fechar(self):
        if self.serial:
            try:
                self.serial.close()
            except serial.SerialException:
                pass
            self.serial = None

    # --- recepcao ---------------------------------------------------------

    def ler_rajada(self):
        """Espera o primeiro byte e junta os seguintes ate a linha ficar em silencio.

        Assim uma mensagem enviada de uma vez pelo STM32 aparece num bloco so,
        em vez de picada conforme os bytes chegam.
        """
        dados = bytearray(self.serial.read(1))
        while dados:
            time.sleep(SILENCIO_FIM_RAJADA)
            pendentes = self.serial.in_waiting
            if not pendentes:
                break
            dados += self.serial.read(pendentes)
        return bytes(dados)

    def laco_recepcao(self):
        while self.rodando:
            if self.serial is None and not self.conectar():
                break
            try:
                dados = self.ler_rajada()
            except (serial.SerialException, OSError):
                self.exibir("*** conexao perdida ***")
                self.fechar()
                continue
            if dados:
                self.exibir_recebido(dados)

    # --- envio ------------------------------------------------------------

    def enviar(self, dados):
        if self.serial is None:
            self.exibir("*** nao conectado; nada enviado ***")
            return
        try:
            self.serial.write(dados)
        except (serial.SerialException, OSError) as e:
            self.exibir(f"*** erro ao enviar: {e} ***")
            return
        if self.modo == "hex":
            self.exibir(f"TX {len(dados)} B\n{formatar_hex(dados)}")

    def tratar_linha(self, linha):
        if linha in ("/sair", "/q"):
            self.rodando = False
        elif linha == "/ajuda":
            self.exibir(__doc__.split("Comandos digitados durante a execucao:")[1].rstrip())
        elif linha == "/modo":
            self.modo = "texto" if self.modo == "hex" else "hex"
            self.exibir(f"*** modo: {self.modo} ***")
        elif linha.startswith("/hex"):
            try:
                self.enviar(interpretar_hex(linha[4:]))
            except ValueError:
                self.exibir("*** hexadecimal invalido; ex.: /hex 7E 01 00 ***")
        elif linha:
            self.enviar(linha.encode("utf-8"))

    def executar(self):
        receptor = threading.Thread(target=self.laco_recepcao, daemon=True)
        receptor.start()
        self.exibir("Terminal USB-CDC da jiga. Digite /ajuda para os comandos.")
        try:
            while self.rodando:
                self.tratar_linha(input().strip())
        except (KeyboardInterrupt, EOFError):
            pass
        self.rodando = False
        receptor.join(timeout=1)
        self.fechar()
        print("encerrado.")


def main():
    parser = argparse.ArgumentParser(description="Terminal USB-CDC da jiga de testes.")
    parser.add_argument("--porta", help="porta serial (ex.: COM14). Padrao: detecta o STM32")
    parser.add_argument("--modo", choices=["hex", "texto"], default="hex",
                        help="forma de exibir os dados recebidos (padrao: hex)")
    parser.add_argument("--log", help="arquivo onde gravar o que for exibido")
    parser.add_argument("--listar", action="store_true", help="lista as portas seriais e sai")
    args = parser.parse_args()

    if args.listar:
        listar_portas()
        return 0

    log = open(args.log, "a", encoding="utf-8") if args.log else None
    try:
        Terminal(args.porta, args.modo, log).executar()
    finally:
        if log:
            log.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
