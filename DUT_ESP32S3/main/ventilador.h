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
// de 1,50 V (histerese de 300 mV em torno do limiar de 1,65 V). O mesmo comportamento roda no Arduino Uno.
#define dVENTILADOR_LIGA_MV     (1800U)
#define dVENTILADOR_DESLIGA_MV  (1500U)

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
