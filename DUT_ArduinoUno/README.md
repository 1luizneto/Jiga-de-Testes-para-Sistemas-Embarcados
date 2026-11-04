# DUT alternativo do CT22 (Arduino Uno)

> **Não validado.** A validação do TCC é feita só com o ESP32-S3 (decisão de 04/11/2026: sem a placa). O sketch
> fica como ponto de partida para validar a jiga em outro microcontrolador (trabalhos futuros da monografia).

O CT22 confere que a jiga testa microcontroladores diferentes **sem mudar o firmware deles** (RNF02): a mesma
bateria *black-box* (`Software/baterias/ct22_ventilador.yaml`) roda contra o ESP32-S3
([`DUT_ESP32S3`](../DUT_ESP32S3/README.md), `main/ventilador.c`) e contra este sketch, que tem o mesmo
comportamento conhecido:

- lê o "sensor" no **A0** a cada ~1 ms;
- liga a saída no **pino 7** acima de **1,80 V** e desliga abaixo de **1,50 V** (histerese; entre os dois, mantém).

## Ligação com a jiga

| Jiga (NUCLEO-F767ZI) | Arduino Uno | Observação |
|---|---|---|
| PA4 (DAC1) | A0 | sensor (0,1 a 3,2 V, faixa útil do DAC) |
| PC12 (GPIO5) | pino 7 | saída de **5 V**: o PC12 é tolerante a 5 V (FT) com a jiga ligada e sem pull interno |
| GND | GND | |

**Ligar a jiga antes do Uno e desligar o Uno antes da jiga**: com a jiga desligada, o limite do pino FT cai para
3,6 V (seção de compatibilidade de níveis da monografia). Se o Uno for alimentado pelo mesmo USB do PC, ligar a
jiga primeiro basta.

## Gravar e testar

1. Abrir `ventilador/ventilador.ino` na IDE do Arduino, placa "Arduino Uno", e gravar.
2. Ligar como na tabela e rodar, na pasta `Software`:

```
python executar_bateria.py baterias/ct22_ventilador.yaml --repetir 10
```

Os 5 casos devem passar nas 10 execuções. Os testes `Software/tests/test_ct22.py` mostram, com DUTs simulados,
que a bateria distingue o comportamento: um firmware sem histerese falha nos dois casos da histerese.
