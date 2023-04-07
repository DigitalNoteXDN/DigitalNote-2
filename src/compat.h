#ifndef COMPAT_H
#define COMPAT_H

#ifdef WIN32
	#ifdef _WIN32_WINNT
		#undef _WIN32_WINNT
	#endif // _WIN32_WINNT
	
	#define _WIN32_WINNT 0x0501

	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN 1
	#endif // WIN32_LEAN_AND_MEAN
	
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif // NOMINMAX

	#ifdef FD_SETSIZE
		#undef FD_SETSIZE // prevent redefinition compiler warning
	#endif // FD_SETSIZE

	#define FD_SETSIZE 1024 // max number of fds in fd_set

	#include <winsock2.h>
	#include <windows.h>
	#include <ws2tcpip.h>
	
	#define MSG_DONTWAIT		0
#else // WIN32
	#include <sys/fcntl.h>
	#include <sys/mman.h>
	#include <sys/socket.h>
	#include <sys/types.h>
	#include <net/if.h>
	#include <netinet/in.h>
	#include <netinet/tcp.h>
	#include <arpa/inet.h>
	#include <ifaddrs.h>
	#include <limits.h>
	#include <netdb.h>
	#include <unistd.h>
	#include <errno.h>
	
	typedef u_int SOCKET;
	
	#define WSAGetLastError()	errno
	#define WSAEINVAL			EINVAL
	#define WSAEALREADY			EALREADY
	#define WSAEWOULDBLOCK		EWOULDBLOCK
	#define WSAEMSGSIZE			EMSGSIZE
	#define WSAEINTR			EINTR
	#define WSAEINPROGRESS		EINPROGRESS
	#define WSAEADDRINUSE		EADDRINUSE
	#define WSAENOTSOCK			EBADF
	#define INVALID_SOCKET		(SOCKET)(~0)
	#define SOCKET_ERROR		-1
#endif // WIN32

#endif // COMPAT_H
