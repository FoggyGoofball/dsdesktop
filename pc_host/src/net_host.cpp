/*============================================================================
 * pc_host/src/net_host.cpp
 *
 * UDP networking — send video/audio downstream to the Wii,
 * receive upstream telemetry and congestion reports.
 * Cross-platform: Winsock2 on Windows, POSIX sockets on Linux.
 *==========================================================================*/
#include <cstdio>
#include <cstring>
#include <cstdint>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
   typedef int socklen_t;
#  define SOCK_NONBLOCK 0
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
#  define SOCKET int
#  define INVALID_SOCKET (-1)
#  define closesocket close
#endif

#include "../../common/protocol.h"
#include "../include/net_host.h"

static SOCKET s_sock = INVALID_SOCKET;
static struct sockaddr_in s_wii_addr;

int net_host_init(const char *wii_ip, uint16_t port)
{
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock == INVALID_SOCKET) {
        perror("socket");
        return -1;
    }

    /* Bind to DSRD_PORT for receiving upstream */
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_port        = htons(port);
    bind_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(s_sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        perror("bind");
        return -1;
    }

    /* Set non-blocking */
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(s_sock, FIONBIO, &mode);
#else
    int flags = fcntl(s_sock, F_GETFL, 0);
    fcntl(s_sock, F_SETFL, flags | O_NONBLOCK);
#endif

    /* Target Wii address */
    memset(&s_wii_addr, 0, sizeof(s_wii_addr));
    s_wii_addr.sin_family = AF_INET;
    s_wii_addr.sin_port   = htons(port);
    inet_pton(AF_INET, wii_ip, &s_wii_addr.sin_addr);

    return 0;
}

void net_host_shutdown(void)
{
    if (s_sock != INVALID_SOCKET) {
        closesocket(s_sock);
        s_sock = INVALID_SOCKET;
    }
}

int net_host_send(const uint8_t *data, int len)
{
    return sendto(s_sock, (const char *)data, len, 0,
                  (struct sockaddr *)&s_wii_addr, sizeof(s_wii_addr));
}

int net_host_recv(uint8_t *buf, int max_len)
{
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);

    int n = recvfrom(s_sock, (char *)buf, max_len, 0,
                     (struct sockaddr *)&from, &fromlen);
    if (n < 0) {
#ifdef _WIN32
        if (WSAGetLastError() == WSAEWOULDBLOCK) return 0;
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
#endif
        return -1;
    }
    return n;
}
