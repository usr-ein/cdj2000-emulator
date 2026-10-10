/*
 * Drive emulator/qemu/cdj2000_lamps.c with frames read from stdin and print
 * what it makes of them, so tests/test_panel_lamps.py can check the naming,
 * the levels and the blink detection without a machine or any firmware.
 *
 *     mode <CDJ_PANEL_LAMPS|-> <CDJ_INPUT_PORT|->   -> "mode off|track|log|all"
 *     init 2000|nxs off|track|log|all
 *     frame <guest ms> <payload hex, 22 bytes>      -> its log lines, then
 *                                                      "changed" if anything did
 *     summary                                       -> "summary <what state says>"
 *     names 2000|nxs                                -> "name <byte> <mask> <name>" each
 */
#include "cdj2000_lamps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const mode_names[] = { "off", "track", "log", "all" };

static CdjLampsMode mode_named(const char *name)
{
    for (unsigned i = 0; i < 4; i++) {
        if (!strcmp(name, mode_names[i])) {
            return (CdjLampsMode)i;
        }
    }
    fprintf(stderr, "panel-lamps: no mode %s\n", name);
    exit(2);
}

int main(void)
{
    static CdjLamps lamps;
    char line[512];

    cdj_lamps_init(&lamps, false, CDJ_LAMPS_TRACK);
    while (fgets(line, sizeof(line), stdin)) {
        char verb[16] = "", first[64] = "", second[128] = "";

        if (sscanf(line, "%15s %63s %127s", verb, first, second) < 1) {
            continue;
        }
        if (!strcmp(verb, "mode")) {
            CdjLampsMode mode = cdj_lamps_mode(strcmp(first, "-") ? first : NULL,
                                               strcmp(second, "-") ? second : NULL);

            printf("mode %s\n", mode_names[mode]);
        } else if (!strcmp(verb, "init")) {
            cdj_lamps_init(&lamps, !strcmp(first, "nxs"), mode_named(second));
        } else if (!strcmp(verb, "frame")) {
            uint8_t payload[CDJ_LAMPS_PAYLOAD] = { 0 };

            if (strlen(second) != 2 * CDJ_LAMPS_PAYLOAD) {
                fprintf(stderr, "panel-lamps: a payload is %d bytes\n",
                        CDJ_LAMPS_PAYLOAD);
                return 2;
            }
            for (unsigned i = 0; i < CDJ_LAMPS_PAYLOAD; i++) {
                unsigned value;

                sscanf(second + 2 * i, "%2x", &value);
                payload[i] = (uint8_t)value;
            }
            if (cdj_lamps_update(&lamps, payload, strtoll(first, NULL, 0) * 1000000LL,
                                 stdout)) {
                printf("changed\n");
            }
        } else if (!strcmp(verb, "summary")) {
            char summary[1536];

            cdj_lamps_summary(&lamps, summary, sizeof(summary));
            printf("summary %s\n", summary);
        } else if (!strcmp(verb, "names")) {
            unsigned count;
            const CdjPanelLamp *names = cdj_lamps_names(!strcmp(first, "nxs"), &count);

            for (unsigned i = 0; i < count; i++) {
                printf("name %u 0x%02x %s\n", names[i].byte, names[i].mask,
                       names[i].name);
            }
        } else {
            fprintf(stderr, "panel-lamps: no verb %s\n", verb);
            return 2;
        }
        fflush(stdout);
    }
    return 0;
}
