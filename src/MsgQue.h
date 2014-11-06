// Msg.h: interface for the CMsg class.
//
//////////////////////////////////////////////////////////////////////


#pragma once

#define DEF_MSGFROM_CLIENT			1


#define DEF_MSGQUENESIZE			100000


#define DEF_MSGBUFFERSIZE			4096

struct CMsg
{
	/// members

	char   m_cFrom;
	char * m_pData;
	unsigned int   m_dwSize;
	int    m_iIndex;
	char   m_cKey;   // v1.4
	char                   pad[4];
};typedef struct CMsg CMsg;


void CMsg_CMsg(CMsg*This);
void CMsg_XCMsg(CMsg*This);
void CMsg_Get(CMsg*This, char * pFrom, char * pData, unsigned  int * pSize, int * pIndex, char * pKey);
void  CMsg_bPut(CMsg*This, char cFrom, char * pData, unsigned  int dwSize, int iIndex, char cKey);

extern char		                m_pMsgBuffer[DEF_MSGBUFFERSIZE + 1];

extern struct CMsg *            m_pMsgQuene[DEF_MSGQUENESIZE];
extern int			            m_iQueneHead, m_iQueneTail;

int bPutMsgQuene(char cFrom, char * pData, unsigned  int dwMsgSize, int iIndex, char cKey);
int bGetMsgQuene(char * pFrom, char * pData, unsigned  int * pMsgSize, int * pIndex, char * pKey);

void MsgProcess();
