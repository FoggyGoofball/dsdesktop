/*============================================================================
 * wii_proxy/source/ip_manager.c
 *
 * On-screen IP selection, entry, and persistence on SD card.
 *
 * Features:
 *   - Load/save IP list from sd:/apps/dsremote/ips.cfg
 *   - On-screen IP entry via Wiimote (octet-by-octet, ↑↓←→)
 *   - Select from saved IPs or enter new ones
 *   - Remove saved IPs
 *   - Automatic deduplication and persistence
 *==========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gccore.h>
#include <wiiuse/wpad.h>

#include "ip_manager.h"
#include "../../common/protocol.h"

#define IP_CONFIG_PATH "sd:/apps/dsremote/ips.cfg"
#define MAX_SAVED_IPS 10

static char g_saved_ips[MAX_SAVED_IPS][16];
static int g_ip_count = 0;

/*--------------------------------------------------------------------------
 * Load IPs from SD card
 *------------------------------------------------------------------------*/
void ip_manager_load(void)
{
    FILE *f = fopen(IP_CONFIG_PATH, "r");
    if (!f) {
        printf("No saved IPs found. Create sd:/apps/dsremote/ips.cfg\n");
        return;
    }

    g_ip_count = 0;
    char line[32];
    while (fgets(line, sizeof(line), f) && g_ip_count < MAX_SAVED_IPS) {
        /* Strip whitespace and comments */
        if (line[0] == '#' || line[0] == '\n')
            continue;

        char *p = line;
        while (*p && *p != '\n' && *p != '#')
            p++;
        *p = '\0';

        /* Trim leading spaces */
        p = line;
        while (*p == ' ' || *p == '\t')
            p++;

        if (*p != '\0') {
            strncpy(g_saved_ips[g_ip_count], p, sizeof(g_saved_ips[0]) - 1);
            g_saved_ips[g_ip_count][sizeof(g_saved_ips[0]) - 1] = '\0';
            g_ip_count++;
        }
    }

    fclose(f);
    printf("Loaded %d saved IPs from ips.cfg\n", g_ip_count);
}

/*--------------------------------------------------------------------------
 * Save all IPs to SD card
 *------------------------------------------------------------------------*/
static void ip_manager_save(void)
{
    FILE *f = fopen(IP_CONFIG_PATH, "w");
    if (!f) {
        printf("ERROR: Cannot write to sd:/apps/dsremote/ips.cfg\n");
        return;
    }

    fprintf(f, "# Saved PC host IPs\n");
    fprintf(f, "# Format: one IP per line\n\n");

    for (int i = 0; i < g_ip_count; i++) {
        fprintf(f, "%s\n", g_saved_ips[i]);
    }

    fclose(f);
    printf("Saved %d IPs to ips.cfg\n", g_ip_count);
}

/*--------------------------------------------------------------------------
 * Add a new IP and persist
 *------------------------------------------------------------------------*/
void ip_manager_add_ip(const char *ip)
{
    if (!ip || ip[0] == '\0')
        return;

    /* Check if already exists */
    for (int i = 0; i < g_ip_count; i++) {
        if (strcmp(g_saved_ips[i], ip) == 0) {
            printf("IP already in list: %s\n", ip);
            return;
        }
    }

    if (g_ip_count >= MAX_SAVED_IPS) {
        printf("IP list full (max %d)\n", MAX_SAVED_IPS);
        return;
    }

    strncpy(g_saved_ips[g_ip_count], ip, sizeof(g_saved_ips[0]) - 1);
    g_saved_ips[g_ip_count][sizeof(g_saved_ips[0]) - 1] = '\0';
    g_ip_count++;

    ip_manager_save();
    printf("Added IP: %s\n", ip);
}

/*--------------------------------------------------------------------------
 * On-screen IP entry via Wiimote (octet-by-octet, ↑↓←→)
 * Returns 0 on success (ip_buf populated), -1 if cancelled.
 *------------------------------------------------------------------------*/
static int enter_new_ip(char *ip_buf, int buf_len)
{
    /* Seed with common LAN default */
    int octets[4] = {192, 168, 1, 100};
    int cur = 0;   /* which octet is selected (0-3) */

    /* Held-button repeat state */
    int hold_frames = 0;

    while (1) {
        printf("\x1b[2J\x1b[H");  /* clear screen */

        printf("╔════════════════════════════════════════╗\n");
        printf("║   Enter New PC Host IP                 ║\n");
        printf("╚════════════════════════════════════════╝\n\n");

        /* Render the four octets, highlighting the selected one */
        printf("   IP:  ");
        for (int i = 0; i < 4; i++) {
            if (i == cur)
                printf("[%3d]", octets[i]);
            else
                printf(" %3d ", octets[i]);
            if (i < 3) printf(".");
        }
        printf("\n\n");

        printf("  ← →     Select octet\n");
        printf("  ↑ ↓     Adjust value (hold for fast)\n");
        printf("  A       Confirm & save\n");
        printf("  B       Cancel\n\n");

        printf("  Result: %d.%d.%d.%d\n",
               octets[0], octets[1], octets[2], octets[3]);

        WPAD_ScanPads();
        u32 pressed = WPAD_ButtonsDown(0);
        u32 held    = WPAD_ButtonsHeld(0);

        /* Navigation: left / right to select octet */
        if (pressed & WPAD_BUTTON_LEFT) {
            if (cur > 0) cur--;
            hold_frames = 0;
            usleep(120000);
        }
        if (pressed & WPAD_BUTTON_RIGHT) {
            if (cur < 3) cur++;
            hold_frames = 0;
            usleep(120000);
        }

        /* Value adjustment with acceleration on hold */
        int step = 1;
        if (hold_frames > 30) step = 10;  /* fast after ~3 sec */
        if (hold_frames > 60) step = 25;  /* very fast after ~6 sec */

        if (pressed & WPAD_BUTTON_UP) {
            octets[cur] = (octets[cur] + 1 > 255) ? 255 : octets[cur] + 1;
            hold_frames = 0;
            usleep(120000);
        } else if (held & WPAD_BUTTON_UP) {
            hold_frames++;
            if (hold_frames > 8) {
                octets[cur] = (octets[cur] + step > 255) ? 255 : octets[cur] + step;
                usleep(50000);
            }
        }

        if (pressed & WPAD_BUTTON_DOWN) {
            octets[cur] = (octets[cur] - 1 < 0) ? 0 : octets[cur] - 1;
            hold_frames = 0;
            usleep(120000);
        } else if (held & WPAD_BUTTON_DOWN) {
            hold_frames++;
            if (hold_frames > 8) {
                octets[cur] = (octets[cur] - step < 0) ? 0 : octets[cur] - step;
                usleep(50000);
            }
        }

        /* Reset hold counter when neither up nor down held */
        if (!(held & (WPAD_BUTTON_UP | WPAD_BUTTON_DOWN)))
            hold_frames = 0;

        /* Confirm */
        if (pressed & WPAD_BUTTON_A) {
            snprintf(ip_buf, buf_len, "%d.%d.%d.%d",
                     octets[0], octets[1], octets[2], octets[3]);
            return 0;
        }

        /* Cancel */
        if (pressed & WPAD_BUTTON_B) {
            return -1;
        }

        usleep(80000);
    }
}

/*--------------------------------------------------------------------------
 * Display menu and handle selection
 *------------------------------------------------------------------------*/
int ip_manager_show_menu(wii_config_t *cfg)
{
    /* Extra virtual entry: "+ Enter new IP" always at the end */
    int selection = 0;

    while (1) {
        int total_entries = g_ip_count + 1; /* saved IPs + "new" entry */

        printf("\x1b[2J\x1b[H");

        printf("╔════════════════════════════════════════╗\n");
        printf("║   PC Host IP Manager                   ║\n");
        printf("╚════════════════════════════════════════╝\n\n");

        /* List saved IPs */
        for (int i = 0; i < g_ip_count; i++) {
            int active = (strcmp(g_saved_ips[i], cfg->pc_ip) == 0);
            printf(" %s %d. %-15s %s\n",
                   (i == selection) ? ">" : " ",
                   i + 1,
                   g_saved_ips[i],
                   active ? " <-- active" : "");
        }

        /* "Enter new IP" entry */
        printf(" %s +  Enter new IP...\n",
               (selection == g_ip_count) ? ">" : " ");

        printf("\n  Current: %s\n\n", cfg->pc_ip);

        printf("  ↑/↓  Navigate    A  Select/Enter\n");
        printf("  B    Cancel      -  Remove highlighted\n");

        WPAD_ScanPads();
        u32 pressed = WPAD_ButtonsDown(0);

        if (pressed & WPAD_BUTTON_UP) {
            selection = (selection > 0) ? selection - 1 : total_entries - 1;
            usleep(150000);
        }

        if (pressed & WPAD_BUTTON_DOWN) {
            selection = (selection < total_entries - 1) ? selection + 1 : 0;
            usleep(150000);
        }

        if (pressed & WPAD_BUTTON_A) {
            if (selection == g_ip_count) {
                /* --- Enter a new IP ----------------------------------- */
                char new_ip[16] = {0};
                if (enter_new_ip(new_ip, sizeof(new_ip)) == 0 &&
                    new_ip[0] != '\0') {
                    /* Save, select, and connect */
                    ip_manager_add_ip(new_ip);
                    strncpy(cfg->pc_ip, new_ip, sizeof(cfg->pc_ip) - 1);
                    printf("\nConnecting to new IP: %s\n", cfg->pc_ip);
                    usleep(600000);
                    return 1;
                }
                /* Cancelled — stay in menu */
            } else {
                /* --- Select a saved IP -------------------------------- */
                strncpy(cfg->pc_ip, g_saved_ips[selection],
                        sizeof(cfg->pc_ip) - 1);
                printf("\nConnecting to: %s\n", cfg->pc_ip);
                usleep(500000);
                return 1;
            }
        }

        if (pressed & WPAD_BUTTON_B) {
            printf("\nCancelled.\n");
            usleep(200000);
            return 0;
        }

        /* Remove highlighted (MINUS button) */
        if ((pressed & WPAD_BUTTON_MINUS) && selection < g_ip_count) {
            printf("\nRemoving %s...\n", g_saved_ips[selection]);
            if (selection < g_ip_count - 1) {
                memmove(&g_saved_ips[selection],
                        &g_saved_ips[selection + 1],
                        (size_t)(g_ip_count - selection - 1) * sizeof(g_saved_ips[0]));
            }
            g_ip_count--;
            ip_manager_save();
            if (selection >= g_ip_count && selection > 0)
                selection--;
            usleep(400000);
        }

        usleep(80000);
    }

    return 0;
}
