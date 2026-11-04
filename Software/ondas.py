"""
Formas de onda das baterias -> tabela do passo ONDA (Onda.c na jiga).

A jiga toca uma tabela de 2 a 32 pontos igualmente espacados no periodo, com interpolacao linear ou degrau. Aqui cada
forma vira essa tabela, em unidades fisicas (V, %, g, graus/s, graus C, hPa), e depois em valores brutos do destino:

- rampa:          de, ate, duracao_ms                         -> 2 pontos, 1 ciclo (fica no 'ate')
- senoide:        min, max, periodo_ms, fase (graus)          -> 32 pontos (erro da interpolacao < 0,2 % da amplitude)
- triangular:     min, max, periodo_ms                        -> 3 pontos (exata)
- quadrada:       min, max, periodo_ms, razao (%)             -> degrau; a razao e aproximada com ate 31 divisoes
- dente_de_serra: min, max, periodo_ms                        -> 2 pontos, recomeca a cada periodo
- exponencial:    de, ate, tau_ms, duracao_ms (padrao 5 tau)  -> 32 pontos, 1 ciclo (resposta de um sistema de 1a ordem)
- pontos:         [[t_ms, valor], ...], interpolar            -> exata se os instantes caem numa grade de ate 31 passos

Comuns: ciclos (0 = ate o fim da rotina; as formas periodicas comecam com 0), passo_us (padrao 1000; 100 ou mais,
ou de 1 a 99 no DAC por DMA) e ruido (+- amplitude, nas unidades do alvo).
"""

import math
from functools import reduce

from protocolo import (
    ONDA_AMOSTRAS_MAX, ONDA_DAC, ONDA_DEGRAU, ONDA_ESCALA, ONDA_FAIXAS, ONDA_I2C_S16, ONDA_I2C_U16, ONDA_I2C_U20,
    ONDA_INTERPOLA, ONDA_PASSO_MIN_US, ONDA_PONTOS, ONDA_PWM, TENSAO_REFERENCIA, passo_onda,
)
from sensores import BME_DADOS, MPU_ACCEL_XOUT_H, bme_brutos

FORMAS = ("rampa", "senoide", "triangular", "quadrada", "dente_de_serra", "exponencial", "pontos")
PASSO_PADRAO_US = 1000
PONTOS_SENOIDE = 32
PONTOS_EXPONENCIAL = 32
PONTOS_BME = 16

# alvo -> (tipo, registro, faixa fisica, valor fisico -> bruto)
EIXOS = {"x": 0, "y": 1, "z": 2}


class ErroOnda(ValueError):
    pass


def tabela(forma, d):
    """Forma + parametros (unidades fisicas) -> (periodo_us, [valores fisicos], modo, ciclos padrao)."""
    def num(chave, padrao=None):
        if chave not in d:
            if padrao is None:
                raise ErroOnda(f"falta '{chave}'")
            return padrao
        valor = d[chave]
        if isinstance(valor, bool) or not isinstance(valor, (int, float)):
            raise ErroOnda(f"'{chave}' precisa ser numero")
        return float(valor)

    def periodo(chave="periodo_ms"):
        valor = num(chave)
        if valor <= 0:
            raise ErroOnda(f"'{chave}' precisa ser > 0")
        return round(valor * 1000)

    if forma == "rampa":
        return periodo("duracao_ms"), [num("de"), num("ate")], ONDA_INTERPOLA, 1
    if forma == "dente_de_serra":
        return periodo(), [num("min"), num("max")], ONDA_INTERPOLA, 0
    if forma == "triangular":
        return periodo(), [num("min"), num("max"), num("min")], ONDA_INTERPOLA, 0
    if forma == "senoide":
        minimo, maximo, fase = num("min"), num("max"), math.radians(num("fase", 0))
        meio, amp = (maximo + minimo) / 2, (maximo - minimo) / 2
        n = PONTOS_SENOIDE
        return periodo(), [meio + amp * math.sin(2 * math.pi * k / (n - 1) + fase) for k in range(n)], ONDA_INTERPOLA, 0
    if forma == "quadrada":
        minimo, maximo, razao = num("min"), num("max"), num("razao", 50) / 100
        if not 0 < razao < 1:
            raise ErroOnda("'razao' de 0 a 100 % (exclusivo)")
        divisoes, altos = min(((dv, round(razao * dv)) for dv in range(2, ONDA_PONTOS[1])),
                              key=lambda x: (abs(x[1] / x[0] - razao), x[0]))
        altos = min(max(altos, 1), divisoes - 1)
        return periodo(), [maximo] * altos + [minimo] * (divisoes - altos) + [maximo], ONDA_DEGRAU, 0
    if forma == "exponencial":
        de, ate, tau = num("de"), num("ate"), num("tau_ms")
        if tau <= 0:
            raise ErroOnda("'tau_ms' precisa ser > 0")
        duracao = num("duracao_ms", 5 * tau)
        n = PONTOS_EXPONENCIAL
        return (round(duracao * 1000), [ate + (de - ate) * math.exp(-duracao * k / (n - 1) / tau) for k in range(n)],
                ONDA_INTERPOLA, 1)
    if forma == "pontos":
        pontos = d.get("pontos")
        if (not isinstance(pontos, list) or len(pontos) < 2 or
                any(not isinstance(p, list) or len(p) != 2 for p in pontos)):
            raise ErroOnda("'pontos' precisa ser uma lista de [t_ms, valor] com pelo menos 2 pontos")
        tempos = [round(float(t) * 1000) for t, _ in pontos]
        valores = [float(v) for _, v in pontos]
        if tempos[0] != 0 or any(b <= a for a, b in zip(tempos, tempos[1:])):
            raise ErroOnda("'pontos': o primeiro em t = 0 e os instantes crescentes")
        interpolar = d.get("interpolar", True)
        duracao = tempos[-1]
        grade = reduce(math.gcd, tempos[1:])
        n = duracao // grade + 1
        if n > ONDA_PONTOS[1]:
            n = ONDA_PONTOS[1]                   # aproxima: reamostra em 32 pontos
        amostras = []
        for k in range(n):
            t = duracao * k / (n - 1)
            i = max(j for j in range(len(tempos)) if tempos[j] <= t + 1e-9)
            if i == len(tempos) - 1 or not interpolar:
                amostras.append(valores[i])
            else:
                f = (t - tempos[i]) / (tempos[i + 1] - tempos[i])
                amostras.append(valores[i] + (valores[i + 1] - valores[i]) * f)
        return duracao, amostras, ONDA_INTERPOLA if interpolar else ONDA_DEGRAU, 1
    raise ErroOnda(f"forma desconhecida; opcoes: {', '.join(FORMAS)}")


def _interpola(valores, f):
    """Valor em f (0 a 1) de uma tabela de pontos igualmente espacados, com interpolacao linear."""
    x = f * (len(valores) - 1)
    i = min(int(x), len(valores) - 2)
    return valores[i] + (valores[i + 1] - valores[i]) * (x - i)


def _normaliza(brutos):
    """Valores brutos -> (base, amplitude, valores de 0 a 65535)."""
    base, topo = min(brutos), max(brutos)
    if topo == base:
        return base, 0, [0] * len(brutos)
    return base, topo - base, [round((b - base) * ONDA_ESCALA / (topo - base)) for b in brutos]


def alvos(sensores):
    """Alvos disponiveis: nome -> (tipo, registro, (min, max) fisico, fisico -> bruto, ganho por unidade)."""
    a = {}
    for canal in (1, 2):
        a[f"dac{canal}"] = (ONDA_DAC, canal, (0, TENSAO_REFERENCIA), lambda v: round(4095 * v / TENSAO_REFERENCIA),
                            4095 / TENSAO_REFERENCIA)
    for canal in (1, 2, 3):
        a[f"pwm{canal}.razao"] = (ONDA_PWM, canal, (0, 100), lambda v: round(v * 100), 100)
    for s in sensores:
        if s.modelo == "mpu6500":
            g, dps = s.escala_g, s.escala_dps
            for eixo, i in EIXOS.items():
                a[f"mpu6500.aceleracao_{eixo}"] = (ONDA_I2C_S16, MPU_ACCEL_XOUT_H + 2 * i, (-g, g),
                                                    lambda v, g=g: round(v * 32768 / g), 32768 / g)
                a[f"mpu6500.giro_{eixo}"] = (ONDA_I2C_S16, MPU_ACCEL_XOUT_H + 8 + 2 * i, (-dps, dps),
                                              lambda v, d=dps: round(v * 32768 / d), 32768 / dps)
            a["mpu6500.temperatura"] = (ONDA_I2C_S16, MPU_ACCEL_XOUT_H + 6, (-40, 85),
                                        lambda v: round((v - 21) * 333.87), 333.87)
        if s.modelo == "bme280":
            a["bme280.temperatura"] = (ONDA_I2C_U20, BME_DADOS + 3, (-40, 85), None, None)
            a["bme280.pressao"] = (ONDA_I2C_U20, BME_DADOS, (300, 1100), None, None)
            a["bme280.umidade"] = (ONDA_I2C_U16, BME_DADOS + 6, (0, 100), None, None)
    return a


def compilar(d, sensores, estados, gerador):
    """Item 'onda' -> ([passos ONDA], proximo gerador livre, descricao). 'estados': estado atual dos sensores do caso.

    A temperatura do BME280 tambem muda a compensacao da pressao e da umidade (t_fine): para o DUT continuar lendo a
    pressao e a umidade do caso, uma onda de temperatura leva junto duas ondas que corrigem os valores brutos delas.
    """
    alvo, forma = d.get("alvo"), d.get("forma")
    disponiveis = alvos(sensores)
    if alvo not in disponiveis:
        raise ErroOnda(f"alvo '{alvo}' desconhecido; opcoes: {', '.join(disponiveis)}")
    periodo_us, fisicos, modo, ciclos = tabela(forma, d)
    if "ciclos" in d:
        c = d["ciclos"]
        if isinstance(c, bool) or not isinstance(c, int) or not 0 <= c <= 0xFFFF:
            raise ErroOnda("'ciclos' de 0 (ate o fim da rotina) a 65535")
        ciclos = c
    passo = d.get("passo_us", PASSO_PADRAO_US)
    if isinstance(passo, bool) or not isinstance(passo, int) or passo < 1:
        raise ErroOnda("'passo_us' precisa ser inteiro >= 1")
    tipo, registro, (minimo, maximo), bruto, ganho = disponiveis[alvo]
    if passo < ONDA_PASSO_MIN_US and tipo != ONDA_DAC:
        raise ErroOnda(f"'passo_us' abaixo de {ONDA_PASSO_MIN_US} so no DAC (por DMA)")
    if passo < ONDA_PASSO_MIN_US and periodo_us // passo > ONDA_AMOSTRAS_MAX:
        raise ErroOnda(f"DAC por DMA: periodo / passo ate {ONDA_AMOSTRAS_MAX} amostras")
    if periodo_us < passo:
        raise ErroOnda("o periodo (ou a duracao) precisa ser maior que o passo")
    if any(not minimo <= v <= maximo for v in fisicos):
        raise ErroOnda(f"valores fora de {minimo} a {maximo} para '{alvo}'")
    ruido = d.get("ruido", 0)
    if isinstance(ruido, bool) or not isinstance(ruido, (int, float)) or ruido < 0:
        raise ErroOnda("'ruido' precisa ser um numero >= 0")

    ondas = []
    if alvo.startswith("bme280."):
        campo = alvo.split(".")[1]
        # O valor bruto do BME280 nao e linear na grandeza (e a pressao e a umidade dependem da temperatura): com
        # interpolacao, a forma e reamostrada em 16 pontos para o erro entre pontos ficar abaixo de 0,05
        if modo == ONDA_INTERPOLA and len(fisicos) < PONTOS_BME:
            fisicos = [_interpola(fisicos, k / (PONTOS_BME - 1)) for k in range(PONTOS_BME)]
        estado = estados["bme280"]
        pontos = [dict(estado, **{campo: v}) for v in fisicos]
        brutos = [bme_brutos(p["temperatura"], p["pressao"], p["umidade"]) for p in pontos]
        indice = {"temperatura": 0, "pressao": 1, "umidade": 2}
        destinos = [("temperatura", ONDA_I2C_U20, BME_DADOS + 3), ("pressao", ONDA_I2C_U20, BME_DADOS),
                    ("umidade", ONDA_I2C_U16, BME_DADOS + 6)]
        if campo != "temperatura":
            destinos = [x for x in destinos if x[0] == campo]
        for nome, tipo_campo, reg in destinos:
            serie = [b[indice[nome]] for b in brutos]
            base, amp, valores = _normaliza(serie)
            r = 0
            if nome == campo and ruido:
                # valor bruto por unidade, pela derivada no meio da faixa
                meio = (min(fisicos) + max(fisicos)) / 2
                baixo, alto = (bme_brutos(**dict(estado, **{campo: meio + dv}))[indice[nome]] for dv in (-0.5, 0.5))
                r = max(1, round(abs(alto - baixo) * ruido))
            ondas.append((tipo_campo, reg, base, amp, valores, r))
        estado[campo] = fisicos[-1]
    else:
        brutos = [bruto(v) for v in fisicos]
        base, amp, valores = _normaliza(brutos)
        ondas.append((tipo, registro, base, amp, valores, round(ruido * ganho)))
        if alvo.startswith("mpu6500."):
            campo, _, eixo = alvo.split(".")[1].partition("_")
            estado = estados["mpu6500"]
            if eixo:
                v = list(estado[campo])
                v[EIXOS[eixo]] = fisicos[-1]
                estado[campo] = tuple(v)
            else:
                estado[campo] = fisicos[-1]

    passos = []
    for tipo_onda, reg, base, amp, valores, r in ondas:
        if gerador >= 6:
            raise ErroOnda("mais de 6 ondas no caso (a temperatura do BME280 usa 3)")
        if r > 0xFFFF:
            raise ErroOnda("'ruido' grande demais")
        lo, hi = ONDA_FAIXAS[tipo_onda]
        if not (lo <= base <= hi and lo <= base + amp <= hi):
            raise ErroOnda(f"valores fora da faixa do destino de '{alvo}'")
        passos.append(passo_onda(gerador, tipo_onda, reg, passo, periodo_us, valores, base, amp, ciclos, modo, r))
        gerador += 1
    desc = (f"onda {forma} em {alvo}" + (f", {ciclos} ciclo(s)" if ciclos else ", ate o fim da rotina")
            + f", passo {passo} us" + (" (DMA)" if passo < ONDA_PASSO_MIN_US else ""))
    return passos, gerador, desc
