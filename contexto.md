# Contexto: Jiga de Testes para Sistemas Embarcados (TCC) — atualizado em 13/10/2026

## Forma de trabalho (IMPORTANTE)
- Prefiro entender e implementar eu mesmo. Me oriente com perguntas, revisões e roteiros
  ("onde, como e o que colocar em quais arquivos").
- Quando eu me perder ou pedir explicitamente ("me diga o código", "corrija", "faça pra mim"), aí pode
  implementar ou mostrar o código. Fora isso, não entregue o código pronto.
- Ao revisar, leia os arquivos atuais antes; aponte erros com explicação do porquê.
- Responda em português.
- Commits: padrão `feat:`/`fix:`/`chore:`/`docs:` em português, **sem** linha de co-autor do Claude;
  PRs sem a linha "Generated with Claude Code".

## Projeto
TCC de Engenharia de Computação (IFPB Campina Grande, 2026). Jiga de testes hardware-in-the-loop
de baixo custo: app desktop (Python/PySide6) envia comandos via USB-CDC para um STM32, que gera
estímulos (DAC, GPIO, PWM), emula escravos (I2C, SPI, UART) e lê respostas do DUT (ADC, GPIO).
Especificação na monografia (LaTeX, seções 3.5, 3.6 e Apêndice A).

- Repositório: https://github.com/1luizneto/Jiga-de-Testes-para-Sistemas-Embarcados
  (firmware em `projeto/`, app Python em `Software/`)
- A monografia (`Texto/TCC_Monografia`, raiz `documento.tex`) NÃO está no git: copiar à parte.

## Estado do git/GitHub (13/10/2026)
- `main` está em `b5b5415` (USB com stream buffer). Tag **v0.1.0** nesse commit + release no GitHub.
- Trabalho novo no branch **`feat/parser-protocolo`**, **PR #1** aberto
  (https://github.com/1luizneto/Jiga-de-Testes-para-Sistemas-Embarcados/pull/1). Ainda não mergeado.
  Tudo commitado e enviado (inclusive este arquivo).
- Logs da CommTask usam o prefixo `[TASK -> COMM]`; o `LOG_FIRMWARE` do `protocolo.py` está alinhado
  (se mudar o texto de um log no firmware, mudar lá também, senão o `teste_protocolo.py` dá FALHA).
- Para continuar no outro PC: `git fetch && git checkout feat/parser-protocolo`.
- Artefatos de build (`projeto/Debug/`, `projeto/Test/`) ainda estão versionados: não commitar as
  mudanças deles (há uma proposta de tirá-los do git via `.gitignore`).

## Hardware
- NUCLEO-F767ZI (STM32F767ZIT6), STM32CubeIDE + CubeMX, HAL, FreeRTOS V10.2.0 (CMSIS-RTOS v2)
- Toolchain GCC 14.3 (trata `incompatible-pointer-types` e `int-conversion` como ERRO)
- Alimentação pelo ST-LINK (CN1, JP3 em U5V); CN13 (USB OTG FS) = canal de comandos PC↔jiga (CDC,
  VID 0483 / PID 5740). Placa energizada antes de conectar o CN13
- USART3 (VCP do ST-LINK, PID 374B) = UART de debug (DebugLog) a **1.000.000 baud**, mutex `uartDebugMutex`
- Timebase do HAL no TIM6; FPU habilitada; USE_NEWLIB_REENTRANT habilitado

## Tasks (FreeRTOS)
| Task | Papel | Ciclo de vida |
|---|---|---|
| defaultTask | Gerada pelo CubeMX: chama `MX_USB_DEVICE_Init()` e pisca LED (decidi manter o init aí) | Permanente |
| CommTask | USB-CDC: recebe/valida quadros (feito), responde PING/STATUS/ABORT, repassa o resto à TestExecTask | Permanente |
| TestExecTask | Comandos diretos e rotinas (DAC/GPIO/PWM/ADC), cria as tasks de emulação | Permanente (vazia) |
| I2C/SPI/UARTEmulationTask | Emulação de escravos | Sob demanda, alocação estática, auto-encerram com handshake |

- Prioridades previstas (monografia 3.5.2): emulações > TestExecTask > CommTask (ainda não aplicadas)
- CommTask ↔ TestExecTask: filas de 1 elemento
- Critério: só vira task o que precisa de preempção real; o resto é módulo

## Protocolo PC↔Jiga (binário)
SOF(0x7E) | LEN(0–240) | SEQ | CMD | PAYLOAD(0–240) | CRC-16 (2 bytes, little-endian)
- Quadro máx. 246 bytes. CRC sobre LEN+SEQ+CMD+PAYLOAD
- CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, sem reflexão, sem XOR final ("123456789" → 0x29B1).
  No Python: `binascii.crc_hqx(dados, 0xFFFF)`
- ACK: CMD | 0x80; NACK: CMD = 0xFF
- PING SEQ=1: `7E 00 01 01 8C EF`

### Decisões tomadas sobre o parser
- **Enquadramento por comprimento (opção A)**: 0x7E só é SOF no estado AGUARDA_SOF; dentro do quadro é
  dado comum. Byte stuffing (HDLC) foi descartado: quadro de tamanho variável, mais complexidade, e o
  USB bulk já tem CRC e retransmissão (erros reais são de dessincronização). Justificar isso no texto.
- **Timeout de pacote**: quem mede o tempo é a CommTask; `Usb_Read` com 100 ms retorna EMPTY →
  `Comm_Timeout()` descarta quadro pela metade. Parser continua puro (sem FreeRTOS), testável no Unity.
- **Nomes**: `Comm_ProcessByte` = parser (recebe 1 byte). `Comm_ProcessFrame` fica reservado para quem
  receber um `commFrame_t` validado e decidir o que fazer (PING/STATUS/ABORT ou fila para a TestExecTask).
  `Comm_Handle()` fica reservado para o ciclo completo da CommTask (ler USB → parser → ProcessFrame →
  resposta/fila), chamado pela task.
- `AGUARDA_RESULTADO` NÃO fica no parser: é estado da CommTask. Ajustar texto/Figura 5 da monografia
  (metodologia.tex ~linha 402) separando "receptor de quadros" de "fluxo da CommTask".
- Pacote USB ≠ quadro: um quadro de 246 B chega em 64+64+64+54; um pacote pode ter fim de um quadro e
  começo de outro. Pacote menor que 64 B (short packet) não é preenchido com lixo.

## Organização do código (`projeto/`)
- `App/Comm` (Comm, DebugLog), `App/System` (Crc16, Usb, Version.h), `App/Hal_Wrapper` (Bsp, Bsp_Crc,
  Bsp_Usb, Bsp_Uart), `App/Test` (Unity, TestRunner, Suites)
- Padrão: módulos de `System` recebem ponteiros de função da BSP no `_Init`; struct `static` local com os
  dados do módulo; enums de retorno `eXXX_RETURN_...`
- Código próprio fora de Core/Drivers/Middlewares; no código gerado, só dentro de USER CODE
- `Bsp_Init()` é chamado no `main.c` antes do scheduler (inicializa UART, CRC, USB)
- Build configuration `Test`: Unity on-target, saída pela USART3, define `UNIT_TEST_ON_TARGET`;
  `App/Test` excluída das builds Debug/Release. Na build Test a CommTask fica em `osDelay` (não toca no parser)

## Já feito
### Até v0.1.0
- FreeRTOS rodando, tasks criadas; DebugLog com mutex
- CRC-16 pelo periférico de hardware via BSP, validado; Unity on-target com 5 testes de CRC passando
- Terminal serial no PC: `terminal_usb.py` (CLI) e `terminal_gui.py` (PySide6, estilo Termite)
- USB-CDC: `usbd_cdc_if` com `CDC_RegisterRxCallback`; `Usb.h/.c` (camada portável, retornos OK /
  INVALID_ARGUMENT / ERROR / BUSY / EMPTY); `Bsp_Usb.c` com stream buffer estático de 512 B
  (`xStreamBufferSendFromISR` na ISR, `xStreamBufferReceive` com timeout na task), contador `rxDroppedBytes`

### No branch feat/parser-protocolo (v0.2.0, falta validar na placa)
**Firmware**
- `Comm.h`: `commFrame_t` (size, seq, cmd, payload[240], crc16), `dCOMM_PAYLOAD_MAX_SIZE`; retornos
  OK, INVALID_ARGUMENT, ERROR, READING, FRAME_OK, CRC_ERROR, LEN_ERROR, TIMEOUT;
  API `Comm_Init`, `Comm_Reset`, `Comm_Timeout`, `Comm_ProcessByte(uint8_t byte, commFrame_t *frame)`
- `Comm.c`: struct com estado, `raw[243]` (LEN+SEQ+CMD+PAYLOAD, sem SOF/CRC), `count` (bytes no estado
  atual, zera a cada troca de estado), `crcReceived`. AGUARDA_SOF → LE_CABECALHO (LEN>240 → LEN_ERROR antes
  de gravar; LEN==0 pula para VALIDA_CRC) → LE_PAYLOAD → VALIDA_CRC (1º byte = parte baixa, 2º = alta,
  depois `Comm_ValidaQuadro`: CRC via `Crc16_Calcule`, preenche o frame só se válido, sempre reseta).
  `default` → reset + ERROR. Sem DebugLog dentro do parser.
- CommTask: `Usb_Read(rxBuffer[64] static, ..., 100 ms)`; OK → `Comm_ProcessByte` por byte, trata
  FRAME_OK dentro do laço (bloco pode ter 2 quadros) e loga cada resultado; EMPTY → `Comm_Timeout()` e
  loga se descartou. `frame` é `static` (stack da task é 1 KB e o vsnprintf do DebugLog gasta muito). Eco removido.
- `BspUart_Send` corrigido: o mutex não era liberado no caminho de sucesso (a própria task travava 10 ms
  esperando o mutex que ela tinha e transmitia sem proteção). Agora: não transmite sem o mutex, libera nos
  dois caminhos, dispensa o mutex se o kernel não está rodando (log do `main`). Timeouts UART/mutex = 50 ms.
- `App/System/Inc/Version.h` (dFW_VERSION_MAJOR/MINOR/PATCH/STRING = 0.2.0), mostrado no log de boot
- `App/Test/Suites/Test_Comm.c`: 10 testes Unity do parser (PING, 0x7E no payload, payload 240, CRC
  errado, LEN inválido, lixo antes do SOF, dois quadros, timeout, quadro pela metade sem timeout, frame NULL),
  registrados em `Test_Suites.h` e `TestRunner.c`. Passaram no PC (CRC por software); falta na placa.

**Software**
- `protocolo.py`: `crc16_ccitt`, `montar_quadro(seq, cmd, payload)`, `ReceptorQuadros` (espelho do
  parser do firmware, com `timeout()`), `CASOS_TESTE` (8 casos), `LOG_FIRMWARE` / `resultado_do_log`
  (mapeia as linhas `[TASK -> COMM] ...` do DebugLog para os resultados), `simular_caso`
- `terminal_gui.py`: painel "Protocolo" (SEQ automático, CMD, payload → Enviar quadro; combo de casos
  → Executar caso / Executar todos, com esperas via QTimer); título mostra a versão
- `terminal_usb.py`: `/quadro CMD [payload]`, `/teste`, `/teste N`, `/teste todos`, `--versao`
- `teste_protocolo.py`: teste HIL automático. Envia os casos pelo CN13 e lê o DebugLog no ST-Link
  (1 Mbaud), mostrando PASS/FALHA. Precisa dos dois cabos e de nenhum outro terminal aberto nessas portas
- `versao.py` (`__version__ = "0.2.0"`), `pytest.ini`, `requirements-dev.txt`, `tests/`
  (test_protocolo, test_terminal, test_versao): 51 testes passando
- `.github/workflows/software.yml`: pytest no GitHub Actions (sem PySide6)

**Versionamento (SemVer)**
- Versão única para firmware + software. MAJOR = incompatibilidade no protocolo; MINOR = funcionalidade
  compatível; PATCH = correção. `0.x.y` = desenvolvimento; `1.0.0` = jiga funcional com o protocolo completo
- Versão em 3 lugares, conferidos por `tests/test_versao.py`: `Version.h`, `Software/versao.py`, `CHANGELOG.md`
- Passo a passo de release no `README.md` (seção Versionamento): CHANGELOG → Version.h/versao.py →
  testes → commit → `git tag -a vX.Y.Z` → `git push origin main --follow-tags` → `gh release create`

## PRÓXIMOS PASSOS (em ordem)
1. Validar na placa: build **Test** (15 testes Unity: 5 CRC + 10 Comm) e build **Debug** + `python Software/teste_protocolo.py`
2. Se passar: merge do PR #1, tag `v0.2.0` e release no GitHub
3. Responder ACK/NACK na CommTask (`Comm_ProcessFrame`); tratar o fim de transmissão via
   `CDC_TransmitCplt_FS` (o `CDC_Transmit_FS` é assíncrono: o buffer não pode ser reutilizado antes de
   terminar). Atenção ao **ZLP**: resposta com tamanho múltiplo de 64 B precisa de pacote de comprimento
   zero (conferir se o `USBD_CDC_DataIn` do middleware trata)
4. Integrar CommTask ↔ TestExecTask (filas de 1 elemento); aí o `Comm_Handle()` vira o ciclo da CommTask.
   Se o timeout do `Usb_Read` diminuir para atender a fila, medir o silêncio desde o último byte
   (ex.: `xTaskGetTickCount`) em vez de confiar em um único EMPTY

## Pendências menores
- `DebugLog_SendTerminal` formata num `printfBuffer` static compartilhado ANTES do mutex (que só existe
  no `BspUart_Send`): duas tasks logando juntas podem corromper o texto
- Casts `(void *)` em `BspCrc_Init` e `BspUart_Init` escondem assinaturas incompatíveis (as funções
  retornam enum, os ponteiros esperam void) — corrigir as assinaturas
- Ramo de software do `Crc16.c` implementa CRC-16/Modbus (e `dCRC_POLINOMIO 0xA001`, `dCRC_LOW/HIGH`
  usam tipo `u8` inexistente, `dFALSE` não definido); trocar para CCITT-FALSE
- Tirar `projeto/Debug/`, `projeto/Test/` do git (`.gitignore` + `git rm -r --cached`)
- Aplicar as prioridades da monografia nas tasks
- Desativar a Ethernet no CubeMX (inicializada sem uso)
- Habilitar IRQs de I2C2/SPI2/UART4 (prioridade ≥ 5) no Épico D
- Ajustar Figura 5 / texto da máquina de estados (separar parser de CommTask)
- Pergunta aberta (USB): `rxCallback` precisa ser volatile? `rxDroppedBytes` sim (alterado na ISR, lido
  na task), `rxStream` não (escrito uma vez no init, antes da ISR existir) — fechar o raciocínio

## Armadilhas já resolvidas
- Debug falhando com "TCP port 51234 not available" / "Error initializing ST-LINK": o Windows (WinNAT,
  usado por Docker/Hyper-V/WSL) reserva faixas de porta (`netsh interface ipv4 show excludedportrange protocol=tcp`).
  Solução: Debug Configurations → Debugger → Port number 61234 (refazer na máquina nova se acontecer)
- GCC 14 trata `incompatible-pointer-types` e `int-conversion` como ERRO: assinaturas de ponteiros de
  função têm que bater exatamente; não esconder com cast `(void *)`
- Funções `static` do `usbd_cdc_if.c` (CDC_Init_FS etc.) são callbacks chamados pela pilha USB; não chamar direto
- IRQ do OTG_FS na prioridade 5 com "Uses FreeRTOS functions" é obrigatório para usar `...FromISR`
- Mutex comum do FreeRTOS não é recursivo: a task dona que tenta pegá-lo de novo fica bloqueada até o timeout
- Arquivo novo em `App/Test/Suites` entra sozinho na build Test (managed build do CubeIDE regenera os makefiles)

## Setup no notebook novo
- `git fetch && git checkout feat/parser-protocolo`
- `cd Software && pip install -r requirements-dev.txt && pytest` (51 testes)
- CubeIDE: importar `projeto/`; se o debug falhar, porta 61234 (ver armadilhas)
- Terminal do DebugLog: porta do ST-Link a 1000000 baud
