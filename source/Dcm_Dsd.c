/******************************************************************************
 * Module      : DCM                                                          *
 * SubModule   : DSD                                                          *
 * File Name   : Dsd.c                                                        *
 * Description : Source file of Dsd Sub-Module                               *
 * Created on  : April 1, 2020                                                *
 ******************************************************************************/

#include <include/Dcm_Dsl.h>
#include "include/Dcm_Dsd.h"
#include "include/Dcm_Dsp.h"


/**********************************
 * DSD-STATIC-FUNTIONS-PROTOTYPES *
 **********************************/
static inline void UpdateCurrentSid(void);
static boolean DsdLookUpSid(uint8 sid, const Dcm_DsdServiceType **servicePtr);
static void DsdRequestHandler(void);



/******************************************
 *           GLOBAL-VARIABLES             *
 ******************************************/
DsdRunTimeDataType *Glb_DsdRunTime;
Dcm_MsgContextType *DiagMsgContext;
boolean suppressPosRspMsgIndicationBitStatus = FALSE;
static  boolean DslToDsdIndicationCorrectlyDoneFlag = FALSE;
static  uint8 currentSid;


/*****************************************************************************
 *                                                                           *
 *                                FUNCTIONS                                  *
 *                                                                           *
 *****************************************************************************/

/**********************************
 * SET-DIAGNOSTIC-MESSAGE-CONTEXT *
 **********************************/
void DsdSetDiagMsgContext( Dcm_MsgContextType **DiagMsg, DsdRunTimeDataType **DsdRunTime
                          ,const Dcm_DslProtocolRowType *Active_Protocol)
{

    *DsdRunTime = Active_Protocol->DsdrunTime;
    (*DsdRunTime)->PduRxID       = Active_Protocol->DslrunTime->DCM_RxBufferInfo->diagReqestRxPduId;
    (*DsdRunTime)->PduTxID       = Active_Protocol->DslrunTime->DCM_TxBufferInfo->diagResponseTxPduId;
    (*DsdRunTime)->diaReqFromDsl = Active_Protocol->DslrunTime->DCM_RxBufferInfo->dcmRxBuffer.SduDataPtr;
    (*DsdRunTime)->serviceTable  = Active_Protocol->DslProtocolSIDTable;
    Active_Protocol->DslrunTime->DCM_TxBufferInfo->dcmTxBuffer.SduDataPtr = (*DsdRunTime)->diaResToDsl;

    *DiagMsg = Active_Protocol->DiagMsgContext;
    (*DiagMsg)->dcmRxPduId = Active_Protocol->DslrunTime->DCM_RxBufferInfo->diagReqestRxPduId;
    (*DiagMsg)->reqDataLen = Active_Protocol->DslrunTime->DCM_RxBufferInfo->dcmRxBuffer.SduLength - 1;

}



/*****************************************
 * CREATE--AND-FORWARD-NEGATIVE-RESPONSE *
 *****************************************/
static void DsdCreateNRC(Dcm_NegativeResponseCodeType NRC)
{
    if (DCM_FUNCTIONAL_TYPE != Glb_DsdRunTime->addType)
    {
        Glb_DsdRunTime->diaResToDsl[0] = DCM_NEGATIVE_RESPONSE_SID;
        Glb_DsdRunTime->diaResToDsl[1] = Glb_DsdRunTime->diaReqFromDsl[0];
        Glb_DsdRunTime->diaResToDsl[2] = NRC;
        DsdToDslForwardingResponse(DCM_DSD_TX_RESPONSE_READY,DCM_NRC_RESPONSE_LENGTH);
    }
    else
    {
        DsdToDslForwardingResponse(DCM_DSD_TX_RESPONSE_SUPPRESSED,0);
    }
}




/****************************************************
 * CHECK-DSL-INDICATION-AND-HANDLE-DSD-VERIFICATION *
 ****************************************************/
void DsdMain(void)
{
    /*
     * Does DSL trigger any Request handling to DSD ?
     * if yes -> handle the request by calling DsdRequestHandler service
     */
    if (TRUE == DslToDsdIndicationCorrectlyDoneFlag)
    {
        DslToDsdIndicationCorrectlyDoneFlag = FALSE;
        DsdRequestHandler();
    }
    else
    {
        /* MISRA RULE */
    }
}


/*****************************************
 * UPDATE-NEW-REQUEST-SERVICE-IDENTIFIER *
 *****************************************/
static inline void UpdateCurrentSid(void)
{
    currentSid = Glb_DsdRunTime->diaReqFromDsl[DCM_SID];
}


/********************************
 * LOOK-UP-FOR-SID-IF-SUPPORTED *
 ********************************/
static boolean DsdLookUpSid(uint8 sid, const Dcm_DsdServiceType **servicePtr)
{
    uint8 index;
    /* the operation return state */
    boolean found = FALSE;

    /* Searching for the required service by SID */
    for (index = 0; (index < DCM_DSD_UDS_NUM_OF_SUPPORTED_SERVICES) && (found == FALSE) ;index++)
    {
        if ((Glb_DsdRunTime->serviceTable->DsdService[index])->DsdSidTabServiceId == sid)
        {
            if (TRUE == (Glb_DsdRunTime->serviceTable->DsdService[index])->DsdServiceUsed)
            {
                found = TRUE;
                *servicePtr = *(Glb_DsdRunTime->serviceTable->DsdService + index);
            }
            else
            {
                /* MISRA RULE */
            }
        }
        else
        {
            /* MISRA RULE */
        }

    }

    /* return the status */
    return found;
}



/************************************************
 * HANDLE-RECEIVED-REQUEST-AND-DO-VERIFICATIONS *
 ************************************************/
static void DsdRequestHandler(void)
{
    const Dcm_DsdServiceType *currentServicePtr;

    /* Update current SID */
    UpdateCurrentSid();

    /* [SWS_Dcm_00084]
     * Is DCM_RESPOND_ALL_REQUEST enabled and SID in the range from
     * 0x40 to 0x7F ?
     * if yes -> SID passed for the this check
     */
    if ((HIGH == DCM_RESPOND_ALL_REQUEST) && ((currentSid & 0x7F) < 0x40))
    {
        /* [SWS_Dcm_00192], [SWS_Dcm_00193]
         * Is the service supported ?
         * if yes -> please return the requested service parameters
         * in the [passed service container PTR]
         */
        if (DsdLookUpSid(currentSid,&currentServicePtr))
        {
            /*
             * Is the requested service allowed to be processed in the current
             * session?
             * if yes -> Request passed for this check
             */
            if (TRUE == DspSessionLevelPermission(currentServicePtr->numOfSessLevel, currentServicePtr->DcmDsdSidTabSessionLevelRef))
            {
                if (TRUE == DspSecurityLevelPermission(currentServicePtr->numOfSecLevel, currentServicePtr->DcmDsdSidTabSecurityLevelRef))
                {
                    /* [SWS_Dcm_00204]
                     * Is the SubfuncAvail for newly received SID?
                     * if yes -> checking the suppression of positive response
                     */
                    if (currentServicePtr->DsdSidTabSubfuncAvail)
                    {
                        if (Glb_DsdRunTime->diaReqFromDsl[DCM_SUB_FUN_INDEX] & SUPPRESS_POS_RSP_MSG_INDICATION_BIT)
                        {
                            /*Set the suppressPosRspMsgIndicationBitStatus to be
                             * used for response handling
                             */
                            suppressPosRspMsgIndicationBitStatus = TRUE;
                            DiagMsgContext->msgAddInfo.suppressPosResponse = TRUE;

                            /* [SWS_Dcm_00201]
                             * Removing the suppress bit
                             */
                            Glb_DsdRunTime->diaReqFromDsl[DCM_SUB_FUN_INDEX] &= ~SUPPRESS_POS_RSP_MSG_INDICATION_BIT;

                        }
                        else
                        {
                            /* Clear the suppressPosRspMsgIndicationBitStatus to be
                             * used for response handling
                             */
                            suppressPosRspMsgIndicationBitStatus = FALSE;
                            DiagMsgContext->msgAddInfo.suppressPosResponse = FALSE;
                        }
                        /* [SWS_Dcm_00201]
                         * Removing the suppress bit
                         */
                        Glb_DsdRunTime->diaReqFromDsl[DCM_SUB_FUN_INDEX] &= ~SUPPRESS_POS_RSP_MSG_INDICATION_BIT;
                    }
                    else
                    {
                        /* Clear the suppressPosRspMsgIndicationBitStatus to be
                         * used for response handling
                         */
                        suppressPosRspMsgIndicationBitStatus = FALSE;
                    }
                    /* [SWS_Dcm_00221]
                     * Searching for the executable functionality of
                     * the DSP submodule for newly received diagnostic
                     * service identifier using SelectServiceHandler Service
                     */

                    x = DiagMsgContext->reqData[0];

                    y = DiagMsgContext->reqData[1];
                    DsdDspIndication(currentServicePtr,DiagMsgContext,&Glb_DsdRunTime->Error_Code);
                }
                else
                {
                    /* Not allowed in the current security level */
                    DsdCreateNRC(DCM_E_SECURITYACCESSDENIED);
                }
            }
            else
            {
                         /* Not allowed in the active session */
                DsdCreateNRC(DCM_E_SERVICENOTSUPPORTEDINACTIVESESSION);
            }

        }
        else
        {
              /* Requested service not supported */
            DsdCreateNRC(DCM_E_SERVICENOTSUPPORTED);
        }
    }
    else
    {
        DsdCreateNRC(DCM_E_REQUESTOUTOFRANGE);
    }


}


/**************************************
 * INDICATE-DSD-WITH-RECEIVED-REQUEST *
 **************************************/
void DslToDsdDataIndication (const Dcm_DslProtocolRowType *Active_protocol)
{

    DsdSetDiagMsgContext(&DiagMsgContext,&Glb_DsdRunTime,Active_protocol);
      /* Set Indication Flag to Trigger Dsd Sub-Module Handler */
    DslToDsdIndicationCorrectlyDoneFlag = TRUE;
 }


/************************************
 * FORWARD-RESULT-OF-SERVICE-TO-DSD *
 ************************************/
void DspToDsdForwardingResponse(Std_ReturnType ResponseType)
{
    if(E_OK == ResponseType)
    {       /* [SWS_Dcm_00200] if suppress is True then no Responce from Dsd
             * [SWS_Dcm_00222] when the Dsp has finished the Dsd will
             * assemble the response
             */
            if (suppressPosRspMsgIndicationBitStatus != TRUE)
            {
               /* [SWS_Dcm_00223] Dsd shall add sid and data stream in msgdata
                * [SWS_Dcm_00224] Dsd shall transfer msgdata into a responce
                *  buffer and shall add sid at the first byte of that buffer
                */
                Glb_DsdRunTime->diaResToDsl[0] = (currentSid | DCM_POS_RESPONSE_SID);
                Glb_DsdRunTime->resDataLen = DiagMsgContext->resDataLen + 1;
                DsdToDslForwardingResponse(DCM_DSD_TX_RESPONSE_READY,Glb_DsdRunTime->resDataLen);
            }
            else
            {
               DsdToDslForwardingResponse(DCM_DSD_TX_RESPONSE_SUPPRESSED,0);
            }
    }
    else if (E_NOT_OK == ResponseType)
    {
        DsdCreateNRC(Glb_DsdRunTime->Error_Code);
    }
    else
    {
        /* MISRA RULE*/
    }

}
