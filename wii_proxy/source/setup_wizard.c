/*============================================================================
 * wii_proxy/source/setup_wizard.c
 *
 * First-boot setup wizard UI.
 * Guides user through initial configuration via on-screen prompts.
 *==========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gccore.h>
#include <wiiuse/wpad.h>

#include "setup_wizard.h"
#include "config_ui.h"
#include "../../common/protocol.h"

#define CONFIG_PATH "sd:/apps/dsremote/proxy.cfg"

/*--------------------------------------------------------------------------
 * Helper: On-screen IP entry with Wiimote
 *------------------------------------------------------------------------*/
static int enter_ip_address(char *ip_buf, int buf_len)
{
    int octets[4] = {192, 168, 1, 100};  /* Default: 192.168.1.100 */
    int current_octet = 0;

    while (1) {
        printf("\x1b[2J\x1b[H");  /* Clear screen */

        printf("╔════════════════════════════════════════╗\n");
        printf("║   Enter PC Host IP Address             ║\n");
        printf("╚════════════════════════════════════════╝\n\n");

        printf("IP: ");
        for (int i = 0; i < 4; i++) {
            if (i == current_octet)
                printf("►%3d◄", octets[i]);
            else
                printf("%3d", octets[i]);

            if (i < 3)
                printf(".");
        }
        printf("\n\n");

        printf("Controls:\n");
        printf("  ← →     : Select octet\n");
        printf("  ↑ ↓     : Adjust value\n");
        printf("  A       : Confirm\n");
        printf("  B       : Reset to default\n");

        printf("\nCurrent: %d.%d.%d.%d\n",
               octets[0], octets[1], octets[2], octets[3]);

        WPAD_ScanPads();
        u32 pressed = WPAD_ButtonsDown(0);

        if (pressed & WPAD_BUTTON_LEFT) {
            if (current_octet > 0)
                current_octet--;
            usleep(150000);
        }

        if (pressed & WPAD_BUTTON_RIGHT) {
            if (current_octet < 3)
                current_octet++;
            usleep(150000);
        }

        if (pressed & WPAD_BUTTON_UP) {
            if (octets[current_octet] < 255)
                octets[current_octet]++;
            usleep(100000);
        }

        if (pressed & WPAD_BUTTON_DOWN) {
            if (octets[current_octet] > 0)
                octets[current_octet]--;
            usleep(100000);
        }

        if (pressed & WPAD_BUTTON_A) {
            /* Confirm IP */
            snprintf(ip_buf, buf_len, "%d.%d.%d.%d",
                     octets[0], octets[1], octets[2], octets[3]);
            printf("\nIP set to: %s\n", ip_buf);
            usleep(500000);
            return 0;
        }

        if (pressed & WPAD_BUTTON_B) {
            /* Reset to default */
            octets[0] = 192;
            octets[1] = 168;
            octets[2] = 1;
            octets[3] = 100;
            printf("\nReset to default.\n");
            usleep(500000);
        }

        usleep(100000);
    }

    return 0;
}

/*--------------------------------------------------------------------------
 * Helper: Select mode (USB or Wi-Fi)
 *------------------------------------------------------------------------*/
static int select_mode(void)
{
    int selection = 0;  /* 0 = USB, 1 = Wi-Fi */

    while (1) {
        printf("\x1b[2J\x1b[H");  /* Clear screen */

        printf("╔════════════════════════════════════════╗\n");
        printf("║   Select Backhaul Mode                 ║\n");
        printf("╚════════════════════════════════════════╝\n\n");

        printf("%s USB Ethernet (RECOMMENDED, low latency)\n",
               selection == 0 ? "→" : " ");
        printf("%s Wi-Fi Only (higher latency, no adapter needed)\n",
               selection == 1 ? "→" : " ");

        printf("\nRecommendation:\n");
        printf("  USB Ethernet gives best performance.\n");
        printf("  Use Wi-Fi only if you don't have adapter.\n\n");

        printf("Controls:\n");
        printf("  ↑ ↓     : Select mode\n");
        printf("  A       : Confirm\n");

        WPAD_ScanPads();
        u32 pressed = WPAD_ButtonsDown(0);

        if (pressed & WPAD_BUTTON_UP) {
            if (selection > 0)
                selection--;
            usleep(150000);
        }

        if (pressed & WPAD_BUTTON_DOWN) {
            if (selection < 1)
                selection++;
            usleep(150000);
        }

        if (pressed & WPAD_BUTTON_A) {
            printf("\nMode selected: %s\n",
                   selection == 0 ? "USB Ethernet" : "Wi-Fi");
            usleep(500000);
            return selection;
        }

        usleep(100000);
    }

    return 0;
}

/*--------------------------------------------------------------------------
 * Main setup wizard
 *------------------------------------------------------------------------*/
int setup_wizard_run(wii_config_t *cfg)
{
    printf("\x1b[2J\x1b[H");  /* Clear screen */

    printf("╔════════════════════════════════════════════════╗\n");
    printf("║   DS Remote Desktop — First Boot Setup         ║\n");
    printf("╚════════════════════════════════════════════════╝\n\n");

    printf("Welcome! Let's configure your Wii proxy.\n");
    printf("This wizard will help you set up:\n");
    printf("  1. Backhaul mode (USB or Wi-Fi)\n");
    printf("  2. PC host IP address\n");
    printf("  3. Wi-Fi channel settings\n\n");

    printf("Press any button to begin...\n");
    WPAD_ScanPads();
    while (WPAD_ButtonsDown(0) == 0) {
        usleep(100000);
        WPAD_ScanPads();
    }
    usleep(300000);

    /* Step 1: Select mode */
    printf("\n\n--- Step 1: Backhaul Mode ---\n");
    int mode = select_mode();
    cfg->use_usb_ethernet = mode == 0 ? 1 : 0;

    /* Step 2: Enter PC IP */
    printf("\n--- Step 2: PC Host IP ---\n");
    char pc_ip[16];
    enter_ip_address(pc_ip, sizeof(pc_ip));
    strncpy(cfg->pc_ip, pc_ip, sizeof(cfg->pc_ip) - 1);
    cfg->pc_ip[sizeof(cfg->pc_ip) - 1] = '\0';

    /* Step 3: Channel selection */
    printf("\x1b[2J\x1b[H");
    printf("╔════════════════════════════════════════╗\n");
    printf("║   Channel Calibration                  ║\n");
    printf("╚════════════════════════════════════════╝\n\n");

    printf("Auto-calibration can measure which Wi-Fi\n");
    printf("channel has the lowest latency.\n\n");

    printf("→ AUTO-CALIBRATE (recommended, ~10 seconds)\n");
    printf("  FIXED CHANNEL (faster startup)\n\n");

    printf("Press A for auto-calibrate, B for fixed...\n");

    WPAD_ScanPads();
    u32 button = 0;
    while ((button = WPAD_ButtonsDown(0)) == 0) {
        usleep(100000);
        WPAD_ScanPads();
    }

    if (button & WPAD_BUTTON_A) {
        cfg->auto_channel = 1;
        printf("Auto-calibration enabled.\n");
    } else {
        cfg->auto_channel = 0;
        printf("Fixed channel mode.\n");
    }
    usleep(500000);

    /* Save configuration to SD card */
    printf("\n--- Saving Configuration ---\n");
    FILE *f = fopen(CONFIG_PATH, "w");
    if (!f) {
        printf("ERROR: Cannot write to SD card!\n");
        printf("Check that sd:/apps/dsremote/ exists.\n");
        usleep(2000000);
        return -1;
    }

    fprintf(f, "# DS Remote Desktop — Wii Proxy Configuration\n");
    fprintf(f, "# Auto-generated by setup wizard\n\n");
    fprintf(f, "mode=%s\n", cfg->use_usb_ethernet ? "usb" : "wifi");
    fprintf(f, "channel=1\n");
    fprintf(f, "auto_channel=%d\n", cfg->auto_channel);
    fprintf(f, "channels=1,6,11\n");
    fprintf(f, "probe_count=18\n");
    fprintf(f, "probe_timeout_ms=80\n");
    fprintf(f, "pc_ip=%s\n", cfg->pc_ip);
    fprintf(f, "pc_port=%d\n", DSRD_PORT);

    fclose(f);
    printf("✓ Configuration saved to proxy.cfg\n");

    /* Summary */
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║   Setup Complete!                      ║\n");
    printf("╚════════════════════════════════════════╝\n\n");

    printf("Configuration:\n");
    printf("  Mode      : %s\n", cfg->use_usb_ethernet ? "USB Ethernet" : "Wi-Fi");
    printf("  PC IP     : %s\n", cfg->pc_ip);
    printf("  Channel   : %s\n", cfg->auto_channel ? "Auto-calibrate" : "Fixed (1)");
    printf("\nReady to start streaming!\n\n");

    printf("Press any button to continue...\n");
    WPAD_ScanPads();
    while (WPAD_ButtonsDown(0) == 0) {
        usleep(100000);
        WPAD_ScanPads();
    }
    usleep(300000);

    return 0;
}
