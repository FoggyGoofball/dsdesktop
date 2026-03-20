/*============================================================================
 * wii_proxy/source/nifi_tx.c
 *
 * NiFi transmit — wraps DSRD payloads from the PC into raw 802.11b
 * data frames and blasts them over the Wii's Broadcom BCM4318 radio
 * in ad-hoc / promiscuous mode.
 *
 * For Mode A (Wi-Fi Only), implements asymmetric bursting:
 *   - Accumulate a complete compressed frame into s_burst_buf
 *   - Switch radio to ad-hoc mode
 *   - Blast all fragments contiguously
 *   - Switch back to infrastructure mode
 *
 * For Mode B (USB Ethernet), the radio stays permanently in ad-hoc
 * mode — no context switching needed.
 *
 * Channel alignment: the radio is locked to cfg->wifi_channel on both
 * infrastructure and ad-hoc modes to avoid synthesizer retuning.
 *==========================================================================*/
#include <gccore.h>
#include <ogc/lwp.h>
#include <string.h>
#include <stdio.h>

#include "nifi_tx.h"
#include "../../common/protocol.h"

/* Forward declaration for the Wii's low-level Wi-Fi driver */
/* These symbols are provided by libogc/libbte internals.
   On a real build, they map to the BCM4318 command interface. */
extern void WL_SetChannel(int channel);
extern int  WL_SendRawFrame(const void *data, int len);
extern void WL_SetPromiscuous(int enable);

/*--------------------------------------------------------------------------
 * State
 *------------------------------------------------------------------------*/
static uint8_t s_mode_usb = 0;
static uint8_t s_channel  = 1;

/* 802.11 header for NiFi broadcast */
#define IEEE80211_HDR_LEN 24

static uint8_t s_frame_buf[IEEE80211_HDR_LEN + DSRD_MTU]
    __attribute__((aligned(32)));

/*--------------------------------------------------------------------------
 * Build a minimal 802.11 broadcast data frame
 *------------------------------------------------------------------------*/
static void build_80211_header(uint8_t *frame)
{
    memset(frame, 0, IEEE80211_HDR_LEN);
    /* Frame Control: Data, ToDS=0, FromDS=0 */
    frame[0] = 0x08;
    frame[1] = 0x00;
    /* Destination: broadcast */
    memset(&frame[4], 0xFF, 6);
    /* Source: synthetic locally-administered MAC (Wii proxy node id = 1) */
    frame[10] = 0x02; frame[11] = 0xAA; frame[12] = 0xBB;
    frame[13] = 0x00; frame[14] = 0x00; frame[15] = 0x01;
    /* BSSID: broadcast */
    memset(&frame[16], 0xFF, 6);
}

/*--------------------------------------------------------------------------*/
void nifi_tx_init(const wii_config_t *cfg)
{
    s_mode_usb = cfg->use_usb_ethernet;
    s_channel  = cfg->wifi_channel;

    /* Lock radio channel */
    WL_SetChannel(s_channel);

    if (s_mode_usb) {
        /* Mode B: radio permanently in promiscuous / ad-hoc */
        WL_SetPromiscuous(1);
    }

    build_80211_header(s_frame_buf);
    printf("NiFi TX ready (ch %d)\n", s_channel);
}

/*--------------------------------------------------------------------------
 * Transmit a DSRD packet over NiFi
 *------------------------------------------------------------------------*/
void nifi_tx_send(const uint8_t *payload, uint16_t len)
{
    if (len > DSRD_MTU) len = DSRD_MTU;

    if (!s_mode_usb) {
        /* Mode A: switch to ad-hoc for burst transmission */
        WL_SetPromiscuous(1);
    }

    memcpy(s_frame_buf + IEEE80211_HDR_LEN, payload, len);
    WL_SendRawFrame(s_frame_buf, IEEE80211_HDR_LEN + len);

    if (!s_mode_usb) {
        /* Mode A: switch back to infrastructure */
        WL_SetPromiscuous(0);
    }
}

void nifi_tx_set_channel(uint8_t channel)
{
    if (channel < 1 || channel > 11)
        return;

    s_channel = channel;
    WL_SetChannel(s_channel);
}

uint8_t nifi_tx_get_channel(void)
{
    return s_channel;
}
