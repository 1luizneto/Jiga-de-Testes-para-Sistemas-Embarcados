# Changelog

Todas as mudanças relevantes do projeto (firmware e software) ficam registradas aqui.

O formato segue o [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/) e o projeto usa
[Versionamento Semântico](https://semver.org/lang/pt-BR/). As regras de versão estão no [README](README.md#versionamento).

## [Não lançado]

## [0.5.0] - 2026-10-20

### Adicionado
- Firmware: módulo `DigitalIo` (saídas GPIO1–4 com validação de pino 1–4 e nível 0/1, leitura das
  entradas GPIO5–8 numa máscara com bit 0 = GPIO5, estado seguro em nível baixo) e `Bsp_Gpio` (HAL).
  10 testes Unity com GPIO falso.
- Firmware: módulo `AnalogIn` (canal 0–2, leitura de 12 bits; valor acima de 4095 vindo da BSP é tratado
  como falha) e `Bsp_Adc` (uma conversão por polling no ADC1/ADC2/ADC3, com timeout). 6 testes Unity
  com ADC falso.
- Firmware: comandos `GPIO_SET` (0x20), `GPIO_READ` (0x21, ACK com a máscara) e `ADC_READ` (0x40, ACK com
  canal e valor u16): os primeiros comandos com dados no ACK. Tamanho ou faixa inválidos → NACK
  PARAMETRO_INVALIDO sem tocar no pino nem no ADC. 9 testes Unity novos no `Exec` (70 no total).
- Software: `protocolo.py` conhece os três comandos (`montar_gpio_set`, `montar_gpio_read`,
  `montar_adc_read`) e mostra a máscara e o valor do ADC (em V) nas respostas; a `JigaSimulada` simula
  GPIOs e ADC, com opção de loopback (entradas = saídas, A0/A1 = DAC1/DAC2).
- Software: casos de teste podem conferir respostas que variam (`conferir`, com o motivo da reprovação) e
  ser marcados como "requer loopback". 12 casos novos (29 no total): `GPIO_SET`/`GPIO_READ`/`ADC_READ`
  válidos e inválidos, **CT10** (DAC1 → A0 e DAC2 → A1, 11 pontos na faixa útil, ±41 códigos = 1 % do
  fundo de escala) e **CT11** (GPIO1–4 → GPIO5–8, as 16 combinações).
- Software: opção `--loopback` no `teste_protocolo.py`, caixa "Incluir loopback" na GUI e
  `/teste loopback` no terminal; sem ela, os casos com jumper são pulados.
- Software: `medir_loopback.py`, que faz a varredura DAC → ADC com várias leituras por ponto e salva um
  CSV (média, mínimo, máximo, erro em códigos e em mV, reta ajustada por canal).
- Software: 56 testes pytest novos (149 no total).

### Alterado
- Firmware: sampling time dos três ADCs de 3 para 480 ciclos (fonte do DUT com impedância de alguns kΩ).
- Firmware: o `RESET_ALL` (`Exec_SafeState`) também leva GPIO1–4 a nível baixo e tenta todos os
  periféricos mesmo que um falhe.
- Software: o `teste_protocolo.py` lê o DebugLog e o USB durante o caso (e não só no fim), com buffer de
  recepção maior: os casos longos geram dezenas de respostas.
- Repositório: `projeto/Debug/` e `projeto/Test/` (artefatos de build) saíram do git e entraram no
  `.gitignore`.

## [0.4.0] - 2026-10-13

### Adicionado
- Firmware: módulo `AnalogOut` (validação de canal 1–2 e valor 0–4095, estado seguro) e `Bsp_Dac` (HAL);
  a jiga liga com as duas saídas analógicas em 0.
- Firmware: módulo `Exec` (despachante dos comandos diretos): `RESET_ALL` (estado seguro) e `DAC_SET`
  (canal 1–2, valor 0–4095); tamanho ou faixa inválidos → NACK PARAMETRO_INVALIDO sem acionar a saída;
  comando desconhecido → CMD_DESCONHECIDO. 10 testes Unity com DAC falso.
- Firmware: a CommTask repassa os comandos (exceto PING) para o `Exec` e responde com o resultado
  (`Comm_BuildResult`); um comando pendente por vez: retransmissão do pendente é ignorada e outro
  comando recebe NACK OCUPADO. `DAC_SET` e `RESET_ALL` funcionam pelo USB. 8 testes Unity novos.
- Firmware: filas de 1 elemento entre CommTask e TestExecTask (criadas no `.ioc`, alocação estática):
  a TestExecTask executa os comandos e devolve o resultado; a CommTask continua lendo o USB durante a
  execução.
- Software: `protocolo.py` conhece `RESET_ALL` e `DAC_SET` (`montar_dac_set`) e os erros
  PARAMETRO_INVALIDO e OCUPADO; a `JigaSimulada` espelha o firmware novo (execução, comando pendente,
  OCUPADO e o DAC).
- Software: 7 casos de teste novos (17 no total): `RESET_ALL`, `DAC_SET` válido, canal/valor/tamanho
  inválidos (CT09), "Comando durante execução" (OCUPADO) e `RESET_ALL` no fim da bateria.
- Software: 26 testes pytest novos (93 no total).

### Corrigido
- Firmware: o DebugLog formatava a mensagem no buffer compartilhado antes de pegar o mutex; com duas
  tasks logando, uma podia enviar o texto da outra. Agora o lock cobre a formatação e o envio.
- Firmware: `BspUart_Init` sem o cast `(void *)`: a função de envio tem a mesma assinatura do DebugLog.
- Software: com logs chegando sem pausa (ex.: a cada 5 ms), a GUI e o terminal não exibiam nada até a
  porta fechar; a junção de bytes em rajadas agora tem tempo máximo (50 ms).

## [0.3.0] - 2026-10-13

### Adicionado
- Firmware: `Comm_BuildFrame` monta quadros de resposta, com 6 testes Unity usando os exemplos do Apêndice A.
- Firmware: `Comm_ProcessFrame` decide a resposta de cada quadro: ACK do PING com as versões do
  protocolo e do firmware, NACK CMD_DESCONHECIDO e reenvio da última resposta quando o SEQ se repete.
- Firmware: a CommTask responde cada quadro válido pelo USB-CDC (ACK/NACK do `Comm_ProcessFrame`);
  quadro com CRC errado ou LEN inválido não tem resposta e o PC retransmite por timeout.
- Firmware: detecção de sessão nova pelo DTR (`SET_CONTROL_LINE_STATE`): quando um programa abre a
  porta no PC, a CommTask limpa o cache de SEQ, e o primeiro comando da sessão não recebe a resposta
  guardada da sessão anterior.
- Software: `protocolo.py` interpreta as respostas da jiga (`interpretar_resposta`, ACK/NACK e as
  versões no ACK do PING) e tem uma `JigaSimulada`, espelho do `Comm_ProcessFrame`.
- Software: os casos de teste conferem também a resposta que volta pelo USB, além do DebugLog, no
  `teste_protocolo.py` e na GUI (colunas com log e USB, bytes recebidos no tooltip).
- Software: casos de teste "Comando desconhecido" e "SEQ repetido" (10 casos no total).
- Software: GUI e terminal de linha de comando mostram cada resposta recebida já decodificada.
- Software: 14 testes pytest das respostas e da jiga simulada, com os exemplos do Apêndice A.

### Alterado
- Firmware: timeout entre bytes de 100 ms para 20 ms, conforme o Apêndice A da monografia.
- Firmware: `CDC_Transmit_FS` copia os dados para o buffer da USB, então quem chama pode reutilizar
  o próprio buffer assim que a função retorna.
- Software: os casos "Payload com 0x7E" e "Payload maximo" usam o CMD 0x55 (inexistente) em vez de
  0x10, para a resposta esperada (NACK) não mudar quando o DAC_SET for implementado.

## [0.2.0] - 2026-10-13

### Adicionado
- Firmware: receptor de quadros do protocolo (`Comm_ProcessByte`) com máquina de estados
  AGUARDA_SOF → LE_CABECALHO → LE_PAYLOAD → VALIDA_CRC, validação por CRC-16/CCITT-FALSE e
  descarte imediato de LEN > 240.
- Firmware: timeout de pacote (`Comm_Timeout`): 100 ms sem bytes descarta um quadro pela metade.
- Firmware: CommTask passa os bytes do USB-CDC ao parser e loga cada resultado no DebugLog.
- Firmware: `Version.h` com a versão do firmware, exibida no log de inicialização.
- Firmware: 10 testes Unity do parser (build `Test`).
- Software: módulo `protocolo.py` (CRC, montagem de quadros, receptor espelho do firmware e casos de teste).
- Software: painel "Protocolo" no `terminal_gui.py`: montar quadros e executar os casos de teste
  com veredito pelo DebugLog (porta do ST-Link), tabela PASS/FALHA e log de cada caso.
- Software: comandos `/quadro` e `/teste` e opção `--versao` no `terminal_usb.py`.
- Software: `teste_protocolo.py`, teste automático com a placa (envia pelo USB-CDC e confere o DebugLog).
- Software: testes com pytest (`Software/tests`) e CI no GitHub Actions.

### Alterado
- Firmware: a CommTask deixa de fazer eco do USB-CDC.
- Firmware: timeout da UART de debug de 10 ms para 50 ms.

### Corrigido
- Firmware: `BspUart_Send` não liberava o mutex quando o envio dava certo. Agora libera nos dois
  caminhos, não transmite sem o mutex e dispensa o mutex antes do scheduler iniciar.

## [0.1.0] - 2026-10-09

### Adicionado
- Firmware: projeto STM32CubeIDE para a NUCLEO-F767ZI com FreeRTOS (CMSIS-RTOS v2).
- Firmware: DebugLog pela USART3 (VCP do ST-Link).
- Firmware: CRC-16 pelo periférico de hardware, com testes Unity on-target (build `Test`).
- Firmware: USB-CDC com eco, recepção por stream buffer estático do FreeRTOS.
- Firmware: esqueleto da máquina de estados do protocolo.
- Software: terminal USB-CDC de linha de comando (`terminal_usb.py`) e gráfico com PySide6 (`terminal_gui.py`).

[Não lançado]: https://github.com/1luizneto/Jiga-de-Testes-para-Sistemas-Embarcados/compare/v0.5.0...HEAD
[0.5.0]: https://github.com/1luizneto/Jiga-de-Testes-para-Sistemas-Embarcados/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/1luizneto/Jiga-de-Testes-para-Sistemas-Embarcados/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/1luizneto/Jiga-de-Testes-para-Sistemas-Embarcados/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/1luizneto/Jiga-de-Testes-para-Sistemas-Embarcados/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/1luizneto/Jiga-de-Testes-para-Sistemas-Embarcados/releases/tag/v0.1.0
