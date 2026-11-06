/***********************************************************************************************************************
 *   @file         test_falhas.c
 *   @brief        Testes no PC das falhas injetadas no ventilador do CT22 (compilado uma vez por dVENTILADOR_DEFEITO)
 *   @author       Luiz Neto
 *   @details      Cada falha erra num ponto que a bateria do CT22 confere; a 3 (atraso de 50 ms) fica na task e so
 *                 aparece no hardware.
 *
 *   Changelog
 *   @version      <b>1.0.0 - 05/11/2026</b> \n Luiz Neto \n Primeira versao
 *
 *   @{
 **********************************************************************************************************************/

#include <stdio.h>
#include "unity.h"
#include "ventilador.h"

void setUp(void) {}
void tearDown(void) {}

// Saida do Unity (unity_config.h da jiga)
void TestOutput_PutChar(int c)
{
	putchar(c);
}

#if (dVENTILADOR_DEFEITO == 1)
// Sem histerese: em 1,58 V desliga mesmo vindo de ligado, e em 1,72 V liga mesmo vindo de desligado
static void Test_Falha_SemHisterese(void)
{
	TEST_ASSERT_FALSE(Ventilador_Atualiza(true, 1580U));
	TEST_ASSERT_TRUE(Ventilador_Atualiza(false, 1720U));
	TEST_ASSERT_TRUE(Ventilador_Atualiza(false, 2000U));
	TEST_ASSERT_FALSE(Ventilador_Atualiza(true, 1200U));
}
#elif (dVENTILADOR_DEFEITO == 2)
// Limiar de ligar em 2,10 V: 2,0 V nao liga
static void Test_Falha_LimiarDeslocado(void)
{
	TEST_ASSERT_FALSE(Ventilador_Atualiza(false, 2000U));
	TEST_ASSERT_TRUE(Ventilador_Atualiza(false, 2101U));
	TEST_ASSERT_TRUE(Ventilador_Atualiza(true, 1580U));
}
#elif (dVENTILADOR_DEFEITO == 4)
// Saida presa em nivel baixo
static void Test_Falha_SaidaPresa(void)
{
	TEST_ASSERT_FALSE(Ventilador_Atualiza(false, 3000U));
	TEST_ASSERT_FALSE(Ventilador_Atualiza(true, 2000U));
}
#endif

int main(void)
{
	UNITY_BEGIN();
#if (dVENTILADOR_DEFEITO == 1)
	RUN_TEST(Test_Falha_SemHisterese);
#elif (dVENTILADOR_DEFEITO == 2)
	RUN_TEST(Test_Falha_LimiarDeslocado);
#elif (dVENTILADOR_DEFEITO == 4)
	RUN_TEST(Test_Falha_SaidaPresa);
#endif
	return UNITY_END();
}

/** @} */
