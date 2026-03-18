/*============================================================================
 * ds_client/arm7/source/main.c
 *
 * ARM7 co-processor firmware for the DS Remote Desktop client.
 *
 * IMPORTANT: This project uses the calico default ARM7 binary
 * (ds7_maine.elf), which handles Wi-Fi, touch, sound, and the wireless
 * manager server out of the box.  A custom ARM7 is NOT needed.
 *
 * This file is kept only as documentation and for future audio
 * customisation.  The Makefile does NOT compile this — it uses the
 * pre-built default ARM7 from the calico/default-arm7 package.
 *
 * If you later need custom ARM7 logic (e.g. direct hardware DAC
 * control for lower-latency audio), you can build this as a custom
 * ARM7 binary and point ndstool at it with -7.
 *
 * Audio approach with the default ARM7:
 *   The ARM9 uses libnds soundPlaySample() to stream audio.
 *   It sets up a looping playback buffer and continuously fills
 *   it with incoming PCM data.  The default ARM7 firmware services
 *   the sound hardware channel commands via the FIFO IPC.
 *==========================================================================*/
#ifdef BUILD_CUSTOM_ARM7  /* compile guard — not built by default */

#include <nds.h>
#include <string.h>

/*--------------------------------------------------------------------------
 * Audio ring buffer — pointer and size received from ARM9 via FIFO
 *------------------------------------------------------------------------*/
#define IPC_AUDIO_START  0xA0D10001u
#define IPC_AUDIO_STOP   0xA0D10002u

static volatile uint8_t *s_ring_ptr  = NULL;
static volatile uint32_t s_ring_size = 0;
static volatile uint16_t s_ring_tail = 0;
static volatile int       s_audio_on = 0;

#define AUDIO_SAMPLE_RATE  16384

/*--------------------------------------------------------------------------
 * Timer 0 IRQ — feed one sample to the sound hardware
 *------------------------------------------------------------------------*/
static void audio_timer_handler(void)
{
    if (!s_audio_on || !s_ring_ptr) return;

    volatile uint16_t *head_ptr =
        (volatile uint16_t *)(s_ring_ptr + s_ring_size);
    uint16_t head = *head_ptr;

    if (s_ring_tail == head) return;

    uint8_t sample = s_ring_ptr[s_ring_tail];
    s_ring_tail = (s_ring_tail + 1) % s_ring_size;

    SCHANNEL_CR(0) = 0;
    SCHANNEL_SOURCE(0) = (uint32_t)&sample;
    SCHANNEL_LENGTH(0) = 1;
    SCHANNEL_TIMER(0)  = SOUND_FREQ(AUDIO_SAMPLE_RATE);
    SCHANNEL_CR(0) = SCHANNEL_ENABLE | SOUND_ONE_SHOT |
                     SOUND_VOL(127) | SOUND_PAN(64) |
                     SOUND_FORMAT_8BIT;
}

/*--------------------------------------------------------------------------
 * FIFO handlers
 *------------------------------------------------------------------------*/
static void fifo_value_handler(uint32_t value, void *userdata)
{
    (void)userdata;
    if (value == IPC_AUDIO_START)      s_audio_on = 1;
    else if (value == IPC_AUDIO_STOP)  s_audio_on = 0;
    else                               s_ring_size = value;
}

static void fifo_address_handler(void *address, void *userdata)
{
    (void)userdata;
    s_ring_ptr = (volatile uint8_t *)address;
    s_ring_tail = 0;
}

/*--------------------------------------------------------------------------
 * MAIN
 *------------------------------------------------------------------------*/
int main(void)
{
    irqInit();
    fifoInit();

    readUserSettings();
    initClockIRQ();
    touchInit();

    powerOn(POWER_SOUND);
    writePowerManagement(PM_CONTROL_REG,
                         readPowerManagement(PM_CONTROL_REG) & ~PM_SOUND_MUTE);
    REG_SOUNDCNT = SOUND_ENABLE;
    REG_MASTER_VOLUME = 127;

    irqSet(IRQ_VBLANK, 0);
    irqEnable(IRQ_VBLANK);

    fifoSetValue32Handler(FIFO_USER_01, fifo_value_handler, NULL);
    fifoSetAddressHandler(FIFO_USER_01, fifo_address_handler, NULL);

    irqSet(IRQ_TIMER0, audio_timer_handler);
    irqEnable(IRQ_TIMER0);
    TIMER0_DATA = TIMER_FREQ_1024(AUDIO_SAMPLE_RATE);
    TIMER0_CR   = TIMER_ENABLE | TIMER_IRQ_REQ | TIMER_DIV_1024;

    for (;;) {
        swiWaitForVBlank();
    }

    return 0;
}

#endif /* BUILD_CUSTOM_ARM7 */
