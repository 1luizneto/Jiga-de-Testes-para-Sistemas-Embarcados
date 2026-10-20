# Contexto: Jiga de Testes para Sistemas Embarcados (TCC) — atualizado em 20/10/2026

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

## Estado do git/GitHub (20/10/2026)
- Tags/releases: **v0.1.0** (USB com stream buffer), **v0.2.0** (parser de quadros), **v0.3.0** (respostas
  ACK/NACK), **v0.4.0** (fila CommTask ↔ TestExecTask, `RESET_ALL`, `DAC_SET`).
- **v0.5.0** (`GPIO_SET`, `GPIO_READ`, `ADC_READ`, CT10 e CT11) fechada no branch **`feat_adc-gpio`**:
  versão já em 0.5.0 nos 3 lugares; 29/29 casos HIL passaram com loopback (ainda com o firmware 0.4.0 no
  PING). Falta: medição com o `medir_loopback.py` (dados do Cap. 4), validação final com a versão nova
  gravada, PR → `main`, merge, `git tag -a v0.5.0` no `main`, push da tag e release (o notebook da empresa
  não tem o `gh`; fazer pela página do GitHub).
- Depois do merge: branch da v0.6.0 **a partir do `main` atualizado** (`git checkout main && git pull &&
  git checkout -b feat_pwm`). Branch criado de `main` desatualizado já deu problema uma vez.
- Mensagem de commit: só o texto entre aspas (o commit `ca7c6e9` saiu com `git commit -m "..."` dentro
  da mensagem; ficou assim porque já estava no `origin`).
- Logs da CommTask usam o prefixo `[TASK -> COMM]`, os da TestExecTask `[TASK -> EXEC]`; o `LOG_FIRMWARE`
  do `protocolo.py` está alinhado com os do parser (se mudar o texto de um log do parser no firmware,
  mudar lá também, senão o `teste_protocolo.py` dá FALHA).
- Artefatos de build (`projeto/Debug/`, `projeto/Test/`) saíram do git na v0.5.0 e estão no
  `.gitignore`: o CubeIDE gera tudo de novo no build. Para commitar código, `git add projeto/App` (ou o
  arquivo), nunca a pasta `projeto` inteira sem olhar o `git status`.
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
- Saídas digitais GPIO1–4 = PC8–PC11 (push-pull, ligam em nível baixo pelo `MX_GPIO_Init`). Entradas
  GPIO5–8 = PC12, PD2, PG2, PG3 (rótulos `GPIO1_IN`…`GPIO4_IN` no `.ioc`, **não renomear**), **sem pull
  interno**: são FT e aceitam até 5,5 V com a jiga ligada, mas acima de 4 V o pull interno precisa estar
  desligado (DS11532, Tabela 14, nota 5). Sem resistor externo, entrada solta lê valor indefinido
- ADC: canal 0 = A0 = PA3 (ADC1_IN3), canal 1 = A1 = PC0 (ADC2_IN10), canal 2 = A2 = PC3 (ADC3_IN13), um
  ADC por canal, 12 bits, **sampling time de 480 ciclos**, referência 3,3 V
- PWM (v0.6.0, já no `.ioc`): PWM1 = PE9 (TIM1_CH1, 216 MHz, 16 bits), PWM2 = PA0 (TIM2_CH1, 108 MHz,
  32 bits), PWM3 = PA6 (TIM3_CH1, 108 MHz, 16 bits)
- Loopback dos testes (jumpers): PA4→PA3, PA5→PC0 (CT10); PC8→PC12, PC9→PD2, PC10→PG2, PC11→PG3 (CT11)
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
  u16 LE 0–4095]), GPIO_SET (0x20, [pino u8 1–4][nível u8 0/1]), GPIO_READ (0x21, sem payload → ACK
  [máscara u8, bit 0 = GPIO5 … bit 3 = GPIO8, bits 4–7 em 0]), ADC_READ (0x40, [canal u8 0–2] → ACK
  [canal u8][valor u16 LE]). Ex.: DAC_SET SEQ=20, canal 1, 2048 = `7E 03 14 10 01 00 08 33 E0`; ACK do
  ADC_READ SEQ=3, canal 0, 2047 = `7E 03 03 C0 00 FF 07 9E EE` (Apêndice A)
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
- **Estado seguro** (Apêndice A): no boot e no `RESET_ALL` (`Exec_SafeState`). Cada periférico novo entra
  no `Exec_SafeState`, que tenta **todos** mesmo que um falhe (para não deixar estímulo aplicado). No
  boot, o DAC precisa do `AnalogOut_SafeState` no `BspDac_Init`; os GPIOs não, o `MX_GPIO_Init` já liga
  as saídas em nível baixo.
- **ACK com dados (v0.5.0)**: a função local do comando recebe também o `execResult_t` e preenche `dados`
  e `size` **só no ACK** (o `Exec_Process` zera o `size` antes; NACK nunca leva dado velho).
- **Leitura do GPIO pino a pino**: a BSP lê uma entrada; quem monta a máscara (bit 0 = GPIO5) é o
  `DigitalIo`, que é testável. Em falha de leitura a máscara não é escrita.
- **ADC por polling, uma conversão por comando** (`Start → PollForConversion(10 ms) → GetValue → Stop`;
  o ADC é parado também no timeout). Sampling time de 480 ciclos (~20 µs) porque com 3 ciclos o
  capacitor de amostragem não carrega com fonte de alguns kΩ. Valor > 4095 vindo da BSP = falha.
- **Casos de teste com resposta que varia** (`CasoTeste.conferir`): sem fio, o `GPIO_READ` e o `ADC_READ`
  só conferem o formato (1 byte com bits 4–7 em 0; canal certo e ≤ 4095); com loopback, o CT11 confere a
  máscara exata e o CT10 a leitura a **±41 códigos** do DAC (1 % do fundo de escala = 33 mV). A reprovação
  diz o motivo (ex.: `resposta 8: A0 leu 1320; esperado 1278 +/- 41`).
- **Casos de loopback** (`requer_loopback`): só entram no "executar todos" com `--loopback` / caixa
  "Incluir loopback"; o "RESET_ALL no fim" continua sendo o último. Nos casos com vários comandos, um de
  cada vez, com `PAUSA_COMANDO` = 50 ms entre eles (senão o 2º recebe OCUPADO).
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
- `App/Comm` (Comm, DebugLog), `App/System` (Crc16, Usb, AnalogOut, AnalogIn, DigitalIo, Version.h),
  `App/Exec` (Exec), `App/Hal_Wrapper` (Bsp, Bsp_Crc, Bsp_Usb, Bsp_Uart, Bsp_Dac, Bsp_Gpio, Bsp_Adc),
  `App/Test` (Unity, TestRunner, Suites)
- As funções do `DigitalIo` usam o prefixo `DigitalIO_` (O maiúsculo; escolha do Luiz), os tipos e enums
  `digitalIo…`/`eDIGITALIO_…`
- Padrão: módulos de `System` recebem ponteiros de função da BSP no `_Init`; struct `static` local com os
  dados do módulo; enums de retorno `eXXX_RETURN_...`. Os testes Unity trocam a BSP por funções falsas
  pelo mesmo `_Init` (ex.: DAC falso no `Test_Exec.c`).
- **Nomes de módulo de `App` não podem repetir um periférico do CubeMX** (Windows não diferencia
  maiúsculas: `Dac.h` colidiu com `Core/Inc/dac.h`). Planejado: `AnalogOut` (DAC), `AnalogIn` (ADC),
  `DigitalIo` (GPIO), `Pwm`, `I2cSlave`, `SpiSlave`, `UartEmu`. `Bsp_Xxx` pode, o prefixo evita a colisão.
- Código próprio fora de Core/Drivers/Middlewares; no código gerado, só dentro de USER CODE
- `Bsp_Init()` é chamado no `main.c` antes do scheduler (UART, CRC, USB, DAC, GPIO, ADC)
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

### v0.5.0 — GPIO e ADC (20/10/2026)
**Firmware**
- CubeMX: sampling time de 480 ciclos nos três ADCs (entradas continuam sem pull, rótulos sem renomear)
- `DigitalIo` (System: `DigitalIO_Set`/`Read`/`SafeState`, máscara bit 0 = GPIO5) + `Bsp_Gpio` (tabelas
  porta/pino, `HAL_GPIO_WritePin`/`ReadPin`)
- `AnalogIn` (System: `AnalogIn_Read`, canal 0–2, valor > 4095 = falha) + `Bsp_Adc` (handle por canal,
  polling com timeout de 10 ms)
- `Exec`: `GPIO_SET`, `GPIO_READ` e `ADC_READ` (primeiros ACKs com dados); `Exec_SafeState` com DAC e GPIO
- **70 testes Unity** (45 + 10 DigitalIo com GPIO falso + 6 AnalogIn com ADC falso + 9 Exec); o
  `Test_Exec.c` registra DAC, GPIO e ADC falsos no `IniciaExec`

**Software**
- `protocolo.py`: os três comandos, decodificação da máscara e do valor do ADC (em V); `JigaSimulada` com
  GPIO, ADC e `loopback=True`; `CasoTeste` com `conferir`, `requer_loopback`, `resumo_esperado`
- **29 casos HIL** (26 sem fio + CT10 DAC1→A0, CT10 DAC2→A1, CT11): **29/29 na placa com os jumpers**
  — **CT10 e CT11 atendidos**
- `teste_protocolo.py --loopback`, caixa "Incluir loopback" na GUI, `/teste loopback`; log e USB lidos a
  cada passo (buffer maior)
- `medir_loopback.py`: varredura DAC → ADC com N leituras por ponto → CSV + reta ajustada por canal
- **149 testes pytest**

**Repositório**: `projeto/Debug/` e `projeto/Test/` fora do git (`.gitignore`)

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
| ~~0.5.0~~ | `ADC_READ`, `GPIO_SET`, `GPIO_READ` | CT10 (DAC→ADC) ✅, CT11 (GPIO) ✅ |
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
1. Fechar a v0.5.0:
   - Medição: `python Software/medir_loopback.py --pontos 33 --amostras 16` (faixa útil) e, se quiser
     mostrar a saturação, `--de 0 --ate 4095 --pontos 41`; guardar os CSVs para o Cap. 4 e anotar o
     resultado em "Medição do ADC" abaixo
   - Gravar Test (70 Unity) e Debug (ACK do PING com `firmware 0.5.0`); `teste_protocolo.py` sem jumpers
     (26/26, 3 pulados) e com `--loopback` (29/29); `pytest` (149)
   - PR, merge, tag `v0.5.0`, release
2. v0.6.0: `PWM_SET` (0x30, [canal u8 1–3][frequência u32 Hz][razão cíclica u16 em centésimos de %,
   0–10000]) e `PWM_STOP` (0x31, [canal u8 1–3]); exemplo do Apêndice A: PWM_SET SEQ=4, canal 1, 1 kHz,
   50 % = `7E 07 04 30 01 E8 03 00 00 88 13 5A 47`. Módulo `Pwm` + `Bsp_Pwm` (cálculo de PSC/ARR/CCR por
   canal: TIM1 a 216 MHz e TIM3 a 108 MHz são de 16 bits, TIM2 a 108 MHz é de 32 bits), PWM parado no
   estado seguro. Decidir: faixa de frequência aceita (RF03), arredondamento e o que fazer quando a
   frequência pedida não é exata (NACK ou mais próxima). Critério: frequência ±1 %, razão cíclica ±1 ponto
   percentual (CT12, osciloscópio). Sem loopback possível pela própria jiga ainda (a captura de entrada
   não existe); medir no osciloscópio

### Medição do ADC (v0.5.0, 20/10/2026) — loopback DAC → ADC, `medir_loopback.py`
33 pontos de 124 a 3971 por canal, 16 leituras por ponto (`medicao_faixa_util.csv`, fora do git).

| Canal | Ajuste (leitura = ganho × DAC + offset) | Erro médio por ponto | Pior leitura isolada | Fora de ±41 |
|---|---|---|---|---|
| DAC1 → A0 | 1,00253 × DAC − 1,00 | −1,3 a +9,9 códigos (cresce com o código) | +19 códigos (+15,3 mV) | 0 de 528 |
| DAC2 → A1 | 1,00042 × DAC − 1,99 | −3,1 a +1,6 códigos | −19 códigos (−15,3 mV) | 0 de 528 |

- **Erro sistemático pequeno**: ganho de +0,25 % no canal 1 (≈ +10 códigos = 8 mV no topo da faixa) e
  +0,04 % no canal 2; offset de −1 a −2 códigos nos dois.
- **Quem causa a diferença entre os canais é o DAC**: na medição do multímetro (v0.4.0), o código 2048 deu
  +3 mV (≈ +3,7 códigos) no DAC1 e −2 mV (≈ −2,5 códigos) no DAC2; o loopback deu +4,9 e −0,2 códigos.
  Sobra ~1–2 códigos para o ADC. Para confirmar o ganho do DAC1, medir o PA4 no código 3971 (esperado
  ~3,208 V se o erro for do DAC).
- **Ruído**: a faixa mín–máx de 16 leituras fica em ~15–30 códigos (≈ ±10 códigos, ~8 mV); é ele que
  define a pior leitura isolada (±19), não o erro médio. Margem para a tolerância: ~2×.
- **Conclusão para o Cap. 4**: dentro da faixa útil o conjunto DAC + ADC atende o critério de 1 % do
  fundo de escala com folga (pior caso 15 mV de 33 mV). Se um dia precisar de tolerância menor, o
  caminho é média de N conversões no firmware (oversampling), não mudar o hardware.

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
- **Entradas digitais GPIO5–8 (PC12, PD2, PG2, PG3), seção do hardware / Cap. 3**:
  - São FT: com a jiga ligada (VDD = 3,3 V) aguentam até 5,5 V (DS11532, Tabela 14:
    mín(VDD + 3,6; 5,5)); conferir na tabela de pinos que os 4 são FT. Colocar o DS11532 no `.bib`
  - Nota 5 da Tabela 14: acima de 4 V os pull-up/pull-down internos precisam estar desligados → as
    entradas ficam sem pull interno; o resistor, quando necessário, é externo e escolhido conforme o DUT
    (pull-up para a tensão do DUT em saída open-drain; pull-down para leitura definida com fio solto;
    saída push-pull e o loopback do CT11 não precisam)
  - Restrição de uso: com a jiga desligada (VDD = 0) o limite cai para 3,6 V → ligar a jiga antes do
    DUT e desligar o DUT antes da jiga (não se aplica se o DUT for alimentado pela própria jiga)
  - TODO da metodologia.tex (~linha 161) sobre rótulos: não renomear; dizer que GPIO5–8 correspondem aos
    rótulos `GPIO1_IN`…`GPIO4_IN` do `.ioc`
- **ADC**: sampling time de 480 ciclos (fonte do DUT com impedância de alguns kΩ); leitura por polling,
  uma conversão por comando
- **CT10 e CT11** (quadro de casos de teste): descrever a ligação por jumpers (PA4→PA3, PA5→PC0,
  PC8–PC11→PC12/PD2/PG2/PG3), os 11 pontos de 124 a 3971 (faixa útil do DAC) e o critério de ±41 códigos;
  no Cap. 4, a tabela/gráfico do `medir_loopback.py` (erro por ponto, reta ajustada) e o 29/29
- **Verificação automática com tolerância**: os casos do PC conferem leituras analógicas por faixa e não
  byte a byte (é o mesmo critério do `qua:criterios`); vale citar na seção do app/casos de teste

## Pendências menores (código)
- Cast `(void *)` no `BspCrc_Init` esconde assinatura incompatível — corrigir como foi feito no `BspUart_Init`
- Ramo de software do `Crc16.c` implementa CRC-16/Modbus (e `dCRC_POLINOMIO 0xA001`, `dCRC_LOW/HIGH`
  usam tipo `u8` inexistente, `dFALSE` não definido); trocar para CCITT-FALSE
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
- **`undefined reference` + `dangerous relocation` no link**: a função é chamada mas não existe em nenhum
  `.c` compilado (aconteceu com o `BspGpio_Init`: o comentário ficou, o corpo sumiu)
- **Função que devolve valor sem `return`** compila (só warning) e devolve lixo: o `GPIO_SET` responderia
  NACK aleatório. Ler os warnings do build
- **Arquivo com nome errado** (`Bsp.Adc.c`) compila igual; renomear com `git mv` e dar F5 no projeto para
  os makefiles esquecerem o nome antigo
- **Stage perdido antes do commit**: o `chore:` dos artefatos de build saiu só com o `.gitignore`;
  conferir `git diff --cached --stat` antes de commitar

## Setup no notebook novo
- `git fetch` e checkout do branch atual (ver "Estado do git")
- `cd Software && pip install -r requirements-dev.txt && pytest` (149 testes)
- CubeIDE: importar `projeto/`; as configurações `projeto Debug` e `projeto Test` vêm do git; se o debug
  falhar, porta 61234 (ver armadilhas)
- Terminal do DebugLog: porta do ST-Link a 1000000 baud
