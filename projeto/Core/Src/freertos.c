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

#ifdef UNIT_TEST_ON_TARGET
#include "TestRunner.h"
#endif

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define dCOMM_RX_TIMEOUT_MS (100U)

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
/* Definitions for uartDebugMutex */
osMutexId_t uartDebugMutexHandle;
const osMutexAttr_t uartDebugMutex_attributes = {
  .name = "uartDebugMutex"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

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

	static uint8_t echoBuffer[64];
	uint32_t echoSize = 0;

  for(;;)
  {
	  if (Usb_Read(echoBuffer, sizeof(echoBuffer), &echoSize, dCOMM_RX_TIMEOUT_MS) == eUSB_RETURN_OK)
	  {

		  while (Usb_Write(echoBuffer, (uint16_t)echoSize) == eUSB_RETURN_BUSY)
		  {
			  vTaskDelay(1);
		  }
	  }

	  //Comm_Handle();
  }
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
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END TestExecTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

