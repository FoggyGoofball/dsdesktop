#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "channel_calib.h"
#include "nifi_tx.h"
#include "nifi_rx.h"
#include "../../common/protocol.h"

typedef struct {
    uint8_t channel;
    uint16_t sent;
    uint16_t recv;
    uint32_t rtts_us[64];
    uint32_t median_us;
    uint32_t p95_us;
    uint16_t loss_permille;
    uint32_t score;
} ch_metric_t;

static uint32_t now_us(void)
{
    return (uint32_t)ticks_to_microsecs(gettick());
}

static int cmp_u32(const void *a, const void *b)
{
    uint32_t aa = *(const uint32_t *)a;
    uint32_t bb = *(const uint32_t *)b;
    return (aa > bb) - (aa < bb);
}

static void send_ping(uint32_t token, uint8_t channel, uint16_t seq)
{
    uint8_t buf[sizeof(dsrd_header_t) + sizeof(dsrd_diag_ping_t)];
    dsrd_header_t *hdr = (dsrd_header_t *)buf;
    dsrd_diag_ping_t *ping = (dsrd_diag_ping_t *)(buf + sizeof(dsrd_header_t));

    dsrd_header_init(hdr, PKT_DIAG_PING, seq,
                     sizeof(dsrd_diag_ping_t), 1 /* Wii */, 0);

    ping->token = token;
    ping->tx_time_us = now_us();
    ping->channel = channel;
    ping->_pad[0] = ping->_pad[1] = ping->_pad[2] = 0;

    nifi_tx_send(buf, sizeof(buf));
}

static int wait_for_pong(uint32_t token, uint16_t timeout_ms, uint32_t *out_rtt_us)
{
    uint8_t rx[DSRD_MTU + 64];
    uint32_t start = now_us();
    uint32_t timeout_us = (uint32_t)timeout_ms * 1000u;

    while ((now_us() - start) < timeout_us) {
        int n = nifi_rx_recv(rx, sizeof(rx));
        if (n >= (int)(sizeof(dsrd_header_t) + sizeof(dsrd_diag_pong_t))) {
            dsrd_header_t *hdr = (dsrd_header_t *)rx;
            if (!dsrd_header_valid(hdr) || hdr->type != PKT_DIAG_PONG) {
                usleep(1000);
                continue;
            }

            if ((int)(sizeof(dsrd_header_t) + hdr->payload_len) > n ||
                hdr->payload_len < sizeof(dsrd_diag_pong_t)) {
                usleep(1000);
                continue;
            }

            dsrd_diag_pong_t *pong =
                (dsrd_diag_pong_t *)(rx + sizeof(dsrd_header_t));
            if (pong->token == token) {
                *out_rtt_us = now_us() - start;
                return 1;
            }
        }
        usleep(1000);
    }

    return 0;
}

static void finalize_metric(ch_metric_t *m)
{
    if (m->recv == 0) {
        m->median_us = 999999;
        m->p95_us = 999999;
        m->loss_permille = 1000;
        m->score = 0xFFFFFFFFu;
        return;
    }

    qsort(m->rtts_us, m->recv, sizeof(uint32_t), cmp_u32);

    uint32_t med_idx = m->recv / 2;
    uint32_t p95_idx = (m->recv * 95) / 100;
    if (p95_idx >= m->recv) p95_idx = m->recv - 1;

    m->median_us = m->rtts_us[med_idx];
    m->p95_us = m->rtts_us[p95_idx];

    m->loss_permille = (uint16_t)(((m->sent - m->recv) * 1000u) / m->sent);

    /* Score: p95 dominates, then median, then loss */
    m->score = (m->p95_us * 50u) / 100u
             + (m->median_us * 30u) / 100u
             + ((uint32_t)m->loss_permille * 200u);
}

uint8_t channel_calib_run(const wii_config_t *cfg)
{
    if (!cfg || !cfg->auto_channel || cfg->channel_candidate_count == 0)
        return 0;

    printf("[CAL] Running channel latency benchmark...\n");

    ch_metric_t metrics[11];
    memset(metrics, 0, sizeof(metrics));

    uint16_t seq = 0;

    for (int ci = 0; ci < cfg->channel_candidate_count; ci++) {
        uint8_t ch = cfg->channel_candidates[ci];
        if (ch < 1 || ch > 11)
            continue;

        ch_metric_t *m = &metrics[ci];
        m->channel = ch;

        nifi_tx_set_channel(ch);
        usleep(250000); /* settle */

        for (int p = 0; p < cfg->probe_count && p < 64; p++) {
            uint32_t token = ((uint32_t)ch << 24) | ((uint32_t)p << 8) | (seq & 0xFF);
            uint32_t rtt_us = 0;

            send_ping(token, ch, seq++);
            m->sent++;

            if (wait_for_pong(token, cfg->probe_timeout_ms, &rtt_us)) {
                m->rtts_us[m->recv++] = rtt_us;
            }

            usleep(4000); /* keep probes lightweight */
        }

        finalize_metric(m);

        printf("[CAL] ch %u: sent=%u recv=%u loss=%.1f%% med=%.2fms p95=%.2fms score=%lu\n",
               m->channel,
               m->sent,
               m->recv,
               m->loss_permille / 10.0f,
               m->median_us / 1000.0f,
               m->p95_us / 1000.0f,
               (unsigned long)m->score);
    }

    uint32_t best_score = 0xFFFFFFFFu;
    uint8_t best_channel = 0;

    for (int ci = 0; ci < cfg->channel_candidate_count; ci++) {
        ch_metric_t *m = &metrics[ci];
        if (m->channel == 0)
            continue;
        if (m->score < best_score) {
            best_score = m->score;
            best_channel = m->channel;
        }
    }

    if (best_channel) {
        printf("[CAL] Best channel selected: %u\n", best_channel);
        nifi_tx_set_channel(best_channel);
    } else {
        printf("[CAL] No valid channel result, keeping configured channel.\n");
    }

    return best_channel;
}
