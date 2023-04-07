#ifndef MYCLOSESOCKET_H
#define MYCLOSESOCKET_H

inline int myclosesocket(SOCKET& hSocket)
{
	if (hSocket == INVALID_SOCKET)
	{
		return WSAENOTSOCK;
	}

#ifdef WIN32
	int ret = closesocket(hSocket);
#else
	int ret = close(hSocket);
#endif

	hSocket = INVALID_SOCKET;

	return ret;
}

#define closesocket(s)	myclosesocket(s)

#endif // MYCLOSESOCKET_H
