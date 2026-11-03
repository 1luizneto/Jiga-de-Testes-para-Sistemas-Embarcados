# Software (aplicação desktop)

Terminal para conversar com o STM32 pelo USB-CDC e ferramentas para testar o protocolo.
A versão está em `versao.py` (ver [Versionamento](../README.md#versionamento)).

| Arquivo | Papel |
|---|---|
| `terminal_gui.py` | Terminal gráfico (PySide6) |
| `terminal_usb.py` | Terminal de linha de comando |
| `protocolo.py` | Quadro, CRC-16/CCITT-FALSE, receptor e cálculo do PWM (espelhos do firmware) e casos de teste |
| `teste_protocolo.py` | Teste automático com a placa |
| `medir_loopback.py` | Medição do loopback DAC → ADC (dados do CT10), salva em CSV |
| `tests/` | Testes do software (pytest) |

## Instalação

```
pip install -r requirements.txt
```

## Terminal gráfico (estilo Termite)

```
python terminal_gui.py
```

- Seleciona sozinho a porta do STM32 (USB-CDC); a lista se atualiza ao ligar/desligar a placa.
- Recebidos em verde, enviados em azul; exibição em texto ou hex + ASCII, com horário opcional.
- Linha de envio com histórico (setas ↑/↓), final de linha configurável e envio em hex (ex.: `7E 01 02 0A`).
- Reconecta sozinho se a placa reiniciar; "Salvar..." grava o registro em .txt; Ctrl+L limpa.
- O baud só importa para a UART do ST-Link (DebugLog, 1000000 baud); o USB-CDC o ignora.
- Painel **Protocolo**: monta um quadro (SEQ automático, CMD e payload em hex; SOF, LEN e CRC são
  calculados) e executa os casos de teste do parser, um ou todos. Cada caso mostra o que a placa
  deve logar; para ver o resultado, abra uma segunda janela na porta do ST-Link.
- Linha **I2C**: papel (escrava/mestre), endereço e velocidade com "Configurar" (I2C_CONFIG); registrador
  inicial e dados em hex com "Carregar" (I2C_SLAVE_LOAD) e "Ler mapa" (I2C_SLAVE_DUMP), para preparar o
  que o DUT vai ler e conferir o que ele escreveu. A transferência como mestre fica no terminal (`/i2c_xfer`).
- Linha **SPI**: papel, modo (0 a 3) e frequência do mestre com "Configurar" (SPI_CONFIG; ao lado, o divisor
  e a frequência que o SPI2 gera de fato); dados em hex com "Carregar" (SPI_SLAVE_LOAD: o que a jiga escrava
  envia ao DUT), "Ler recebidos" (SPI_SLAVE_READ: o que o DUT enviou) e "Transferir" (SPI_MASTER_XFER).
- Linha **UART**: baud (8N1) com "Configurar" (UART_CONFIG); texto ou hex com "Enviar" (UART_WRITE) e
  "Ler" (UART_READ, até o máximo escolhido) para conversar com o DUT pela UART4 (PD1 TX, PD0 RX). Com
  "+
" marcado, o envio termina em `
`, como pedem os comandos do DUT de referência (ESP32-S3).
- Linha **Rotina**: identificador, tempo máximo e os passos em hex (cada passo é o código do comando direto
  seguido do payload dele; `78` = ESPERA, `79` = ESPERA_GPIO), com "Executar" (RUN_ROUTINE; o número de
  passos é contado pelo tamanho de cada um), "Estado" (ROUTINE_STATUS), "Resultados" (READ_RESULT a partir
  do offset) e "Abortar" (ABORT).
- Linha **PWM**: canal, frequência (1 Hz a 100 kHz) e razão cíclica (0 a 100 %, passo de 0,01 %),
  com "Aplicar PWM" (PWM_SET) e "Parar PWM" (PWM_STOP). Mostra o PSC, ARR e CCR que a jiga vai usar
  e a forma de onda que sai de fato no pino: é o valor de referência para medir no osciloscópio (CT12).

## Terminal de linha de comando

```
python terminal_usb.py                 # detecta o STM32 (VID 0483 / PID 5740)
python terminal_usb.py --porta COM14   # porta específica
python terminal_usb.py --modo texto    # exibe como texto em vez de hex + ASCII
python terminal_usb.py --log saida.txt # grava o que for exibido
python terminal_usb.py --listar        # lista as portas seriais
python terminal_usb.py --versao        # mostra a versão
```

Durante a execução:

| Comando          | Ação                                   |
|------------------|----------------------------------------|
| `texto qualquer` | envia o texto (sem quebra de linha)    |
| `/hex 7E 01 02`  | envia os bytes em hexadecimal          |
| `/quadro 01`     | envia um quadro (CMD em hex, SEQ automático) |
| `/quadro 10 AA BB` | quadro com payload                   |
| `/pwm 1 1000 50` | PWM1 (PE9) em 1000 Hz e 50 % (canal 1 a 3; razão aceita `12,34`) |
| `/pwm_stop 1`    | para o PWM1 (razão 0, saída em nível baixo) |
| `/i2c_config 0 50 0` | jiga escrava no 0x50 a 100 kHz (papel 0/1, endereço em hex, velocidade 0 = 100 kHz, 1 = 400 kHz) |
| `/i2c_load 10 DE AD` | carrega `DE AD` no mapa a partir do registrador 0x10 |
| `/i2c_dump 10 4` | lê 4 registradores do mapa a partir de 0x10 |
| `/i2c_xfer 68 2 0F` | como mestre: escreve `0F` no dispositivo 0x68 e lê 2 bytes |
| `/spi_config 1 0 1000000` | jiga mestre no modo 0 a 1 MHz (papel 0/1, modo 0 a 3, frequência em Hz só no mestre) |
| `/spi_load 11 22 33` | o que a jiga escrava envia ao DUT a cada transação (sem dados, só `FF`) |
| `/spi_read` | o que o DUT enviou à jiga escrava (e esvazia a recepção) |
| `/spi_xfer 9F 00 00` | como mestre: envia os bytes e recebe a mesma quantidade |
| `/uart_config 115200` | liga a UART4 (PD1 TX, PD0 RX) em 8N1 |
| `/uart_write 4F 4B 0A`, `/uart_texto ola` | envia bytes em hex / texto ao DUT |
| `/uart_read 16` | lê até 16 bytes recebidos do DUT (sem número, até 239) |
| `/rotina_run 7 1000 10 01 B2 09 79 05 01 20 A1 07 00 40 00` | RUN_ROUTINE: identificador, tempo máximo (ms) e os passos em hex (no exemplo, o do Apêndice A: DAC1 em 2482, ESPERA_GPIO no GPIO5 até 500 ms e ADC do A0) |
| `/rotina_status` | ROUTINE_STATUS: estado, passos concluídos e erro |
| `/rotina_result 0` | READ_RESULT a partir do offset (os resultados saem em partes de até 236 B) |
| `/rotina_abort` | ABORT: interrompe a rotina (a jiga vai ao estado seguro) |
| `/teste`         | lista os casos de teste do protocolo   |
| `/teste 3`, `/teste todos` | envia um caso / todos (menos os de loopback) |
| `/teste loopback` | envia todos, incluindo o CT10, o CT11, o CT13 e o CT19 com jumper |
| `/teste dut` | envia todos, incluindo o CT20 (DUT de referência ligado na jiga) |
| `/dut PING` | manda a linha ao DUT pela UART4 da jiga e lê a resposta (antes, `/uart_config 115200`) |
| `/modo`          | alterna entre hex e texto              |
| `/ajuda`         | mostra os comandos                     |
| `/sair`, Ctrl+C  | encerra                                |

Se a placa for reiniciada, a porta USB some; o terminal espera e reconecta sozinho.

## Teste automático com a placa

Com os **dois** cabos ligados (CN13 = USB-CDC e CN1 = ST-Link), o script envia cada caso de teste
pelo USB-CDC e confere, no DebugLog, se a CommTask logou o resultado esperado:

```
python teste_protocolo.py            # detecta as duas portas
python teste_protocolo.py --listar   # lista os casos
python teste_protocolo.py --caso 8   # só um caso
python teste_protocolo.py --loopback # inclui o CT10, o CT11, o CT13 e o CT19 com jumper (precisa dos jumpers)
python teste_protocolo.py --dut      # inclui o CT20 (precisa do ESP32-S3 ligado na jiga)
python teste_protocolo.py --porta COM14 --porta-log COM8
```

Não deixe outro terminal aberto nessas portas enquanto o script roda.

Os casos marcados como `[loopback]` (CT10, CT11, CT13 e o CT19 GPIO1 → GPIO5) só passam com os jumpers DAC1 (PA4) → A0 (PA3),
DAC2 (PA5) → A1 (PC0), GPIO1–4 (PC8–PC11) → GPIO5–8 (PC12, PD2, PG2, PG3), MOSI (PB15) → MISO (PC2) e
TX (PD1) → RX (PD0); sem `--loopback` eles são pulados. Na GUI, a mesma escolha é a caixa "Incluir loopback".

Os casos marcados como `[DUT]` (CT20) só passam com o DUT de referência (ESP32-S3, firmware em
[`DUT_ESP32S3/`](../DUT_ESP32S3/README.md)) ligado na jiga, **sem** os jumpers do CT13; sem `--dut` eles são
pulados. Na GUI, a caixa "Incluir DUT". O `dut.py` tem o espelho do firmware do ESP32 (`DutSimulado`), que
define a resposta esperada desses casos.

Os casos das rotinas (CT14 a CT18 e o CT19 "base de tempo") rodam sem jumpers: a rotina continua na jiga
enquanto o script manda os próximos comandos, e as esperas do caso deixam ela chegar no ponto conferido (a
jiga simulada anda o relógio dela nas mesmas esperas). Os instantes medidos pela jiga são conferidos com a
tolerância de 50 µs do critério de tempo da monografia; o CT19 com jumper (GPIO1 → GPIO5) confere também o
tempo de reação do ESPERA_GPIO nas duas bordas e o tempo limite.

## Medição do loopback DAC → ADC

Com os jumpers do DAC ligados, faz a varredura do CT10 guardando os valores: média, mínimo e máximo
das leituras de cada ponto, erro em códigos e em mV, e a reta ajustada por canal. Só usa o USB-CDC.

```
python medir_loopback.py                               # 11 pontos da faixa útil, 8 leituras cada
python medir_loopback.py --pontos 33 --amostras 16
python medir_loopback.py --de 0 --ate 4095 --pontos 41 # inclui as pontas (saturação do buffer)
python medir_loopback.py --saida medicao.csv
```

## Testes do software

```
pip install -r requirements-dev.txt
pytest
```

Cobrem o CRC e o cálculo do PWM (com os mesmos vetores dos testes Unity do firmware), a montagem de
quadros, o receptor, a jiga simulada, a coerência dos casos de teste e se firmware, software e CHANGELOG estão na mesma versão.
Rodam também no GitHub Actions a cada push.
