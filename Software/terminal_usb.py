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
    python terminal_usb.py --versao        # mostra a versao e sai

Comandos digitados durante a execucao:
    texto qualquer   envia o texto (sem quebra de linha)
    /hex 7E 01 02    envia os bytes em hexadecimal
    /quadro 01       envia um quadro do protocolo (CMD em hex; SEQ automatico)
    /quadro 10 AA BB quadro com payload em hex
    /pwm 1 1000 50   PWM1 (PE9) em 1000 Hz e 50 % (canal 1 a 3; razao aceita 12,34)
    /pwm_stop 1      para o PWM1 (razao 0, saida em nivel baixo)
    /teste           lista os casos de teste do protocolo
    /teste 3         envia o caso 3 (/teste todos envia todos, menos os de loopback)
    /teste loopback  envia todos, incluindo o CT10 e o CT11 (jumpers ligados)
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

from protocolo import (
    CASOS_TESTE, QUADRO_OK, ReceptorQuadros, casos_a_executar, descrever_log, descrever_pwm,
    interpretar_resposta, montar_pwm_set, montar_pwm_stop, montar_quadro, porcento_para_razao,
)
from versao import __version__

# VID/PID padrao do USB-CDC da STMicroelectronics (Virtual COM Port)
STM32_VID = 0x0483
STM32_CDC_PID = 0x5740

BYTES_POR_LINHA = 16

# Tempo sem receber bytes que marca o fim de uma rajada (s)
SILENCIO_FIM_RAJADA = 0.005

# Tempo maximo de uma rajada (s): com fluxo continuo (logs a cada poucos ms) o silencio nunca
# chega, e sem esse limite nada seria exibido ate a porta fechar
TEMPO_MAX_RAJADA = 0.05

# Pausa entre casos de teste, para o log de um nao se misturar com o do outro (s)
PAUSA_ENTRE_CASOS = 0.3


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


def interpretar_pwm(texto):
    """'1 1000 12,5' -> (canal, frequencia em Hz, razao em centesimos de %): (1, 1000, 1250).

    Nao confere a faixa do PWM: um valor invalido vai para a jiga, que responde NACK.
    """
    campos = texto.split()
    if len(campos) != 3:
        raise ValueError("uso: /pwm canal frequencia razao")
    return int(campos[0]), int(campos[1]), porcento_para_razao(campos[2].replace(",", "."))


class Terminal:
    def __init__(self, porta, modo, log):
        self.porta_fixa = porta
        self.modo = modo
        self.log = log
        self.serial = None
        self.rodando = True
        self.trava_saida = threading.Lock()
        self.seq = 0
        self.receptor = ReceptorQuadros()  # decodifica as respostas da jiga

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
        # Cada quadro completo vindo da jiga tambem aparece decodificado (pode chegar picado)
        for resultado, quadro in self.receptor.processar(dados):
            if resultado != QUADRO_OK:
                self.exibir(f"  => quadro invalido ({resultado})")
                continue
            try:
                self.exibir(f"  => {interpretar_resposta(quadro).descrever()}")
            except ValueError as e:
                self.exibir(f"  => resposta invalida: {e}")

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
        em vez de picada conforme os bytes chegam. A rajada dura no maximo
        TEMPO_MAX_RAJADA, para um fluxo continuo continuar aparecendo.
        """
        dados = bytearray(self.serial.read(1))
        inicio = time.monotonic()
        while dados and time.monotonic() - inicio < TEMPO_MAX_RAJADA:
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

    def enviar_quadro(self, argumentos):
        """'/quadro CMD [payload]': monta o quadro com SEQ automatico."""
        try:
            campos = interpretar_hex(argumentos)
            if not campos:
                raise ValueError
            quadro = montar_quadro(self.seq, campos[0], campos[1:])
        except ValueError:
            self.exibir("*** uso: /quadro CMD [payload], em hex; ex.: /quadro 01 ***")
            return
        self.exibir(f"*** quadro SEQ={self.seq} CMD=0x{campos[0]:02X} LEN={len(campos) - 1} ***")
        self.seq = (self.seq + 1) & 0xFF
        self.enviar(quadro)

    def enviar_pwm(self, argumentos, parar=False):
        """'/pwm canal frequencia razao' ou '/pwm_stop canal': monta o quadro com SEQ automatico."""
        try:
            if parar:
                canal = int(argumentos)
                quadro = montar_pwm_stop(self.seq, canal)
                descricao = f"PWM{canal} parado"
            else:
                canal, frequencia, razao = interpretar_pwm(argumentos)
                quadro = montar_pwm_set(self.seq, canal, frequencia, razao)
                try:
                    descricao = descrever_pwm(canal, frequencia, razao)
                except ValueError as e:
                    descricao = f"fora da faixa ({e}): a jiga deve responder NACK"
        except ValueError:
            self.exibir("*** uso: /pwm canal frequencia razao (ex.: /pwm 1 1000 50) ou /pwm_stop canal ***")
            return
        self.exibir(f"*** SEQ={self.seq} {descricao} ***")
        self.seq = (self.seq + 1) & 0xFF
        self.enviar(quadro)

    def executar_casos(self, argumento):
        """'/teste': lista os casos; '/teste N', '/teste todos' ou '/teste loopback' envia.

        'todos' pula os casos que precisam dos jumpers (CT10, CT11); 'loopback' inclui eles.
        """
        if not argumento:
            for i, caso in enumerate(CASOS_TESTE, 1):
                marca = " [loopback]" if caso.requer_loopback else ""
                self.exibir(f"  {i:2d}. {caso.nome:24s} {caso.descricao}{marca}")
            return
        if argumento in ("todos", "loopback"):
            casos = casos_a_executar(argumento == "loopback")
        elif argumento.isdigit() and 1 <= int(argumento) <= len(CASOS_TESTE):
            casos = [CASOS_TESTE[int(argumento) - 1]]
        else:
            self.exibir(f"*** caso invalido; use 1 a {len(CASOS_TESTE)}, 'todos' ou 'loopback' ***")
            return
        for caso in casos:
            self.exibir(f"*** caso '{caso.nome}' | esperado: log {descrever_log(caso.esperado)}"
                        f" | USB {caso.descrever_esperado()} ***")
            for tipo, valor in caso.passos:
                if tipo == "enviar":
                    self.enviar(valor)
                else:
                    time.sleep(valor)
            time.sleep(PAUSA_ENTRE_CASOS)

    def tratar_linha(self, linha):
        if linha in ("/sair", "/q"):
            self.rodando = False
        elif linha == "/ajuda":
            self.exibir(__doc__.split("Comandos digitados durante a execucao:")[1].rstrip())
        elif linha == "/modo":
            self.modo = "texto" if self.modo == "hex" else "hex"
            self.exibir(f"*** modo: {self.modo} ***")
        elif linha.startswith("/quadro"):
            self.enviar_quadro(linha[7:])
        elif linha.startswith("/pwm_stop"):
            self.enviar_pwm(linha[9:].strip(), parar=True)
        elif linha.startswith("/pwm"):
            self.enviar_pwm(linha[4:].strip())
        elif linha.startswith("/teste"):
            self.executar_casos(linha[6:].strip())
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
        self.exibir(f"Terminal USB-CDC da jiga v{__version__}. Digite /ajuda para os comandos.")
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
    parser.add_argument("--versao", action="version", version=f"%(prog)s {__version__}")
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
