// Msg.cpp: implementation of the CMsg class.
//
//////////////////////////////////////////////////////////////////////


#include "MsgQue.h"
#include <stdio.h>      /* printf, scanf, NULL */
#include <stdlib.h>     /* malloc, free, rand */
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

void CMsg_CMsg(CMsg*This)
{
	This->m_pData = NULL;
	This->m_dwSize = NULL;

}
void CMsg_XCMsg(CMsg*This)
{

	if (This->m_pData != NULL) free(This->m_pData);

	This->m_pData = NULL;

}
void CMsg_Get(CMsg*This, char * pFrom, char * pData, unsigned  int * pSize, int * pIndex, char * pKey)
{

	*pFrom = This->m_cFrom;
	memcpy(pData, This->m_pData, This->m_dwSize);
	*pSize = This->m_dwSize;
	*pIndex = This->m_iIndex;
	*pKey = This->m_cKey;

}
void CMsg_bPut(CMsg*This, char cFrom, char * pData, unsigned  int dwSize, int iIndex, char cKey)
{
	This->m_pData = (char*)malloc(dwSize + 1);
	if (This->m_pData == NULL) return ;



	
	memcpy(This->m_pData, pData, dwSize);

	This->m_dwSize = dwSize;
	This->m_cFrom = cFrom;
	This->m_iIndex = iIndex;
	This->m_cKey = cKey;

}






 char		                m_pMsgBuffer[DEF_MSGBUFFERSIZE + 1];
 struct CMsg *              m_pMsgQuene[DEF_MSGQUENESIZE];
 int			            m_iQueneHead = 0, m_iQueneTail = 0;


 void CreateMsgQuene()
 {

	 int i;
	 for (i = 0; i < DEF_MSGQUENESIZE; i++)  {
		 m_pMsgQuene[i] = NULL;
	 }


	 m_iQueneHead = 0;
	 m_iQueneTail = 0;
 }

 void  DestroyMsgQuene()
{

	 int i;
	 for ( i = 0; i < DEF_MSGQUENESIZE; i++) {
		 if (m_pMsgQuene[i]) {
			 free(m_pMsgQuene[i]);
			 m_pMsgQuene[i] = NULL;
		 }
	 }
}



 int bPutMsgQuene(char cFrom, char * pData, unsigned  int dwMsgSize, int iIndex, char cKey)
 {
	 if (m_pMsgQuene[m_iQueneTail] != NULL) return 0;

	 m_pMsgQuene[m_iQueneTail] = (CMsg*) malloc(sizeof(CMsg));
	 if (m_pMsgQuene[m_iQueneTail] == NULL) return 0;

	// if (CMsg_bPut#include "stdafx.h"(m_pMsgQuene[m_iQueneTail], cFrom, pData, dwMsgSize, iIndex, cKey) == 0) return 0;

	 m_iQueneTail++;
	 if (m_iQueneTail >= DEF_MSGQUENESIZE) m_iQueneTail = 0;

	 return 1;
 }





 int bGetMsgQuene(char * pFrom, char * pData, unsigned  int * pMsgSize, int * pIndex, char * pKey)
 {
	 if (m_pMsgQuene[m_iQueneHead] == NULL) return 0;


	 CMsg_Get(m_pMsgQuene[m_iQueneHead],pFrom, pData, pMsgSize, pIndex, pKey);
	
	 free(m_pMsgQuene[m_iQueneHead]);
	 m_pMsgQuene[m_iQueneHead] = NULL;

	 m_iQueneHead++;
	 if (m_iQueneHead >= DEF_MSGQUENESIZE) m_iQueneHead = 0;

	 return 1;
 }



 void MsgProcess()
 {
	 char * pData, cFrom, cKey;
	 unsigned int  * dwpMsgID, dwMsgSize;

	 int    iClientH;
	

	 int i;
	 
	 for ( i = 0; i < DEF_MSGBUFFERSIZE; i++)
		 m_pMsgBuffer[i] = 0;
	

	 pData = (char *)m_pMsgBuffer;

	 while (bGetMsgQuene(&cFrom, pData, &dwMsgSize, &iClientH, &cKey) == 1) {

		 dwpMsgID = (unsigned int *)(pData);


		 for ( i = 0; i < DEF_MSGBUFFERSIZE; i++)
			 m_pMsgBuffer[i] = 0;

	 }


 }
