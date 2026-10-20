/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "DebugLog.h"
#include "crc.h"
#include "Crc16.h"
#include "Comm.h"
#include "Usb.h"
#include "Exec.h"

#ifdef UNIT_TEST_ON_TARGET
#include "TestRunner.h"
#endif

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
typedef StaticQueue_t osStaticMessageQDef_t;
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define dCOMM_RX_TIMEOUT_MS (20U)

/// Tempo maximo esperando a USB terminar o envio anterior antes de desistir da resposta
#define dCOMM_TX_TIMEOUT_MS (50U)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */


uint32_t I2CEmulationTaskBuffer[ 256 ];
StaticTask_t I2CEmulationTaskControlBlock;

const osThreadAttr_t I2CEmulationTask_attributes = {
  .name = "I2CEmulationTask",
  .cb_mem = &I2CEmulationTaskControlBlock,
  .cb_size = sizeof(I2CEmulationTaskControlBlock),
  .stack_mem = &I2CEmulationTaskBuffer[0],
  .stack_size = sizeof(I2CEmulationTaskBuffer),
  .priority = (osPriority_t) osPriorityHigh,
};

uint32_t SPIEmulationTaskBuffer[ 256 ];
StaticTask_t SPIEmulationTaskControlBlock;

const osThreadAttr_t SPIEmulationTask_attributes = {
  .name = "SPIEmulationTask",
  .cb_mem = &SPIEmulationTaskControlBlock,
  .cb_size = sizeof(SPIEmulationTaskControlBlock),
  .stack_mem = &SPIEmulationTaskBuffer[0],
  .stack_size = sizeof(SPIEmulationTaskBuffer),
  .priority = (osPriority_t) osPriorityHigh,
};

uint32_t UARTEmulationTaskBuffer[ 256 ];
StaticTask_t UARTEmulationTaskControlBlock;

const osThreadAttr_t UARTEmulationTask_attributes = {
  .name = "UARTEmulationTask",
  .cb_mem = &UARTEmulationTaskControlBlock,
  .cb_size = sizeof(UARTEmulationTaskControlBlock),
  .stack_mem = &UARTEmulationTaskBuffer[0],
  .stack_size = sizeof(UARTEmulationTaskBuffer),
  .priority = (osPriority_t) osPriorityHigh,
};


//taskNameTesteHandle = osThreadNew(entryFunctionTeste, NULL, &taskNameTeste_attributes);

#ifndef UNIT_TEST_ON_TARGET
/// Buffer de envio da CommTask (respostas imediatas e respostas de resultado). O CDC_Transmit_FS copia os
/// dados, entao pode ser reutilizado assim que o Usb_Write retornar OK. static: fora da stack de 1 KB
static uint8_t commTxBuffer[dCOMM_FRAME_MAX_SIZE];
#endif

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Comm */
osThreadId_t CommHandle;
const osThreadAttr_t Comm_attributes = {
  .name = "Comm",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for TestExec */
osThreadId_t TestExecHandle;
const osThreadAttr_t TestExec_attributes = {
  .name = "TestExec",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for comandoQueue */
osMessageQueueId_t comandoQueueHandle;
uint8_t comandoQueueBuffer[ 1 * sizeof( commFrame_t ) ];
osStaticMessageQDef_t comandoQueueControlBlock;
const osMessageQueueAttr_t comandoQueue_attributes = {
  .name = "comandoQueue",
  .cb_mem = &comandoQueueControlBlock,
  .cb_size = sizeof(comandoQueueControlBlock),
  .mq_mem = &comandoQueueBuffer,
  .mq_size = sizeof(comandoQueueBuffer)
};
/* Definitions for resultadoQueue */
osMessageQueueId_t resultadoQueueHandle;
uint8_t resultadoQueueBuffer[ 1 * sizeof( execResult_t ) ];
osStaticMessageQDef_t resultadoQueueControlBlock;
const osMessageQueueAttr_t resultadoQueue_attributes = {
  .name = "resultadoQueue",
  .cb_mem = &resultadoQueueControlBlock,
  .cb_size = sizeof(resultadoQueueControlBlock),
  .mq_mem = &resultadoQueueBuffer,
  .mq_size = sizeof(resultadoQueueBuffer)
};
/* Definitions for uartDebugMutex */
osMutexId_t uartDebugMutexHandle;
const osMutexAttr_t uartDebugMutex_attributes = {
  .name = "uartDebugMutex"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

#ifndef UNIT_TEST_ON_TARGET
static void CommTask_Responde(const commFrame_t *frame);
static void CommTask_VerificaResultado(void);
static void CommTask_Envia(uint16_t tamanho, const char *tipo);
#endif

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void CommTask(void *argument);
void TestExecTask(void *argument);

extern void MX_USB_DEVICE_Init(void);
void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */



  /* USER CODE END Init */
  /* Create the mutex(es) */
  /* creation of uartDebugMutex */
  uartDebugMutexHandle = osMutexNew(&uartDebugMutex_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of comandoQueue */
  comandoQueueHandle = osMessageQueueNew (1, sizeof(commFrame_t), &comandoQueue_attributes);

  /* creation of resultadoQueue */
  resultadoQueueHandle = osMessageQueueNew (1, sizeof(execResult_t), &resultadoQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of Comm */
  CommHandle = osThreadNew(CommTask, NULL, &Comm_attributes);

  /* creation of TestExec */
  TestExecHandle = osThreadNew(TestExecTask, NULL, &TestExec_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
#ifdef UNIT_TEST_ON_TARGET
  TestRunner_Start();
#endif

  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* init code for USB_DEVICE */
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
	  HAL_GPIO_TogglePin(LD1_GPIO_Port, LD1_Pin);
	  osDelay(250);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_CommTask */
/**
* @brief Function implementing the Comm thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_CommTask */
void CommTask(void *argument)
{
  /* USER CODE BEGIN CommTask */
  /* Infinite loop */
	DebugLog_SendTerminal("[TASK -> COMM] Inicio da Task\n");
	commReturn_t retComm = Comm_Init();
	DebugLog_SendTerminal("[TASK -> COMM] Comm = %d\n", retComm);

#ifdef UNIT_TEST_ON_TARGET
	// Na build Test o parser e exercitado pelo TestRunner: a CommTask nao pode mexer no estado dele
	for(;;)
	{
		osDelay(1000);
	}
#else
	// static: fora da stack de 1 KB da task, que ainda precisa caber o vsnprintf do DebugLog
	static uint8_t rxBuffer[64];
	static commFrame_t frame;
	uint32_t rxSize = 0;
	usbReturn_t retUsb;

  for(;;)
  {
	  retUsb = Usb_Read(rxBuffer, sizeof(rxBuffer), &rxSize, dCOMM_RX_TIMEOUT_MS);

	  // Um programa abriu a porta no PC: ele recomeca a contagem de SEQ, entao a resposta guardada da sessao
	  // anterior nao vale mais. Consultado antes de processar os bytes lidos, que ja sao da sessao nova
	  // (o PC liga o DTR ao abrir a porta, antes de enviar qualquer dado)
	  if (Usb_NewSession() != 0U)
	  {
		  Comm_Init();
		  DebugLog_SendTerminal("[TASK -> COMM] Sessao nova (porta aberta no PC): cache de SEQ limpo\n");
	  }

	  if (retUsb == eUSB_RETURN_OK)
	  {
		  // Um bloco do USB pode ter pedaco de quadro ou varios quadros: o parser consome byte a byte
		  for (uint32_t i = 0; i < rxSize; i++)
		  {
			  // O quadro e tratado assim que fica pronto, antes que o proximo sobrescreva o frame
			  switch (Comm_ProcessByte(rxBuffer[i], &frame))
			  {
				  case eCOMM_RETURN_FRAME_OK:

					  DebugLog_SendTerminal("[TASK -> COMM] Quadro OK: LEN=%u SEQ=%u CMD=0x%02X\n", frame.size, frame.seq, frame.cmd);
					  CommTask_Responde(&frame);

				  break;

				  case eCOMM_RETURN_CRC_ERROR:

					  DebugLog_SendTerminal("[TASK -> COMM] Erro de CRC: quadro descartado\n");

				  break;

				  case eCOMM_RETURN_LEN_ERROR:

					  DebugLog_SendTerminal("[TASK -> COMM] LEN maior que %u: quadro descartado\n", dCOMM_PAYLOAD_MAX_SIZE);

				  break;

				  default:
				  break;
			  }
		  }
	  }
	  else if (retUsb == eUSB_RETURN_EMPTY)
	  {
		  // Timeout de pacote: dCOMM_RX_TIMEOUT_MS sem bytes descarta um quadro pela metade
		  if (Comm_Timeout() == eCOMM_RETURN_TIMEOUT)
		  {
			  DebugLog_SendTerminal("[TASK -> COMM] Timeout: quadro incompleto descartado\n");
		  }
	  }

	  // A cada volta (no maximo dCOMM_RX_TIMEOUT_MS): a TestExecTask terminou algum comando?
	  CommTask_VerificaResultado();
  }
#endif
  /* USER CODE END CommTask */
}

/* USER CODE BEGIN Header_TestExecTask */
/**
* @brief Function implementing the TestExec thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_TestExecTask */
void TestExecTask(void *argument)
{
  /* USER CODE BEGIN TestExecTask */
	static commFrame_t comando;
	static execResult_t resultado;

	DebugLog_SendTerminal("[TASK -> EXEC] Inicio da Task\n");

  /* Infinite loop */
  for(;;)  {

	  if (osMessageQueueGet(comandoQueueHandle, &comando, NULL, osWaitForever) != osOK)
	  {
		  continue;
	  }

	  Exec_Process(&comando, &resultado);

	  DebugLog_SendTerminal("[TASK -> EXEC] Executado: CMD=0x%02X SEQ=%u erro=%u\n", resultado.cmd, resultado.seq, resultado.erro);

	  osMessageQueuePut(resultadoQueueHandle, &resultado, 0U, osWaitForever);
  }
  /* USER CODE END TestExecTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

#ifndef UNIT_TEST_ON_TARGET
/***********************************************************************************************************************
 * @brief      Decide a resposta de um quadro valido (Comm_ProcessFrame) e envia, ou repassa para a TestExecTask.
 * @details    Quadro com CRC errado ou LEN invalido nao chega aqui: nao tem resposta, o PC retransmite por timeout.
 *             Comando repassado (FORWARD) vai pela comandoQueue; a resposta dele sai no CommTask_VerificaResultado.
 * @param[in]  frame  Quadro recebido e validado pelo Comm_ProcessByte
 **********************************************************************************************************************/
static void CommTask_Responde(const commFrame_t *frame)
{
	uint16_t txSize = 0;
	commReturn_t ret = Comm_ProcessFrame(frame, commTxBuffer, sizeof(commTxBuffer), &txSize);

	switch (ret)
	{
		case eCOMM_RETURN_OK:
		case eCOMM_RETURN_RESENT:

			CommTask_Envia(txSize, (ret == eCOMM_RETURN_RESENT) ? "reenviada" : "enviada");

		break;

		case eCOMM_RETURN_IGNORED:

			DebugLog_SendTerminal("[TASK -> COMM] Retransmissao do comando pendente: ignorada\n");

		break;

		case eCOMM_RETURN_FORWARD:

			if (osMessageQueuePut(comandoQueueHandle, frame, 0U, 0U) != osOK)
			{
				if (Comm_BuildResult(frame->seq, frame->cmd, (uint8_t)eCOMM_ERROR_OCUPADO, NULL, 0U,
				                     commTxBuffer, sizeof(commTxBuffer), &txSize) == eCOMM_RETURN_OK)
				{
					CommTask_Envia(txSize, "enviada");
				}
			}

		break;

		default:

			DebugLog_SendTerminal("[TASK -> COMM] Erro ao montar a resposta (%d)\n", ret);

		break;
	}
}

/***********************************************************************************************************************
 * @brief      Confere, sem bloquear, se a TestExecTask devolveu um resultado; se sim, monta o ACK/NACK e envia.
 * @details    Sem bloquear porque a CommTask precisa continuar lendo o USB. Resultado de um comando que nao esta
 *             mais pendente (ex.: o PC reabriu a porta enquanto ele executava) e descartado.
 **********************************************************************************************************************/
static void CommTask_VerificaResultado(void)
{
	static execResult_t resultado;
	uint16_t txSize = 0;

	if (osMessageQueueGet(resultadoQueueHandle, &resultado, NULL, 0U) != osOK)
	{
		return;
	}

	if (Comm_BuildResult(resultado.seq, resultado.cmd, resultado.erro, resultado.dados, resultado.size,
	                     commTxBuffer, sizeof(commTxBuffer), &txSize) != eCOMM_RETURN_OK)
	{
		DebugLog_SendTerminal("[TASK -> COMM] Resultado descartado: SEQ=%u nao esta pendente\n", resultado.seq);
		return;
	}

	CommTask_Envia(txSize, "enviada");
}

/***********************************************************************************************************************
 * @brief      Envia a resposta que esta no commTxBuffer pelo USB-CDC.
 * @details    Se a USB ainda esta enviando a anterior, tenta de novo por ate dCOMM_TX_TIMEOUT_MS, para a task nao
 *             travar se o PC parar de ler a porta.
 * @param[in]  tamanho  Bytes da resposta no commTxBuffer
 * @param[in]  tipo     "enviada" ou "reenviada" (so para o log)
 **********************************************************************************************************************/
static void CommTask_Envia(uint16_t tamanho, const char *tipo)
{
	uint32_t espera = 0;
	usbReturn_t retUsb;

	while ((retUsb = Usb_Write(commTxBuffer, tamanho)) == eUSB_RETURN_BUSY)
	{
		if (espera >= dCOMM_TX_TIMEOUT_MS)
		{
			break;
		}
		osDelay(1);
		espera++;
	}

	if (retUsb != eUSB_RETURN_OK)
	{
		DebugLog_SendTerminal("[TASK -> COMM] Resposta nao enviada (%d)\n", retUsb);
		return;
	}

	DebugLog_SendTerminal("[TASK -> COMM] Resposta %s: LEN=%u SEQ=%u CMD=0x%02X\n",
	                      tipo, commTxBuffer[1], commTxBuffer[2], commTxBuffer[3]);
}
#endif

/* USER CODE END Application */

