"""
Sensores emulados pela jiga como escrava I2C: MPU-6500 (acelerometro, giroscopio e temperatura) e BME280
(temperatura, pressao e umidade).

A jiga nao sabe o que e um sensor: ela tem um mapa de 256 registradores, uma mascara dos bits que o DUT pode escrever
(I2C_SLAVE_MASK) e pode responder em dois enderecos (I2C_SLAVE_ADDR2). Este modulo converte o sensor em bytes:

- registradores iniciais e mascaras (registrador so de leitura = 0x00; bit de reset que volta sozinho a 0 no sensor
  real fica com o bit em 0, para o driver do DUT ver o reset "terminado");
- grandezas fisicas -> registradores de dados (g, graus/s e graus C no MPU-6500, big-endian; no BME280, o valor bruto
  que, depois da compensacao da Bosch com os coeficientes de calibracao do mapa, da a grandeza pedida).

Os registradores dos dois sensores nao se sobrepoem (MPU-6500 de 0x0D a 0x75, BME280 de 0x88 a 0xFE), por isso os
dois cabem no mesmo mapa. A compensacao do BME280 segue as formulas inteiras da folha de dados (secao 4.2.3 e 8.2);
a inversa e uma busca binaria sobre o valor bruto, que e monotonico em cada grandeza.
"""

from dataclasses import dataclass, field

# --- MPU-6500 ------------------------------------------------------------------------------------------------
MPU_ENDERECO = 0x68
MPU_WHO_AM_I = 0x75
MPU_WHO_AM_I_VALOR = 0x70
MPU_SMPLRT_DIV, MPU_CONFIG, MPU_GYRO_CONFIG, MPU_ACCEL_CONFIG, MPU_ACCEL_CONFIG2 = 0x19, 0x1A, 0x1B, 0x1C, 0x1D
MPU_INT_PIN_CFG, MPU_INT_ENABLE, MPU_INT_STATUS = 0x37, 0x38, 0x3A
MPU_ACCEL_XOUT_H = 0x3B          # 0x3B-0x40 aceleracao, 0x41-0x42 temperatura, 0x43-0x48 giroscopio
MPU_SIGNAL_PATH_RESET, MPU_USER_CTRL, MPU_PWR_MGMT_1, MPU_PWR_MGMT_2 = 0x68, 0x6A, 0x6B, 0x6C
MPU_ESCALAS_G = {2: 0, 4: 1, 8: 2, 16: 3}            # fundo de escala -> bits [4:3] do ACCEL_CONFIG
MPU_ESCALAS_DPS = {250: 0, 500: 1, 1000: 2, 2000: 3}  # fundo de escala -> bits [4:3] do GYRO_CONFIG
MPU_TEMP_SENSIBILIDADE = 333.87  # LSB/grau C; temperatura = bruto / 333,87 + 21
MPU_TEMP_REFERENCIA = 21.0

# --- BME280 --------------------------------------------------------------------------------------------------
BME_ENDERECO = 0x76
BME_CHIP_ID = 0xD0
BME_CHIP_ID_VALOR = 0x60
BME_RESET, BME_CTRL_HUM, BME_STATUS, BME_CTRL_MEAS, BME_CONFIG = 0xE0, 0xF2, 0xF3, 0xF4, 0xF5
BME_CALIB_T_P = 0x88             # 0x88-0x9F: dig_T1-T3, dig_P1-P9; 0xA1: dig_H1
BME_CALIB_H = 0xE1               # 0xE1-0xE7: dig_H2-H6
BME_DADOS = 0xF7                 # press (3), temp (3), hum (2)

# Coeficientes de calibracao do sensor emulado: os do exemplo da folha de dados do BMP280 (T e P) e valores tipicos
# de um BME280 real (H). O DUT os le do mapa e compensa como faria com um sensor de verdade.
BME_CALIBRACAO = {
    "T1": 27504, "T2": 26435, "T3": -1000,
    "P1": 36477, "P2": -10685, "P3": 3024, "P4": 2855, "P5": 140, "P6": -7, "P7": 15500, "P8": -14600, "P9": 6000,
    "H1": 75, "H2": 362, "H3": 0, "H4": 313, "H5": 50, "H6": 30,
}

ESTADO_INICIAL_MPU = {"aceleracao": (0.0, 0.0, 1.0), "giro": (0.0, 0.0, 0.0), "temperatura": 25.0}
ESTADO_INICIAL_BME = {"temperatura": 25.0, "pressao": 1013.25, "umidade": 50.0}


def _s16(valor):
    """Inteiro -> 2 bytes big-endian com sinal, saturado na faixa do int16."""
    return max(-32768, min(32767, round(valor))).to_bytes(2, "big", signed=True)


# --- MPU-6500 ------------------------------------------------------------------------------------------------

def mpu_aceleracao(g, escala_g=2):
    """(x, y, z) em g -> 6 bytes (ACCEL_XOUT_H...ACCEL_ZOUT_L)."""
    return b"".join(_s16(v * 32768 / escala_g) for v in g)


def mpu_giro(dps, escala_dps=250):
    """(x, y, z) em graus/s -> 6 bytes (GYRO_XOUT_H...GYRO_ZOUT_L)."""
    return b"".join(_s16(v * 32768 / escala_dps) for v in dps)


def mpu_temperatura(graus):
    return _s16((graus - MPU_TEMP_REFERENCIA) * MPU_TEMP_SENSIBILIDADE)


def mpu_le(dados, escala_g=2, escala_dps=250):
    """14 bytes a partir do 0x3B -> (aceleracao, temperatura, giro): o que o driver do DUT calcula."""
    v = [int.from_bytes(dados[i:i + 2], "big", signed=True) for i in range(0, 14, 2)]
    return (tuple(x * escala_g / 32768 for x in v[0:3]), v[3] / MPU_TEMP_SENSIBILIDADE + MPU_TEMP_REFERENCIA,
            tuple(x * escala_dps / 32768 for x in v[4:7]))


def mpu_registradores(estado, escala_g=2, escala_dps=250):
    """[(offset, bytes, mascaras)] do MPU-6500: valores de reset, dados do estado e o que o DUT pode escrever."""
    dados = (mpu_aceleracao(estado["aceleracao"], escala_g) + mpu_temperatura(estado["temperatura"])
             + mpu_giro(estado["giro"], escala_dps))
    return [
        # SMPLRT_DIV, CONFIG, GYRO_CONFIG, ACCEL_CONFIG, ACCEL_CONFIG2: livres (o DUT configura)
        (MPU_SMPLRT_DIV, bytes([0, 0, MPU_ESCALAS_DPS[escala_dps] << 3, MPU_ESCALAS_G[escala_g] << 3, 0]),
         bytes([0xFF] * 5)),
        # INT_PIN_CFG, INT_ENABLE livres; 0x39 reservado; INT_STATUS com DATA_RDY sempre 1, so leitura
        (MPU_INT_PIN_CFG, bytes([0, 0, 0, 0x01]), bytes([0xFF, 0xFF, 0x00, 0x00])),
        (MPU_ACCEL_XOUT_H, dados, bytes(14)),
        # SIGNAL_PATH_RESET (bits que voltam a 0), 0x69, USER_CTRL (bits 0-2 de reset voltam a 0),
        # PWR_MGMT_1 (bit 7 DEVICE_RESET volta a 0; reset = 0x01), PWR_MGMT_2
        (MPU_SIGNAL_PATH_RESET, bytes([0, 0, 0, 0x01, 0]), bytes([0x00, 0xFF, 0xF8, 0x7F, 0xFF])),
        (MPU_WHO_AM_I, bytes([MPU_WHO_AM_I_VALOR]), bytes([0x00])),
    ]


# --- BME280: compensacao (folha de dados, formulas inteiras) --------------------------------------------------

def _div_c(a, b):
    """Divisao inteira com truncamento para zero, como em C (o // do Python arredonda para baixo)."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def bme_t_fine(adc_t, c=BME_CALIBRACAO):
    var1 = (((adc_t >> 3) - (c["T1"] << 1)) * c["T2"]) >> 11
    var2 = (((((adc_t >> 4) - c["T1"]) * ((adc_t >> 4) - c["T1"])) >> 12) * c["T3"]) >> 14
    return var1 + var2


def bme_temperatura(adc_t, c=BME_CALIBRACAO):
    """graus C (a formula inteira da centesimos)."""
    return ((bme_t_fine(adc_t, c) * 5 + 128) >> 8) / 100


def bme_pressao(adc_p, t_fine, c=BME_CALIBRACAO):
    """hPa (a formula de 64 bits da Pa em Q24.8)."""
    var1 = t_fine - 128000
    var2 = var1 * var1 * c["P6"]
    var2 = var2 + ((var1 * c["P5"]) << 17)
    var2 = var2 + (c["P4"] << 35)
    var1 = ((var1 * var1 * c["P3"]) >> 8) + ((var1 * c["P2"]) << 12)
    var1 = (((1 << 47) + var1) * c["P1"]) >> 33
    if var1 == 0:
        return 0.0
    p = 1048576 - adc_p
    p = _div_c(((p << 31) - var2) * 3125, var1)
    var1 = (c["P9"] * (p >> 13) * (p >> 13)) >> 25
    var2 = (c["P8"] * p) >> 19
    p = ((p + var1 + var2) >> 8) + (c["P7"] << 4)
    return p / 256 / 100


def bme_umidade(adc_h, t_fine, c=BME_CALIBRACAO):
    """% (a formula de 32 bits da Q22.10)."""
    v = t_fine - 76800
    v = (((((adc_h << 14) - (c["H4"] << 20) - (c["H5"] * v)) + 16384) >> 15) *
         (((((((v * c["H6"]) >> 10) * (((v * c["H3"]) >> 11) + 32768)) >> 10) + 2097152) * c["H2"] + 8192) >> 14))
    v = v - (((((v >> 15) * (v >> 15)) >> 7) * c["H1"]) >> 4)
    v = max(0, min(v, 419430400))
    return (v >> 12) / 1024


def _busca(funcao, alvo, bits):
    """Valor bruto de 'bits' bits cujo resultado (monotonico) fica mais perto do alvo."""
    lo, hi = 0, (1 << bits) - 1
    crescente = funcao(hi) > funcao(lo)
    while hi - lo > 1:
        meio = (lo + hi) // 2
        if (funcao(meio) < alvo) == crescente:
            lo = meio
        else:
            hi = meio
    return min((lo, hi), key=lambda x: abs(funcao(x) - alvo))


def bme_brutos(temperatura, pressao, umidade, c=BME_CALIBRACAO):
    """(adc_T, adc_P, adc_H) que, compensados, dao a temperatura (C), a pressao (hPa) e a umidade (%) pedidas."""
    adc_t = _busca(lambda x: bme_temperatura(x, c), temperatura, 20)
    t_fine = bme_t_fine(adc_t, c)
    adc_p = _busca(lambda x: bme_pressao(x, t_fine, c), pressao, 20)
    adc_h = _busca(lambda x: bme_umidade(x, t_fine, c), umidade, 16)
    return adc_t, adc_p, adc_h


def bme_dados(temperatura, pressao, umidade, c=BME_CALIBRACAO):
    """8 bytes a partir do 0xF7: press_msb, lsb, xlsb, temp_msb, lsb, xlsb, hum_msb, lsb."""
    adc_t, adc_p, adc_h = bme_brutos(temperatura, pressao, umidade, c)

    def vinte(v):
        return bytes([(v >> 12) & 0xFF, (v >> 4) & 0xFF, (v & 0x0F) << 4])
    return vinte(adc_p) + vinte(adc_t) + adc_h.to_bytes(2, "big")


def bme_le(dados, c=BME_CALIBRACAO):
    """8 bytes a partir do 0xF7 -> (temperatura, pressao, umidade): o que o driver do DUT calcula."""
    adc_p = (dados[0] << 12) | (dados[1] << 4) | (dados[2] >> 4)
    adc_t = (dados[3] << 12) | (dados[4] << 4) | (dados[5] >> 4)
    adc_h = (dados[6] << 8) | dados[7]
    t_fine = bme_t_fine(adc_t, c)
    return bme_temperatura(adc_t, c), bme_pressao(adc_p, t_fine, c), bme_umidade(adc_h, t_fine, c)


def bme_calibracao_bytes(c=BME_CALIBRACAO):
    """(bytes de 0x88 a 0xA1, bytes de 0xE1 a 0xE7), no formato da folha de dados."""
    def u16(v):
        return v.to_bytes(2, "little")

    def s16(v):
        return v.to_bytes(2, "little", signed=True)
    tp = u16(c["T1"]) + s16(c["T2"]) + s16(c["T3"]) + u16(c["P1"]) + b"".join(s16(c[f"P{i}"]) for i in range(2, 10))
    tp += bytes([0, c["H1"]])                        # 0xA0 reservado, 0xA1 = dig_H1
    h4, h5 = c["H4"] & 0xFFF, c["H5"] & 0xFFF
    h = s16(c["H2"]) + bytes([c["H3"], h4 >> 4, ((h5 & 0x0F) << 4) | (h4 & 0x0F), h5 >> 4,
                               c["H6"].to_bytes(1, "little", signed=True)[0]])
    return tp, h


def bme_calibracao_de_bytes(tp, h):
    """O que o driver do DUT faz ao ler a calibracao (para os testes conferirem o formato)."""
    def u16(i):
        return int.from_bytes(tp[i:i + 2], "little")

    def s16(b, i):
        return int.from_bytes(b[i:i + 2], "little", signed=True)
    c = {"T1": u16(0), "T2": s16(tp, 2), "T3": s16(tp, 4), "P1": u16(6)}
    for i in range(2, 10):
        c[f"P{i}"] = s16(tp, 6 + 2 * (i - 1))
    c["H1"] = tp[25]
    c["H2"], c["H3"] = s16(h, 0), h[2]
    h4 = (h[3] << 4) | (h[4] & 0x0F)
    h5 = (h[5] << 4) | (h[4] >> 4)
    c["H4"] = h4 - 4096 if h4 & 0x800 else h4
    c["H5"] = h5 - 4096 if h5 & 0x800 else h5
    c["H6"] = int.from_bytes(h[6:7], "little", signed=True)
    return c


def bme_registradores(estado):
    """[(offset, bytes, mascaras)] do BME280: calibracao, id, reset, controle e dados do estado."""
    tp, h = bme_calibracao_bytes()
    dados = bme_dados(estado["temperatura"], estado["pressao"], estado["umidade"])
    return [
        (BME_CALIB_T_P, tp, bytes(len(tp))),
        (BME_CHIP_ID, bytes([BME_CHIP_ID_VALOR]), bytes([0x00])),
        (BME_RESET, bytes([0x00]), bytes([0x00])),          # escrever 0xB6 "reseta" e le 0x00
        (BME_CALIB_H, h, bytes(len(h))),
        # ctrl_hum (bits 0-2), status (so leitura, sempre pronto), ctrl_meas, config
        (BME_CTRL_HUM, bytes([0, 0, 0, 0]), bytes([0x07, 0x00, 0xFF, 0xFF])),
        (BME_DADOS, dados, bytes(8)),
    ]


# --- Descricao para a bateria --------------------------------------------------------------------------------

@dataclass
class SensorEmulado:
    """Um sensor da secao 'sensores' da bateria."""

    modelo: str                 # "mpu6500" ou "bme280"
    endereco: int
    barramento: str = "i2c"
    escala_g: int = 2
    escala_dps: int = 250
    estado: dict = field(default_factory=dict)
    modo_spi: int = 0           # modo do SPI (0 ou 3, os dos dois sensores), so com barramento "spi"

    def __post_init__(self):
        if not self.estado:
            self.estado = dict(ESTADO_INICIAL_MPU if self.modelo == "mpu6500" else ESTADO_INICIAL_BME)

    def registradores(self, estado=None):
        estado = estado or self.estado
        if self.modelo == "mpu6500":
            return mpu_registradores(estado, self.escala_g, self.escala_dps)
        return bme_registradores(estado)

    def dados(self, estado):
        """(offset, bytes) dos registradores de dados para o estado (o que muda durante o caso)."""
        if self.modelo == "mpu6500":
            return MPU_ACCEL_XOUT_H, (mpu_aceleracao(estado["aceleracao"], self.escala_g)
                                      + mpu_temperatura(estado["temperatura"])
                                      + mpu_giro(estado["giro"], self.escala_dps))
        return BME_DADOS, bme_dados(estado["temperatura"], estado["pressao"], estado["umidade"])
