#!/bin/sh
# Compila e roda no PC os testes do interpretador de comandos, do mapa I2C e do ventilador (CT22) do DUT (gcc + Unity da jiga).
# Uso, de qualquer pasta: sh DUT_ESP32S3/test_host/rodar.sh
# O codigo do DUT compila com -Werror; o Unity, sem avisos (nao e codigo nosso).
set -e
AQUI=$(cd "$(dirname "$0")" && pwd)
MAIN="$AQUI/../main"
UNITY="$AQUI/../../projeto/App/Test/Unity"
SAIDA="${TMPDIR:-/tmp}/test_dut"
mkdir -p "$SAIDA"

gcc -std=c11 -w -DUNITY_INCLUDE_CONFIG_H -I"$UNITY" -c "$UNITY/unity.c" -o "$SAIDA/unity.o"
gcc -std=c11 -Wall -Wextra -Werror -DUNITY_INCLUDE_CONFIG_H -I"$MAIN" -I"$UNITY" \
	"$AQUI/test_dut.c" "$MAIN/comandos.c" "$MAIN/mapa_i2c.c" "$MAIN/ventilador.c" "$SAIDA/unity.o" -o "$SAIDA/test_dut"
"$SAIDA/test_dut"
