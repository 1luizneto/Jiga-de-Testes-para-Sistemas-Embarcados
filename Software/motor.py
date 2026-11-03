"""
Motor de testes (secao 3.6.4 da monografia, fluxograma de execucao de uma bateria).

1. A bateria ja chega compilada e validada (bateria.py): nenhum erro de descricao aparece no meio da execucao.
2. PING (versao do firmware no relatorio) e RESET_ALL: a jiga comeca no estado seguro.
3. Para cada caso: RUN_ROUTINE, ROUTINE_STATUS ate a rotina terminar, READ_RESULT e o veredito.
   - CONCLUIDA -> cada verificacao e avaliada no seu registro: passou se todas passaram, senao falhou;
   - ERRO (o DUT nao respondeu num barramento) -> comportamento do DUT: falhou, com o codigo do erro;
   - ABORTADA, TEMPO_ESGOTADO ou falha de comunicacao (depois das retransmissoes) -> inconclusivo e a bateria
     para: nao se sabe se todos os estimulos foram aplicados;
   - interromper() (botao da interface) -> ABORT na jiga, o caso fica inconclusivo e os seguintes nao rodam.
4. RESET_ALL no fim, mesmo quando a bateria para no meio.
"""

import threading
import time

from cliente import ErroComunicacao, ErroNack
from protocolo import (
    ADC_VALOR_MAX, CODIGOS_ERRO, ESTADO_CONCLUIDA, ESTADO_ERRO, ESTADOS_ROTINA, GPIO_OCORREU, ROTINA_ENTRADAS,
    TENSAO_REFERENCIA, VERSAO_PROTOCOLO, interpretar_resultados,
)
from relatorio import FALHOU, INCONCLUSIVO, PASSOU, Relatorio, ResultadoCaso, ResultadoVerificacao

INTERVALO_STATUS = 0.02     # s entre dois ROUTINE_STATUS
FOLGA_TEMPO = 1.0           # s alem do tempo maximo da rotina antes de desistir de esperar o estado final
EPSILON_V = 1e-9


def _hex(dados):
    return bytes(dados).hex(" ").upper() or "(nada)"


def _volts(valor):
    return f"{valor:.3f} V"


def avaliar(verificacao, registro):
    """Verificacao + registro do passo (ou None) -> ResultadoVerificacao."""
    p, tipo = verificacao.parametros, verificacao.tipo
    if registro is None:
        return ResultadoVerificacao(verificacao.descricao, _esperado(verificacao), "sem medicao", False,
                                    verificacao.indice_passo)
    d = registro.dados
    if tipo == "gpio_in":
        nivel = (d[0] >> (p["pino"] - ROTINA_ENTRADAS[0])) & 1
        passou, obtido = nivel == p["esperado"], str(nivel)
    elif tipo == "adc":
        tensao = int.from_bytes(d[1:3], "little") * TENSAO_REFERENCIA / ADC_VALOR_MAX
        passou = abs(tensao - p["esperado"]) <= p["tolerancia"] + EPSILON_V
        obtido = f"{_volts(tensao)} (codigo {int.from_bytes(d[1:3], 'little')})"
    elif tipo == "reacao_gpio":
        decorrido = int.from_bytes(d[1:5], "little")
        passou = d[0] == GPIO_OCORREU
        obtido = f"{decorrido / 1000:.3f} ms" if passou else f"nao mudou em {decorrido / 1000:.3f} ms"
    elif tipo in ("uart_read", "spi_read"):
        recebidos = d[1:1 + d[0]]
        passou, obtido = recebidos == p["esperado"], _texto(recebidos)
    else:                        # i2c_dump, i2c_xfer, spi_xfer: o registro sao os bytes lidos
        passou, obtido = d == p["esperado"], _hex(d)
    return ResultadoVerificacao(verificacao.descricao, _esperado(verificacao), obtido, passou,
                                verificacao.indice_passo, registro.instante_us)


def _texto(dados):
    try:
        texto = dados.decode("ascii")
        if all(c.isprintable() or c in "\r\n" for c in texto):
            return repr(texto)
    except UnicodeDecodeError:
        pass
    return _hex(dados)


def _esperado(verificacao):
    p, tipo = verificacao.parametros, verificacao.tipo
    if tipo == "gpio_in":
        return str(p["esperado"])
    if tipo == "adc":
        return f"{_volts(p['esperado'])} +- {p['tolerancia'] * 1000:g} mV"
    if tipo == "reacao_gpio":
        return f"<= {p['max_ms']:g} ms"
    if tipo in ("uart_read", "spi_read"):
        return _texto(p["esperado"])
    return _hex(p["esperado"])


class MotorDeTestes:
    def __init__(self, cliente, intervalo_status=INTERVALO_STATUS, folga=FOLGA_TEMPO):
        self.cliente = cliente
        self.intervalo_status = intervalo_status
        self.folga = folga
        self._parar = threading.Event()

    def interromper(self):
        """Pedido do usuario: o caso em andamento recebe ABORT e fica inconclusivo."""
        self._parar.set()

    def executar(self, bateria, ao_iniciar_caso=None, ao_terminar_caso=None):
        """Executa a bateria inteira e devolve o Relatorio."""
        self._parar.clear()
        relatorio = Relatorio(bateria.nome, bateria.dut, bateria.arquivo,
                              porta=str(getattr(self.cliente.transporte, "porta", "") or ""))
        comandos, retransmissoes = self.cliente.comandos, self.cliente.retransmissoes
        try:
            protocolo, relatorio.firmware = self.cliente.ping()
            if protocolo != VERSAO_PROTOCOLO:
                raise ErroComunicacao(f"versao do protocolo {protocolo}; esperada {VERSAO_PROTOCOLO}")
            self.cliente.reset_all()
            for n, caso in enumerate(bateria.casos, 1):
                if self._parar.is_set():
                    relatorio.interrompida, relatorio.motivo = True, "interrompida pelo usuario"
                    break
                if ao_iniciar_caso:
                    ao_iniciar_caso(caso, n, len(bateria.casos))
                resultado = self._executar_caso(caso)
                relatorio.registrar(resultado)
                if ao_terminar_caso:
                    ao_terminar_caso(resultado)
                if resultado.veredito == INCONCLUSIVO:
                    relatorio.interrompida, relatorio.motivo = True, f"caso {caso.identificador}: {resultado.motivo}"
                    break
        except (ErroComunicacao, ErroNack) as e:
            relatorio.interrompida, relatorio.motivo = True, f"comunicacao com a jiga: {e}"
        finally:
            try:
                self.cliente.reset_all()
            except (ErroComunicacao, ErroNack):
                pass
            relatorio.comandos = self.cliente.comandos - comandos
            relatorio.retransmissoes = self.cliente.retransmissoes - retransmissoes
        return relatorio

    def _executar_caso(self, caso):
        resultado = ResultadoCaso(caso.nome, caso.identificador, INCONCLUSIVO, estimulos=list(caso.estimulos))
        inicio = time.monotonic()
        try:
            self._rodar(caso, resultado)
        except ErroNack as e:
            resultado.motivo = f"jiga recusou: {e}"
        except ErroComunicacao as e:
            resultado.motivo = f"comunicacao: {e}"
        resultado.duracao_s = round(time.monotonic() - inicio, 3)
        return resultado

    def _rodar(self, caso, resultado):
        self.cliente.run_routine(caso.identificador, caso.tempo_max_ms, caso.passos)
        esperado, abortado = 0.0, False
        while True:
            status = self.cliente.status()
            if status.finalizada:
                break
            if self._parar.is_set() and not abortado:
                self.cliente.abort()
                abortado = True
                continue
            if esperado > caso.tempo_max_ms / 1000 + self.folga:
                raise ErroComunicacao(f"a rotina nao terminou em {esperado:.1f} s")
            self.cliente.aguardar(self.intervalo_status)
            esperado += self.intervalo_status
        resultado.estado = ESTADOS_ROTINA.get(status.estado, str(status.estado))
        if status.estado not in (ESTADO_CONCLUIDA, ESTADO_ERRO):
            resultado.motivo = ("interrompida pelo usuario" if abortado
                                else f"rotina {resultado.estado} no passo {status.passos}")
            return
        identificador, dados = self.cliente.read_result()
        if identificador != caso.identificador:
            raise ErroComunicacao(f"resultados da rotina {identificador}; esperada {caso.identificador}")
        resultado.resultados_hex = bytes(dados).hex(" ").upper()
        registros = {r.indice: r for r in interpretar_resultados(dados)}
        resultado.verificacoes = [avaliar(v, registros.get(v.indice_passo)) for v in caso.verificacoes]
        if status.estado == ESTADO_ERRO:
            resultado.veredito = FALHOU
            resultado.motivo = (f"o DUT nao respondeu no passo {status.passos} "
                                f"({CODIGOS_ERRO.get(status.erro, status.erro)})")
            return
        falhas = [v.descricao for v in resultado.verificacoes if not v.passou]
        resultado.veredito = FALHOU if falhas else PASSOU
        if falhas:
            resultado.motivo = f"{len(falhas)} verificacao(oes) fora do esperado"
