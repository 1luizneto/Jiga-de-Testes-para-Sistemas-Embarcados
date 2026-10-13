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

## Projeto
TCC de Engenharia de Computação (IFPB Campina Grande, 2026). Jiga de testes hardware-in-the-loop
de baixo custo: app desktop (Python/PySide6) envia comandos via USB-CDC para um STM32, que gera
estímulos (DAC, GPIO, PWM), emula escravos (I2C, SPI, UART) e lê respostas do DUT (ADC, GPIO).
Especificação na monografia (LaTeX, seções 3.5, 3.6 e Apêndice A; casos de teste CT01–CT24 na 3.7).

- Repositório: https://github.com/1luizneto/Jiga-de-Testes-para-Sistemas-Embarcados
  (firmware em `projeto/`, app Python em `Software/`)
- A monografia (`Texto/TCC_Monografia`, raiz `documento.tex`) NÃO está no git: copiar à parte.

## Estado do git/GitHub (13/10/2026)
- Tags/releases: **v0.1.0** (USB com stream buffer), **v0.2.0** (parser de quadros).
- **v0.3.0** (respostas ACK/NACK) fechada no branch **`feat_respostas-protocolo`**: versão já em 0.3.0 nos
  3 lugares e validada na placa. Falta: PR desse branch → `main`, merge, `git tag -a v0.3.0` no `main`,
  push da tag e release no GitHub (o notebook da empresa não tem o `gh`; fazer pela página do GitHub).
- Depois do merge: branch da v0.4.0 **a partir do `main` atualizado** (`git checkout main && git pull &&
  git checkout -b feat/comandos-dac`). Branch criado de `main` desatualizado já deu problema uma vez.
- Logs da CommTask usam o prefixo `[TASK -> COMM]`; o `LOG_FIRMWARE` do `protocolo.py` está alinhado
  (se mudar o texto de um log no firmware, mudar lá também, senão o `teste_protocolo.py` dá FALHA).
- Artefatos de build (`projeto/Debug/`, `projeto/Test/`) ainda estão versionados: não commitar as
  mudanças deles (há uma proposta de tirá-los do git via `.gitignore`).
- `projeto/projeto Debug.launch` está versionado e deve gravar `Debug/projeto.elf`. A configuração
  `projeto Test` (grava `Test/projeto.elf`, build `Test`) fica só no workspace do CubeIDE. Se o
  `projeto Debug.launch` aparecer alterado no `git status`, desconfiar antes de commitar.

## Hardware
- NUCLEO-F767ZI (STM32F767ZIT6), STM32CubeIDE 2.2 + CubeMX, HAL, FreeRTOS V10.2.0 (CMSIS-RTOS v2)
- Toolchain GCC 14.3 (trata `incompatible-pointer-types` e `int-conversion` como ERRO)
- Alimentação pelo ST-LINK (CN1, JP3 em U5V); CN13 (USB OTG FS) = canal de comandos PC↔jiga (CDC,
  VID 0483 / PID 5740). Placa energizada antes de conectar o CN13
- USART3 (VCP do ST-LINK, PID 374B) = UART de debug (DebugLog) a **1.000.000 baud**, mutex `uartDebugMutex`
- Timebase do HAL no TIM6; FPU habilitada; USE_NEWLIB_REENTRANT habilitado

## Tasks (FreeRTOS)
| Task | Papel | Ciclo de vida |
|---|---|---|
| defaultTask | Gerada pelo CubeMX: chama `MX_USB_DEVICE_Init()` e pisca LED (decidi manter o init aí) | Permanente |
| CommTask | USB-CDC: recebe/valida quadros, responde (ACK/NACK, feito), repassa comandos à TestExecTask (v0.4.0) | Permanente |
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
- ACK: mesmo SEQ, CMD | 0x80, payload com os dados. NACK: mesmo SEQ, CMD = 0xFF, payload [cmd, erro]
- PING SEQ=1: `7E 00 01 01 8C EF`; ACK do PING = [versão do protocolo (1), fw maior, menor, correção]
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
- **Nomes**: `Comm_ProcessByte` = parser; `Comm_BuildFrame` = monta quadro; `Comm_ProcessFrame` = decide
  a resposta de um quadro válido. `Comm_Handle()` fica reservado para o ciclo completo da CommTask.
- `AGUARDA_RESULTADO` NÃO fica no parser: é estado da CommTask.
- Pacote USB ≠ quadro: um quadro de 246 B chega em 64+64+64+54; um pacote pode ter fim de um quadro e
  começo de outro. O ZLP (resposta múltipla de 64 B) já é tratado pelo middleware da ST.
- `CDC_Transmit_FS` copia os dados para o `UserTxBufferFS`: o buffer de quem chama fica livre ao retornar
  (seguro porque só a CommTask transmite e só copia com `TxState == 0`).

## Organização do código (`projeto/`)
- `App/Comm` (Comm, DebugLog), `App/System` (Crc16, Usb, Version.h), `App/Hal_Wrapper` (Bsp, Bsp_Crc,
  Bsp_Usb, Bsp_Uart), `App/Test` (Unity, TestRunner, Suites)
- Padrão: módulos de `System` recebem ponteiros de função da BSP no `_Init`; struct `static` local com os
  dados do módulo; enums de retorno `eXXX_RETURN_...`
- Código próprio fora de Core/Drivers/Middlewares; no código gerado, só dentro de USER CODE
- `Bsp_Init()` é chamado no `main.c` antes do scheduler (inicializa UART, CRC, USB)
- Build configuration `Test`: Unity on-target, saída pela USART3, define `UNIT_TEST_ON_TARGET`;
  `App/Test` excluída das builds Debug/Release. Na build Test a CommTask fica em `osDelay` (não toca no parser)
- Compilar fora do IDE: `make -j8 all` em `projeto/Debug` ou `projeto/Test`, com o toolchain e o make dos
  plugins do CubeIDE no PATH. Gravar: `STM32_Programmer_CLI -c port=SWD mode=UR -w <elf> -v -rst`

## Já feito
### v0.1.0 — USB-CDC
- FreeRTOS, tasks, DebugLog com mutex; CRC-16 pelo periférico (5 testes Unity)
- USB-CDC: `usbd_cdc_if` com callbacks; `Usb.h/.c` portável; `Bsp_Usb.c` com stream buffer estático de 512 B
- Terminal serial no PC: `terminal_usb.py` (CLI) e `terminal_gui.py` (PySide6)

### v0.2.0 — receptor de quadros
- `Comm_ProcessByte` (máquina de estados AGUARDA_SOF → LE_CABECALHO → LE_PAYLOAD → VALIDA_CRC), `Comm_Timeout`
- Correção do mutex no `BspUart_Send`; `Version.h`; 10 testes Unity do parser
- `protocolo.py`, `teste_protocolo.py` (HIL: envia pelo CN13 e confere o DebugLog), pytest + CI
- GUI com veredito pelo DebugLog (porta do ST-Link) e tabela PASS/FALHA

### v0.3.0 — respostas (13/10/2026)
**Firmware**
- `Comm_BuildFrame` (monta quadro; 6 testes Unity com os exemplos do Apêndice A)
- `Comm_ProcessFrame`: PING → ACK com versões; outro CMD → NACK CMD_DESCONHECIDO; cache de SEQ
  (`lastResponse`, `lastSeq`, limpo só no `Comm_Init`); 6 testes Unity. Total: **27 testes Unity**
- CommTask responde pelo USB (`CommTask_Responde` no `freertos.c`, retry com limite de 50 ms se BUSY)
- `CDC_Transmit_FS` copia para o `UserTxBufferFS`
- Sessão nova pelo DTR: `CDC_RegisterLineStateCallback` (usbd_cdc_if) → `BspUsb_LineStateFromIsr` (borda
  0→1) → `Usb_NewSession()` → CommTask chama `Comm_Init()`. `CDC_Init_FS` avisa DTR = 0 a cada enumeração
- Timeout entre bytes de 20 ms

**Software**
- `protocolo.py`: `interpretar_resposta`/`Resposta.descrever`, `montar_ack_ping`/`montar_nack`,
  `JigaSimulada` (espelho do `Comm_ProcessFrame`, com `nova_sessao`), `avaliar_caso` (log + USB),
  `CasoTeste.respostas`. **10 casos** (novos: "Comando desconhecido", "SEQ repetido"); cada caso usa SEQs
  próprios. O ACK do PING esperado usa a versão do software: placa com firmware antigo → caso falha
- `teste_protocolo.py` e GUI conferem DebugLog **e** resposta do USB; GUI e CLI decodificam as respostas
- **67 testes pytest**

**Versionamento (SemVer)**
- Versão única para firmware + software. MAJOR = incompatibilidade no protocolo; MINOR = funcionalidade
  compatível; PATCH = correção. `0.x.y` = desenvolvimento; `1.0.0` = jiga funcional com o protocolo completo
- Versão em 3 lugares, conferidos por `tests/test_versao.py`: `Version.h`, `Software/versao.py`, `CHANGELOG.md`
- Ao subir a versão, regravar a placa: o ACK do PING carrega a versão e os casos de teste conferem

## PLANEJAMENTO DAS VERSÕES (até a 1.0.0)
Cada versão fecha quando os casos de teste dela (CT da monografia, seção 3.7) passam na placa.

| Versão | Entrega | Casos de teste |
|---|---|---|
| 0.4.0 | CommTask ↔ TestExecTask (filas de 1 elemento), estado seguro, `RESET_ALL`, `DAC_SET`, NACK PARAMETRO_INVALIDO; prioridades das tasks; DebugLog seguro com 2 tasks logando | CT09; DAC medido com multímetro |
| 0.5.0 | `ADC_READ`, `GPIO_SET`, `GPIO_READ` | CT10 (DAC→ADC), CT11 (GPIO) |
| 0.6.0 | `PWM_SET`, `PWM_STOP` | CT12 (osciloscópio) |
| 0.7.0 | Emulação de escravo I2C (I2CEmulationTask sob demanda, 0x50–0x53) | parte do CT20 |
| 0.8.0 | Escravo SPI e UART (0x60–0x63, 0x70–0x72) | CT13, CT20 |
| 0.9.0 | Rotinas: `RUN_ROUTINE`, `ROUTINE_STATUS`, `READ_RESULT`, `ABORT`, passos ESPERA/ESPERA_GPIO, base de tempo de 1 µs (TIM5), NACK OCUPADO, tempo máximo | CT14–CT19 |
| 0.10.0 | App desktop da seção 3.6: `ClienteJiga` com retransmissão (100 ms, 3 tentativas), baterias YAML → rotinas, veredito, relatório | CT02–CT07, CT21 |
| 0.11.0 | Validação com DUTs (ESP32, Arduino Uno) e desempenho | CT22–CT24 |
| 1.0.0 | Jiga completa: todos os CT passando; resultados no Cap. 4 | CT01–CT24 |

- Correções entre versões viram PATCH (ex.: 0.4.1). Limpezas entram na versão que mexer no arquivo
  (ex.: DebugLog na 0.4.0, porque a TestExecTask passa a logar; IRQs de I2C2/SPI2/UART4 na 0.7/0.8).
- A ordem segue a dependência: o caminho de um comando (fila → TestExecTask → periférico → ACK) nasce
  na 0.4.0 e é reaproveitado por todos os comandos seguintes; as rotinas (0.9.0) reutilizam as funções
  dos comandos diretos; o app (0.10.0) precisa do protocolo completo.

## PRÓXIMOS PASSOS (v0.4.0)
1. Fechar a v0.3.0 no GitHub (PR, merge, tag, release) e criar o branch da v0.4.0 a partir do `main`
2. Roteiro da arquitetura CommTask ↔ TestExecTask: o que vai na fila (o `commFrame_t` inteiro? só
   SEQ/CMD/payload?), quem monta o ACK/NACK do comando executado, como a CommTask espera o resultado
   (`AGUARDA_RESULTADO`) sem deixar de ler o USB, e o NACK OCUPADO se chegar outro comando
3. Estado seguro (saídas em repouso) e `RESET_ALL`
4. `DAC_SET` (canal 1–2, valor 0–4095; fora da faixa → NACK PARAMETRO_INVALIDO), medir com multímetro
5. Casos de teste novos no `protocolo.py` / `JigaSimulada` para os comandos

## PENDÊNCIAS NO TEXTO DA MONOGRAFIA
- **Apêndice A, códigos de erro**: 0x04 CRC_INVALIDO e 0x05 TAMANHO_EXCEDIDO nunca são enviados (a jiga
  não responde a quadro corrompido): marcar como reservados ou retirar, e justificar (SEQ/CMD não confiáveis)
- **CT03** (metodologia.tex, quadro de casos de teste) espera "NACK CRC_INVALIDO": trocar para "quadro
  descartado, sem resposta; o PC retransmite por timeout"
- **Apêndice A, Sequenciamento e Retransmissão**: o cache de SEQ vale por sessão; a sessão começa quando
  o PC abre a porta (DTR em `SET_CONTROL_LINE_STATE`); explicar o problema que isso resolve (PC reinicia
  o SEQ e receberia a resposta guardada da sessão anterior)
- **Figura 5 / seção da máquina de estados** (metodologia.tex ~linha 402): separar o "receptor de
  quadros" (parser puro) do "fluxo da CommTask" (`AGUARDA_RESULTADO`, respostas)
- **Justificar o enquadramento por comprimento** (sem byte stuffing), com os argumentos das Decisões
- **Mistura CMSIS-RTOS v2 + API nativa do FreeRTOS**: stream buffer não existe na CMSIS nem no CubeMX
- **Timeout entre bytes de 20 ms**: o texto já diz 20 ms; o firmware agora bate (antes era 100 ms)

## Pendências menores (código)
- `DebugLog_SendTerminal` formata num `printfBuffer` static compartilhado ANTES do mutex (que só existe
  no `BspUart_Send`): duas tasks logando juntas podem corromper o texto (resolver na v0.4.0)
- Casts `(void *)` em `BspCrc_Init` e `BspUart_Init` escondem assinaturas incompatíveis (as funções
  retornam enum, os ponteiros esperam void) — corrigir as assinaturas
- Ramo de software do `Crc16.c` implementa CRC-16/Modbus (e `dCRC_POLINOMIO 0xA001`, `dCRC_LOW/HIGH`
  usam tipo `u8` inexistente, `dFALSE` não definido); trocar para CCITT-FALSE
- Tirar `projeto/Debug/`, `projeto/Test/` do git (`.gitignore` + `git rm -r --cached`)
- Aplicar as prioridades da monografia nas tasks (v0.4.0)
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
- "Testes Unity não aparecem": a única configuração de execução versionada (`projeto Debug`) grava o
  `Debug/projeto.elf`, mesmo com a build ativa em Test. Criar a `projeto Test` por **Duplicate** e trocar
  o C/C++ Application para `Test/projeto.elf` e a Build Configuration para `Test`
- GCC 14 trata `incompatible-pointer-types` e `int-conversion` como ERRO: assinaturas de ponteiros de
  função têm que bater exatamente; não esconder com cast `(void *)`
- Funções `static` do `usbd_cdc_if.c` (CDC_Init_FS etc.) são callbacks chamados pela pilha USB; não chamar direto
- IRQ do OTG_FS na prioridade 5 com "Uses FreeRTOS functions" é obrigatório para usar `...FromISR`
- Mutex comum do FreeRTOS não é recursivo: a task dona que tenta pegá-lo de novo fica bloqueada até o timeout
- Arquivo novo em `App/Test/Suites` entra sozinho na build Test (managed build do CubeIDE regenera os makefiles)
- PR mergeado não recebe commits novos do branch: o que for commitado depois precisa de outro PR

## Setup no notebook novo
- `git fetch` e checkout do branch atual (ver "Estado do git")
- `cd Software && pip install -r requirements-dev.txt && pytest` (67 testes)
- CubeIDE: importar `projeto/`; se o debug falhar, porta 61234 (ver armadilhas); criar a configuração `projeto Test`
- Terminal do DebugLog: porta do ST-Link a 1000000 baud
