/***********************************************************************************************************************
 *   @file         Bsp_Usb.c
 *   @addtogroup   App
 *   @brief        Arquivo que implementa a abstração da USB
 *   @author       Luiz Neto
 *   @details
 *   \n <b>Ferramentas:</b>
 *   - Generic.
 *
 *   \n <b>Dependencias:</b>
 *   - None.
 *
 *   \n <b>Observacoes:</b>
 *   - None.
 *
 *   Changelog
 *   @version      <b>1.0.0 - 07/10/2026</b> \n Luiz Neto \n Primeira versao
 *   @version      <b>1.1.0 - 13/10/2026</b> \n Luiz Neto \n Deteccao de sessao nova pelo DTR (porta aberta no
 *                 PC), informada pelo Usb_NewSession
 *
 *   @copyright
 *   @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include "Bsp_Usb.h"
#include "Usb.h"
#include "main.h"
#include "usbd_cdc_if.h"
#include "DebugLog.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "stream_buffer.h"

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/

/// Capacidade do stream buffer de recepcao: 2 quadros maximos (2 x 246 B)
#define dRX_STREAM_SIZE (512U)

#define dRX_STREAM_TRIGGER (1U)

/***********************************************************************************************************************
 * TIPOS LOCAIS
 **********************************************************************************************************************/

static struct bspUSb
{

	uint8_t rxStreamStorage[dRX_STREAM_SIZE + 1U];

	StaticStreamBuffer_t rxStreamControl;

	StreamBufferHandle_t rxStream;

	volatile uint32_t rxDroppedBytes;

	/// Ultimo DTR informado pelo PC (1 = porta aberta). Escrito so na ISR do OTG_FS
	volatile uint8_t dtr;

	/// 1 quando o DTR foi de 0 para 1 (sessao nova) e a task ainda nao consultou. Escrito na ISR, limpo na task
	volatile uint8_t newSession;

} bspUsb;

/***********************************************************************************************************************
 * VARIAVEIS LOCAIS
 **********************************************************************************************************************/


/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

static usbReturn_t BspUsb_Transmit(uint8_t* buffer, uint16_t bufferSize);
static usbReturn_t BspUsb_Received(uint8_t* buffer, uint32_t bufferSize, uint32_t* readSize, uint32_t timeoutMs);
static void BspUsb_RxFromIsr(uint8_t *buffer, uint32_t bufferSize);
static void BspUsb_LineStateFromIsr(uint8_t dtr);
static uint8_t BspUsb_NewSession(void);


/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

bspUsbReturn_t BspUsb_Init(void)
{

	bspUsb.rxStream = xStreamBufferCreateStatic(dRX_STREAM_SIZE,
												dRX_STREAM_TRIGGER,
												bspUsb.rxStreamStorage,
												&bspUsb.rxStreamControl);
	bspUsb.rxDroppedBytes = 0;
	bspUsb.dtr            = 0U;
	bspUsb.newSession     = 0U;

	if (bspUsb.rxStream == NULL)
	{
		return eBSP_USB_RETURN_ERROR;
	}

	CDC_RegisterRxCallback(BspUsb_RxFromIsr);
	CDC_RegisterLineStateCallback(BspUsb_LineStateFromIsr);

	if (Usb_Init(BspUsb_Transmit, BspUsb_Received, BspUsb_NewSession) != eUSB_RETURN_OK)
	{
		return eBSP_USB_RETURN_ERROR;
	}

	return eBSP_USB_RETURN_OK;
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * @brief      Envia os bytes pelo USB-CDC (Usb_Write).
 * @details    O CDC_Transmit_FS copia os dados para o buffer da USB: o buffer de quem chama fica livre ao retornar.
 * @param[in]  buffer      Bytes a enviar
 * @param[in]  bufferSize  Quantidade de bytes
 * @return     eUSB_RETURN_OK, eUSB_RETURN_BUSY (envio anterior em andamento) ou eUSB_RETURN_ERROR (nao enumerada)
 **********************************************************************************************************************/
static usbReturn_t BspUsb_Transmit(uint8_t* buffer, uint16_t bufferSize)
{
	uint8_t ret = CDC_Transmit_FS(buffer, bufferSize);

	if (ret == USBD_OK)
	{
		return eUSB_RETURN_OK;
	}
	if (ret == USBD_BUSY)
	{
		return eUSB_RETURN_BUSY;
	}

	return eUSB_RETURN_ERROR;
}

/***********************************************************************************************************************
 * @brief      Le os bytes recebidos pelo USB-CDC (Usb_Read), bloqueando a task ate chegar algo ou dar timeout.
 * @param[out] buffer      Onde os bytes sao copiados
 * @param[in]  bufferSize  Capacidade do buffer
 * @param[out] readSize    Quantidade de bytes lidos (0 no timeout)
 * @param[in]  timeoutMs   Tempo maximo de espera, em ms
 * @return     eUSB_RETURN_OK ou eUSB_RETURN_EMPTY (timeout sem bytes)
 **********************************************************************************************************************/
static usbReturn_t BspUsb_Received(uint8_t* buffer, uint32_t bufferSize, uint32_t* readSize, uint32_t timeoutMs)
{
	*readSize = xStreamBufferReceive(bspUsb.rxStream, buffer, bufferSize, pdMS_TO_TICKS(timeoutMs));

	if (*readSize == 0)
	{
		return eUSB_RETURN_EMPTY;
	}

	return eUSB_RETURN_OK;
}

/***********************************************************************************************************************
 * @brief      Callback do CDC_Receive_FS: coloca o pacote recebido no stream buffer. Roda na ISR do OTG_FS.
 * @details    So funcoes FromISR (nada de DebugLog, que usa mutex). O que nao cabe no stream buffer e descartado e
 *             contado em rxDroppedBytes, porque a ISR nao pode esperar.
 * @param[in]  buffer      Pacote recebido
 * @param[in]  bufferSize  Tamanho do pacote
 **********************************************************************************************************************/
static void BspUsb_RxFromIsr(uint8_t *buffer, uint32_t bufferSize)
{
	BaseType_t higherPriorityTaskWoken = pdFALSE;

	size_t written = xStreamBufferSendFromISR(bspUsb.rxStream, buffer, bufferSize, &higherPriorityTaskWoken);

	if (written < bufferSize)
	{
		bspUsb.rxDroppedBytes += (bufferSize - written);
	}

	portYIELD_FROM_ISR(higherPriorityTaskWoken);
}

/***********************************************************************************************************************
 * @brief      Callback do SET_CONTROL_LINE_STATE e da enumeracao. Roda na ISR do OTG_FS.
 * @details    So a borda de subida do DTR (porta fechada -> aberta) conta como sessao nova: o PC pode repetir a
 *             requisicao com o DTR ainda ligado (ex.: mudando so o RTS).
 * @param[in]  dtr  1 = porta aberta no PC, 0 = fechada (ou nova enumeracao)
 **********************************************************************************************************************/
static void BspUsb_LineStateFromIsr(uint8_t dtr)
{
	if ((dtr != 0U) && (bspUsb.dtr == 0U))
	{
		bspUsb.newSession = 1U;
	}

	bspUsb.dtr = dtr;
}

/***********************************************************************************************************************
 * @brief      Le e limpa o aviso de sessao nova (Usb_NewSession). Roda na task.
 * @return     1 se um programa abriu a porta desde a ultima consulta, 0 caso contrario
 **********************************************************************************************************************/
static uint8_t BspUsb_NewSession(void)
{
	uint8_t novaSessao;

	// Leitura e limpeza juntas: a secao critica mascara a ISR do OTG_FS (prioridade 5), entao uma abertura
	// de porta que chegue no meio nao e apagada sem ter sido vista
	taskENTER_CRITICAL();
	novaSessao        = bspUsb.newSession;
	bspUsb.newSession = 0U;
	taskEXIT_CRITICAL();

	return novaSessao;
}


/** @} DOXYGEN GROUP TAG END OF FILE */


