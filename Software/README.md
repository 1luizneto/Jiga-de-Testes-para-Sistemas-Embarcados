# Software (aplicação desktop)

Terminal para conversar com o STM32 pelo USB-CDC e ferramentas para testar o protocolo.
A versão está em `versao.py` (ver [Versionamento](../README.md#versionamento)).

| Arquivo | Papel |
|---|---|
| `app_jiga.py` | Aplicação de baterias de testes (PySide6): abre, executa e mostra os vereditos |
| `executar_bateria.py` | A mesma execução pela linha de comando (regressão, CT21) |
| `cliente.py`, `bateria.py`, `motor.py`, `relatorio.py` | Cliente da jiga, baterias YAML, motor de testes e relatório |
| `baterias/` | Baterias de exemplo |
| `desempenho.py` | Testes de desempenho CT23 (1000 PING) e CT24 (10 000 comandos), salvos em `medicoes/` |
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

## Aplicação de baterias de testes

```
python app_jiga.py [baterias/loopback.yaml]          # janela: abrir, executar, interromper, vereditos
python executar_bateria.py baterias/sem_fios.yaml    # a mesma execução pela linha de comando
python executar_bateria.py baterias/loopback.yaml --repetir 10   # CT21
python executar_bateria.py baterias/loopback.yaml --simulada --loopback
```

Os testes são descritos em **baterias** YAML (seção 3.6.3 da monografia). Cada caso vira uma rotina, enviada à
jiga em um RUN_ROUTINE; o PC acompanha com ROUTINE_STATUS, lê as medições com READ_RESULT e dá o veredito:

- **passou**: a rotina concluiu e todas as verificações bateram;
- **falhou**: alguma verificação fora do esperado, ou a rotina terminou em ERRO (o DUT não respondeu num
  barramento, comportamento do próprio DUT);
- **inconclusivo**: a comunicação falhou mesmo depois das retransmissões, a rotina esgotou o tempo máximo ou foi
  interrompida (botão "Interromper" = ABORT). A bateria para aí, porque não se sabe se os estímulos foram aplicados.

A bateria inteira é validada antes de falar com a jiga (todos os erros de uma vez, com o caso e o item). A jiga
começa e termina no estado seguro (RESET_ALL), e volta a ele sozinha no fim de cada rotina: **cada caso configura
o que usa** (UART, I2C, SPI), sem depender do anterior. Cada execução grava um relatório CSV (uma linha por
verificação, `;`, abre no Excel) e um JSON (com a área de resultados em hex) na pasta `relatorios/`.

```yaml
bateria: controle-ventilador
dut: ESP32
casos:
  - nome: liga acima do limiar em até 100 ms
    estimulos:
      - dac: {canal: 1, tensao: 2.0}
    verificar:
      - reacao_gpio: {pino: 5, nivel: 1, max_ms: 100}
  - nome: desliga abaixo do limiar
    estimulos:
      - dac: {canal: 1, tensao: 1.0}
    espera_ms: 500                    # entre os estímulos e as verificações
    verificar:
      - gpio_in: {pino: 5, esperado: 0}
      - adc: {canal: 0, esperado: 1.65, tolerancia: 0.05}
```

| Estímulo | Campos | Passo |
|---|---|---|
| `dac` | `canal` 1–2, `tensao` (V) ou `codigo` 0–4095 | DAC_SET (código = round(4095 × V / 3,3)) |
| `gpio_out` | `pino` 1–4, `nivel` 0/1 | GPIO_SET |
| `pwm` / `pwm_stop` | `canal` 1–3, `frequencia` (Hz), `razao` (%) | PWM_SET / PWM_STOP |
| `espera_ms` / `espera_us` | número | ESPERA |
| `i2c_config` | `papel` escravo/mestre, `endereco`, `velocidade` 100/400 (kHz) | I2C_CONFIG |
| `i2c_load` | `offset`, `dados` (hex) | I2C_SLAVE_LOAD |
| `spi_config` | `papel` escrava/mestre, `modo` 0–3, `frequencia` (mestre) | SPI_CONFIG |
| `spi_load` | `dados` (hex) | SPI_SLAVE_LOAD |
| `uart_config` / `uart_write` | `baud` / `texto` ou `dados` (hex) | UART_CONFIG / UART_WRITE |

| Verificação | Campos | Passo e critério |
|---|---|---|
| `gpio_in` | `pino` 5–8, `esperado` 0/1 | GPIO_READ; nível igual |
| `adc` | `canal` 0–2, `esperado` (V), `tolerancia` (V, padrão 0,033 = 1 % do fundo de escala) | ADC_READ; dentro da tolerância |
| `reacao_gpio` | `pino` 5–8, `nivel`, `max_ms` | ESPERA_GPIO; a borda ocorreu antes do limite (o tempo medido vai para o relatório) |
| `uart_read` | `esperado_texto` ou `esperado` (hex), `max` | UART_READ; bytes iguais |
| `i2c_dump` | `offset`, `esperado` (hex) | I2C_SLAVE_DUMP; bytes iguais |
| `i2c_xfer` | `endereco`, `escrita`, `esperado` (hex) | I2C_MASTER_XFER; bytes lidos iguais |
| `spi_read` / `spi_xfer` | `esperado` / `dados` + `esperado` (hex) | SPI_SLAVE_READ / SPI_MASTER_XFER; bytes iguais |
| `uart_valores` | `comando` (texto), `esperado` (números), `tolerancia`, `baud`, `espera_ms` | UART_WRITE, ESPERA e UART_READ; cada número da resposta dentro da tolerância |

O tempo máximo de cada rotina é (esperas + limites + transmissão da UART) × 1,1 + 100 ms; `tempo_max_ms` no caso
substitui o cálculo. Um caso que não cabe em um quadro (240 B) ou cujos resultados passam de 2048 B é recusado na
validação: divida em dois.

| Bateria | Ligação | Para quê |
|---|---|---|
| `baterias/sem_fios.yaml` | nenhuma | conferir o app com a placa (mapa I2C, buffers, estímulos e esperas) |
| `baterias/loopback.yaml` | jumpers do CT10, CT11 e CT13 | CT21 (dez execuções com os mesmos vereditos) |
| `baterias/ct22_ventilador.yaml` | DAC1 → sensor do DUT, saída do DUT → GPIO5 | CT22 (ESP32-S3, *black-box*; serve também ao Arduino Uno) |
| `baterias/ct25_sensores.yaml` | a do CT20 (UART e I2C com o ESP32-S3) | CT25: o DUT lê o MPU-6500 e o BME280 emulados |
| `baterias/ondas_sem_fios.yaml` | nenhuma | ondas num sensor emulado (lidas pelo mapa), no DAC por DMA e no PWM |
| `baterias/ct26_sensores_spi.yaml` | a do CT20 (UART, I2C e SPI com o ESP32-S3) | CT26: BME280 no SPI e MPU-6500 no I2C ao mesmo tempo |
| `baterias/ct19_osciloscopio.yaml` | osciloscópio no PC8 (GPIO1) | CT19: pulsos de 1, 10 e 100 ms (`ct19_pulso_100ms.yaml`: só o de 100 ms, para o *single*) |
| `baterias/ct27_osciloscopio.yaml` | osciloscópio no PA4 (DAC1) e no PE9 (PWM1) | CT27: senoide e triangular na razão do PWM, 2 min cada, em modo contínuo (`ct27_pwm.yaml`: só o PWM) |
| `baterias/exemplo_monografia.yaml` | DUT hipotético da figura | exemplo da seção 3.6.3 (compilado no CT06) |

Sem os jumpers, a `loopback.yaml` falha nos casos analógicos, da UART e do SPI; os de GPIO dão resultado
indefinido (entrada solta, sem pull).

### Sensores emulados (MPU-6500 e BME280)

A jiga pode responder no I2C como um **MPU-6500** (acelerômetro, giroscópio e temperatura) e um **BME280**
(temperatura, pressão e umidade), ao mesmo tempo: o DUT lê a jiga com o driver normal do sensor. Na bateria:

```yaml
sensores:
  mpu6500: {barramento: i2c, endereco: 0x68, escala_g: 2, escala_dps: 250}   # 0x68 ou 0x69
  bme280:  {barramento: i2c, endereco: 0x76}                                  # 0x76 ou 0x77
  # ou um dos dois no SPI (modo 0 ou 3): bme280: {barramento: spi, modo: 0}
casos:
  - nome: placa inclinada, dia quente
    estimulos:
      - mpu6500: {aceleracao: [0.5, 0, 0.866], giro: [10, -20, 90], temperatura: 31.5}
      - bme280: {temperatura: 35.2, pressao: 1005.8, umidade: 82.5}
    verificar:
      - uart_valores: {comando: "MPU\n", esperado: [0.5, 0, 0.866, 10, -20, 90, 31.5], tolerancia: 0.01}
```

- A configuração dos sensores (registradores de reset, calibração do BME280, máscaras dos bits que o DUT pode
  escrever e os dois endereços) é o **preparo** do caso: comandos diretos que o motor manda antes do RUN_ROUTINE
  (não cabe nos 240 B da rotina). Na rotina ficam só as mudanças de valor.
- Os registradores só de leitura (WHO_AM_I, chip_id, dados, calibração) não mudam quando o DUT escreve; os bits de
  reset (DEVICE_RESET do MPU-6500, 0xE0 do BME280) leem 0 depois da escrita, como se o reset tivesse terminado.
- O BME280 entrega valores brutos que o DUT compensa com a calibração: o PC calcula o valor bruto pela inversa da
  compensação da Bosch (`sensores.py`), com erro de até ±0,005 nas três grandezas.
- O valor de aceleração do MPU-6500 depende do fundo de escala que o driver do DUT configura: `escala_g` e
  `escala_dps` na bateria têm que ser os do driver.
- **Sensor no SPI** (`barramento: spi`, um sensor por vez: a jiga tem um CS): a escrava SPI entra no **modo
  registradores** (`SPI_SLAVE_REG`) e usa o mesmo mapa e as mesmas máscaras da escrava I2C. Em cada transação, o 1º
  byte é o endereço (bit 7 = leitura) e os seguintes são os dados, com auto-incremento; no BME280, que manda só 7
  bits de endereço no SPI, a jiga completa com 0x80. **Restrição:** o mestre precisa de uma pausa de alguns µs entre
  o byte de endereço e o 1º dado: só depois de receber o endereço a jiga sabe qual registrador enviar (no I2C, ela
  pode segurar o clock; no SPI, não). Depois do 1º dado a FIFO de 4 bytes vai cheia e a leitura segue na velocidade
  do mestre. O DUT de referência manda o endereço numa transferência e os dados em outra, com o CS baixo e 20 µs
  entre elas (comandos `MPUS` e `BMES`).
- `uart_valores` manda um comando de texto ao DUT pela UART (115200 baud por padrão), espera 50 ms, lê a resposta e
  confere cada número com a tolerância (um número para todos ou uma lista).

### Ondas (gerador da jiga)

Um estímulo `onda` faz a jiga variar um valor no tempo **sozinha**, em segundo plano, enquanto a rotina continua
(esperas, leituras e verificações acontecem com a onda rodando):

```yaml
estimulos:
  - onda: {alvo: bme280.temperatura, forma: rampa, de: 20, ate: 40, duracao_ms: 10000}
  - onda: {alvo: mpu6500.aceleracao_z, forma: senoide, min: 0.5, max: 1.5, periodo_ms: 200, ciclos: 20, ruido: 0.01}
  - onda: {alvo: dac1, forma: senoide, min: 0.5, max: 2.5, periodo_ms: 1, passo_us: 10}     # 1 kHz por DMA
  - onda: {alvo: pwm1.razao, forma: triangular, min: 0, max: 100, periodo_ms: 100}          # depois de um pwm
  - onda: {alvo: bme280.umidade, forma: pontos, interpolar: false, pontos: [[0, 40], [1000, 80], [3000, 40]]}
```

| Forma | Campos | Tabela na jiga |
|---|---|---|
| `rampa` | `de`, `ate`, `duracao_ms` | 2 pontos, 1 ciclo (fica no `ate`) |
| `senoide` | `min`, `max`, `periodo_ms`, `fase` (graus) | 32 pontos (erro da interpolação < 0,5 % da amplitude) |
| `triangular` | `min`, `max`, `periodo_ms` | 3 pontos (exata) |
| `quadrada` | `min`, `max`, `periodo_ms`, `razao` (%) | degrau; razão aproximada com até 31 divisões |
| `dente_de_serra` | `min`, `max`, `periodo_ms` | 2 pontos, recomeça a cada período |
| `exponencial` | `de`, `ate`, `tau_ms`, `duracao_ms` (padrão 5 τ) | 32 pontos, 1 ciclo (sistema de 1ª ordem) |
| `pontos` | `pontos: [[t_ms, valor], ...]`, `interpolar` | exata se os instantes caem numa grade de até 31 passos |

Comuns: `ciclos` (0 = até o fim da rotina; padrão das formas periódicas), `passo_us` (padrão 1000) e `ruido`
(± amplitude uniforme, nas unidades do alvo). Alvos: `dac1`, `dac2`, `pwm1.razao`…`pwm3.razao`,
`mpu6500.aceleracao_x/y/z`, `mpu6500.giro_x/y/z`, `mpu6500.temperatura`, `bme280.temperatura`, `bme280.pressao` e
`bme280.umidade`.

- A jiga toca uma tabela de 2 a 32 pontos igualmente espaçados no período, com interpolação linear ou degrau
  (`Onda.c`); a forma é montada no PC (`ondas.py`). Até **6 ondas** por caso.
- **`passo_us` ≥ 100**: cada onda é atualizada numa interrupção agendada no canal 2 do TIM5 (a base de tempo das
  rotinas). Nos sensores, 1 ms basta: o sensor real também não muda mais rápido do que o DUT lê. **No DAC, de 1 a
  99 µs**: um período de até 1024 amostras é tocado por DMA com o trigger do TIM7 (DAC1) ou do TIM4 (DAC2), sem usar
  o processador.
- A onda na razão do PWM troca só o registrador de comparação (o contador não reinicia): fazer um `pwm` antes, com a
  frequência.
- Uma onda na temperatura do BME280 leva junto duas ondas que corrigem os valores brutos da pressão e da umidade (a
  compensação depende da temperatura), em 16 pontos: o DUT continua lendo a pressão e a umidade do caso.
- O fim da rotina (e o ABORT e o RESET_ALL) para todas as ondas antes do estado seguro.

Camadas (figura de classes da seção 3.6.2): `cliente.py` (`TransporteSerial`, `TransporteSimulado` e
`ClienteJiga`: SEQ, tempo limite de 100 ms e 3 tentativas com o mesmo SEQ), `bateria.py`
(`CarregadorDeBaterias`, `Bateria`, `CasoDeTeste`), `motor.py` (`MotorDeTestes`), `relatorio.py` e `app_jiga.py`
(`JanelaPrincipal`). O codec do protocolo é o `protocolo.py`.

## Testes de desempenho (CT23 e CT24)

```
python desempenho.py                  # os dois casos na jiga
python desempenho.py --ct 23 --n 1000
python desempenho.py --simulada       # confere o script sem a placa
```

O CT23 mede no PC (`time.perf_counter`) o tempo de ida e volta de 1000 PING: média, mediana, percentis 95 e 99 e
máximo (critério: máximo < 50 ms). O CT24 manda 10 000 comandos alternando DAC_SET e ADC_READ e conta os erros
não recuperados (critério: nenhum) e as retransmissões. Os tempos de cada comando vão para
`medicoes/ct23_ping.csv` e `medicoes/ct24_escrita_leitura.csv`. Na placa (firmware 0.10.0, Windows 11):

| Caso | Média | Mediana | p95 | p99 | Máximo | Erros | Retransmissões |
|---|---|---|---|---|---|---|---|
| CT23 (PING) | 1,46 ms | 1,17 ms | 2,26 ms | 2,47 ms | 2,67 ms | 0 | 0 |
| CT24 (DAC_SET/ADC_READ) | 2,13 ms | 2,07 ms | 2,54 ms | 2,69 ms | 3,08 ms | 0 | 0 de 10 000 |

O PING é respondido pela CommTask na hora; os comandos do CT24 passam pela fila e pela TestExecTask, o que
acrescenta ~0,7 ms. Numa primeira rodada do CT24 houve um pico isolado de 15 ms (escalonamento do Windows),
ainda bem abaixo do tempo limite de 100 ms.

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
Os casos de unidade da monografia estão em `test_ct02_ct04.py` (CT02 a CT04), `test_cliente.py` (CT05, retransmissão),
`test_bateria.py` (CT06) e `test_motor.py` (CT07, vereditos). O `test_app.py` abre a janela sem mostrar na tela e é
pulado sem o PySide6 (como no GitHub Actions).
Rodam também no GitHub Actions a cada push.
