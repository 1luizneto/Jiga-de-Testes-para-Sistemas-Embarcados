#!/bin/sh
# Compila e roda no PC os testes Unity que nao dependem do hardware: o Exec e a Rotina (Test_Exec.c) e o gerador de ondas (Test_Onda.c), com os
# modulos de System e os perifericos falsos da suite. Os mesmos testes rodam na placa (build Test).
# Uso, de qualquer pasta: sh projeto/App/Test/host/rodar.sh
# O codigo da jiga compila com -Werror; o Unity, sem avisos (nao e codigo nosso).
set -e
AQUI=$(cd "$(dirname "$0")" && pwd)
APP="$AQUI/../.."
UNITY="$APP/Test/Unity"
SAIDA="${TMPDIR:-/tmp}/test_jiga_host"
mkdir -p "$SAIDA"

# Runner gerado a partir dos testes da suite (na placa, quem chama e o TestRunner.c)
TESTES=$(sed -n 's/^void \(Test_[A-Za-z0-9_]*\)(void)$/\1/p' "$APP/Test/Suites/Test_Exec.c" "$APP/Test/Suites/Test_Onda.c" | tr -d '\r')
{
	echo '#include <stdio.h>'
	echo '#include "unity.h"'
	echo 'void TestOutput_PutChar(int c) { putchar(c); }'
	echo 'void setUp(void) {}'
	echo 'void tearDown(void) {}'
	for t in $TESTES; do echo "void $t(void);"; done
	echo 'int main(void)'
	echo '{'
	echo '	UNITY_BEGIN();'
	for t in $TESTES; do echo "	RUN_TEST($t);"; done
	echo '	return UNITY_END();'
	echo '}'
} > "$SAIDA/runner.c"

gcc -std=c11 -w -DUNITY_INCLUDE_CONFIG_H -I"$UNITY" -c "$UNITY/unity.c" -o "$SAIDA/unity.o"
gcc -std=c11 -Wall -Wextra -Werror -DUNITY_INCLUDE_CONFIG_H -I"$UNITY" -I"$APP/Exec/Inc" -I"$APP/Comm/Inc" \
	-I"$APP/System/Inc" "$SAIDA/runner.c" "$APP/Test/Suites/Test_Exec.c" "$APP/Test/Suites/Test_Onda.c" "$APP/Exec/Exec.c" "$APP/Exec/Rotina.c" "$APP/Exec/Onda.c" \
	"$APP/System/AnalogOut.c" "$APP/System/AnalogIn.c" "$APP/System/DigitalIo.c" "$APP/System/Pwm.c" \
	"$APP/System/I2cBus.c" "$APP/System/SpiBus.c" "$APP/System/UartBus.c" "$SAIDA/unity.o" -o "$SAIDA/test_jiga"
"$SAIDA/test_jiga"
