# Software (aplicação desktop)

Primeira versão: um terminal para visualizar o que o STM32 envia pelo USB-CDC.

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
- O baud só importa para a UART do ST-Link (DebugLog); o USB-CDC o ignora.

## Terminal de linha de comando

```
python terminal_usb.py                 # detecta o STM32 (VID 0483 / PID 5740)
python terminal_usb.py --porta COM14   # porta específica
python terminal_usb.py --modo texto    # exibe como texto em vez de hex + ASCII
python terminal_usb.py --log saida.txt # grava o que for exibido
python terminal_usb.py --listar        # lista as portas seriais
```

Durante a execução:

| Comando          | Ação                                   |
|------------------|----------------------------------------|
| `texto qualquer` | envia o texto (sem quebra de linha)    |
| `/hex 7E 01 02`  | envia os bytes em hexadecimal          |
| `/modo`          | alterna entre hex e texto              |
| `/ajuda`         | mostra os comandos                     |
| `/sair`, Ctrl+C  | encerra                                |

Se a placa for reiniciada, a porta USB some; o terminal espera e reconecta sozinho.
