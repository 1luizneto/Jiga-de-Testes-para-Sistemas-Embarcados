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
- Toda mudança num `.c` de `projeto/App` ganha uma linha `@version` no bloco "Changelog" do cabeçalho
  do arquivo (MINOR para funcionalidade, PATCH para correção), além do `CHANGELOG.md`.
- Funções locais (`static`) usam o mesmo bloco de comentário das públicas (`/****` com `@brief`,
  `@details`, `@param`, `@return` alinhados). Nos arquivos de teste, comentário de uma linha numerado.

## Projeto
TCC de Engenharia de Computação (IFPB Campina Grande, 2026). Jiga de testes hardware-in-the-loop
de baixo custo: app desktop (Python/PySide6) envia comandos via USB-CDC para um STM32, que gera
estímulos (DAC, GPIO, PWM), emula escravos (I2C, SPI, UART) e lê respostas do DUT (ADC, GPIO).
Especificação na monografia (LaTeX, seções 3.5, 3.6 e Apêndice A; casos de teste CT01–CT24 na 3.7).

- Repositório: https://github.com/1luizneto/Jiga-de-Testes-para-Sistemas-Embarcados
  (firmware em `projeto/`, app Python em `Software/`)
- A monografia (`Texto/TCC_Monografia`, raiz `documento.tex`) NÃO está no git: copiar à parte.

## Estado do git/GitHub (13/10/2026)
- Tags/releases: **v0.1.0** (USB com stream buffer), **v0.2.0** (parser de quadros), **v0.3.0** (respostas
  ACK/NACK).
- **v0.4.0** (fila CommTask ↔ TestExecTask, `RESET_ALL`, `DAC_SET`) fechada no branch **`feat_comandos-dac`**:
  versão já em 0.4.0 nos 3 lugares. Falta: medição do DAC (decisão do buffer), validação final na placa
  com a versão nova, PR → `main`, merge, `git tag -a v0.4.0` no `main`, push da tag e release (o notebook
  da empresa não tem o `gh`; fazer pela página do GitHub).
- Depois do merge: branch da v0.5.0 **a partir do `main` atualizado** (`git checkout main && git pull &&
  git checkout -b feat/adc-gpio`). Branch criado de `main` desatualizado já deu problema uma vez.
- Logs da CommTask usam o prefixo `[TASK -> COMM]`, os da TestExecTask `[TASK -> EXEC]`; o `LOG_FIRMWARE`
  do `protocolo.py` está alinhado com os do parser (se mudar o texto de um log do parser no firmware,
  mudar lá também, senão o `teste_protocolo.py` dá FALHA).
- Artefatos de build (`projeto/Debug/`, `projeto/Test/`) ainda estão versionados: não commitar as
  mudanças deles (há uma proposta de tirá-los do git via `.gitignore`).
- Duas configurações de gravação versionadas: `projeto/projeto Debug.launch` (grava `Debug/projeto.elf`)
  e `projeto/projeto Test.launch` (grava `Test/projeto.elf`, build `Test`). Escolher pela setinha do
  botão de Debug, **sem editar** a aba Main. Se um `.launch` aparecer alterado no `git status`, alguém
  editou a configuração: desfazer com `git restore`.

## Hardware
- NUCLEO-F767ZI (STM32F767ZIT6), STM32CubeIDE 2.2 + CubeMX, HAL, FreeRTOS V10.2.0 (CMSIS-RTOS v2)
- Toolchain GCC 14.3 (trata `incompatible-pointer-types` e `int-conversion` como ERRO)
- Alimentação pelo ST-LINK (CN1, JP3 em U5V); CN13 (USB OTG FS) = canal de comandos PC↔jiga (CDC,
  VID 0483 / PID 5740). Placa energizada antes de conectar o CN13
- USART3 (VCP do ST-LINK, PID 374B) = UART de debug (DebugLog) a **1.000.000 baud**, mutex `uartDebugMutex`
- DAC1 = PA4 (canal 1), DAC2 = PA5 (canal 2): 12 bits, sem trigger, **buffer de saída ligado** (CubeMX);
  faixa útil medida ~0,1 a ~3,2 V (ver "Medição do DAC")
- Timebase do HAL no TIM6; FPU habilitada; USE_NEWLIB_REENTRANT habilitado

## Tasks (FreeRTOS)
| Task | Papel | Prioridade | Ciclo de vida |
|---|---|---|---|
| defaultTask | Gerada pelo CubeMX: chama `MX_USB_DEVICE_Init()` e pisca LED (decidi manter o init aí) | Normal | Permanente |
| CommTask | USB-CDC: recebe/valida quadros, responde PING, repassa os comandos pela fila, responde o resultado | Normal | Permanente |
| TestExecTask | Executa os comandos (`Exec_Process`): bloqueada na `comandoQueue`, devolve na `resultadoQueue` | AboveNormal | Permanente |
| I2C/SPI/UARTEmulationTask | Emulação de escravos | High | Sob demanda, alocação estática, auto-encerram com handshake |

- As prioridades já seguem a monografia (3.5.2): emulações > TestExecTask > CommTask
- Filas de 1 elemento, criadas no `.ioc` (Tasks and Queues), alocação **estática**: `comandoQueue`
  (`commFrame_t`) e `resultadoQueue` (`execResult_t`)
- Critério: só vira task o que precisa de preempção real; o resto é módulo

## Protocolo PC↔Jiga (binário)
SOF(0x7E) | LEN(0–240) | SEQ | CMD | PAYLOAD(0–240) | CRC-16 (2 bytes, little-endian)
- Quadro máx. 246 bytes. CRC sobre LEN+SEQ+CMD+PAYLOAD
- CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, sem reflexão, sem XOR final ("123456789" → 0x29B1).
  No Python: `binascii.crc_hqx(dados, 0xFFFF)`
- ACK: mesmo SEQ, CMD | 0x80, payload com os dados. NACK: mesmo SEQ, CMD = 0xFF, payload [cmd, erro]
- PING SEQ=1: `7E 00 01 01 8C EF`; ACK do PING = [versão do protocolo (1), fw maior, menor, correção]
- Comandos implementados: PING (0x01), RESET_ALL (0x02, sem payload), DAC_SET (0x10, [canal u8 1–2][valor
  u16 LE 0–4095]). Ex.: DAC_SET SEQ=20, canal 1, 2048 = `7E 03 14 10 01 00 08 33 E0`
- Códigos de erro do NACK (Apêndice A, `commError_t`): 0x01 CMD_DESCONHECIDO, 0x02 PARAMETRO_INVALIDO,
  0x03 NAO_CONFIGURADO, 0x04 CRC_INVALIDO, 0x05 TAMANHO_EXCEDIDO, 0x06 ERRO_PERIFERICO, 0x07 OCUPADO,
  0x08 ROTINA_INVALIDA, 0x09 SEM_RESULTADO
- `0x55` não é usado por nenhum comando: os casos de teste usam como "comando inexistente"

### Decisões tomadas
- **Enquadramento por comprimento**: 0x7E só é SOF no estado AGUARDA_SOF; dentro do quadro é dado
  comum. Byte stuffing (HDLC) descartado: quadro de tamanho variável, mais complexidade, e o USB bulk
  já tem CRC e retransmissão (erros reais são de dessincronização).
- **Timeout entre bytes de 20 ms** (Apêndice A): `Usb_Read` com 20 ms retorna EMPTY → `Comm_Timeout()`
  descarta quadro pela metade. Quem mede o tempo é a CommTask; o parser continua puro (testável no Unity).
- **Quadro corrompido (CRC errado ou LEN > 240) não tem resposta**: SEQ e CMD não são confiáveis (e com
  LEN inválido o SEQ nem chegou); o PC retransmite por timeout.
- **Cache de SEQ por sessão**: a jiga guarda o SEQ e a última resposta; SEQ repetido → reenvia a
  resposta guardada sem executar. A sessão começa quando um programa abre a porta no PC (borda 0→1 do
  DTR no `SET_CONTROL_LINE_STATE`): aí a CommTask chama `Comm_Init()` e o cache é esquecido.
- **Caminho de um comando (v0.4.0)**: `Comm_ProcessFrame` responde na hora o PING e as retransmissões; os
  outros comandos voltam `FORWARD` → `comandoQueue` (o `commFrame_t` inteiro) → TestExecTask →
  `Exec_Process` → `resultadoQueue` (`execResult_t`: seq, cmd, erro, size, dados) → a CommTask confere a
  fila **sem bloquear** a cada volta do laço (≤ 20 ms) → `Comm_BuildResult` monta ACK (erro 0) ou NACK.
  Quem monta o quadro é sempre a CommTask (igual à figura de sequência da monografia).
- **Um comando pendente por vez**: mesmo SEQ do pendente → ignorado (`IGNORED`); PING → atendido; outro
  comando → NACK OCUPADO **sem cache** (se repetido depois, é executado). Como a CommTask só confere o
  resultado depois de processar o bloco de bytes inteiro (até 64 B), dois comandos no mesmo envio geram o
  OCUPADO do 2º **antes** do ACK do 1º. Resultado de comando que não está mais pendente (sessão nova) é
  descartado.
- **Quem decide se um comando existe é o `Exec`** (CMD_DESCONHECIDO); o `Comm` só atende o PING.
- **Validação em um lugar só**: o `Exec` confere o tamanho do payload; o módulo do periférico
  (`AnalogOut_Set`) confere a faixa e recusa sem tocar no hardware (CT09: "nenhuma saída alterada").
- **Estado seguro** (Apêndice A): no boot (`BspDac_Init` → `AnalogOut_SafeState`) e no `RESET_ALL`
  (`Exec_SafeState`). Cada periférico novo entra no `Exec_SafeState`.
- **Nomes**: `Comm_ProcessByte` = parser; `Comm_BuildFrame` = monta quadro; `Comm_ProcessFrame` = decide a
  resposta; `Comm_BuildResult` = resposta a partir do resultado da execução.
- `AGUARDA_RESULTADO` NÃO fica no parser: é o "pendente" do `Comm` + a espera da CommTask pela fila.
- Pacote USB ≠ quadro: um quadro de 246 B chega em 64+64+64+54; um pacote pode ter fim de um quadro e
  começo de outro. O ZLP (resposta múltipla de 64 B) já é tratado pelo middleware da ST.
- `CDC_Transmit_FS` copia os dados para o `UserTxBufferFS`: o buffer de quem chama fica livre ao retornar
  (seguro porque só a CommTask transmite e só copia com `TxState == 0`).
- **DebugLog com lock**: o `DebugLog_Init` recebe send/lock/unlock da BSP; o lock cobre a formatação no
  `printfBuffer` E o envio (antes, duas tasks podiam misturar mensagens). Não chamar de ISR.

## Organização do código (`projeto/`)
- `App/Comm` (Comm, DebugLog), `App/System` (Crc16, Usb, AnalogOut, Version.h), `App/Exec` (Exec),
  `App/Hal_Wrapper` (Bsp, Bsp_Crc, Bsp_Usb, Bsp_Uart, Bsp_Dac), `App/Test` (Unity, TestRunner, Suites)
- Padrão: módulos de `System` recebem ponteiros de função da BSP no `_Init`; struct `static` local com os
  dados do módulo; enums de retorno `eXXX_RETURN_...`. Os testes Unity trocam a BSP por funções falsas
  pelo mesmo `_Init` (ex.: DAC falso no `Test_Exec.c`).
- **Nomes de módulo de `App` não podem repetir um periférico do CubeMX** (Windows não diferencia
  maiúsculas: `Dac.h` colidiu com `Core/Inc/dac.h`). Planejado: `AnalogOut` (DAC), `AnalogIn` (ADC),
  `DigitalIo` (GPIO), `Pwm`, `I2cSlave`, `SpiSlave`, `UartEmu`. `Bsp_Xxx` pode, o prefixo evita a colisão.
- Código próprio fora de Core/Drivers/Middlewares; no código gerado, só dentro de USER CODE
- `Bsp_Init()` é chamado no `main.c` antes do scheduler (UART, CRC, USB, DAC)
- Build configuration `Test`: Unity on-target, saída pela USART3, define `UNIT_TEST_ON_TARGET`;
  `App/Test` excluída das builds Debug/Release. Na build Test a CommTask fica em `osDelay` e a TestExecTask
  parada na fila (os testes chamam o `Exec_Process` direto)
- Include paths: `App/Comm/Inc`, `App/System/Inc`, `App/Hal_Wrapper/Inc`, `App/Exec/Inc` em todas as
  configurações; `App/Test`, `App/Test/Unity`, `App/Test/Suites` **só na Test**
- Compilar fora do IDE: `make -j8 all` em `projeto/Debug` ou `projeto/Test`, com o toolchain e o make dos
  plugins do CubeIDE no PATH (os makefiles só se atualizam quando o IDE compila depois de um Refresh).
  Gravar: `STM32_Programmer_CLI -c port=SWD mode=UR -w <elf> -v -rst`

## Já feito
### v0.1.0 — USB-CDC
- FreeRTOS, tasks, DebugLog com mutex; CRC-16 pelo periférico (5 testes Unity)
- USB-CDC: `usbd_cdc_if` com callbacks; `Usb.h/.c` portável; `Bsp_Usb.c` com stream buffer estático de 512 B
- Terminal serial no PC: `terminal_usb.py` (CLI) e `terminal_gui.py` (PySide6)

### v0.2.0 — receptor de quadros
- `Comm_ProcessByte` (máquina de estados AGUARDA_SOF → LE_CABECALHO → LE_PAYLOAD → VALIDA_CRC), `Comm_Timeout`
- `protocolo.py`, `teste_protocolo.py` (HIL), pytest + CI; GUI com veredito pelo DebugLog

### v0.3.0 — respostas
- `Comm_BuildFrame`, `Comm_ProcessFrame` (ACK do PING, NACK, cache de SEQ), resposta pelo USB,
  `CDC_Transmit_FS` copiando, sessão nova pelo DTR, timeout entre bytes de 20 ms
- PC: `interpretar_resposta`, `JigaSimulada`, casos conferem DebugLog **e** resposta do USB

### v0.4.0 — primeiro comando de verdade (13/10/2026)
**Firmware**
- DebugLog com lock (send/lock/unlock da BSP), sem o cast `(void *)` no `BspUart_Init`
- `AnalogOut` (System: valida canal/valor, `AnalogOut_SafeState`) + `Bsp_Dac` (HAL_DAC_SetValue +
  HAL_DAC_Start); estado seguro no boot
- `Exec` (`App/Exec`): `Exec_Process` com RESET_ALL e DAC_SET; `Exec_SafeState`
- `Comm`: `FORWARD`/`IGNORED`, `Comm_BuildResult`, pendente e OCUPADO
- Filas no `.ioc`; TestExecTask executa; CommTask com `CommTask_Responde`/`VerificaResultado`/`Envia` e um
  `commTxBuffer` só
- **45 testes Unity** (5 CRC + 10 parser + 6 BuildFrame + 6 ProcessFrame + 8 BuildResult/pendente +
  10 Exec com DAC falso)

**Software**
- `protocolo.py`: `CMD_RESET_ALL`, `CMD_DAC_SET`, `montar_dac_set`, erros PARAMETRO_INVALIDO/OCUPADO; a
  `JigaSimulada` espelha `Comm` + `Exec` + DAC, inclusive o OCUPADO no mesmo bloco de 64 B
- **17 casos HIL** (novos: RESET_ALL, DAC_SET válido, canal/valor/tamanho inválidos, "Comando durante
  execução", RESET_ALL no fim) — **CT09 atendido**
- Terminais: junção de rajadas com tempo máximo (50 ms): logs contínuos voltaram a aparecer
- **93 testes pytest**

**Versionamento (SemVer)**
- Versão única para firmware + software. MAJOR = incompatibilidade no protocolo; MINOR = funcionalidade
  compatível; PATCH = correção. `0.x.y` = desenvolvimento; `1.0.0` = jiga funcional com o protocolo completo
- Versão em 3 lugares, conferidos por `tests/test_versao.py`: `Version.h`, `Software/versao.py`, `CHANGELOG.md`
- Ao subir a versão, regravar a placa: o ACK do PING carrega a versão e os casos de teste conferem

## PLANEJAMENTO DAS VERSÕES (até a 1.0.0)
Cada versão fecha quando os casos de teste dela (CT da monografia, seção 3.7) passam na placa.

| Versão | Entrega | Casos de teste |
|---|---|---|
| ~~0.4.0~~ | CommTask ↔ TestExecTask, estado seguro, `RESET_ALL`, `DAC_SET`, OCUPADO, DebugLog com lock | CT09 ✅; DAC no multímetro |
| 0.5.0 | `ADC_READ`, `GPIO_SET`, `GPIO_READ` | CT10 (DAC→ADC), CT11 (GPIO) |
| 0.6.0 | `PWM_SET`, `PWM_STOP` | CT12 (osciloscópio) |
| 0.7.0 | Emulação de escravo I2C (I2CEmulationTask sob demanda, 0x50–0x53) | parte do CT20 |
| 0.8.0 | Escravo SPI e UART (0x60–0x63, 0x70–0x72) | CT13, CT20 |
| 0.9.0 | Rotinas: `RUN_ROUTINE`, `ROUTINE_STATUS`, `READ_RESULT`, `ABORT`, passos ESPERA/ESPERA_GPIO, base de tempo de 1 µs (TIM5), tempo máximo | CT14–CT19 |
| 0.10.0 | App desktop da seção 3.6: `ClienteJiga` com retransmissão (100 ms, 3 tentativas), baterias YAML → rotinas, veredito, relatório | CT02–CT07, CT21 |
| 0.11.0 | Validação com DUTs (ESP32, Arduino Uno) e desempenho | CT22–CT24 |
| 1.0.0 | Jiga completa: todos os CT passando; resultados no Cap. 4 | CT01–CT24 |

- Correções entre versões viram PATCH (ex.: 0.4.1). Limpezas entram na versão que mexer no arquivo.
- A ordem segue a dependência: o caminho de um comando (fila → TestExecTask → periférico → ACK) nasceu
  na 0.4.0 e é reaproveitado por todos os comandos seguintes (cada comando novo = um `case` no
  `Exec_Process` + módulo do periférico + casos no `protocolo.py`/`JigaSimulada`).

## PRÓXIMOS PASSOS
1. Fechar a v0.4.0: Test (45 Unity) e Debug (17/17, ACK do PING com `firmware 0.4.0`) com a versão nova
   gravada; PR, merge, tag, release
2. v0.5.0: `ADC_READ` (canal 0–2 → ACK [canal, valor u16]; primeiro comando com **dados** no ACK — o
   `Comm_BuildResult` já trata, teste Unity `ResultComDados`), `GPIO_SET` (pino 1–4, nível) e `GPIO_READ`
   (máscara GPIO5–8). Módulos `AnalogIn`/`DigitalIo` + `Bsp_Adc`/`Bsp_Gpio`; GPIO1–4 em nível baixo no
   `Exec_SafeState`. Loopbacks DAC1→A0, DAC2→A1 e GPIO1–4→GPIO5–8 (CT10, CT11)

### Medição do DAC (v0.4.0, 13/10/2026) — multímetro, sem carga, pino 3V3 = 3,300 V
| Código | Ideal | PA4 (canal 1) | PA5 (canal 2) | Erro |
|---|---|---|---|---|
| 0 | 0,000 V | 0,052 V | 0,051 V | +52 / +51 mV |
| 1 | 0,0008 V | 0,052 V | 0,051 V | igual ao 0 (abaixo do piso do buffer) |
| 2048 | 1,650 V | 1,653 V | 1,648 V | +3 / −2 mV (~0,1 %) |
| 4095 | 3,300 V | 3,254 V | 3,254 V | −46 / −46 mV |

Comandos usados (quadros prontos, enviados pela linha de envio em Hex): DAC_SET SEQ 110–117, ex.
4095 no canal 1 = `7E 03 74 10 01 FF 0F F7 8A`.

**Decisão (opção A): manter o buffer de saída ligado e definir uma faixa útil.** No meio da escala o DAC
é preciso (±3 mV); nas pontas a saída satura a ~50 mV dos trilhos (limite do amplificador do buffer).
Desligar o buffer aproximaria das pontas, mas deixaria a saída com impedância alta (kΩ): a carga do DUT
derrubaria a tensão justamente no meio da faixa. `HAL_DAC_Stop` no estado seguro deixaria o pino
flutuando, pior para o DUT do que 50 mV fixos. Faixa útil: **~0,1 V a ~3,2 V (códigos ~124 a ~3971)**.
Com a tolerância proposta (±33 mV, 1 % do fundo de escala) as pontas (0 e 4095) não passam; dentro da
faixa útil, sim. Firmware não muda.

## PENDÊNCIAS NO TEXTO DA MONOGRAFIA
- **Apêndice A, códigos de erro**: 0x04 CRC_INVALIDO e 0x05 TAMANHO_EXCEDIDO nunca são enviados (a jiga
  não responde a quadro corrompido): marcar como reservados ou retirar, e justificar (SEQ/CMD não confiáveis)
- **CT03** (metodologia.tex, quadro de casos de teste) espera "NACK CRC_INVALIDO": trocar para "quadro
  descartado, sem resposta; o PC retransmite por timeout"
- **Apêndice A, Sequenciamento e Retransmissão**: o cache de SEQ vale por sessão; a sessão começa quando
  o PC abre a porta (DTR em `SET_CONTROL_LINE_STATE`); explicar o problema que isso resolve
- **Figura 5 / seção da máquina de estados** (metodologia.tex ~linha 402): separar o "receptor de
  quadros" (parser puro) do "fluxo da CommTask" (pendente, espera do resultado na fila, respostas)
- **Figura de sequência de um comando** (metodologia.tex ~linha 345): agora corresponde ao código
  (fila de comando, fila de resultado, "monta ACK/NACK" na CommTask); citar a verificação da fila de
  resultado sem bloquear a cada ≤ 20 ms e a latência que isso acrescenta
- **OCUPADO**: dois comandos no mesmo envio → o OCUPADO do 2º chega antes do ACK do 1º (o PC associa pelo
  SEQ, não pela ordem); OCUPADO não entra no cache de SEQ
- **Faixa do DAC (decisão: buffer ligado + faixa útil)**:
  - **RF01** ("0 a 3,3 V"): trocar para a faixa útil ~0,1 a ~3,2 V, ou manter e explicar a faixa útil na
    seção do hardware (limite do buffer de saída: satura a ~50 mV dos trilhos)
  - **CT10**: a varredura de 11 pontos fica dentro da faixa útil (~0,1 a ~3,2 V)
  - **Estado seguro** (Apêndice A, "DAC em 0 V"): é o código 0, que dá ~50 mV por causa do buffer
  - **Cap. 4**: tabela medida (0, 1, 2048, 4095 nos dois canais; 3V3 = 3,300 V) como prévia do CT10, e a
    justificativa de manter o buffer (impedância de saída com carga do DUT)
- **Justificar o enquadramento por comprimento** (sem byte stuffing), com os argumentos das Decisões
- **Mistura CMSIS-RTOS v2 + API nativa do FreeRTOS**: stream buffer não existe na CMSIS nem no CubeMX
  (as filas sim: criadas no `.ioc`)
- **Timeout entre bytes de 20 ms**: o texto já diz 20 ms; o firmware bate desde a v0.3.0

## Pendências menores (código)
- Cast `(void *)` no `BspCrc_Init` esconde assinatura incompatível — corrigir como foi feito no `BspUart_Init`
- Ramo de software do `Crc16.c` implementa CRC-16/Modbus (e `dCRC_POLINOMIO 0xA001`, `dCRC_LOW/HIGH`
  usam tipo `u8` inexistente, `dFALSE` não definido); trocar para CCITT-FALSE
- Tirar `projeto/Debug/`, `projeto/Test/` do git (`.gitignore` + `git rm -r --cached`)
- Desativar a Ethernet no CubeMX (inicializada sem uso)
- Habilitar IRQs de I2C2/SPI2/UART4 (prioridade ≥ 5) quando chegar nas emulações (v0.7.0/v0.8.0)
- Limitação conhecida: um quadro manual na GUI com o mesmo SEQ de um caso de teste faz o caso receber a
  resposta guardada (é o cache funcionando); reconectar a porta (sessão nova) ou usar outro SEQ
- Pergunta aberta (USB): `rxCallback` precisa ser volatile? `rxDroppedBytes` sim (alterado na ISR, lido
  na task), `rxStream` não (escrito uma vez no init, antes da ISR existir) — fechar o raciocínio

## Armadilhas já resolvidas
- Debug falhando com "TCP port 51234 not available" / "Error initializing ST-LINK": o Windows (WinNAT,
  usado por Docker/Hyper-V/WSL) reserva faixas de porta (`netsh interface ipv4 show excludedportrange protocol=tcp`).
  Solução: Debug Configurations → Debugger → Port number 61234 (refazer na máquina nova se acontecer)
- "Testes Unity não aparecem": a placa estava com o `Debug/projeto.elf`. Gravar pela configuração
  `projeto Test` (escolhida na setinha do botão de Debug), não editando a `projeto Debug`
- **Include path**: editar **uma configuração de cada vez** (Debug, depois Test). Com
  "[All configurations]" o CubeIDE grava só os caminhos comuns em todas e apagou os de `App/Test` da Test
  (`TestRunner.h: No such file`)
- **Nome de módulo igual ao de um periférico do CubeMX** (`Dac.h` × `dac.h`): no Windows o include pega o
  do CubeMX (o `Core/Inc` vem antes nos include paths) e aparecem dezenas de "undeclared". Renomear o módulo
- **Trocar de branch com abas abertas no CubeIDE**: o IDE salva a aba antiga por cima do arquivo novo no
  próximo build (aconteceu com o `usbd_cdc_if.c` e o link quebrou). Fechar as abas ou dar Refresh antes
- **Teste temporário entrando no commit**: conferir `git status`/`git diff --stat` antes de cada commit
  (o teste do multímetro na TestExecTask foi commitado uma vez)
- **Terminais "parados" com log contínuo**: a junção em rajadas esperava 5 ms de silêncio que nunca vinha;
  agora a rajada tem tempo máximo de 50 ms
- GCC 14 trata `incompatible-pointer-types` e `int-conversion` como ERRO: assinaturas de ponteiros de
  função têm que bater exatamente; não esconder com cast `(void *)`
- Funções `static` do `usbd_cdc_if.c` (CDC_Init_FS etc.) são callbacks chamados pela pilha USB; não chamar direto
- IRQ do OTG_FS na prioridade 5 com "Uses FreeRTOS functions" é obrigatório para usar `...FromISR`
- Mutex comum do FreeRTOS não é recursivo: a task dona que tenta pegá-lo de novo fica bloqueada até o timeout
- Arquivo novo em `App/...` entra sozinho no build (managed build do CubeIDE regenera os makefiles); pasta
  nova com `Inc/` precisa do include path
- PR mergeado não recebe commits novos do branch: o que for commitado depois precisa de outro PR
- GUI aberta antes de atualizar o código continua com o software antigo na memória: reiniciar a GUI

## Setup no notebook novo
- `git fetch` e checkout do branch atual (ver "Estado do git")
- `cd Software && pip install -r requirements-dev.txt && pytest` (93 testes)
- CubeIDE: importar `projeto/`; as configurações `projeto Debug` e `projeto Test` vêm do git; se o debug
  falhar, porta 61234 (ver armadilhas)
- Terminal do DebugLog: porta do ST-Link a 1000000 baud
