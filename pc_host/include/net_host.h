/*============================================================================
 * pc_host/include/net_host.h
 *
 * PC host networking — UDP send/recv to/from the Wii proxy.
 *==========================================================================*/
#ifndef DSRD_PC_NET_HOST_H
#define DSRD_PC_NET_HOST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int  net_host_init(const char *wii_ip, uint16_t port);
void net_host_shutdown(void);

int  net_host_send(const uint8_t *data, int len);
int  net_host_recv(uint8_t *buf, int max_len);  /* non-blocking */

#ifdef __cplusplus
}
#endif
#endif
