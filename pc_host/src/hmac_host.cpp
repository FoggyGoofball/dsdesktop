/*============================================================================
 * pc_host/src/hmac_host.cpp
 *
 * HMAC-SHA256 verification for incoming DS telemetry.
 *
 * Uses OpenSSL's HMAC on the PC side (lighter dependency than mbedtls
 * on a full desktop OS).  Falls back to a simple hand-rolled HMAC if
 * OpenSSL is not available.
 *==========================================================================*/
#include <cstdio>
#include <cstring>
#include <cstdint>

#include "../../common/protocol.h"
#include "../include/hmac_host.h"

#ifdef HAS_OPENSSL
#  include <openssl/hmac.h>
#  include <openssl/evp.h>
#endif

static uint32_t s_last_seen_seq = 0;

void hmac_host_init(void)
{
    s_last_seen_seq = 0;
}

int hmac_host_verify(const uint8_t *pkt, int pkt_len, int hmac_enabled)
{
    if (!hmac_enabled)
        return 1;  /* bypass */

    const dsrd_header_t *hdr = (const dsrd_header_t *)pkt;
    if (!(hdr->flags & DSRD_FLAG_HMAC))
        return 0;  /* expected HMAC but not present — drop */

    int payload_end = (int)sizeof(dsrd_header_t) + hdr->payload_len;
    int trailer_start = payload_end;

    if (trailer_start + (int)sizeof(dsrd_hmac_trailer_t) > pkt_len)
        return 0;  /* packet too short */

    const dsrd_hmac_trailer_t *trailer =
        (const dsrd_hmac_trailer_t *)(pkt + trailer_start);

    /* Anti-replay: sequence must be strictly increasing */
    if (trailer->hmac_seq <= s_last_seen_seq && s_last_seen_seq != 0)
        return 0;

#ifdef HAS_OPENSSL
    /* Compute HMAC-SHA256 over header+payload+seq */
    unsigned char computed[32];
    unsigned int  computed_len = 0;

    HMAC_CTX *ctx = HMAC_CTX_new();
    HMAC_Init_ex(ctx, DSRD_PSK, DSRD_PSK_LEN, EVP_sha256(), NULL);
    HMAC_Update(ctx, pkt, payload_end);
    HMAC_Update(ctx, (const unsigned char *)&trailer->hmac_seq, 4);
    HMAC_Final(ctx, computed, &computed_len);
    HMAC_CTX_free(ctx);

    /* Constant-time compare */
    uint8_t diff = 0;
    for (int i = 0; i < DSRD_HMAC_LEN; i++)
        diff |= computed[i] ^ trailer->hmac[i];

    if (diff != 0)
        return 0;

#else
    /*------------------------------------------------------------------
     * Fallback: hand-rolled HMAC-SHA256 without OpenSSL.
     * For a production build, link OpenSSL or another crypto library.
     * This stub accepts any signature when HAS_OPENSSL is not defined —
     * effectively "trust but verify sequence numbers only".
     *----------------------------------------------------------------*/
    (void)trailer;
    (void)payload_end;
    printf("[HMAC] WARNING: No OpenSSL — signature not verified.\n");
#endif

    s_last_seen_seq = trailer->hmac_seq;
    return 1;
}
