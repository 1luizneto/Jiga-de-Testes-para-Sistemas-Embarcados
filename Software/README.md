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
| `/teste`         | lista os casos de teste do protocolo   |
| `/teste 3`, `/teste todos` | envia um caso / todos (menos os de loopback) |
| `/teste loopback` | envia todos, incluindo o CT10 e o CT11 |
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
python teste_protocolo.py --loopback # inclui o CT10 e o CT11 (precisa dos jumpers)
python teste_protocolo.py --porta COM14 --porta-log COM8
```

Não deixe outro terminal aberto nessas portas enquanto o script roda.

Os casos marcados como `[loopback]` (CT10 e CT11) só passam com os jumpers DAC1 (PA4) → A0 (PA3),
DAC2 (PA5) → A1 (PC0) e GPIO1–4 (PC8–PC11) → GPIO5–8 (PC12, PD2, PG2, PG3); sem `--loopback` eles
são pulados. Na GUI, a mesma escolha é a caixa "Incluir loopback".

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
