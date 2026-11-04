"""
Baterias de testes em YAML (secao 3.6.3 da monografia): carregar, validar e compilar cada caso em uma rotina.

    bateria: controle-ventilador
    dut: ESP32
    casos:
      - nome: liga acima do limiar em ate 100 ms
        estimulos:
          - dac: {canal: 1, tensao: 2.0}
        verificar:
          - reacao_gpio: {pino: 5, nivel: 1, max_ms: 100}

Cada caso vira uma rotina: os estimulos e as verificacoes, na ordem, viram passos (as grandezas fisicas sao
convertidas para os valores do protocolo); 'espera_ms' do caso entra entre os dois. Os valores esperados e as
tolerancias ficam no PC, para avaliar as medicoes (motor.py). Toda a bateria e validada antes de falar com a jiga:
ErroBateria traz todos os erros encontrados, cada um com o caso e o item.

A rotina termina sempre no estado seguro: cada caso configura o que usa (UART, I2C, SPI), sem depender do anterior.

Sensores emulados (secao 'sensores' da bateria, sensores.py): a configuracao do sensor (registradores iniciais,
mascaras, enderecos) passa de 100 B e nao cabe junto com o caso nos 240 B de uma rotina; ela vira o 'preparo' do caso,
comandos diretos que o motor envia antes do RUN_ROUTINE, com a jiga livre. Na rotina ficam so as mudancas de valor
(estimulos 'mpu6500' e 'bme280', um I2C_SLAVE_LOAD dos registradores de dados).
"""

import math
from dataclasses import dataclass, field

import yaml

from protocolo import (
    ADC_CANAIS, ADC_VALOR_MAX, CMD_ADC_READ, CMD_DAC_SET, CMD_GPIO_READ, CMD_GPIO_SET, CMD_I2C_CONFIG,
    CMD_I2C_MASTER_XFER, CMD_I2C_SLAVE_DUMP, CMD_I2C_SLAVE_LOAD, CMD_PWM_SET, CMD_PWM_STOP, CMD_SPI_CONFIG,
    CMD_SPI_MASTER_XFER, CMD_SPI_SLAVE_LOAD, CMD_SPI_SLAVE_READ, CMD_UART_CONFIG, CMD_UART_READ, CMD_UART_WRITE,
    DAC_CANAIS, DAC_VALOR_MAX, GPIO_SAIDAS, I2C_ENDERECO_MAX, I2C_ENDERECO_MIN, I2C_PAPEL_ESCRAVO, I2C_PAPEL_MESTRE,
    I2C_TAMANHO_MAPA, PASSO_ESPERA, PASSO_ESPERA_GPIO, PAYLOAD_MAX, PWM_CANAIS, PWM_FREQUENCIA_MAX,
    PWM_FREQUENCIA_MIN, ROTINA_ENTRADAS, ROTINA_INDICE_CABECALHO, ROTINA_TEMPO_MAX_MS, SPI_FREQUENCIA_MIN,
    SPI_MODO_MAX, SPI_PAPEL_ESCRAVO, SPI_PAPEL_MESTRE, SPI_TAMANHO_BUFFER, TENSAO_REFERENCIA, UART_BAUD_MAX,
    UART_BAUD_MIN, UART_TAMANHO_MAX, montar_rotina, porcento_para_razao, validar_rotina,
    CMD_I2C_SLAVE_ADDR2, CMD_I2C_SLAVE_MASK, CMD_SPI_SLAVE_REG,
)
from sensores import SensorEmulado
from ondas import ErroOnda, compilar as compilar_onda

TOLERANCIA_ADC_V = 0.033            # 1 % do fundo de escala (criterios de aceitacao, secao 3.7.3)
MARGEM_TEMPO_MS = 100               # tempo maximo da rotina = (esperas + limites) x 1,1 + 100 ms
BITS_POR_BYTE_UART = 10
SPI_PAPEIS = {"escrava": SPI_PAPEL_ESCRAVO, "mestre": SPI_PAPEL_MESTRE}
I2C_PAPEIS = {"escravo": I2C_PAPEL_ESCRAVO, "mestre": I2C_PAPEL_MESTRE}
I2C_VELOCIDADES_KHZ = {100: 0, 400: 1}
SENSORES = {"mpu6500": 0x68, "bme280": 0x76}          # modelo -> endereco padrao
ENDERECOS_SENSOR = {"mpu6500": (0x68, 0x69), "bme280": (0x76, 0x77)}
UART_DUT_BAUD = 115200                                # UART do DUT de referencia
ESPERA_DUT_MS = 50                                    # tempo para o DUT responder um comando de texto
PAUSA_DESCARTE_UART = 0.01                            # s: o DUT responde ao fim de linha do descarte


class ErroBateria(Exception):
    """Bateria invalida: 'erros' e a lista de mensagens (uma por problema)."""

    def __init__(self, erros):
        self.erros = list(erros)
        super().__init__("\n".join(self.erros))


@dataclass
class Verificacao:
    """O que conferir no registro de um passo de aquisicao."""

    tipo: str
    parametros: dict
    indice_passo: int
    descricao: str


@dataclass
class CasoDeTeste:
    nome: str
    identificador: int
    passos: list
    verificacoes: list
    tempo_max_ms: int
    estimulos: list = field(default_factory=list)     # descricao de cada estimulo, para o relatorio
    preparo: list = field(default_factory=list)       # (cmd, payload) ou pausa em s, antes do RUN_ROUTINE

    @property
    def payload(self):
        return montar_rotina(self.identificador, self.tempo_max_ms, self.passos)


@dataclass
class Bateria:
    nome: str
    casos: list
    dut: str = ""
    arquivo: str = ""
    sensores: list = field(default_factory=list)      # SensorEmulado


class _Item:
    """Erro de um item: guarda o contexto para a mensagem."""

    def __init__(self, contexto, erros):
        self.contexto, self.erros = contexto, erros

    def falha(self, mensagem):
        self.erros.append(f"{self.contexto}: {mensagem}")
        raise _ItemInvalido

    def numero(self, dados, chave, minimo=None, maximo=None, inteiro=False, padrao=None):
        if chave not in dados:
            if padrao is not None:
                return padrao
            self.falha(f"falta '{chave}'")
        valor = dados[chave]
        if isinstance(valor, bool) or not isinstance(valor, (int, float)) or (inteiro and not isinstance(valor, int)):
            self.falha(f"'{chave}' precisa ser {'inteiro' if inteiro else 'numero'} (veio {valor!r})")
        if (minimo is not None and valor < minimo) or (maximo is not None and valor > maximo):
            self.falha(f"'{chave}' = {valor} fora de {minimo} a {maximo}")
        return valor

    def escolha(self, dados, chave, opcoes):
        if chave not in dados:
            self.falha(f"falta '{chave}'")
        valor = dados[chave]
        if valor not in opcoes:
            self.falha(f"'{chave}' = {valor!r}; opcoes: {', '.join(str(o) for o in opcoes)}")
        return opcoes[valor] if isinstance(opcoes, dict) else valor

    def bytes_(self, dados, chave, minimo=1, maximo=PAYLOAD_MAX, texto=None):
        """'dados' em hex ("DE AD"), lista de inteiros ou, com texto=chave, texto (com \\n no YAML)."""
        if texto and texto in dados:
            valor = dados[texto]
            if not isinstance(valor, str):
                self.falha(f"'{texto}' precisa ser texto")
            resultado = valor.encode("utf-8")
        elif chave in dados:
            valor = dados[chave]
            try:
                if isinstance(valor, str):
                    resultado = bytes.fromhex(valor)
                elif isinstance(valor, int) and not isinstance(valor, bool):
                    resultado = bytes([valor])
                else:
                    resultado = bytes(valor)
            except (ValueError, TypeError):
                self.falha(f"'{chave}' precisa ser hex (\"DE AD\") ou lista de bytes")
        else:
            self.falha(f"falta '{chave}'" + (f" ou '{texto}'" if texto else ""))
        if not minimo <= len(resultado) <= maximo:
            self.falha(f"'{chave if not texto or texto not in dados else texto}' com {len(resultado)} B; "
                       f"de {minimo} a {maximo}")
        return resultado

    def sem_sobras(self, dados, chaves):
        sobras = sorted(set(dados) - set(chaves))
        if sobras:
            self.falha(f"campo(s) desconhecido(s): {', '.join(map(str, sobras))}")


class _ItemInvalido(Exception):
    pass


def tensao_para_codigo(tensao):
    """V -> codigo de 12 bits do DAC: round(4095 x V / 3,3)."""
    return round(DAC_VALOR_MAX * tensao / TENSAO_REFERENCIA)


def _hex(dados):
    return dados.hex(" ").upper()


def _texto_ou_hex(dados):
    try:
        texto = dados.decode("ascii")
        if texto.isprintable() or texto.replace("\n", "").replace("\r", "").isprintable():
            return repr(texto)
    except UnicodeDecodeError:
        pass
    return _hex(dados)


class _Compilador:
    """Compila um caso: lista de passos, verificacoes e o tempo das esperas."""

    def __init__(self, item, sensores=()):
        self.item = item
        self.passos, self.verificacoes, self.estimulos = [], [], []
        self.tempo_us = 0
        self.uart_baud = None
        self.descarte_uart = None   # baud do descarte da linha do DUT no preparo (uart_valores)
        self.sensores = {s.modelo: s for s in sensores}
        self.estados = {s.modelo: dict(s.estado) for s in sensores}
        self.gerador = 0

    def _passo(self, cmd, parametros=b""):
        self.passos.append(bytes([cmd]) + bytes(parametros))
        return len(self.passos) - 1

    def _verificacao(self, tipo, parametros, indice, descricao):
        self.verificacoes.append(Verificacao(tipo, parametros, indice, descricao))

    def espera(self, chave, valor):
        fator = 1000 if chave == "espera_ms" else 1
        if isinstance(valor, bool) or not isinstance(valor, (int, float)) or valor < 0:
            self.item.falha(f"'{chave}' precisa ser um numero >= 0")
        us = round(valor * fator)
        if us > 0xFFFF_FFFF:
            self.item.falha(f"'{chave}' grande demais")
        self._passo(PASSO_ESPERA, us.to_bytes(4, "little"))
        self.tempo_us += us
        self.estimulos.append(f"espera {us / 1000:g} ms")

    def estimulo(self, tipo, d):
        it = self.item
        if tipo == "dac":
            canal = it.numero(d, "canal", inteiro=True)
            if canal not in DAC_CANAIS:
                it.falha(f"canal do DAC {canal}; opcoes: 1, 2")
            if "codigo" in d:
                it.sem_sobras(d, ("canal", "codigo"))
                codigo = it.numero(d, "codigo", 0, DAC_VALOR_MAX, inteiro=True)
                texto = f"DAC{canal} = codigo {codigo}"
            else:
                it.sem_sobras(d, ("canal", "tensao"))
                tensao = it.numero(d, "tensao", 0, TENSAO_REFERENCIA)
                codigo = tensao_para_codigo(tensao)
                texto = f"DAC{canal} = {tensao:g} V (codigo {codigo})"
            self._passo(CMD_DAC_SET, bytes([canal]) + codigo.to_bytes(2, "little"))
        elif tipo == "gpio_out":
            it.sem_sobras(d, ("pino", "nivel"))
            pino = it.numero(d, "pino", inteiro=True)
            if pino not in GPIO_SAIDAS:
                it.falha(f"saida GPIO{pino}; opcoes: 1 a 4")
            nivel = it.escolha(d, "nivel", (0, 1))
            self._passo(CMD_GPIO_SET, bytes([pino, nivel]))
            texto = f"GPIO{pino} = {nivel}"
        elif tipo == "pwm":
            it.sem_sobras(d, ("canal", "frequencia", "razao"))
            canal = it.escolha(d, "canal", PWM_CANAIS)
            frequencia = it.numero(d, "frequencia", PWM_FREQUENCIA_MIN, PWM_FREQUENCIA_MAX, inteiro=True)
            razao = it.numero(d, "razao", 0, 100)
            self._passo(CMD_PWM_SET, bytes([canal]) + frequencia.to_bytes(4, "little")
                        + porcento_para_razao(razao).to_bytes(2, "little"))
            texto = f"PWM{canal} = {frequencia} Hz, {razao:g} %"
        elif tipo == "pwm_stop":
            it.sem_sobras(d, ("canal",))
            canal = it.escolha(d, "canal", PWM_CANAIS)
            self._passo(CMD_PWM_STOP, bytes([canal]))
            texto = f"PWM{canal} parado"
        elif tipo == "i2c_config":
            it.sem_sobras(d, ("papel", "endereco", "velocidade"))
            papel = it.escolha(d, "papel", I2C_PAPEIS)
            endereco = 0
            if papel == I2C_PAPEL_ESCRAVO:
                endereco = it.numero(d, "endereco", I2C_ENDERECO_MIN, I2C_ENDERECO_MAX, inteiro=True)
            velocidade = it.escolha(d, "velocidade", I2C_VELOCIDADES_KHZ) if "velocidade" in d else 0
            self._passo(CMD_I2C_CONFIG, bytes([papel, endereco, velocidade]))
            texto = f"I2C {d['papel']}" + (f" 0x{endereco:02X}" if papel == I2C_PAPEL_ESCRAVO else "")
        elif tipo == "i2c_load":
            it.sem_sobras(d, ("offset", "dados"))
            offset = it.numero(d, "offset", 0, I2C_TAMANHO_MAPA - 1, inteiro=True)
            dados = it.bytes_(d, "dados", maximo=I2C_TAMANHO_MAPA - offset)
            self._passo(CMD_I2C_SLAVE_LOAD, bytes([offset, len(dados)]) + dados)
            texto = f"I2C mapa[0x{offset:02X}] = {_hex(dados)}"
        elif tipo == "spi_config":
            it.sem_sobras(d, ("papel", "modo", "frequencia"))
            papel = it.escolha(d, "papel", SPI_PAPEIS)
            modo = it.numero(d, "modo", 0, SPI_MODO_MAX, inteiro=True, padrao=0)
            frequencia = 0
            if papel == SPI_PAPEL_MESTRE:
                frequencia = it.numero(d, "frequencia", SPI_FREQUENCIA_MIN, 0xFFFF_FFFF, inteiro=True)
            self._passo(CMD_SPI_CONFIG, bytes([papel, modo]) + frequencia.to_bytes(4, "little"))
            texto = f"SPI {d['papel']}, modo {modo}" + (f", {frequencia} Hz" if frequencia else "")
        elif tipo == "spi_load":
            it.sem_sobras(d, ("dados",))
            dados = it.bytes_(d, "dados", minimo=0, maximo=SPI_TAMANHO_BUFFER)
            self._passo(CMD_SPI_SLAVE_LOAD, bytes([len(dados)]) + dados)
            texto = f"SPI escrava envia {_hex(dados) or '(nada)'}"
        elif tipo == "uart_config":
            it.sem_sobras(d, ("baud",))
            self.uart_baud = it.numero(d, "baud", UART_BAUD_MIN, UART_BAUD_MAX, inteiro=True)
            self._passo(CMD_UART_CONFIG, self.uart_baud.to_bytes(4, "little"))
            texto = f"UART {self.uart_baud} baud"
        elif tipo in SENSORES:
            texto = self.estimulo_sensor(tipo, d)
        elif tipo == "onda":
            it.sem_sobras(d, ("alvo", "forma", "de", "ate", "duracao_ms", "min", "max", "periodo_ms", "fase", "razao",
                              "tau_ms", "pontos", "interpolar", "ciclos", "passo_us", "ruido"))
            try:
                passos, self.gerador, texto = compilar_onda(d, list(self.sensores.values()), self.estados, self.gerador)
            except ErroOnda as e:
                it.falha(str(e))
            self.passos.extend(passos)
        elif tipo == "uart_write":
            it.sem_sobras(d, ("dados", "texto"))
            if self.uart_baud is None:
                it.falha("uart_write sem uart_config antes, no mesmo caso")
            dados = it.bytes_(d, "dados", maximo=UART_TAMANHO_MAX, texto="texto")
            self._passo(CMD_UART_WRITE, bytes([len(dados)]) + dados)
            self.tempo_us += math.ceil(len(dados) * BITS_POR_BYTE_UART * 1_000_000 / self.uart_baud)
            texto = f"UART envia {_texto_ou_hex(dados)}"
        else:
            it.falha(f"estimulo desconhecido '{tipo}'")
        self.estimulos.append(texto)

    def estimulo_sensor(self, modelo, d):
        """Novo valor de um sensor emulado: atualiza o estado do caso e carrega os registradores de dados."""
        it = self.item
        if modelo not in self.sensores:
            it.falha(f"'{modelo}' nao esta na secao 'sensores' da bateria")
        estado = self.estados[modelo]
        if modelo == "mpu6500":
            it.sem_sobras(d, ("aceleracao", "giro", "temperatura"))
            sensor = self.sensores[modelo]
            for chave, limite in (("aceleracao", sensor.escala_g), ("giro", sensor.escala_dps)):
                if chave in d:
                    valor = d[chave]
                    if (not isinstance(valor, list) or len(valor) != 3 or
                            any(isinstance(v, bool) or not isinstance(v, (int, float)) for v in valor)):
                        it.falha(f"'{chave}' precisa ser [x, y, z]")
                    if any(abs(v) > limite for v in valor):
                        it.falha(f"'{chave}' fora do fundo de escala de +-{limite}")
                    estado[chave] = tuple(float(v) for v in valor)
            if "temperatura" in d:
                estado["temperatura"] = it.numero(d, "temperatura", -40, 85)
        else:
            it.sem_sobras(d, ("temperatura", "pressao", "umidade"))
            for chave, minimo, maximo in (("temperatura", -40, 85), ("pressao", 300, 1100), ("umidade", 0, 100)):
                if chave in d:
                    estado[chave] = it.numero(d, chave, minimo, maximo)
        if not d:
            it.falha("sem valores")
        offset, dados = self.sensores[modelo].dados(estado)
        self._passo(CMD_I2C_SLAVE_LOAD, bytes([offset, len(dados)]) + dados)
        return f"{modelo}: " + ", ".join(f"{k} {v}" for k, v in d.items())

    def verificacao(self, tipo, d):
        it = self.item
        if tipo == "uart_valores":
            it.sem_sobras(d, ("comando", "esperado", "tolerancia", "baud", "espera_ms", "max"))
            comando = it.bytes_(d, "comando", maximo=UART_TAMANHO_MAX, texto="comando") if isinstance(
                d.get("comando"), str) else it.falha("'comando' precisa ser texto (ex.: \"MPU\\n\")")
            esperado = d.get("esperado")
            if (not isinstance(esperado, list) or not esperado or
                    any(isinstance(v, bool) or not isinstance(v, (int, float)) for v in esperado)):
                it.falha("'esperado' precisa ser uma lista de numeros")
            tolerancia = d.get("tolerancia", 0)
            tolerancias = tolerancia if isinstance(tolerancia, list) else [tolerancia] * len(esperado)
            if (len(tolerancias) != len(esperado) or
                    any(isinstance(v, bool) or not isinstance(v, (int, float)) or v < 0 for v in tolerancias)):
                it.falha("'tolerancia' precisa ser um numero >= 0 ou uma lista do tamanho de 'esperado'")
            baud = it.numero(d, "baud", UART_BAUD_MIN, UART_BAUD_MAX, inteiro=True, padrao=UART_DUT_BAUD)
            espera = it.numero(d, "espera_ms", 0, 60_000, padrao=ESPERA_DUT_MS)
            maximo = it.numero(d, "max", 1, UART_TAMANHO_MAX, inteiro=True, padrao=UART_TAMANHO_MAX)
            if self.uart_baud != baud:
                self._passo(CMD_UART_CONFIG, baud.to_bytes(4, "little"))
                self.uart_baud = baud
            if self.descarte_uart is None:
                self.descarte_uart = baud
            self._passo(CMD_UART_WRITE, bytes([len(comando)]) + comando)
            us = round(espera * 1000) + math.ceil(len(comando) * BITS_POR_BYTE_UART * 1_000_000 / baud)
            self._passo(PASSO_ESPERA, us.to_bytes(4, "little"))
            self.tempo_us += us
            indice = self._passo(CMD_UART_READ, bytes([maximo]))
            self._verificacao(tipo, {"esperado": [float(v) for v in esperado],
                                     "tolerancia": [float(v) for v in tolerancias]}, indice,
                              f"DUT responde a {_texto_ou_hex(comando)} com {esperado}")
        elif tipo == "gpio_in":
            it.sem_sobras(d, ("pino", "esperado"))
            pino = it.escolha(d, "pino", ROTINA_ENTRADAS)
            esperado = it.escolha(d, "esperado", (0, 1))
            indice = self._passo(CMD_GPIO_READ)
            self._verificacao(tipo, {"pino": pino, "esperado": esperado}, indice, f"GPIO{pino} = {esperado}")
        elif tipo == "adc":
            it.sem_sobras(d, ("canal", "esperado", "tolerancia"))
            canal = it.escolha(d, "canal", ADC_CANAIS)
            esperado = it.numero(d, "esperado", 0, TENSAO_REFERENCIA)
            tolerancia = it.numero(d, "tolerancia", 0, TENSAO_REFERENCIA, padrao=TOLERANCIA_ADC_V)
            indice = self._passo(CMD_ADC_READ, bytes([canal]))
            self._verificacao(tipo, {"canal": canal, "esperado": esperado, "tolerancia": tolerancia}, indice,
                              f"A{canal} = {esperado:g} V +- {tolerancia:g} V")
        elif tipo == "reacao_gpio":
            it.sem_sobras(d, ("pino", "nivel", "max_ms"))
            pino = it.escolha(d, "pino", ROTINA_ENTRADAS)
            nivel = it.escolha(d, "nivel", (0, 1))
            max_ms = it.numero(d, "max_ms", 0, 0xFFFF_FFFF / 1000)
            limite = round(max_ms * 1000)
            indice = self._passo(PASSO_ESPERA_GPIO, bytes([pino, nivel]) + limite.to_bytes(4, "little"))
            self.tempo_us += limite
            self._verificacao(tipo, {"pino": pino, "nivel": nivel, "max_ms": max_ms}, indice,
                              f"GPIO{pino} vai a {nivel} em ate {max_ms:g} ms")
        elif tipo == "uart_read":
            it.sem_sobras(d, ("esperado", "esperado_texto", "max"))
            esperado = it.bytes_(d, "esperado", minimo=0, maximo=UART_TAMANHO_MAX, texto="esperado_texto")
            maximo = it.numero(d, "max", 0, UART_TAMANHO_MAX, inteiro=True, padrao=UART_TAMANHO_MAX)
            indice = self._passo(CMD_UART_READ, bytes([maximo]))
            self._verificacao(tipo, {"esperado": esperado}, indice, f"UART recebe {_texto_ou_hex(esperado)}")
        elif tipo == "i2c_dump":
            it.sem_sobras(d, ("offset", "esperado"))
            offset = it.numero(d, "offset", 0, I2C_TAMANHO_MAPA - 1, inteiro=True)
            esperado = it.bytes_(d, "esperado", maximo=min(PAYLOAD_MAX, I2C_TAMANHO_MAPA - offset))
            indice = self._passo(CMD_I2C_SLAVE_DUMP, bytes([offset, len(esperado)]))
            self._verificacao(tipo, {"esperado": esperado}, indice, f"I2C mapa[0x{offset:02X}] = {_hex(esperado)}")
        elif tipo == "i2c_xfer":
            it.sem_sobras(d, ("endereco", "escrita", "esperado"))
            endereco = it.numero(d, "endereco", I2C_ENDERECO_MIN, I2C_ENDERECO_MAX, inteiro=True)
            escrita = it.bytes_(d, "escrita", minimo=0) if "escrita" in d else b""
            esperado = it.bytes_(d, "esperado", minimo=0) if "esperado" in d else b""
            if not escrita and not esperado:
                it.falha("i2c_xfer sem 'escrita' nem 'esperado'")
            indice = self._passo(CMD_I2C_MASTER_XFER, bytes([endereco, len(escrita), len(esperado)]) + escrita)
            self._verificacao(tipo, {"esperado": esperado}, indice,
                              f"I2C 0x{endereco:02X}: escreve {_hex(escrita) or '-'}, le {_hex(esperado) or '-'}")
        elif tipo == "spi_read":
            it.sem_sobras(d, ("esperado",))
            esperado = it.bytes_(d, "esperado", minimo=0, maximo=SPI_TAMANHO_BUFFER)
            indice = self._passo(CMD_SPI_SLAVE_READ)
            self._verificacao(tipo, {"esperado": esperado}, indice, f"SPI escrava recebe {_hex(esperado) or '(nada)'}")
        elif tipo == "spi_xfer":
            it.sem_sobras(d, ("dados", "esperado"))
            dados = it.bytes_(d, "dados", maximo=SPI_TAMANHO_BUFFER)
            esperado = it.bytes_(d, "esperado", maximo=SPI_TAMANHO_BUFFER)
            if len(esperado) != len(dados):
                it.falha("spi_xfer: 'esperado' precisa ter o tamanho de 'dados' (full-duplex)")
            indice = self._passo(CMD_SPI_MASTER_XFER, bytes([len(dados)]) + dados)
            self._verificacao(tipo, {"esperado": esperado}, indice, f"SPI envia {_hex(dados)}, recebe {_hex(esperado)}")
        else:
            it.falha(f"verificacao desconhecida '{tipo}'")


def _um_item(item_yaml, contexto, erros):
    if not isinstance(item_yaml, dict) or len(item_yaml) != 1:
        erros.append(f"{contexto}: cada item e um mapa com uma chave (ex.: '- dac: {{canal: 1, tensao: 2.0}}')")
        raise _ItemInvalido
    [(tipo, dados)] = item_yaml.items()
    return tipo, dados


def tempo_maximo_ms(tempo_us):
    """(esperas + limites) x 1,1 + 100 ms, em ms inteiros (arredondado para cima), ate 1 h."""
    return min(ROTINA_TEMPO_MAX_MS, -(-tempo_us * 11 // 10_000) + MARGEM_TEMPO_MS)


def preparo_descarte_uart(baud):
    """Descarte da linha do DUT antes da rotina (casos com uart_valores): UART_CONFIG, um fim de linha e uma pausa.

    Com a UART da jiga desligada (estado seguro entre os casos) o TX fica solto, e o DUT pode juntar bytes sem fim
    de linha na frente do comando; o fim de linha os encerra (o DUT responde ERR CMD) e o UART_CONFIG da propria
    rotina esvazia a recepcao antes do comando. Sem pausa, a resposta poderia chegar depois desse UART_CONFIG.
    """
    if baud is None:
        return []
    return [(CMD_UART_CONFIG, baud.to_bytes(4, "little")), (CMD_UART_WRITE, bytes([1]) + b"\n"),
            PAUSA_DESCARTE_UART]


def preparo_sensores(sensores):
    """Comandos diretos que deixam a jiga respondendo como os sensores: (cmd, payload) na ordem de envio.

    Segundo endereco antes do I2C_CONFIG (que o aplica); o mapa e as mascaras antes de ligar a escrava, para o DUT
    nunca ler um registrador vazio. O I2C fica a 400 kHz (atende tambem mestres de 100 kHz).
    """
    if not sensores:
        return []
    i2c = [s for s in sensores if s.barramento == "i2c"]
    spi = [s for s in sensores if s.barramento == "spi"]
    comandos = [(CMD_I2C_SLAVE_ADDR2, bytes([i2c[1].endereco if len(i2c) > 1 else 0]))] if i2c else []
    # O mapa e as mascaras sao os da escrava I2C, usados tambem pela escrava SPI no modo registradores
    for sensor in sensores:
        for offset, valores, mascaras in sensor.registradores():
            comandos.append((CMD_I2C_SLAVE_LOAD, bytes([offset, len(valores)]) + valores))
            comandos.append((CMD_I2C_SLAVE_MASK, bytes([offset, len(mascaras)]) + mascaras))
    if i2c:
        comandos.append((CMD_I2C_CONFIG, bytes([I2C_PAPEL_ESCRAVO, i2c[0].endereco, I2C_VELOCIDADES_KHZ[400]])))
    if spi:
        # BME280: so 7 bits de endereco no SPI; a jiga completa com 0x80 (registradores 0x80-0xFF)
        ou = 0x80 if spi[0].modelo == "bme280" else 0x00
        comandos.append((CMD_SPI_CONFIG, bytes([SPI_PAPEL_ESCRAVO, spi[0].modo_spi]) + bytes(4)))
        comandos.append((CMD_SPI_SLAVE_REG, bytes([1, ou])))
    return comandos


def carregar_sensores(dados, erros):
    """Secao 'sensores' -> lista de SensorEmulado (erros em 'erros')."""
    if dados is None:
        return []
    if not isinstance(dados, dict):
        erros.append("'sensores' precisa ser um mapa (ex.: {mpu6500: {barramento: i2c, endereco: 0x68}})")
        return []
    sensores = []
    for modelo, config in dados.items():
        item = _Item(f"sensores, {modelo}", erros)
        try:
            if modelo not in SENSORES:
                item.falha(f"sensor desconhecido; opcoes: {', '.join(SENSORES)}")
            config = config or {}
            if not isinstance(config, dict):
                item.falha("precisa ser um mapa (barramento, endereco, ...)")
            chaves = ("barramento", "endereco", "escala_g", "escala_dps", "modo") if modelo == "mpu6500" else (
                "barramento", "endereco", "modo")
            item.sem_sobras(config, chaves)
            barramento = item.escolha(config, "barramento", ("i2c", "spi")) if "barramento" in config else "i2c"
            endereco = item.escolha(config, "endereco", ENDERECOS_SENSOR[modelo]) if "endereco" in config \
                else SENSORES[modelo]
            sensor = SensorEmulado(modelo, endereco, barramento)
            if "modo" in config:
                if barramento != "spi":
                    item.falha("'modo' so com 'barramento: spi'")
                sensor.modo_spi = item.escolha(config, "modo", (0, 3))
            if modelo == "mpu6500":
                if "escala_g" in config:
                    sensor.escala_g = item.escolha(config, "escala_g", (2, 4, 8, 16))
                if "escala_dps" in config:
                    sensor.escala_dps = item.escolha(config, "escala_dps", (250, 500, 1000, 2000))
            sensores.append(sensor)
        except _ItemInvalido:
            pass
    return sensores


def compilar_caso(dados, identificador, erros, sensores=()):
    """Um caso do YAML -> CasoDeTeste (ou None, com as mensagens em 'erros')."""
    nome = dados.get("nome") if isinstance(dados, dict) else None
    rotulo = f"caso {identificador}" + (f" ('{nome}')" if nome else "")
    if not isinstance(dados, dict):
        erros.append(f"{rotulo}: precisa ser um mapa (nome, estimulos, verificar)")
        return None
    inicio = len(erros)
    desconhecidos = sorted(set(dados) - {"nome", "estimulos", "espera_ms", "verificar", "tempo_max_ms"})
    if desconhecidos:
        erros.append(f"{rotulo}: campo(s) desconhecido(s): {', '.join(map(str, desconhecidos))}")
    if not isinstance(nome, str) or not nome.strip():
        erros.append(f"{rotulo}: falta 'nome'")
    compilador = _Compilador(None, sensores)
    for secao, tratar in (("estimulos", compilador.estimulo), ("verificar", compilador.verificacao)):
        lista = dados.get(secao) or []
        if not isinstance(lista, list):
            erros.append(f"{rotulo}: '{secao}' precisa ser uma lista")
            continue
        if secao == "verificar" and "espera_ms" in dados:
            compilador.item = _Item(f"{rotulo}, espera_ms", erros)
            try:
                compilador.espera("espera_ms", dados["espera_ms"])
            except _ItemInvalido:
                pass
        for n, item_yaml in enumerate(lista, 1):
            contexto = f"{rotulo}, {secao} {n}"
            try:
                tipo, parametros = _um_item(item_yaml, contexto, erros)
                compilador.item = _Item(f"{contexto} ({tipo})", erros)
                if tipo in ("espera_ms", "espera_us"):
                    compilador.espera(tipo, parametros)
                elif not isinstance(parametros, dict):
                    compilador.item.falha("os parametros precisam ser um mapa ({chave: valor})")
                else:
                    tratar(tipo, parametros)
            except _ItemInvalido:
                pass
    if not compilador.passos and len(erros) == inicio:
        erros.append(f"{rotulo}: sem estimulos nem verificacoes")
    if len(erros) > inicio:
        return None
    if "tempo_max_ms" in dados:
        tempo = dados["tempo_max_ms"]
        if isinstance(tempo, bool) or not isinstance(tempo, int) or not 0 < tempo <= ROTINA_TEMPO_MAX_MS:
            erros.append(f"{rotulo}: 'tempo_max_ms' de 1 a {ROTINA_TEMPO_MAX_MS}")
            return None
    else:
        tempo = tempo_maximo_ms(compilador.tempo_us)
    caso = CasoDeTeste(nome, identificador, compilador.passos, compilador.verificacoes, tempo, compilador.estimulos,
                       preparo_sensores(sensores) + preparo_descarte_uart(compilador.descarte_uart))
    try:
        payload = caso.payload
    except ValueError as e:
        erros.append(f"{rotulo}: nao cabe em um quadro ({e}); divida o caso em dois")
        return None
    indice, _ = validar_rotina(payload)
    if indice is not None:
        onde = "cabecalho" if indice == ROTINA_INDICE_CABECALHO else f"passo {indice}"
        erros.append(f"{rotulo}: rotina recusada na validacao ({onde}; resultados acima de 2048 B?)")
        return None
    return caso


class CarregadorDeBaterias:
    """Le e valida o YAML de uma bateria; ErroBateria com todos os erros."""

    def carregar(self, arquivo):
        with open(arquivo, encoding="utf-8") as f:
            texto = f.read()
        bateria = self.carregar_texto(texto)
        bateria.arquivo = str(arquivo)
        return bateria

    def carregar_texto(self, texto):
        try:
            dados = yaml.safe_load(texto)
        except yaml.YAMLError as e:
            raise ErroBateria([f"YAML invalido: {e}"]) from e
        return self.validar(dados)

    def validar(self, dados):
        if not isinstance(dados, dict):
            raise ErroBateria(["a bateria precisa ser um mapa com 'bateria' e 'casos'"])
        erros = []
        desconhecidos = sorted(set(dados) - {"bateria", "dut", "descricao", "sensores", "casos"})
        if desconhecidos:
            erros.append(f"campo(s) desconhecido(s): {', '.join(map(str, desconhecidos))}")
        nome = dados.get("bateria")
        if not isinstance(nome, str) or not nome.strip():
            erros.append("falta o nome da bateria ('bateria: ...')")
        casos_yaml = dados.get("casos")
        if not isinstance(casos_yaml, list) or not casos_yaml:
            erros.append("falta a lista 'casos'")
            casos_yaml = []
        if len(casos_yaml) > 0xFFFF:
            erros.append("casos demais")
        sensores = carregar_sensores(dados.get("sensores"), erros)
        i2c = [s for s in sensores if s.barramento == "i2c"]
        if len({s.endereco for s in i2c}) != len(i2c):
            erros.append("sensores: dois sensores no mesmo endereco")
        if sum(s.barramento == "spi" for s in sensores) > 1:
            erros.append("sensores: so um sensor no SPI (a jiga tem um CS)")
        casos = [compilar_caso(caso, n, erros, sensores) for n, caso in enumerate(casos_yaml, 1)]
        if erros:
            raise ErroBateria(erros)
        return Bateria(nome, casos, str(dados.get("dut") or ""), sensores=sensores)
