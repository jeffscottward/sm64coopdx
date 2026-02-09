#ifndef SOCKET_LINUX_H
#define SOCKET_LINUX_H

#ifdef TARGET_WEB
/*
 * Web/Emscripten builds cannot use BSD sockets for UDP networking.
 * Browsers do not support raw socket(), bind(), sendto(), recvfrom().
 * Emscripten provides partial POSIX socket stubs but they do not work
 * for UDP datagrams. We provide minimal type stubs so that socket.h
 * consumers (network.c, djui panels, chat_commands.c, etc.) still
 * compile without pulling in unavailable POSIX headers.
 *
 * The actual socket NetworkSystem (gNetworkSystemSocket) is replaced
 * with a no-op stub in socket.c under TARGET_WEB.
 */

#include <stdint.h>
#include <string.h>

#define SOCKET int
#define INVALID_SOCKET (-1)
#define SOCKET_LAST_ERROR 0
#define NO_ERROR (0)
#define SOCKADDR void
#define SOCKET_ERROR (-1)
#define SOCKET_EWOULDBLOCK 0
#define SOCKET_ECONNRESET  0
#define RX_ADDR_SIZE_TYPE unsigned int

/* Minimal sockaddr_in6 stub so struct declarations compile. */
#ifndef _WEB_SOCKADDR_IN6_STUB
#define _WEB_SOCKADDR_IN6_STUB
struct in6_addr {
    uint8_t s6_addr[16];
};
struct sockaddr_in6 {
    uint16_t       sin6_family;
    uint16_t       sin6_port;
    struct in6_addr sin6_addr;
};
#endif /* _WEB_SOCKADDR_IN6_STUB */

#define INET6_ADDRSTRLEN 46
#define AF_INET6 0

#else /* !TARGET_WEB — native POSIX builds */

#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>

#define SOCKET unsigned int
#define INVALID_SOCKET (unsigned int)(-1)
#define SOCKET_LAST_ERROR errno
#define NO_ERROR (0)
#define SOCKADDR struct sockaddr
#define SOCKET_ERROR (-1)
#define closesocket(fd) close(fd)
#define SOCKET_EWOULDBLOCK EWOULDBLOCK
#define SOCKET_ECONNRESET ECONNRESET
#define RX_ADDR_SIZE_TYPE unsigned int

#endif /* TARGET_WEB */

#endif /* SOCKET_LINUX_H */
