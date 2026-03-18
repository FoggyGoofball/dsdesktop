/*============================================================================
 * pc_host/include/net_utils.h
 *
 * Network utility functions: local IP discovery, etc.
 *==========================================================================*/
#ifndef NET_UTILS_H
#define NET_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Get the local IP address of the machine (non-loopback).
 * Attempts to find a routable IPv4 address.
 * Returns: 0 on success, -1 if no suitable address found.
 * ip_buf: output buffer (must be at least 16 bytes)
 */
int net_utils_get_local_ip(char *ip_buf, int ip_buf_len);

#ifdef __cplusplus
}
#endif
#endif /* NET_UTILS_H */
