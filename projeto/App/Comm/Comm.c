/***********************************************************************************************************************
 *   @file         Comm.c
 *   @addtogroup   App/Comm
 *   @brief        Arquivo que implementa o protocolo de comunicacao
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
 *   @version      <b>1.0.0 - 01/10/2026</b> \n Luiz Neto \n Primeira versao
 *
 *   @copyright
 *   @{
 **********************************************************************************************************************/


/***********************************************************************************************************************
 * INCLUDES
 **********************************************************************************************************************/

#include "Comm.h"
#include "usb_device.h"
#include "usbd_cdc.h"
#include "DebugLog.h"

/***********************************************************************************************************************
 * DEFINES LOCAIS
 **********************************************************************************************************************/


/***********************************************************************************************************************
 * TIPOS LOCAIS
 **********************************************************************************************************************/


typedef enum commState
{
	eCOMM_STATE_AGUARDA_SOF,
	eCOMM_STATE_LE_CABECALHO,
	eCOMM_STATE_LE_PAYLOAD,
	eCOMM_STATE_VALIDA_CRC,

	eCOMM_STATE_END_ENUM
} commState_t;


static struct comm
{

	commState_t commState;

}comm;



/***********************************************************************************************************************
 * VARIAVEIS LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * PROTOTIPOS LOCAIS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * FUNCOES PUBLICAS
 **********************************************************************************************************************/

commReturn_t Comm_Init(void)
{

	comm.commState = eCOMM_STATE_AGUARDA_SOF;

	return eCOMM_RETURN_OK;
}

commReturn_t Comm_Handle(void)
{

	switch (comm.commState)
	{

		case eCOMM_STATE_AGUARDA_SOF:

			//DebugLog_SendTerminal("");
			comm.commState = eCOMM_STATE_LE_CABECALHO;

		break;

		case eCOMM_STATE_LE_CABECALHO:

			comm.commState = eCOMM_STATE_LE_PAYLOAD;

		break;

		case eCOMM_STATE_LE_PAYLOAD:

			comm.commState = eCOMM_STATE_VALIDA_CRC;

		break;

		case eCOMM_STATE_VALIDA_CRC:

			comm.commState = eCOMM_STATE_AGUARDA_SOF;

		break;

		default:

			comm.commState = eCOMM_STATE_END_ENUM;

		break;

	}

	return eCOMM_RETURN_OK;
}

/***********************************************************************************************************************
 * FUNCOES LOCAIS
 **********************************************************************************************************************/

/** @} DOXYGEN GROUP TAG END OF FILE */
