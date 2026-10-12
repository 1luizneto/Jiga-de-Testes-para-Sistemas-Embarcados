# Jiga de Testes para Sistemas Embarcados

Repositório do TCC: jiga de testes *hardware-in-the-loop* de baixo custo. Uma aplicação desktop
(Python/PySide6) envia comandos por USB-CDC para um STM32, que gera estímulos (DAC, GPIO, PWM),
emula escravos (I2C, SPI, UART) e lê as respostas do dispositivo sob teste (ADC, GPIO).

| Pasta | Conteúdo |
|---|---|
| [`projeto/`](projeto) | Firmware da NUCLEO-F767ZI (STM32CubeIDE, HAL, FreeRTOS) |
| [`Software/`](Software) | Aplicação desktop e testes do lado do PC ([README](Software/README.md)) |
| [`CHANGELOG.md`](CHANGELOG.md) | O que mudou em cada versão |

## Testes

| O quê | Onde | Como rodar |
|---|---|---|
| Firmware (Unity, na placa) | `projeto/App/Test` | Build configuration **Test** no CubeIDE; resultado na UART do ST-Link |
| Software (pytest) | `Software/tests` | `cd Software && pytest` (roda também no GitHub Actions) |
| Placa + PC (HIL) | `Software/teste_protocolo.py` | Ver o [README do Software](Software/README.md#teste-automático-com-a-placa) |

## Versionamento

O projeto segue o [Versionamento Semântico](https://semver.org/lang/pt-BR/) (`MAJOR.MINOR.PATCH`).
Firmware e software têm **uma única versão**, porque os dois lados falam o mesmo protocolo.

| Parte | Quando sobe | Exemplo |
|---|---|---|
| **MAJOR** | Mudança incompatível no protocolo PC ↔ jiga (formato do quadro, significado de um comando) | `1.4.2` → `2.0.0` |
| **MINOR** | Funcionalidade nova compatível (comando novo, emulação nova, tela nova) | `0.2.0` → `0.3.0` |
| **PATCH** | Correção de bug sem funcionalidade nova | `0.2.0` → `0.2.1` |

Enquanto a versão for `0.x.y`, o projeto está em desenvolvimento e o protocolo ainda pode mudar.
A `1.0.0` marca a jiga funcional, com o protocolo da monografia completo.

A versão fica em três lugares, conferidos por um teste (`Software/tests/test_versao.py`):

- `projeto/App/System/Inc/Version.h` (firmware; aparece no log de inicialização)
- `Software/versao.py` (software; aparece no título da janela e em `--versao`)
- `CHANGELOG.md`

### Como lançar uma versão

1. Mover os itens de `[Não lançado]` do `CHANGELOG.md` para uma seção nova `## [X.Y.Z] - AAAA-MM-DD`
   e atualizar os links do fim do arquivo.
2. Atualizar `Version.h` (MAJOR, MINOR, PATCH e a string) e `Software/versao.py`.
3. Rodar os testes: `pytest` em `Software/` e a build **Test** na placa.
4. Commit, tag anotada e envio para o GitHub:

```bash
git commit -am "chore: release vX.Y.Z"
```

```bash
git tag -a vX.Y.Z -m "vX.Y.Z"
```

```bash
git push origin main --follow-tags
```

5. Criar a release no GitHub com as notas do CHANGELOG (opcionalmente anexando o `projeto.elf`):

```bash
gh release create vX.Y.Z --title "vX.Y.Z" --notes-file notas.md
```
