# Contexto: Jiga de Testes para Sistemas Embarcados (TCC) — atualizado em 03/11/2026

## Forma de trabalho (IMPORTANTE)
- **Desde 03/11/2026 o Claude conduz o projeto até o fim**: implementa, decide (anotando aqui o porquê),
  configura o CubeMX, faz os commits (um por passo, nunca um commit com tudo), push, PR, merge, tag e
  release, atualiza a monografia e escreve o firmware do DUT (ESP32-S3). O Luiz faz o teste manual no fim
  (ligações físicas) e a gente corrige código, texto e interface juntos.
- Antes disso (até a v0.8.0, passo 10) o Luiz implementava com roteiros; o histórico do git mostra os dois
  momentos.
- Ao revisar, ler os arquivos atuais antes; apontar erros com explicação do porquê.
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
  (firmware em `projeto/`, app Python em `Software/`, DUT de referência ESP32-S3 em `DUT_ESP32S3/`)
- A monografia (`Texto/TCC_Monografia`, raiz `documento.tex`, compila com `latexmk -pdf main.tex`) fica
  num **git local**, sem remoto (desde 03/11/2026). Neste repositório entra só o PDF final, na pasta
  `Monografia/`. O Overleaf lê a pasta do LaTeX como está (sem os arquivos gerados, que o `.gitignore` exclui).

## Estado do git/GitHub (03/11/2026)
- Tags/releases: **v0.1.0** (USB com stream buffer), **v0.2.0** (parser de quadros), **v0.3.0** (respostas
  ACK/NACK), **v0.4.0** (fila CommTask ↔ TestExecTask, `RESET_ALL`, `DAC_SET`), **v0.5.0** (`GPIO_SET`,
  `GPIO_READ`, `ADC_READ`, CT10 e CT11), **v0.6.0** (`PWM_SET`, `PWM_STOP`, CT12), **v0.7.0** (I2C).
- **v0.8.0** (SPI 0x60–0x63 e UART 0x70–0x72): PR #8, tag e release (03/11/2026).
- **v0.8.1** (DUT de referência ESP32-S3 + CT20) fechada no branch **`feat_dut-esp32`**: 172/172 Unity e
  62/62 HIL com o firmware 0.8.1, 17 Unity do DUT no PC, 381 pytest; ESP32 gravado e testado pelo USB.
  Falta o CT20 na bancada (ESP32 ligado na jiga): teste manual.
- Cada versão nova nasce de um branch criado do `main` atualizado (`git checkout main && git pull &&
  git checkout -b <branch>`). Branch criado de `main` desatualizado já deu problema uma vez.
- `gh` instalado no PC de casa (`C:\Program Files\GitHub CLI`, conta 1luizneto); o notebook da empresa não tem.
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
- PWM: PWM1 = PE9 (TIM1_CH1, 216 MHz, 16 bits), PWM2 = PA0 (TIM2_CH1, 108 MHz, 32 bits), PWM3 = PA6
  (TIM3_CH1, 108 MHz, 16 bits); PWM mode 1, preload do ARR ligado. Clock dos timers = 2 × PCLK (APB1 = /4,
  APB2 = /2, TIMPRE = 0), lido do RCC pela `Bsp_Pwm` (log no boot)
- I2C2: PF0 = SDA, PF1 = SCL (CN9), open-drain **sem pull interno**: pull-ups externos de ~4,7 kΩ para o
  3V3 da Nucleo. IRQs de evento e de erro na prioridade 5. Timing (clock de 54 MHz): `0x20404768` = 100 kHz
  (o do `.ioc`), `0x6000030D` = 400 kHz (calculado pelo CubeMX, fica na tabela da `Bsp_I2c`)
- SPI2: PB10 = SCK, PB15 = MOSI, PC2 = MISO (CN7/CN9/CN10), 8 bits, clock de 54 MHz (PCLK1); PD7 = CS
  por software (não é o NSS de hardware do SPI2): saída em nível alto como mestre e no estado seguro;
  entrada com pull-up e EXTI nas duas bordas como escrava. IRQ do SPI2 e EXTI9_5 na prioridade 5
- UART4: PD1 = TX, PD0 = RX, 8N1, sem pull interno (o RX solto não recebeu lixo na placa). IRQ na prioridade 5
- Loopback dos testes (jumpers): PA4→PA3, PA5→PC0 (CT10); PC8→PC12, PC9→PD2, PC10→PG2, PC11→PG3 (CT11);
  PB15→PC2 (MOSI→MISO) e PD1→PD0 (TX→RX) (CT13).
  Planejado (opção G, ainda não montado): filtro RC PA6 → 10 kΩ → A2 (PC3) com 10 µF ao GND, para
  conferir a razão cíclica do PWM3 pelo ADC (média = 3,3 V × razão; τ = 0,1 s, esperar ~0,5 s)
- Timebase do HAL no TIM6; FPU habilitada; USE_NEWLIB_REENTRANT habilitado

## DUT de referência (ESP32-S3, v0.8.1)
- Kit ESP32-S3 (QFN56 rev 0.2, 16 MB de flash, 8 MB de PSRAM octal: GPIO33–37 ficam de fora), USB-serial
  CH343 na **COM12** (UART0). ESP-IDF **v6.1** em `C:\esp\v6.1\esp-idf`; ativar no PowerShell com
  `. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1`. Build: `idf.py -B C:\esp\build_dut build`
  (pasta de build curta: com o caminho do repositório, o mbedtls passa de 260 caracteres e o build quebra)
- **Comandado por texto pela jiga**: UART1 do ESP32 (GPIO17 TX → PD0, GPIO18 RX ← PD1, 115200 8N1) na UART4
  da jiga; o PC manda `UART_WRITE "I2CM 50 10 4\n"` e lê `UART_READ` → `OK 01020304\n`. O CT20 roda inteiro
  pelo `teste_protocolo.py --dut`. O USB do kit (UART0) aceita os mesmos comandos (teste do ESP32 sozinho)
- Ligação: PF0↔GPIO8 (SDA), PF1↔GPIO9 (SCL), pull-ups de 4,7 kΩ; PD7↔GPIO10 (CS, 220 Ω em série
  recomendado), PB15↔GPIO11 (MOSI), PB10↔GPIO12 (SCK), PC2↔GPIO13 (MISO); GND comum. **Tirar os jumpers do
  CT13** antes. Tabela em `DUT_ESP32S3/README.md` e em `Software/dut.py` (`DUT_LIGACAO`, usada no tooltip da GUI)
- Comandos: `PING`, `OFF`, `I2CM <end> <escrita|-> <nr>`, `I2CS <end>`, `I2CS_LOAD <off> <dados>`,
  `I2CS_DUMP <off> <n>`, `SPIM <modo> <freq> <dados>`, `SPIS <modo>`, `SPIS_LOAD <dados|->`, `SPIS_READ`;
  respostas `OK [hex]` ou `ERR CMD|ARG|NACK|ESTADO|PERIFERICO`
- Código: `comandos.c` e `mapa_i2c.c` em lógica pura (17 testes Unity no PC com o Unity da jiga,
  `sh DUT_ESP32S3/test_host/rodar.sh`, também no CI); `barramentos.c` com os drivers do ESP-IDF; `main.c` com
  uma task por UART e um mutex (o interpretador usa buffers estáticos)
- Espelho em Python: `Software/dut.py` (`DutSimulado`), ligado à `JigaSimulada` (`dut=`): define a resposta
  esperada dos casos do CT20 e é conferido pelos mesmos vetores do Unity do DUT (`tests/test_dut.py`)
- Versão do DUT = versão da jiga (`DUT_ESP32S3/main/versao_dut.h`, conferida pelo `test_versao.py`)

## Tasks (FreeRTOS)
| Task | Papel | Prioridade | Ciclo de vida |
|---|---|---|---|
| defaultTask | Gerada pelo CubeMX: chama `MX_USB_DEVICE_Init()` e pisca LED (decidi manter o init aí) | Normal | Permanente |
| CommTask | USB-CDC: recebe/valida quadros, responde PING, repassa os comandos pela fila, responde o resultado | Normal | Permanente |
| TestExecTask | Executa os comandos (`Exec_Process`): bloqueada na `comandoQueue`, devolve na `resultadoQueue` | AboveNormal | Permanente |

- **Emulações sem task (v0.7.0 e v0.8.0)**: o escravo I2C roda nos callbacks do HAL (ISR do I2C2), o
  escravo SPI na ISR do SPI2 e do EXTI do CS, e a recepção da UART na ISR da UART4. Uma task só poria uma
  troca de contexto no meio de cada byte, e quem dita o tempo é o DUT. As
  `I2C/SPI/UARTEmulationTask` da monografia foram dispensadas (mudar o texto).
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
  [máscara u8, bit 0 = GPIO5 … bit 3 = GPIO8, bits 4–7 em 0]), PWM_SET (0x30, [canal u8 1–3][frequência
  u32 LE, 1–100000 Hz][razão u16 LE, 0–10000 centésimos de %]), PWM_STOP (0x31, [canal u8 1–3]),
  ADC_READ (0x40, [canal u8 0–2] → ACK [canal u8][valor u16 LE]). Ex.: DAC_SET SEQ=20, canal 1, 2048 =
  `7E 03 14 10 01 00 08 33 E0`; ACK do ADC_READ SEQ=3, canal 0, 2047 = `7E 03 03 C0 00 FF 07 9E EE`;
  PWM_SET SEQ=4, canal 1, 1 kHz, 50 % = `7E 07 04 30 01 E8 03 00 00 88 13 5A 47`; PWM_STOP SEQ=5, canal 1
  = `7E 01 05 31 01 01 3F` (os dois validados na placa)
- I2C (v0.7.0): I2C_CONFIG (0x50, [papel u8 0 escravo/1 mestre][endereço de 7 bits u8, 0x08–0x77, ignorado
  como mestre][velocidade u8 0 = 100 kHz/1 = 400 kHz]), I2C_SLAVE_LOAD (0x51, [offset u8][quantidade
  u8][dados]), I2C_SLAVE_DUMP (0x52, [offset u8][quantidade u8, até 240] → ACK com os bytes),
  I2C_MASTER_XFER (0x53, [endereço u8][nw u8][nr u8, até 240][nw bytes] → ACK com os nr bytes lidos).
  Ex.: I2C_CONFIG SEQ=0, escravo 0x50, 100 kHz = `7E 03 00 50 00 50 00 74 BB`
- SPI (v0.8.0): SPI_CONFIG (0x60, [papel u8 0 escrava/1 mestre][modo u8 0–3][frequência u32 LE, só no
  mestre, ≥ 210 938 Hz]), SPI_SLAVE_LOAD (0x61, [qtd u8 0–239][dados]), SPI_SLAVE_READ (0x62, sem payload →
  ACK [qtd][dados]), SPI_MASTER_XFER (0x63, [qtd u8 1–239][dados] → ACK com os qtd bytes lidos)
- UART (v0.8.0): UART_CONFIG (0x70, [baud u32 LE, 1200–1 000 000], 8N1), UART_WRITE (0x71, [qtd u8
  1–239][dados], ACK só depois de enviar), UART_READ (0x72, [máximo u8] → ACK [qtd][dados], até 239)
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
- **Cálculo do PWM (v0.6.0)**: contagens = round(clock / f); divisor = ⌈contagens / (ARRmax + 1)⌉ (até
  65536, o PSC é de 16 bits nos três); período = round(clock / (divisor × f)), limitado a ARRmax + 1 e no
  mínimo 2; PSC = divisor − 1, ARR = período − 1, CCR = round(razão × período / 10000). O menor divisor
  deixa o ARR o maior possível (melhor resolução da razão). Frequência não exata → **a mais próxima**
  (erro < 0,05 % até 100 kHz; ex.: 50 Hz no TIM3 = 49,9997 Hz), não NACK. 10000 → CCR = ARR + 1 (sempre
  alto). Só aritmética inteira (no Python, `(a + b // 2) // b`: o `round()` arredonda o meio para o par).
- **PWM parado = razão 0 com o timer rodando** (saída conduzida em nível baixo, nunca flutuando);
  `PWM_STOP`, `RESET_ALL` e o boot mantêm a frequência e zeram só a razão. A `Bsp_Pwm` aplica PSC/ARR/CCR,
  força o update (`EGR = UG`) e só chama `HAL_TIM_PWM_Start` se o canal estiver READY. A configuração só
  é guardada no `Pwm` se a BSP aplicou.
- **Espelhos no Python conferidos pelos mesmos vetores do Unity**: CRC (`Test_Crc16.c`) e cálculo do PWM
  (`Test_Pwm.c` ↔ `calcular_pwm`). Se o C e o Python divergirem, o pytest quebra.
- **Escravo I2C (v0.7.0)**: mapa de 256 registradores no padrão de uma EEPROM 24C02 (na escrita do DUT,
  o 1º byte é o ponteiro e os seguintes são gravados com auto-incremento; a leitura sai a partir do
  ponteiro; depois de 0xFF volta a 0x00; leitura sem escrita continua de onde parou). `LOAD`/`DUMP`
  funcionam **sem `I2C_CONFIG`** (o PC carrega antes de ligar o escravo e o DUT nunca lê lixo). Estado seguro
  (boot e `RESET_ALL`): I2C desligado (`HAL_I2C_DeInit`, pinos soltos) e **mapa zerado**. A task copia o
  mapa com a IRQ de evento do I2C2 desligada (lock da BSP); o DUT espera com o SCL segurado.
- **Byte a mais na leitura do DUT (F7)**: o TXDR é recarregado assim que o byte anterior começa a sair;
  no NACK final o HAL descarta o recarregado e chama o `SlaveTxCpltCallback` com o `TXIS` apagado. A
  `Bsp_I2c` distingue os dois casos pelo `TXIS` e chama `I2cBus_FimLeitura()` (devolve o ponteiro) uma vez
  por leitura. **Ponto mais arriscado da versão: só se confirma com o DUT (CT20)**.
- **Mestre I2C por polling** na TestExecTask: escrita de 1 ou 2 bytes + leitura = `HAL_I2C_Mem_Read` (com
  repeated start); escrita maior + leitura = duas transações, com STOP entre elas. Timeout de 50 ms;
  falta de ACK → NACK ERRO_PERIFERICO; sem papel de mestre → NAO_CONFIGURADO.
- **Escrava SPI (v0.8.0)**: armada com `HAL_SPI_TransmitReceive_IT` de 239 bytes (buffer de transmissão =
  o do `LOAD`, completado com 0xFF). Descida do CS (EXTI) → limpa o SSI (o SPI passa a ouvir); subida →
  liga o SSI, drena o RXNE, entrega `239 − RxXferCount` bytes ao `SpiBus_FimTransacao`, reseta o SPI2 pelo
  RCC (única forma de esvaziar a FIFO de transmissão), limpa a IRQ pendente, `HAL_SPI_Init` e rearma.
  Assim **toda transação recomeça do byte 0**. O RX acumula entre transações até o `SPI_SLAVE_READ`, que
  esvazia. `LOAD`/`READ` funcionam sem `SPI_CONFIG`. Clock do DUT recomendado: até ~1 MHz (a escrava
  reage na ISR; não foi medido o limite)
- **Mestre SPI**: frequência = a maior de 54 MHz / 2..256 que **não passa** da pedida (o DUT nunca recebe
  clock acima do que aguenta; 1 MHz → /64 = 843,75 kHz); abaixo de 210 938 Hz → PARAMETRO_INVALIDO.
  Transferência por polling, CS baixo só durante ela, timeout de 50 ms. A GUI mostra a frequência real
- **UART**: 8N1 fixo (o Apêndice A não pede outro formato), buffer circular de 256 bytes preenchido na ISR
  (cheio → descarta os novos; contador de perdidos dispensado: o `System` não conhece o DebugLog);
  `UART_CONFIG` esvazia o buffer; escrita por polling com timeout = 10 bits × qtd / baud + 10 ms (a
  9600 baud, 16 bytes ≈ 17 ms, cabe na `PAUSA_COMANDO`)
- **Sem fio, nenhum caso lê a UART configurada** (RX solto pode ler lixo); a leitura de verdade fica no
  CT13. Na placa o RX solto não recebeu nada, então o pull-up no PD0 ficou dispensado
- **DUT comandado pela UART da jiga (v0.8.1)**: um canal de controle por texto em vez de um firmware fixo.
  O DUT fica genérico (qualquer sequência de mestre/escravo vira um caso no Python) e o CT20 não precisa de
  um segundo cabo nem de sincronizar dois programas no PC. De quebra, a UART da jiga é exercitada com um
  parceiro real
- **Só um mestre por vez no barramento**: o ESP32 liga o mestre só durante o comando e os escravos até o
  `OFF`; fora disso os pinos ficam como entrada. Cada caso do CT20 põe o outro lado como escravo antes e
  termina com o DUT livre (`OFF`). O CS (PD7 ↔ GPIO10) é o único ponto com duas saídas push-pull: o ESP32
  só aciona o GPIO10 dentro do `SPIM` (com 20 µs de folga entre CS e clock, para a ISR do EXTI da jiga)
- **Escravo I2C do ESP32 não avança o ponteiro na leitura**: o driver do ESP-IDF chama o `on_request` no
  casamento do endereço (depois de entregar a escrita pendente pelo `on_receive`) e serve os bytes de um
  ring buffer, sem dizer quantos o mestre leu; a task põe 128 bytes do mapa a partir do ponteiro. Para ler um
  registrador, a jiga escreve só o ponteiro antes (`I2C_MASTER_XFER` com `nw = 1`, repeated start)
- **Escravo SPI do ESP32**: a carga vale a partir da transação armada depois do `SPIS_LOAD` (mandar o LOAD
  antes do `SPIS`). Transação de 239 bytes, buffers de 240 com DMA; o que chega acumula até o `SPIS_READ`
- **CT20 confere o byte a mais do F7**: o DUT lê 4 bytes de 0x10 e depois 2 sem ponteiro; se a jiga não
  devolvesse o byte buscado a mais, viriam 06 07 em vez de 05 06
- **Versão 0.8.1 como PATCH**: o firmware da jiga não mudou (o protocolo é o mesmo); o que entrou foi o DUT
  e os casos de teste
- **DebugLog com lock**: o `DebugLog_Init` recebe send/lock/unlock da BSP; o lock cobre a formatação no
  `printfBuffer` E o envio (antes, duas tasks podiam misturar mensagens). Não chamar de ISR.

## Organização do código (`projeto/`)
- `App/Comm` (Comm, DebugLog), `App/System` (Crc16, Usb, AnalogOut, AnalogIn, DigitalIo, Pwm, I2cBus,
  SpiBus, UartBus, Version.h), `App/Exec` (Exec), `App/Hal_Wrapper` (Bsp, Bsp_Crc, Bsp_Usb, Bsp_Uart,
  Bsp_Dac, Bsp_Gpio, Bsp_Adc, Bsp_Pwm, Bsp_I2c, Bsp_Spi, Bsp_Uart4; `Bsp_Uart` é a USART3 do DebugLog),
  `App/Test` (Unity, TestRunner, Suites)
- As funções do `DigitalIo` usam o prefixo `DigitalIO_` (O maiúsculo; escolha do Luiz), os tipos e enums
  `digitalIo…`/`eDIGITALIO_…`
- Padrão: módulos de `System` recebem ponteiros de função da BSP no `_Init`; struct `static` local com os
  dados do módulo; enums de retorno `eXXX_RETURN_...`. Os testes Unity trocam a BSP por funções falsas
  pelo mesmo `_Init` (ex.: DAC falso no `Test_Exec.c`).
- **Nomes de módulo de `App` não podem repetir um periférico do CubeMX** (Windows não diferencia
  maiúsculas: `Dac.h` colidiu com `Core/Inc/dac.h`). Planejado: `AnalogOut` (DAC), `AnalogIn` (ADC),
  `DigitalIo` (GPIO), `Pwm`, `I2cBus` (escravo e mestre; `I2c.h` colidiria com `i2c.h`), `SpiBus`, `UartBus`.
  `Bsp_Xxx` pode, o prefixo evita a colisão.
- Código próprio fora de Core/Drivers/Middlewares; no código gerado, só dentro de USER CODE
- `Bsp_Init()` é chamado no `main.c` antes do scheduler (UART, CRC, USB, DAC, GPIO, ADC, PWM, I2C, SPI, UART4)
- Build configuration `Test`: Unity on-target, saída pela USART3, define `UNIT_TEST_ON_TARGET`;
  `App/Test` excluída das builds Debug/Release. Na build Test a CommTask fica em `osDelay` e a TestExecTask
  parada na fila (os testes chamam o `Exec_Process` direto)
- Include paths: `App/Comm/Inc`, `App/System/Inc`, `App/Hal_Wrapper/Inc`, `App/Exec/Inc` em todas as
  configurações; `App/Test`, `App/Test/Unity`, `App/Test/Suites` **só na Test**
- Compilar fora do IDE: `make -j8 all` em `projeto/Debug` ou `projeto/Test`, com o toolchain e o make dos
  plugins do CubeIDE no PATH (os makefiles só se atualizam quando o IDE compila depois de um Refresh).
  Gravar: `STM32_Programmer_CLI -c port=SWD mode=UR -w <elf> -v -rst`
- **Build headless (sem abrir o IDE, atualiza os makefiles)**: `stm32cubeidec.exe --launcher.suppressErrors
  -nosplash -application org.eclipse.cdt.managedbuilder.core.headlessbuild -data <workspace temporário>
  -import <projeto> -build "projeto/Test" -build "projeto/Debug"` (`-cleanBuild` para do zero). Ele regrava
  o `env-hash` do `projeto/.settings/language.settings.xml`: desfazer com `git restore` antes de commitar.
  O `STM32_Programmer_CLI.exe` fica em `plugins/...cubeprogrammer.win32_.../tools/bin` do CubeIDE

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

### v0.6.0 — PWM (26/10/2026)
**Firmware**
- CubeMX: preload do ARR ligado nos três timers
- `Pwm` (System: `Pwm_Calcula`/`Set`/`Stop`/`SafeState`, timers recebidos no `Pwm_Init` como
  `pwmTimer_t {clockHz, periodoMax}`) + `Bsp_Pwm` (clock pelo RCC, `BspPwm_Apply`, PWM parado no boot)
- `Exec`: `PWM_SET` e `PWM_STOP`; `Exec_SafeState` com DAC, GPIO e PWM
- **90 testes Unity** (70 + 13 Pwm com timer falso + 7 Exec); o `Test_Exec.c` registra também o timer falso

**Software**
- `protocolo.py`: os dois comandos, `calcular_pwm` (espelho), `pwm_gerado`/`descrever_pwm` (forma de onda
  no pino), `porcento_para_razao`; `JigaSimulada` com os três PWMs
- **37 casos HIL** (8 novos de PWM): **34/34 na placa sem jumpers**
- `/pwm` e `/pwm_stop` no terminal; linha PWM na GUI com PSC/ARR/CCR e a forma de onda esperada
- **208 testes pytest**
- **CT12 atendido** (osciloscópio, `Software/medicoes/medicao_ct12_pwm.csv`):

| Canal (pino) | Pedido | Esperado no pino | Medido |
|---|---|---|---|
| PWM1 (PE9) | 1 kHz, 50 % | 1000,0 Hz, 50,00 % | 1 kHz, 50 %, sem variação visível |
| PWM1 (PE9) | 100 kHz, 90 % | 100 kHz, 90,00 % | 100 kHz ± 10 Hz, 90 % |
| PWM2 (PA0) | 20 kHz, 12,34 % | 20 kHz, 12,33 % (CCR 666 de 5400) | 20 kHz ± 10 Hz, 12,34 % |
| PWM2 (PA0) | 1 Hz, 50 % | 1,000 Hz, 50,00 % | 1 Hz, 50 %, sem variação visível |
| PWM3 (PA6) | 50 Hz, 75 % | 49,9997 Hz, 75,00 % | 50 Hz, 75 %, sem variação visível |
| PWM3 (PA6) | 100 kHz, 25 % | 100 kHz, 25,00 % | 100 kHz ± 10 Hz, 25 % |
| — | Parar / 100 % | 0 V / 3,3 V contínuos | 0 V / 3,3 V contínuos |

  A oscilação de ±10 Hz (0,01 % a 0,05 %) só aparece em 20 e 100 kHz: é a resolução da medida automática
  do osciloscópio, não do PWM (o clock vem do cristal de 8 MHz do ST-Link). Tolerância: ±1 % e ±1 p.p.

### v0.7.0 — I2C, sem validação no barramento (28/10/2026)
**Firmware**
- CubeMX: IRQs de evento e de erro do I2C2 (prioridade 5)
- `I2cBus` (System: `Config`/`Load`/`Dump`/`MasterXfer`/`SafeState` e os eventos `InicioEscrita`,
  `ByteRecebido`, `ProximoByte`, `FimLeitura`, chamados pela ISR) + `Bsp_I2c` (callbacks do HAL, mestre por
  polling, lock pela IRQ de evento, I2C desligado no boot)
- `Exec`: os 4 comandos; `Exec_SafeState` com DAC, GPIO, PWM e I2C
- **117 testes Unity** (90 + 17 I2cBus com barramento falso + 10 Exec); o `Test_Exec.c` registra também o
  barramento I2C falso

**Software**
- `protocolo.py`: os 4 comandos; `JigaSimulada` com o mapa, o papel e `i2c_dispositivos` (escravos
  simulados para o papel de mestre)
- **46 casos HIL** (9 novos de I2C, SEQ 40–51): **43/43 na placa sem jumpers** (só comandos e mapa, sem DUT)
- `/i2c_config`, `/i2c_load`, `/i2c_dump`, `/i2c_xfer` no terminal; linha I2C na GUI
- **246 testes pytest**

### v0.8.1 — DUT de referência e CT20 (03/11/2026)
- `DUT_ESP32S3/`: firmware do ESP32-S3 (ver "DUT de referência"); gravado e testado pelo USB: todos os
  comandos, NACK sem jiga, ERR ESTADO com papel em conflito, boot limpo
- 17 testes Unity do DUT no PC (job `dut-host` no CI)
- `Software/dut.py` (`DutSimulado`) + `JigaSimulada` com endereço e ponteiro da escrava I2C
- **72 casos HIL**: 5 do CT20 (`[DUT]`, SEQ 74–117) — PING, I2C jiga escrava, I2C jiga mestra, SPI jiga
  escrava, SPI jiga mestra. Sem o ESP32, cada um falha só na resposta do DUT (0 bytes), sem travar a jiga
- `teste_protocolo.py --dut`, `/teste dut`, `/dut <comando>`, caixa "Incluir DUT" e "+\n" na UART da GUI
- **381 testes pytest**

### v0.8.0 — SPI e UART (03/11/2026)
**Firmware**
- CubeMX: SPI2 em 8 bits (estava em 4), IRQs do SPI2 e da UART4 (prioridade 5)
- `SpiBus` (System: `Divisor`/`Config`/`Load`/`Read`/`MasterXfer`/`SafeState` e os eventos `PreparaTx` e
  `FimTransacao`, chamados pela ISR) + `Bsp_Spi` (escrava por IRQ com o CS no EXTI do PD7, mestre por
  polling, lock pelas IRQs do SPI2 e do EXTI, SPI desligado no boot)
- `UartBus` (System: `Config`/`Write`/`Read`/`SafeState` e `ByteRecebido`, chamado pela ISR) + `Bsp_Uart4`
  (recepção por IRQ byte a byte, escrita por polling, UART desligada no boot)
- `Exec`: os 7 comandos; `Exec_SafeState` com DAC, GPIO, PWM, I2C, SPI e UART
- **172 testes Unity** (117 + 17 SpiBus + 15 UartBus + 23 Exec): **172/172 na placa**

**Software**
- `protocolo.py`: os 7 comandos, `spi_divisor`/`descrever_spi`; `JigaSimulada` com os buffers do SPI e da
  UART, `transacao_spi`/`recepcao_uart` (o papel do DUT) e loopback
- **67 casos HIL** (19 novos sem fio, SEQ 52–73; CT13 SPI e CT13 UART em loopback, SEQ 230–248):
  **62/62 na placa sem jumpers**. Os comandos do CT13 executam na placa (todos com ACK); a conferência dos
  dados precisa dos jumpers (teste manual)
- `/spi_*`, `/uart_*` e `/uart_texto` no terminal; linhas SPI e UART na GUI
- **335 testes pytest**

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
| ~~0.6.0~~ | `PWM_SET`, `PWM_STOP` | CT12 (osciloscópio) ✅ |
| ~~0.7.0~~ | I2C escravo e mestre (0x50–0x53), sem task, sem validação no barramento | — (CT20 na 0.8.1) |
| ~~0.8.0~~ | SPI (escravo e mestre, 0x60–0x63) e UART (0x70–0x72), sem task | CT13 (comandos ✅; dados no teste manual) |
| ~~0.8.1~~ | Firmware do DUT de referência (ESP32-S3) e casos do CT20 | **CT20** (simulado ✅; bancada no teste manual) |
| 0.9.0 | Rotinas: `RUN_ROUTINE`, `ROUTINE_STATUS`, `READ_RESULT`, `ABORT`, passos ESPERA/ESPERA_GPIO, base de tempo de 1 µs (TIM5), tempo máximo | CT14–CT19 |
| 0.10.0 | App desktop da seção 3.6: `ClienteJiga` com retransmissão (100 ms, 3 tentativas), baterias YAML → rotinas, veredito, relatório | CT02–CT07, CT21 |
| 0.11.0 | Validação com DUTs (ESP32, Arduino Uno) e desempenho | CT22–CT24 |
| 1.0.0 | Jiga completa: todos os CT passando; resultados no Cap. 4 | CT01–CT24 |

- Correções entre versões viram PATCH (ex.: 0.4.1). Limpezas entram na versão que mexer no arquivo.
- A ordem segue a dependência: o caminho de um comando (fila → TestExecTask → periférico → ACK) nasceu
  na 0.4.0 e é reaproveitado por todos os comandos seguintes (cada comando novo = um `case` no
  `Exec_Process` + módulo do periférico + casos no `protocolo.py`/`JigaSimulada`).

## PRÓXIMOS PASSOS
1. Monografia: levar ao texto tudo o que mudou desde 30/09 (ver "Pendências no texto") e gerar o PDF em
   `Monografia/` a cada versão
2. v0.9.0: rotinas (`RUN_ROUTINE` etc., CT14–CT19), depois app da seção 3.6 (0.10.0), desempenho (0.11.0)
3. Teste manual do Luiz (jumpers do CT10/CT11/CT13 e o ESP32 ligado), correções em PATCH
4. Opcional, quando montar o filtro RC (opção G, ver Hardware): caso de loopback do PWM só no Python
   (`PWM_SET` 25/50/75 % no PWM3, esperar ~0,5 s, `ADC_READ` do A2, conferir razão × 4095 ± 41)

### Medição do ADC (v0.5.0, 20/10/2026) — loopback DAC → ADC, `medir_loopback.py`
33 pontos de 124 a 3971 por canal, 16 leituras por ponto (`Software/medicoes/medicao_faixa_util.csv`).

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
- **PWM (v0.6.0)**:
  - **RF03**: faixa de 1 Hz a 100 kHz, razão cíclica de 0 a 100 % em passos de 0,01 % (a resolução real
    depende do período: a 100 kHz no TIM3 são 1080 contagens, ~0,09 %); frequência não exata → a mais
    próxima, não NACK; justificar o menor prescaler (melhor resolução da razão)
  - **Apêndice A**: `PWM_STOP` = razão 0 com o timer rodando (saída em nível baixo); estado seguro idem
  - **CT12**: a tabela medida (6 pontos, 3 canais, 1 Hz a 100 kHz, 0 % e 100 %), o critério (±1 %, ±1 p.p.)
    e a observação da resolução do osciloscópio (±10 Hz em 20/100 kHz). Faltam o **modelo do
    osciloscópio** (instrumentos de medição) e, se possível, capturas de tela das formas de onda
  - Arredondamento dos registradores como exemplo (20 kHz a 12,34 % sai 12,33 %; 50 Hz no TIM3 sai
    49,9997 Hz): a GUI mostra o valor esperado no pino
- **I2C (v0.7.0)**:
  - **Seção do firmware (tasks, 3.5.2)**: a `I2CEmulationTask` não existe; o escravo roda na ISR do I2C2
    (callbacks do HAL), byte a byte. Justificar: o mestre dita o tempo de cada byte e uma task só
    acrescentaria troca de contexto. Revisar a tabela de tasks e a figura, se houver
  - **Apêndice A**: endereços válidos 0x08–0x77 (os outros são reservados pelo padrão); mapa no padrão
    24C02 (1º byte escrito = ponteiro, auto-incremento, volta de 0xFF para 0x00); `LOAD`/`DUMP` sem
    `I2C_CONFIG`; `DUMP` e `nr` até 240 (limite do payload do ACK); estado seguro desliga o I2C e zera o mapa;
    XFER sem papel de mestre → NAO_CONFIGURADO; falta de ACK → ERRO_PERIFERICO (o texto já diz isso)
  - **CT20**: ligação (PF0/PF1, pull-ups de 4,7 kΩ para o 3V3, GND comum) e como o DUT informa o valor
    (UART do DUT → UART4 da jiga, se for essa a decisão)
- **SPI e UART (v0.8.0)**:
  - **Seção do firmware (tasks)**: sem `SPIEmulationTask` nem `UARTEmulationTask` (mesma justificativa do I2C)
  - **Apêndice A**: os 7 comandos com os formatos acima; escrava SPI recomeça do byte 0 a cada transação
    e completa com 0xFF; RX acumulado até o READ (até 239); mestre com a frequência ≤ pedida (54 MHz /
    2..256); UART 8N1, buffer de 256 bytes; WRITE/READ/XFER sem configuração → NAO_CONFIGURADO; estado
    seguro desliga os dois (CS em nível alto)
  - **CT13**: ligação (PB15→PC2, PD1→PD0), modos 0–3 a 1 MHz e 239 bytes a 6,75 MHz; UART a 9600, 115200
    e 1 000 000 baud; comparação exata
  - **Hardware**: PD7 = CS por software (duplo papel); PD0 sem pull
- **Verificação automática com tolerância**: os casos do PC conferem leituras analógicas por faixa e não
  byte a byte (é o mesmo critério do `qua:criterios`); vale citar na seção do app/casos de teste

## Pendências menores (código)
- **USB travada em BUSY** (visto em 26/10/2026): depois de algum envio que o PC não buscou, toda resposta
  dá `Resposta nao enviada (3)` (3 = `eUSB_RETURN_BUSY`, o `TxState` do CDC fica preso) até resetar a
  placa. Se repetir, liberar o envio pendente quando o PC reabre a porta (borda do DTR, onde o
  `Comm_Init()` já é chamado) e anotar o que foi feito antes para reproduzir
- Cast `(void *)` no `BspCrc_Init` esconde assinatura incompatível — corrigir como foi feito no `BspUart_Init`
- Ramo de software do `Crc16.c` implementa CRC-16/Modbus (e `dCRC_POLINOMIO 0xA001`, `dCRC_LOW/HIGH`
  usam tipo `u8` inexistente, `dFALSE` não definido); trocar para CCITT-FALSE
- Desativar a Ethernet no CubeMX (inicializada sem uso)
- Escravo I2C com PF0/PF1 soltos (sem pull-ups): ruído pode gerar eventos espúrios até o `RESET_ALL`; a
  `Bsp_I2c` religa a escuta depois de erro, então não trava. Montar os pull-ups de 4,7 kΩ de vez
- Limitação conhecida: um quadro manual na GUI com o mesmo SEQ de um caso de teste faz o caso receber a
  resposta guardada (é o cache funcionando); reconectar a porta (sessão nova) ou usar outro SEQ
- Pergunta aberta (USB): `rxCallback` precisa ser volatile? `rxDroppedBytes` sim (alterado na ISR, lido
  na task), `rxStream` não (escrito uma vez no init, antes da ISR existir) — fechar o raciocínio

## Armadilhas já resolvidas
- **CubeMX criando arquivos ao gerar código**: `Drivers/CMSIS/Device/ST/STM32F7xx/Source/Templates/
  system_stm32f7xx.c` (cópia do `Core/Src/system_stm32f7xx.c`: daria `multiple definition of SystemInit`
  no link) e `mx.scratch` (temporário). Apagar os dois e não commitar
- **Commit errado ainda não enviado**: `git reset --soft HEAD~1` desfaz o último commit e mantém as
  mudanças no stage (`git reset` tira do stage). Depois do `push`, não usar `reset` nem `--amend`
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
- **Heredoc do bash come barras invertidas** (`\x77` virou `w` num script de edição): escrever scripts com
  o Write e usar strings raw (`r'''...'''`) quando o texto tiver `\`
- **`sed -i` no Git Bash troca CRLF por LF no arquivo inteiro** (aconteceu com o CHANGELOG e um teste):
  editar com Python lendo/gravando bytes ou com o Edit; conferir com `file` antes de commitar
- **Build do ESP-IDF com caminho longo** ("opening dependency file ... No such file"): usar `-B C:\esp\build_dut`
- **Stage perdido antes do commit**: o `chore:` dos artefatos de build saiu só com o `.gitignore`;
  conferir `git diff --cached --stat` antes de commitar

## Setup no notebook novo
- `git fetch` e checkout do branch atual (ver "Estado do git")
- `cd Software && pip install -r requirements-dev.txt && pytest` (381 testes)
- CubeIDE: importar `projeto/`; as configurações `projeto Debug` e `projeto Test` vêm do git; se o debug
  falhar, porta 61234 (ver armadilhas)
- Terminal do DebugLog: porta do ST-Link a 1000000 baud
