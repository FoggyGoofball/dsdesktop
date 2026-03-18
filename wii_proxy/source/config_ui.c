/*============================================================================
 * wii_proxy/source/config_ui.c
 *
 * Load configuration from sd:/apps/dsremote/proxy.cfg or use defaults.
 * Minimal on-screen display of current settings.
 *==========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config_ui.h"
#include "../../common/protocol.h"

/* Default config file path on SD card */
#define CFG_PATH "sd:/apps/dsremote/proxy.cfg"

static void set_default_candidates(wii_config_t *cfg)
{
    cfg->channel_candidate_count = 3;
    cfg->channel_candidates[0] = 1;
    cfg->channel_candidates[1] = 6;
    cfg->channel_candidates[2] = 11;
    for (int i = 3; i < 11; i++)
        cfg->channel_candidates[i] = 0;
}

static void parse_channel_list(wii_config_t *cfg, const char *csv)
{
    uint8_t tmp[11];
    int count = 0;

    memset(tmp, 0, sizeof(tmp));

    char local[64];
    strncpy(local, csv, sizeof(local) - 1);
    local[sizeof(local) - 1] = '\0';

    char *tok = strtok(local, ",");
    while (tok && count < 11) {
        int ch = atoi(tok);
        if (ch >= 1 && ch <= 11)
            tmp[count++] = (uint8_t)ch;
        tok = strtok(NULL, ",");
    }

    if (count > 0) {
        cfg->channel_candidate_count = (uint8_t)count;
        memset(cfg->channel_candidates, 0, sizeof(cfg->channel_candidates));
        for (int i = 0; i < count; i++)
            cfg->channel_candidates[i] = tmp[i];
    }
}

void wii_config_load(wii_config_t *cfg)
{
    /* Defaults */
    cfg->use_usb_ethernet = 0;
    cfg->wifi_channel     = 1;
    cfg->auto_channel     = 1;
    cfg->probe_count      = 18;
    cfg->probe_timeout_ms = 80;
    set_default_candidates(cfg);

    strncpy(cfg->pc_ip, "192.168.1.100", sizeof(cfg->pc_ip) - 1);
    cfg->pc_ip[sizeof(cfg->pc_ip) - 1] = '\0';
    cfg->pc_port = DSRD_PORT;

    /* Try to read config file */
    FILE *f = fopen(CFG_PATH, "r");
    if (!f) {
        printf("No config file found, using defaults.\n");
        return;
    }

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[32], val[64];
        if (sscanf(line, "%31[^=]=%63s", key, val) == 2) {
            if (strcmp(key, "mode") == 0) {
                cfg->use_usb_ethernet = (strcmp(val, "usb") == 0) ? 1 : 0;
            } else if (strcmp(key, "channel") == 0) {
                int ch = atoi(val);
                if (ch >= 1 && ch <= 11) cfg->wifi_channel = (uint8_t)ch;
            } else if (strcmp(key, "auto_channel") == 0) {
                cfg->auto_channel = (uint8_t)(atoi(val) ? 1 : 0);
            } else if (strcmp(key, "channels") == 0) {
                parse_channel_list(cfg, val);
            } else if (strcmp(key, "probe_count") == 0) {
                int p = atoi(val);
                if (p >= 4 && p <= 64) cfg->probe_count = (uint8_t)p;
            } else if (strcmp(key, "probe_timeout_ms") == 0) {
                int t = atoi(val);
                if (t >= 10 && t <= 1000) cfg->probe_timeout_ms = (uint16_t)t;
            } else if (strcmp(key, "pc_ip") == 0) {
                strncpy(cfg->pc_ip, val, sizeof(cfg->pc_ip) - 1);
                cfg->pc_ip[sizeof(cfg->pc_ip) - 1] = '\0';
            } else if (strcmp(key, "pc_port") == 0) {
                int p = atoi(val);
                if (p > 0 && p < 65536) cfg->pc_port = (uint16_t)p;
            }
        }
    }
    fclose(f);
    printf("Config loaded from %s\n", CFG_PATH);
}

void wii_config_show(const wii_config_t *cfg)
{
    printf("--- Current Configuration ---\n");
    printf("  Backhaul : %s\n",
           cfg->use_usb_ethernet ? "USB Ethernet (Mode B)" : "Wi-Fi Only (Mode A)");
    printf("  Channel  : %d\n", cfg->wifi_channel);
    printf("  Auto ch. : %s\n", cfg->auto_channel ? "ON (latency benchmark)" : "OFF (fixed)");
    printf("  Probes   : %u timeout %ums\n", cfg->probe_count, cfg->probe_timeout_ms);

    printf("  Ch list  : ");
    for (int i = 0; i < cfg->channel_candidate_count; i++) {
        printf("%u", cfg->channel_candidates[i]);
        if (i + 1 < cfg->channel_candidate_count) printf(",");
    }
    printf("\n");

    printf("  PC Host  : %s:%d\n", cfg->pc_ip, cfg->pc_port);
    printf("-----------------------------\n\n");
}
