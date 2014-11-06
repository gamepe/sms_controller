

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000



#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <malloc.h>		

#define DEF_XSOCKSTATUS_READINGHEADER	11
#define DEF_XSOCKSTATUS_READINGBODY		12

#define DEF_XSOCKEVENT_ONREAD					-124	
#define DEF_XSOCKEVENT_READCOMPLETE				-125	
#define DEF_XSOCKEVENT_MSGSIZETOOLARGE			-132	



struct XSOCKET
{
	char   m_cType;
	char * m_pRcvBuffer;
	unsigned long   m_dwBufferSize;
	char            m_cStatus;
	unsigned long   m_dwReadSize;
	unsigned long   m_dwTotalReadSize;


}; typedef struct XSOCKET XSOCKET;

char *  XSocket_pGetRcvDataPointer(XSOCKET *This, unsigned long  * pMsgSize, char * pKey );
int     XSocket_bInitBufferSize(XSOCKET *This, unsigned long  dwBufferSize);
void    XSocket_XSocket(XSOCKET *This);
void    DXSocket_XSocket(XSOCKET *This);
int     XSocket_iOnRead(XSOCKET *This, void *pData);
void    XSocket_OnArenaRead(XSOCKET *This);


int bPutMsgQuene(char cFrom, char * pData, unsigned  int dwMsgSize, int iIndex, char cKey);
int bGetMsgQuene(char * pFrom, char * pData, unsigned  int * pMsgSize, int * pIndex, char * pKey);


extern struct XSOCKET *            m_pXSocket;




