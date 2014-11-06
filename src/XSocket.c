// XSocket.cpp: implementation of the XSocket class.
//
//////////////////////////////////////////////////////////////////////

#include <stdio.h>      /* printf, scanf, NULL */
#include <stdlib.h>     /* malloc, free, rand */

#include "XSocket.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

struct XSOCKET *            m_pXSocket;

void XSocket_XSocket(XSOCKET *This)
{



	This->m_cType = NULL;
	This->m_pRcvBuffer = NULL;
	This->m_dwBufferSize = 0;
	This->m_cStatus = DEF_XSOCKSTATUS_READINGHEADER;
	This->m_dwReadSize = 3;
	This->m_dwTotalReadSize = 0;
	
	
}

void DXSocket_XSocket(XSOCKET *This)
{

	
	if (This->m_pRcvBuffer != NULL) {
		
		 free(This->m_pRcvBuffer);
		 This->m_pRcvBuffer = NULL;
	}
	

}

int XSocket_bInitBufferSize(XSOCKET *This, unsigned long  dwBufferSize)
{
	if (This->m_pRcvBuffer != NULL) free(This->m_pRcvBuffer);

	This->m_pRcvBuffer = (char*)malloc(dwBufferSize + 8);
	if (This->m_pRcvBuffer == NULL) return 0;
	
	
	This->m_dwBufferSize = dwBufferSize;

	return 1;
}




int  XSocket_iOnRead(XSOCKET *This,void *pData)
{
 int iRet;
 unsigned short   * wp;

 if (This->m_cStatus == DEF_XSOCKSTATUS_READINGHEADER) {
		

	    memcpy((char *)(This->m_pRcvBuffer + This->m_dwTotalReadSize), pData, This->m_dwReadSize);
		iRet = This->m_dwReadSize;// recv(m_Sock, (char *)(m_pRcvBuffer + m_dwTotalReadSize), m_dwReadSize, 0);
		
		
		This->m_dwReadSize -= iRet;
		This->m_dwTotalReadSize += iRet;
		
		if (This->m_dwReadSize == 0) {
			This->m_cStatus = DEF_XSOCKSTATUS_READINGBODY;
			wp = (unsigned short  *)(This->m_pRcvBuffer + 1);
			This->m_dwReadSize = (int)(*wp - 3);
			
			if (This->m_dwReadSize == 0) {
				This->m_cStatus = DEF_XSOCKSTATUS_READINGHEADER;
				This->m_dwReadSize = 3;
				This->m_dwTotalReadSize = 0;
				XSocket_OnArenaRead(This);
				return DEF_XSOCKEVENT_READCOMPLETE;
			}
			else 
			if (This->m_dwReadSize > This->m_dwBufferSize) {
				This->m_cStatus = DEF_XSOCKSTATUS_READINGHEADER;
				This->m_dwReadSize = 3;
				This->m_dwTotalReadSize = 0;
				return DEF_XSOCKEVENT_MSGSIZETOOLARGE;
			}
		}
		return DEF_XSOCKEVENT_ONREAD;
	}
	else
	if (This->m_cStatus == DEF_XSOCKSTATUS_READINGBODY) {
		
		memcpy((char *)(This->m_pRcvBuffer + This->m_dwTotalReadSize), pData, This->m_dwReadSize);
		iRet = This->m_dwReadSize;// recv(m_Sock, (char *)(m_pRcvBuffer + m_dwTotalReadSize), m_dwReadSize, 0);
		
		

		This->m_dwReadSize -= iRet;
		This->m_dwTotalReadSize += iRet;
		
		if (This->m_dwReadSize == 0) {
			This->m_cStatus = DEF_XSOCKSTATUS_READINGHEADER;
			This->m_dwReadSize = 3;
			This->m_dwTotalReadSize = 0;
		}
		else return DEF_XSOCKEVENT_ONREAD;
	}

	XSocket_OnArenaRead(This);
	return DEF_XSOCKEVENT_READCOMPLETE;
}




char * XSocket_pGetRcvDataPointer(XSOCKET *This,unsigned long * pMsgSize, char * pKey)
{
 unsigned short  * wp;
 unsigned long   dwSize;
 //register unsigned long i;
 char cKey;
	
    cKey = This->m_pRcvBuffer[0];
	if (pKey != NULL) *pKey = cKey;		// v1.4

	wp = (unsigned short  *)(This->m_pRcvBuffer + 1);
	*pMsgSize = (*wp) - 3;				
	dwSize    = (*wp) - 3;


	/*if (cKey != NULL) {
		for (i = 0; i < dwSize; i++) {
			This->m_pRcvBuffer[3 + i] = This->m_pRcvBuffer[3 + i] ^ (cKey ^ (dwSize - i));
			This->m_pRcvBuffer[3 + i] -= (i ^ cKey);
		}
	}*/
	return (This->m_pRcvBuffer + 3);
}






void XSocket_OnArenaRead(XSOCKET *This)
{

	unsigned  long dwMsgSize;
	char * pData;


	char cKey;
	pData = XSocket_pGetRcvDataPointer(This,&dwMsgSize, &cKey);


	if (bPutMsgQuene(1, pData, dwMsgSize, NULL, cKey) == 0) {
		
		//ERROR CRITICAL 
     }


}
