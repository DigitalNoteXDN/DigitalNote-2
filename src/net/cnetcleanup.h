#ifndef CNETCLEANUP_H
#define CNETCLEANUP_H

#include "net.h"
#include "net/myclosesocket.h"
#include "util.h"

class CNetCleanup
{
public:
	CNetCleanup()
	{
		
	}
	
	~CNetCleanup()
	{
		// Close sockets
		for(CNode* pnode : vNodes)
		{
			if (pnode->hSocket != INVALID_SOCKET)
			{
				closesocket(pnode->hSocket);
			}
		}
		
		for(SOCKET hListenSocket : vhListenSocket)
		{
			if (hListenSocket != INVALID_SOCKET)
			{
				if (closesocket(hListenSocket) == SOCKET_ERROR)
				{
					LogPrintf("closesocket(hListenSocket) failed with error %d\n", WSAGetLastError());
				}
			}
		}
		
		// clean up some globals (to help leak detection)
		for(CNode* pnode : vNodes)
		{
			delete pnode;
		}
		
		for(CNode* pnode : vNodesDisconnected)
		{
			delete pnode;
		}
		
		vNodes.clear();
		vNodesDisconnected.clear();
		
		delete semOutbound;
		delete pnodeLocalHost;
		
		semOutbound = NULL;
		pnodeLocalHost = NULL;

#ifdef WIN32
		// Shutdown Windows Sockets
		WSACleanup();
#endif
	}
};

#endif // CNETCLEANUP_H
