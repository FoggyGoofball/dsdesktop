/*============================================================================
 * ds_client/arm9/source/hmac_auth.c
 *
 * HMAC-SHA256 authentication for upstream telemetry.
 * Uses a minimal software SHA-256 (sha256_soft.c) — no external crypto
 * library dependency.
 *
 * The DS computes the HMAC over the header+payload using the PSK, and
 * appends an incrementing sequence number to prevent replay attacks.
 *==========================================================================*/
#include <string.h>

#include "hmac_auth.h"
#include "sha256_soft.h"
#include "../../../common/protocol.h"

static uint32_t s_hmac_seq = 0;

void dsrd_hmac_init(void)
{
    s_hmac_seq = 0;
}

/*--------------------------------------------------------------------------
 * Sign: HMAC-SHA256( PSK, data || seq )
 *------------------------------------------------------------------------*/
void dsrd_hmac_sign(const uint8_t *data, uint16_t len,
                    dsrd_hmac_trailer_t *trailer)
{
    trailer->hmac_seq = s_hmac_seq++;

    /* Build the message: data || seq */
    /* We feed the HMAC in two parts rather than copying */
    uint8_t k_pad[64];
    sha256_ctx_t ctx;

    const uint8_t *key = (const uint8_t *)DSRD_PSK;
    size_t key_len = DSRD_PSK_LEN;

    /* Inner pad */
    memset(k_pad, 0x36, 64);
    for (size_t i = 0; i < key_len && i < 64; i++)
        k_pad[i] ^= key[i];

    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, 64);
    sha256_update(&ctx, data, len);
    sha256_update(&ctx, (const uint8_t *)&trailer->hmac_seq, 4);

    uint8_t inner[32];
    sha256_final(&ctx, inner);

    /* Outer pad */
    memset(k_pad, 0x5c, 64);
    for (size_t i = 0; i < key_len && i < 64; i++)
        k_pad[i] ^= key[i];

    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, 64);
    sha256_update(&ctx, inner, 32);
    sha256_final(&ctx, trailer->hmac);
}

/*--------------------------------------------------------------------------
 * Verify: recompute and compare (constant-time)
 *------------------------------------------------------------------------*/
int dsrd_hmac_verify(const uint8_t *data, uint16_t len,
                     const dsrd_hmac_trailer_t *trailer)
{
    uint8_t computed[DSRD_HMAC_LEN];
    uint8_t k_pad[64];
    sha256_ctx_t ctx;

    const uint8_t *key = (const uint8_t *)DSRD_PSK;
    size_t key_len = DSRD_PSK_LEN;

    /* Inner */
    memset(k_pad, 0x36, 64);
    for (size_t i = 0; i < key_len && i < 64; i++)
        k_pad[i] ^= key[i];

    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, 64);
    sha256_update(&ctx, data, len);
    sha256_update(&ctx, (const uint8_t *)&trailer->hmac_seq, 4);

    uint8_t inner[32];
    sha256_final(&ctx, inner);

    /* Outer */
    memset(k_pad, 0x5c, 64);
    for (size_t i = 0; i < key_len && i < 64; i++)
        k_pad[i] ^= key[i];

    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, 64);
    sha256_update(&ctx, inner, 32);
    sha256_final(&ctx, computed);

    /* Constant-time compare */
    uint8_t diff = 0;
    for (int i = 0; i < DSRD_HMAC_LEN; i++)
        diff |= computed[i] ^ trailer->hmac[i];

    return (diff == 0) ? 1 : 0;
}
