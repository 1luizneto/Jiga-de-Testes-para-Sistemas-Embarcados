/***********************************************************************************************************************
 *   @file       ventilador.h
 *   @{
 **********************************************************************************************************************/

#ifndef MAIN_VENTILADOR_H_
#define MAIN_VENTILADOR_H_

/***********************************************************************************************************************
 * INCLUDES NECESSARIOS
 **********************************************************************************************************************/

#include <stdbool.h>
#include <stdint.h>

/***********************************************************************************************************************
 * DEFINES PUBLICOS
 **********************************************************************************************************************/

// Comportamento conhecido do CT22 (bateria baterias/ct22_ventilador.yaml): liga acima de 1,80 V e desliga abaixo
// de 1,50 V (histerese de 300 mV em torno do limiar de 1,65 V).
#define dVENTILADOR_LIGA_MV     (1800U)
#define dVENTILADOR_DESLIGA_MV  (1500U)

// Falha injetada para mostrar que o CT22 reprova um firmware errado (escolhida na compilacao:
// idf.py -DVENTILADOR_DEFEITO=n). 0: correto; 1: sem histerese (limiar unico de 1,65 V); 2: limiar de ligar
// deslocado para 2,10 V; 3: saida atrasada em 50 ms; 4: saida presa em nivel baixo.
#ifndef dVENTILADOR_DEFEITO
#define dVENTILADOR_DEFEITO     (0)
#endif
#define dVENTILADOR_LIMIAR_UNICO_MV  (1650U)
#define dVENTILADOR_LIGA_ERRADO_MV   (2100U)
#define dVENTILADOR_ATRASO_MS        (50U)

// Periodo da leitura do sensor: o tempo de reacao fica abaixo de ~2 periodos (o CT22 confere ate 20 ms)
#define dVENTILADOR_PERIODO_MS  (1U)

// Pinos no ESP32-S3: sensor no GPIO1 (ADC1 canal 0) <- DAC1 da jiga (PA4); saida no GPIO4 -> GPIO5 da jiga (PC12)
#define dVENTILADOR_PINO_SENSOR (1)
#define dVENTILADOR_PINO_SAIDA  (4)

/***********************************************************************************************************************
 * PROTOTIPOS PUBLICOS
 **********************************************************************************************************************/

bool Ventilador_Atualiza(bool ligado, uint32_t tensaoMv);
void Ventilador_Inicia(void);

#endif /* MAIN_VENTILADOR_H_ */

/** @} */
