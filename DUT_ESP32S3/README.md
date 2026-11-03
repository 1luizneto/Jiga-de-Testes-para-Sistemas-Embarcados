# DUT de referência (ESP32-S3)

Firmware de um dispositivo sob teste de **comportamento conhecido**, usado no **CT20** para validar os
barramentos da jiga num sistema real: o ESP32-S3 lê e escreve a jiga escrava (I2C e SPI) e serve de escravo
para a jiga mestra. Se um caso falhar, a culpa é da jiga, não do DUT.

O ESP32 é comandado **pela própria jiga**: a UART1 dele vai na UART4 da jiga, o PC manda um comando de texto
com `UART_WRITE` e lê a resposta com `UART_READ`. Assim o CT20 roda inteiro pelo `teste_protocolo.py --dut`,
sem outro cabo. Os mesmos comandos funcionam pelo USB do kit (UART0), para testar o ESP32 sozinho.

No **CT22** o mesmo firmware tem um segundo comportamento, que roda o tempo todo numa task própria e não recebe
comando nenhum: o **ventilador** (`main/ventilador.c`). Ele lê o "sensor" no GPIO1 a cada 1 ms e liga a saída
GPIO4 acima de 1,80 V e desliga abaixo de 1,50 V (histerese). A jiga aplica a tensão no DAC1 e confere a
saída no GPIO5, como faria com um firmware qualquer (teste *black-box*, bateria
`Software/baterias/ct22_ventilador.yaml`). O Arduino Uno roda o mesmo comportamento
([`DUT_ArduinoUno`](../DUT_ArduinoUno/README.md)).

## Ligação com a jiga

GND comum. Tudo em 3,3 V. **Tirar os jumpers do CT13** (PB15 → PC2 e PD1 → PD0) antes de ligar o ESP32.

| Jiga (NUCLEO-F767ZI) | ESP32-S3 | Observação |
|---|---|---|
| PD0 (UART4 RX) | GPIO17 (UART1 TX) | |
| PD1 (UART4 TX) | GPIO18 (UART1 RX) | |
| PF0 (I2C2 SDA) | GPIO8 (SDA) | pull-up de 4,7 kΩ para o 3V3 |
| PF1 (I2C2 SCL) | GPIO9 (SCL) | pull-up de 4,7 kΩ para o 3V3 |
| PD7 (CS) | GPIO10 (CS) | resistor de 220 Ω em série recomendado (ver abaixo) |
| PB15 (MOSI) | GPIO11 (MOSI) | |
| PB10 (SCK) | GPIO12 (SCK) | |
| PC2 (MISO) | GPIO13 (MISO) | |
| PA4 (DAC1) | GPIO1 (ADC1 canal 0) | só no CT22 (sensor do ventilador) |
| PC12 (GPIO5) | GPIO4 | só no CT22 (saída do ventilador) |
| GND | GND | |

- **Só um mestre por vez.** Antes de um comando de mestre do ESP32 (`I2CM`, `SPIM`), a jiga tem que estar
  escrava (`I2C_CONFIG`/`SPI_CONFIG` escravo); antes da jiga mestra, o ESP32 tem que estar escravo
  (`I2CS`/`SPIS`) ou desligado (`OFF`). Os casos do CT20 já seguem essa ordem.
- **CS (PD7 ↔ GPIO10).** No estado seguro a jiga deixa o PD7 como saída em nível alto. O ESP32 só aciona o
  GPIO10 durante o `SPIM` (e o caso põe a jiga escrava antes); fora disso o pino é entrada. O resistor em
  série limita a corrente se a ordem for quebrada à mão.
- **Pull-ups do I2C.** O ESP32 liga os pull-ups internos (~45 kΩ), fracos demais sozinhos; os de 4,7 kΩ são
  os que valem (a jiga não tem pull-up interno no PF0/PF1).

## Comandos

Uma linha por comando, terminada em `\n` (`\r` é ignorado), UART em 115200 8N1. Dados em hex sem separador
(`-` = nenhum byte); quantidades, modo e frequência em decimal; endereços e offsets em hex.

| Comando | Resposta | O que faz |
|---|---|---|
| `PING` | `OK DUT ESP32-S3 <versão>` | identificação (a versão é a mesma da jiga) |
| `OFF` | `OK` | desliga os escravos e solta os pinos |
| `I2CM <end> <escrita> <nr>` | `OK [lidos]` | mestre a 100 kHz: escreve e lê `nr` bytes (repeated start) |
| `I2CS <end>` | `OK` | escravo no endereço, com um mapa de 256 registradores |
| `I2CS_LOAD <offset> <dados>` | `OK` | grava no mapa |
| `I2CS_DUMP <offset> <n>` | `OK <dados>` | lê o mapa (o que a jiga mestra escreveu) |
| `SPIM <modo> <freq> <dados>` | `OK <lidos>` | mestre: CS baixo, envia e recebe a mesma quantidade |
| `SPIS <modo>` | `OK` | arma o escravo SPI |
| `SPIS_LOAD <dados>` | `OK` | o que o escravo envia (do byte 0, completando com FF) |
| `SPIS_READ` | `OK [dados]` | o que a jiga mestra enviou desde a última leitura |

Erros: `ERR CMD` (comando desconhecido), `ERR ARG` (argumento fora da faixa), `ERR NACK` (dispositivo I2C não
respondeu), `ERR ESTADO` (barramento ocupado no outro papel: mandar `OFF`), `ERR PERIFERICO` (falha do driver).
Limites: até 239 bytes por transferência (o mesmo dos buffers da jiga), endereços I2C de 0x08 a 0x77, SPI
de 1 kHz a 10 MHz (a jiga escrava reage na ISR: até ~1 MHz é o recomendado).

**Mapa do escravo I2C.** Na escrita do mestre, o 1º byte é o ponteiro e os seguintes são gravados com
auto-incremento (o ponteiro fica depois do último). A leitura sai a partir do ponteiro **sem movê-lo**: o
driver do ESP32 entrega ao mestre um bloco já posto na FIFO e não informa quantos bytes ele leu. Para ler um
registrador, o mestre escreve só o ponteiro antes (como faz o `I2C_MASTER_XFER` com `nw = 1`).

**Escravo SPI.** A carga do `SPIS_LOAD` vale a partir da transação que o firmware arma depois dele: mandar o
`SPIS_LOAD` antes do `SPIS` (ou fazer uma transação a mais).

## Compilar, gravar e testar

ESP-IDF v6.1. No PowerShell:

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
```

```powershell
idf.py -B C:\esp\build_dut build
```

```powershell
idf.py -B C:\esp\build_dut -p COM12 flash monitor
```

A pasta de build fica fora do repositório (`-B`): com o caminho do projeto, os nomes de arquivo do mbedtls
passam do limite de 260 caracteres do Windows e o build falha.

No monitor (ou em qualquer terminal serial a 115200 na porta do kit), digitar `PING` responde
`OK DUT ESP32-S3 <versão>`.

**Testes no PC** (interpretador e mapa, com o Unity do firmware da jiga; rodam também no GitHub Actions):

```bash
sh DUT_ESP32S3/test_host/rodar.sh
```

O espelho em Python (`Software/dut.py`, classe `DutSimulado`) segue as mesmas regras e é conferido pelos
mesmos vetores (`Software/tests/test_dut.py`): é ele que define a resposta esperada dos casos do CT20.

## Arquivos

| Arquivo | Conteúdo |
|---|---|
| `main/comandos.c` | interpretador (lógica pura, testada no PC) |
| `main/mapa_i2c.c` | mapa de registradores do escravo I2C (lógica pura) |
| `main/barramentos.c` | I2C e SPI mestre/escravo com os drivers do ESP-IDF |
| `main/main.c` | UART0 (USB) e UART1 (jiga), uma task por UART, e a task do ventilador |
| `main/ventilador.c` | comportamento do CT22 (a regra da histerese é testada no PC) |
| `main/versao_dut.h` | versão, igual à da jiga (conferida pelo `test_versao.py`) |
| `test_host/` | testes Unity no PC |
