/*============================================================================
 * wii_proxy/source/backhaul.c
 *
 * PC ↔ Wii network backhaul.
 *
 * Mode A (Wi-Fi Only):
 *   Uses libogc net_init() over the built-in Broadcom BCM4318.
 *   The radio is time-division multiplexed with NiFi — we receive a full
 *   frame buffer from the PC, then context-switch to blast NiFi.
 *   Channel alignment is enforced to avoid synthesizer retuning.
 *
 * Mode B (USB Ethernet):
 *   Uses the ASIX AX88772 USB-to-Ethernet driver in libogc.
 *   This frees the BCM4318 for dedicated NiFi use — zero radio
 *   context-switching latency.
 *
 * Both modes use a non-blocking UDP socket on DSRD_PORT.
 *==========================================================================*/
#include <gccore.h>
#include <network.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

#include "backhaul.h"
#include "../../common/protocol.h"

static int  s_sock = -1;
static char s_ip_str[16] = {0};
static struct sockaddr_in s_pc_addr;

/* Upstream send buffer for asymmetric bursting (Mode A) — reserved */
static uint8_t s_burst_buf[DSRD_MTU * 4] __attribute__((aligned(32), used));
static int     s_burst_len __attribute__((used)) = 0;

/*--------------------------------------------------------------------------
 * Initialise the backhaul network interface
 *------------------------------------------------------------------------*/
int backhaul_init(const wii_config_t *cfg)
{
    s32 ret;
    struct in_addr ip_addr;

    if (cfg->use_usb_ethernet) {
        /* Mode B: USB Ethernet — libogc auto-detects AX88772 */
        printf("  Waiting for USB Ethernet...\n");
        ret = net_init();
    } else {
        /* Mode A: Wi-Fi */
        printf("  Connecting via Wi-Fi...\n");
        ret = net_init();
    }

    if (ret < 0) {
        printf("  net_init() failed: %d\n", (int)ret);
        return -1;
    }

    ip_addr.s_addr = net_gethostip();
    if (ip_addr.s_addr == 0) {
        printf("  Failed to obtain IP address.\n");
        return -1;
    }
    strncpy(s_ip_str, inet_ntoa(ip_addr), sizeof(s_ip_str) - 1);

    /* Create non-blocking UDP socket */
    s_sock = net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        printf("  socket() failed: %d\n", s_sock);
        return -1;
    }

    /* Bind to DSRD_PORT */
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_port        = htons(DSRD_PORT);
    bind_addr.sin_addr.s_addr = INADDR_ANY;

    ret = net_bind(s_sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
    if (ret < 0) {
        printf("  bind() failed: %d\n", (int)ret);
        return -1;
    }

    /* Set non-blocking */
    int flags = net_fcntl(s_sock, F_GETFL, 0);
    net_fcntl(s_sock, F_SETFL, flags | O_NONBLOCK);

    /* Store PC upstream address */
    memset(&s_pc_addr, 0, sizeof(s_pc_addr));
    s_pc_addr.sin_family      = AF_INET;
    s_pc_addr.sin_port        = htons(cfg->pc_port);
    s_pc_addr.sin_addr.s_addr = inet_addr(cfg->pc_ip);

    return 0;
}

void backhaul_shutdown(void)
{
    if (s_sock >= 0) {
        net_close(s_sock);
        s_sock = -1;
    }
}

/*--------------------------------------------------------------------------
 * Non-blocking receive from PC
 *------------------------------------------------------------------------*/
int backhaul_recv(uint8_t *buf, int max_len)
{
    if (s_sock < 0) return -1;

    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);

    int n = net_recvfrom(s_sock, buf, max_len, 0,
                         (struct sockaddr *)&from, &fromlen);
    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            return 0;
        return -1;
    }

    /* Remember the PC's address for upstream replies */
    if (n > 0) {
        memcpy(&s_pc_addr, &from, sizeof(from));
    }

    return n;
}

/*--------------------------------------------------------------------------
 * Send upstream telemetry to PC
 *------------------------------------------------------------------------*/
int backhaul_send(const uint8_t *buf, uint16_t len)
{
    if (s_sock < 0) return -1;

    int n = net_sendto(s_sock, buf, len, 0,
                       (struct sockaddr *)&s_pc_addr, sizeof(s_pc_addr));
    return n;
}

const char *backhaul_ip_str(void)
{
    return s_ip_str;
}

/*--------------------------------------------------------------------------
 * Runtime IP change: update PC address and retry connection
 *------------------------------------------------------------------------*/
int backhaul_reconnect(const wii_config_t *cfg)
{
    if (!cfg || !cfg->pc_ip || cfg->pc_ip[0] == '\0')
        return -1;

    /* Update stored address */
    memset(&s_pc_addr, 0, sizeof(s_pc_addr));
    s_pc_addr.sin_family      = AF_INET;
    s_pc_addr.sin_port        = htons(cfg->pc_port);
    s_pc_addr.sin_addr.s_addr = inet_addr(cfg->pc_ip);

    printf("Updated PC address: %s:%u\n", cfg->pc_ip, cfg->pc_port);
    return 0;
}
