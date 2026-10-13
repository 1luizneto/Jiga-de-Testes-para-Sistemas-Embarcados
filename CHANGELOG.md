# Changelog

Todas as mudanças relevantes do projeto (firmware e software) ficam registradas aqui.

O formato segue o [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/) e o projeto usa
[Versionamento Semântico](https://semver.org/lang/pt-BR/). As regras de versão estão no [README](README.md#versionamento).

## [Não lançado]

### Adicionado
- Firmware: `Comm_BuildFrame` monta quadros de resposta, com 6 testes Unity usando os exemplos do Apêndice A.
- Firmware: `Comm_ProcessFrame` decide a resposta de cada quadro: ACK do PING com as versões do
  protocolo e do firmware, NACK CMD_DESCONHECIDO e reenvio da última resposta quando o SEQ se repete.
- Firmware: a CommTask responde cada quadro válido pelo USB-CDC (ACK/NACK do `Comm_ProcessFrame`);
  quadro com CRC errado ou LEN inválido não tem resposta e o PC retransmite por timeout.
- Firmware: detecção de sessão nova pelo DTR (`SET_CONTROL_LINE_STATE`): quando um programa abre a
  porta no PC, a CommTask limpa o cache de SEQ, e o primeiro comando da sessão não recebe a resposta
  guardada da sessão anterior.

### Alterado
- Firmware: timeout entre bytes de 100 ms para 20 ms, conforme o Apêndice A da monografia.
- Firmware: `CDC_Transmit_FS` copia os dados para o buffer da USB, então quem chama pode reutilizar
  o próprio buffer assim que a função retorna.

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

[Não lançado]: https://github.com/1luizneto/Jiga-de-Testes-para-Sistemas-Embarcados/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/1luizneto/Jiga-de-Testes-para-Sistemas-Embarcados/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/1luizneto/Jiga-de-Testes-para-Sistemas-Embarcados/releases/tag/v0.1.0
