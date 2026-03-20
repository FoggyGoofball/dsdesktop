/*============================================================================
 * ds_client/arm7/source/main.c
 *
 * Custom ARM7 runtime for the DS Remote Desktop client.
 *
 * This is used for the flashcart-compatible combined build so we avoid the
 * auto-bundled default ARM7 image. It starts the standard DS peripheral
 * services required by the ARM9 client, including touch, sound and the
 * wireless manager server used by calico/wlmgr on ARM9.
 *==========================================================================*/
#include <calico.h>
#include <nds.h>

int main(void)
{
    /* Read user settings from NVRAM */
    envReadNvramSettings();

    /* Extended keypad server (X/Y/hinge) */
    keypadStartExtServer();

    /* Configure and enable VBlank interrupt */
    lcdSetIrqMask(DISPSTAT_IE_ALL, DISPSTAT_IE_VBLANK);
    irqEnable(IRQ_VBLANK);

    /* RTC / power / block peripherals */
    rtcInit();
    rtcSyncTime();
    pmInit();
    blkInit();

    /* Touch / sound / mic services */
    touchInit();
    touchStartServer(80, MAIN_THREAD_PRIO);
    soundStartServer(MAIN_THREAD_PRIO - 0x10);
    micStartServer(MAIN_THREAD_PRIO - 0x18);

    /* Wireless manager for ARM9 wlmgr APIs */
    wlmgrStartServer(MAIN_THREAD_PRIO - 8);

    /* Idle loop */
    while (pmMainLoop()) {
        threadWaitForVBlank();
    }

    return 0;
}
