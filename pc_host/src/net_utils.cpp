#include "net_utils.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")

int net_utils_get_local_ip(char *ip_buf, int ip_buf_len)
{
    if (!ip_buf || ip_buf_len <= 0)
        return -1;

    /* Determine outgoing interface by creating a UDP socket and
       'connecting' it to a public IP (no packets are sent). */
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET)
        return -1;

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(53);                /* DNS port */
    serv_addr.sin_addr.s_addr = inet_addr("8.8.8.8");

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0) {
        struct sockaddr_in name;
        int namelen = (int)sizeof(name);
        if (getsockname(sock, (struct sockaddr *)&name, &namelen) == 0) {
            const char *ip = inet_ntoa(name.sin_addr);
            if (ip && ip[0] != '\0') {
                strncpy(ip_buf, ip, ip_buf_len - 1);
                ip_buf[ip_buf_len - 1] = '\0';
                closesocket(sock);
                return 0;
            }
        }
    }

    closesocket(sock);
    return -1;
}

#else  /* Linux/macOS */

#  include <unistd.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <net/if.h>
#  include <ifaddrs.h>

int net_utils_get_local_ip(char *ip_buf, int ip_buf_len)
{
    /* Unix: iterate interfaces and find first non-loopback IPv4 */
    struct ifaddrs *ifaddr = NULL;
    struct ifaddrs *ifa = NULL;
    int result = -1;

    if (getifaddrs(&ifaddr) == -1)
        return -1;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        /* Only IPv4 */
        if (ifa->ifa_addr->sa_family != AF_INET)
            continue;

        /* Skip loopback */
        if (ifa->ifa_flags & IFF_LOOPBACK)
            continue;

        /* Found a suitable interface */
        struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
        const char *ip = inet_ntoa(addr->sin_addr);
        if (ip && ip[0] != '\0') {
            strncpy(ip_buf, ip, ip_buf_len - 1);
            ip_buf[ip_buf_len - 1] = '\0';
            result = 0;
            break;
        }
    }

    if (ifaddr)
        freeifaddrs(ifaddr);

    return result;
}

#endif
