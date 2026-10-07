# Software (aplicação desktop)

Primeira versão: um terminal para visualizar o que o STM32 envia pelo USB-CDC.

## Instalação

```
pip install -r requirements.txt
```

## Uso

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
