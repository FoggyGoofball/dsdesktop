/*============================================================================
 * wii_proxy/source/wl_stubs.c
 *
 * Stub implementations for the Broadcom BCM4318 raw radio control
 * functions.  These are referenced by nifi_tx.c, nifi_rx.c, and
 * ack_spoof.c.
 *
 * In a production build, these must be replaced with real BCM4318
 * register-level code or a NiFi-capable Wii radio library.
 *
 * The stubs log a warning and return gracefully so the project links
 * and runs in "backhaul-only" mode for development/testing.
 *==========================================================================*/
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/ios.h>
#include <stdio.h>
#include <string.h>

/*
 * Wii raw wireless backend built from the same WD ioctl flow used by
 * FIX94's wii-ds-rom-sender:
 *   - /dev/net/kd/request   (suspend scheduler)
 *   - /dev/net/ncd/manage   (lock/unlock wireless driver)
 *   - /dev/net/wd/command   (beacon/link/raw send/raw recv)
 */

#define IOCTL_ExecSuspendScheduler 1

static int s_promiscuous = 0;
static int s_channel = 1;

static s32 s_wd_fd = -1;
static s32 s_heap_id = -1;
static s32 s_ncd_rights = 0;
static int s_inited = 0;

static uint8_t *s_send_buf = NULL; /* 0x200 payload + 0x10 WD cfg trailer */

static uint8_t s_beacon_info[0x80] __attribute__((aligned(32)));

/*--------------------------------------------------------------------------
 * Internal helpers
 *------------------------------------------------------------------------*/
static void wl_unlock_driver(void)
{
    if (s_ncd_rights != 0) {
        s32 ncd_fd = IOS_Open("/dev/net/ncd/manage", 0);
        if (ncd_fd >= 0) {
            uint8_t in_data[4] __attribute__((aligned(32)));
            uint8_t out_data[0x20] __attribute__((aligned(32)));
            ioctlv iov[2];

            memcpy(in_data, &s_ncd_rights, 4);
            memset(out_data, 0, sizeof(out_data));

            iov[0].data = in_data;
            iov[0].len  = 4;
            iov[1].data = out_data;
            iov[1].len  = sizeof(out_data);

            IOS_Ioctlv(ncd_fd, 2, 1, 1, iov);
            IOS_Close(ncd_fd);
        }
        s_ncd_rights = 0;
    }
}

static void wl_shutdown(void)
{
    if (s_send_buf && s_heap_id >= 0) {
        iosFree(s_heap_id, s_send_buf);
        s_send_buf = NULL;
    }
    if (s_wd_fd >= 0) {
        IOS_Close(s_wd_fd);
        s_wd_fd = -1;
    }

    /* libogc IPC API has no public heap-destroy call */
    s_heap_id = -1;

    wl_unlock_driver();
    s_inited = 0;
}

static int wl_init_once(void)
{
    if (s_inited)
        return 0;

    /* Suspend network scheduler (same as FIX94 sender) */
    {
        u32 out = 0;
        s32 kd_fd = IOS_Open("/dev/net/kd/request", 0);
        if (kd_fd >= 0) {
            IOS_Ioctl(kd_fd, IOCTL_ExecSuspendScheduler, NULL, 0, &out, 4);
            IOS_Close(kd_fd);
        }
    }

    /* Lock wireless driver via NCD */
    {
        uint8_t out_data[0x20] __attribute__((aligned(32)));
        ioctlv iov;

        s32 ncd_fd = IOS_Open("/dev/net/ncd/manage", 0);
        if (ncd_fd < 0) {
            printf("WL: NCD open failed: %ld\n", ncd_fd);
            return -1;
        }

        memset(out_data, 0, sizeof(out_data));
        iov.data = out_data;
        iov.len  = sizeof(out_data);

        s32 ret = IOS_Ioctlv(ncd_fd, 1, 0, 1, &iov);
        IOS_Close(ncd_fd);
        if (ret < 0) {
            printf("WL: NCD lock failed: %ld\n", ret);
            return -1;
        }

        memcpy(&s_ncd_rights, out_data, 4);
    }

    s_heap_id = iosCreateHeap(0x8000);
    if (s_heap_id < 0) {
        printf("WL: iosCreateHeap failed: %ld\n", s_heap_id);
        wl_unlock_driver();
        return -1;
    }

    s_wd_fd = IOS_Open("/dev/net/wd/command", 0x10001);
    if (s_wd_fd < 0) {
        printf("WL: WD open failed: %ld\n", s_wd_fd);
        wl_shutdown();
        return -1;
    }

    s_send_buf = iosAlloc(s_heap_id, 0x210);
    if (!s_send_buf) {
        printf("WL: iosAlloc(send_buf) failed\n");
        wl_shutdown();
        return -1;
    }

    memset(s_send_buf, 0, 0x210);
    s_send_buf[0x207] = 0x0C;    /* frame flags/mode used by FIX94 */
    s_send_buf[0x209] = (1 << 1);/* channel bitmap: chan1 */

    /* WD_GetInfo */
    {
        uint8_t *tmp = iosAlloc(s_heap_id, 0x1A0);
        if (tmp) {
            memset(tmp, 0, 0x1A0);
            IOS_IoctlvFormat(s_heap_id, s_wd_fd, 0x100E, ":d", tmp, 0x90);

            /* WD_SetConfig */
            memset(tmp, 0, 0x1A0);
            tmp[0xAD] = 4;    /* timeout */
            tmp[0xAF] = 0xC8; /* beacon period 200 ms */
            tmp[0xB0] = 0x0F; /* max peers */

            /* Default mask values used by FIX94 */
            {
                uint32_t maskA = 0x3007F;
                uint32_t maskB = 0;
                memcpy(tmp + 0x180, &maskA, 4);
                memcpy(tmp + 0x184, &maskB, 4);
            }

            IOS_IoctlvFormat(s_heap_id, s_wd_fd, 0x1004,
                             "dd:", tmp, 0x180, tmp + 0x180, 8);

            iosFree(s_heap_id, tmp);
        }
    }

    /* Start beacon with minimal payload */
    {
        uint16_t beacon_in = (uint16_t)(ticks_to_microsecs(gettick()) / 64);
        memset(s_beacon_info, 0, sizeof(s_beacon_info));
        s_beacon_info[0] = 1;
        s_beacon_info[2] = 1;
        s_beacon_info[3] = 8;

        /* Match Download Station style GGID */
        s_beacon_info[4] = 0x20;
        s_beacon_info[5] = 0x01;
        s_beacon_info[6] = 0x40;

        IOS_IoctlvFormat(s_heap_id, s_wd_fd, 0x1006,
                         "hd:", beacon_in, s_beacon_info, 0x80);
    }

    /* Enable link */
    IOS_IoctlvFormat(s_heap_id, s_wd_fd, 0x1002, "i:", 1);

    /* Wait until link state is non-zero */
    {
        int loops = 0;
        s32 ret;
        do {
            ret = IOS_Ioctlv(s_wd_fd, 0x1003, 0, 0, NULL);
            loops++;
            if (loops > 500) break; /* ~timeout guard */
        } while (ret == 0);
    }

    s_inited = 1;
    return 0;
}

/*--------------------------------------------------------------------------
 * Public API expected by nifi_tx.c / nifi_rx.c / ack_spoof.c
 *------------------------------------------------------------------------*/
void WL_SetChannel(int channel)
{
    s_channel = channel;
    if (wl_init_once() < 0)
        return;

    /*
     * WD interface does not expose a simple documented "set channel" ioctl.
     * FIX94 sender relies on WD config + beaconing for MPDL mode.
     * Keep requested channel recorded for diagnostics.
     */
}

int WL_SendRawFrame(const void *data, int len)
{
    if (wl_init_once() < 0)
        return -1;
    if (!data || len <= 0)
        return -1;
    if (len > 0x200)
        len = 0x200;

    memcpy(s_send_buf, data, len);

    /* Keep virtual TSF in sync (same flow as FIX94 wdDoSend) */
    {
        uint16_t cTime = (uint16_t)(ticks_to_microsecs(gettick()) / 64);
        IOS_IoctlvFormat(s_heap_id, s_wd_fd, 0x1010, "h:", cTime);
    }

    s32 ret = IOS_IoctlvFormat(s_heap_id, s_wd_fd, 0x1008,
                               "dd:", s_send_buf, len,
                               s_send_buf + 0x200, 0x10);
    if (ret < 0)
        return (int)ret;

    return len;
}

void WL_SetPromiscuous(int enable)
{
    s_promiscuous = enable ? 1 : 0;
    (void)s_promiscuous;

    if (wl_init_once() < 0)
        return;

    /*
     * WD MPDL path already operates in the required local wireless mode.
     * No documented dedicated promiscuous toggle exists via WD ioctls.
     */
}

int WL_RecvRawFrame(void *buf, int max_len)
{
    if (wl_init_once() < 0)
        return -1;
    if (!buf || max_len <= 0)
        return -1;

    ioctlv recv;
    recv.data = buf;
    recv.len  = max_len;

    s32 ret = IOS_Ioctlv(s_wd_fd, 0x8000, 0, 1, &recv);
    if (ret < 0)
        return (int)ret;

    return (int)ret;
}
