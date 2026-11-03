/*
 * ventilador.ino
 *
 * Segundo DUT do CT22 (Arduino Uno, ATmega328P a 5 V): o mesmo comportamento conhecido do DUT ESP32-S3
 * (DUT_ESP32S3/main/ventilador.c), para mostrar que a jiga testa microcontroladores diferentes sem mudar o
 * firmware deles (RNF02). A jiga nao manda comando nenhum: aplica a tensao do "sensor" e confere a saida.
 *
 * - Sensor: A0 <- DAC1 da jiga (PA4). Liga acima de 1,80 V e desliga abaixo de 1,50 V (histerese).
 * - Saida: pino 7 -> GPIO5 da jiga (PC12). A saida do Uno e de 5 V: o PC12 e tolerante a 5 V (FT) com a jiga
 *   ligada e sem pull interno (secao de compatibilidade de niveis da monografia). Ligar a jiga antes do Uno.
 * - GND comum.
 *
 * Changelog
 * 1.0.0 - 03/11/2026 - Luiz Neto - Primeira versao
 */

const uint8_t PINO_SENSOR = A0;
const uint8_t PINO_SAIDA = 7;
const uint32_t LIGA_MV = 1800;
const uint32_t DESLIGA_MV = 1500;
const uint32_t REFERENCIA_MV = 5000;      // AVcc do Uno; o ADC e de 10 bits

bool ligado = false;

// Mesma regra do ventilador.c do ESP32: entre os dois limiares, mantem o estado
bool atualiza(bool estado, uint32_t tensaoMv)
{
  if (tensaoMv > LIGA_MV) {
    return true;
  }
  if (tensaoMv < DESLIGA_MV) {
    return false;
  }
  return estado;
}

void setup()
{
  pinMode(PINO_SAIDA, OUTPUT);
  digitalWrite(PINO_SAIDA, LOW);
}

void loop()
{
  uint32_t tensaoMv = (uint32_t)analogRead(PINO_SENSOR) * REFERENCIA_MV / 1023;
  bool novo = atualiza(ligado, tensaoMv);
  if (novo != ligado) {
    ligado = novo;
    digitalWrite(PINO_SAIDA, ligado ? HIGH : LOW);
  }
  delay(1);
}
