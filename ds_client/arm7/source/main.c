/*============================================================================
 * ds_client/arm7/source/main.c
 *
 * Custom ARM7 runtime for the DS Remote Desktop client.
 *
 * This is used for the flashcart-compatible combined build so we avoid the
 * auto-bundled default ARM7 image.  It starts the standard BlocksDS / libnds
 * peripheral services (touch, sound) and — critically — installs the dswifi
 * FIFO handler so that ARM9 dswifi calls (Wifi_InitDefault, Wifi_RawTxFrame,
 * Wifi_RawSetPacketHandler, etc.) actually reach the Wi-Fi hardware.
 *
 * NOTE: The previous calico-based ARM7 ran wlmgrStartServer() which is a
 * completely separate wireless stack.  ARM9 now uses dswifi, so the ARM7
 * *must* use installWifiFIFO() + Wifi_Update() from dswifi7.
 *==========================================================================*/
#include <nds.h>
#include <dswifi7.h>

/*--------------------------------------------------------------------------*/
static void vblank_handler(void)
{
    Wifi_Update();
}

static void vcount_handler(void)
{
    inputGetAndSend();
}

static volatile bool s_exit = false;

static void power_button_cb(void)
{
    s_exit = true;
}

/*--------------------------------------------------------------------------*/
int main(void)
{
    readUserSettings();

    irqInit();
    initClockIRQ();

    fifoInit();

    touchInit();
    SetYtrigger(80);

    /* dswifi FIFO — lets ARM9 Wifi_* calls reach the hardware */
    installWifiFIFO();
    installSoundFIFO();
    installSystemFIFO();

    irqSet(IRQ_VCOUNT, vcount_handler);
    irqSet(IRQ_VBLANK, vblank_handler);
    irqEnable(IRQ_VBLANK | IRQ_VCOUNT | IRQ_NETWORK);

    setPowerButtonCB(power_button_cb);

    while (!s_exit) {
        if (0 == (REG_KEYINPUT & (KEY_SELECT | KEY_START | KEY_L | KEY_R)))
            s_exit = true;
        swiWaitForVBlank();
    }

    return 0;
}
