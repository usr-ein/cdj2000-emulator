/*
 * Copyright (C) 2026 LycheeAPPF
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "cdj2000_lamps.h"

#include <inttypes.h>
#include <string.h>

/*
 * The panel lamps, by name.  MAIN builds the frame at 0x0428fe92 (and from
 * its blink copy 0x04fe2ba0 at 0x042905fc) out of the lamp state bytes at
 * 0x04fe2b38 (L0..L7, L+8 and L+12 words, L+31), bit by bit into the staging
 * buffer 0x04fc3010, and copies it to 0xa4501018.  The service-mode path
 * (0x0424bc9a != 0) fills bytes 0/1 from 0x0429173e instead, three words of
 * three bits each -- which is what groups the hot cue bits by button below.
 *
 * Every name here was seen to follow a key in a co-simulation run on stock
 * 4.33 (runs/cosim/px-3..px-7; AGENT-REPORT.md has the times).  The three hot
 * cue "loop" bits and C's "cue" bit are named from that grouping plus C's loop
 * bit, which px-4 lit by storing a running loop in C.
 *
 * Byte 3 is four 2-bit source lamps (the builder copies L7 two bits at a
 * time); SD and USB each moved 1 -> 3 by its SOURCE key (px-5, px-7) and
 * blink while the deck reads that medium.  DISC (bits 1..0) and LINK (bits
 * 7..6) went to 2 on their SOURCE keys with no disc and no link, and back to
 * 0 on the next source (runs/cosim/hl-4 in the loops/hotcue-lamps worktree).
 *
 * The rest were named from their writers plus a run each (runs/cosim/lm-1..4
 * in the panel/lamps-scratch worktree), with the CDJ-2000 manual's part names
 * as the cross-check:
 * - JOG MODE: [0x04fdc218] == 1 is VINYL, not CDJ.  MAIN takes the jog top
 *   (key 15.5) only then (0x042845e6, 0x0428461a): holding it braked the deck
 *   to rate 0 in that mode and did nothing in the other (lm-3), as the manual
 *   has it for VINYL.  So L1.1 (lit when != 1) is the CDJ mode lamp and L1.2
 *   the VINYL one; both were named the other way round before.
 * - Byte 4 is L6, four 2-bit lamps set from the words 0x04c084e0 + 4n
 *   (0x04290c98): 1 = available, 3 = the screen shown.  TAG LIST, INFO and
 *   BROWSE moved them in lm-1, MENU in lm-2.
 * - Byte 7 comes from L+31 (0x0429037c): bits 4/3 -> 7.3/7.4, 2 -> 7.5,
 *   1 -> 7.6.  7.3/7.4 are set together with the VINYL lamp (0x04263b84);
 *   7.5 lit while the jog top was held in VINYL (lm-3 47.07..50.07) and while
 *   paused in VINYL (lm-3 52.06), from 0x0426315c; 7.6 blinks while a
 *   re-stream refills the deck's buffer and is steady once its counters
 *   reach 150 (0x042633f0..0x042634ba; lm-3 55.07..56.67): the jog display's
 *   VINYL mark, touch display and audio memory display.
 * Still unnamed, never seen to change with an SD card or a USB stick:
 * 2.4 (L1.6), 2.7 (L0.0), and byte 5 (L5, two 2-bit fields that 0x04291200
 * sets from 0x0424c092 / 0x0424d074; the disc drive is not modelled).  The
 * builder writes byte 5 bits 4..7 and byte 7 bits 0..2 and 7 as 0.
 * Bytes 6 and 8 are jog ring positions (L+8, L+12) and are not lamps.
 */
static const CdjPanelLamp cdj2000_lamp_names[] = {
    { 0, 0x01, "HOT_CUE_A" },           /* L3.5 */
    { 0, 0x02, "HOT_CUE_A_REC" },       /* L3.6 */
    { 0, 0x04, "HOT_CUE_A_LOOP" },      /* L3.7 */
    { 0, 0x08, "HOT_CUE_B" },           /* L3.2 */
    { 0, 0x10, "HOT_CUE_B_REC" },       /* L3.3 */
    { 0, 0x20, "HOT_CUE_B_LOOP" },      /* L3.4 */
    { 0, 0x40, "HOT_CUE_C" },           /* L2.7 */
    { 0, 0x80, "HOT_CUE_C_REC" },       /* L3.0 */
    { 1, 0x01, "HOT_CUE_C_LOOP" },      /* L3.1 */
    { 1, 0x02, "LOOP_IN" },             /* L2.0 */
    { 1, 0x04, "LOOP_OUT" },            /* L2.1 */
    { 1, 0x08, "REV" },                 /* L2.2: lit from boot with the lever at REV only (lm-4) */
    { 1, 0x10, "CUE" },                 /* L2.3, 0x04263624 */
    { 1, 0x20, "PLAY_PAUSE" },          /* L2.4, 0x04263562 / 0x04263578 */
    { 1, 0x40, "RELOOP_EXIT" },         /* L2.5 */
    { 1, 0x80, "TEMPO_RESET" },         /* L2.6 = [0x04fdc1d5], 0x04263bba */
    { 2, 0x01, "JOG_CDJ" },             /* L1.1, [0x04fdc218] != 1 */
    { 2, 0x02, "JOG_VINYL" },           /* L1.2, [0x04fdc218] == 1 */
    { 2, 0x04, "SD_INDICATOR" },        /* L1.4, 0x04290ed0's blinker */
    { 2, 0x08, "ROTARY_SELECTOR" },     /* L1.5 from 0x04c084f4 (0x04291422): steady after an encoder
                                           turn (0x0428ef06) or a key, blinking while a LOAD runs (lm-2) */
    { 2, 0x20, "MASTER_TEMPO" },        /* L1.7 = [0x04fdc1d4], 0x04263baa */
    { 3, 0x03, "SOURCE_DISC" },         /* L7 bits 1..0: DISC 19.3 -> 2 (hl-4) */
    { 3, 0x0c, "SOURCE_SD" },           /* L7 bits 3..2, 0x04290ae8 */
    { 3, 0x30, "SOURCE_USB" },          /* L7 bits 5..4, 0x04290b00 */
    { 3, 0xc0, "SOURCE_LINK" },         /* L7 bits 7..6: LINK 19.0 -> 2 (hl-4) */
    { 4, 0x03, "BROWSE" },              /* L6 bits 1..0 = [0x04c084e0] */
    { 4, 0x0c, "INFO" },                /* L6 bits 3..2 = [0x04c084e8] */
    { 4, 0x30, "TAG_LIST" },            /* L6 bits 5..4 = [0x04c084e4] */
    { 4, 0xc0, "MENU" },                /* L6 bits 7..6 = [0x04c084ec] */
    { 7, 0x18, "JOG_DISPLAY_VINYL" },   /* L+31 bits 4..3, with JOG_VINYL (0x04263b84) */
    { 7, 0x20, "JOG_DISPLAY_TOUCH" },   /* L+31 bit 2, 0x0426315c */
    { 7, 0x40, "JOG_DISPLAY_MEMORY" },  /* L+31 bit 1, 0x042633f0 */
};

/* The bytes whose other bits CDJ_PANEL_LAMPS=all names by byte.bit. */
static const uint8_t cdj2000_lamp_bytes[] = { 0, 1, 2, 3, 4, 5, 7 };

/* A lamp that toggled twice within this is blinking, not switching. */
#define CDJ_LAMPS_BLINK_NS (1200 * 1000000LL)

CdjLampsMode cdj_lamps_mode(const char *panel_lamps, const char *input_port)
{
    if (panel_lamps && *panel_lamps && strcmp(panel_lamps, "0")) {
        return !strcmp(panel_lamps, "all") ? CDJ_LAMPS_LOG_ALL : CDJ_LAMPS_LOG;
    }
    return input_port && *input_port ? CDJ_LAMPS_TRACK : CDJ_LAMPS_OFF;
}

const CdjPanelLamp *cdj_lamps_names(bool nxs, unsigned *count)
{
    if (nxs) {
        *count = 0;                     /* the NXS panel protocol is its own */
        return NULL;
    }
    *count = sizeof(cdj2000_lamp_names) / sizeof(cdj2000_lamp_names[0]);
    return cdj2000_lamp_names;
}

void cdj_lamps_init(CdjLamps *lamps, bool nxs, CdjLampsMode mode)
{
    unsigned named;
    const CdjPanelLamp *names = cdj_lamps_names(nxs, &named);
    unsigned i;

    memset(lamps, 0, sizeof(*lamps));
    lamps->mode = named ? mode : CDJ_LAMPS_OFF;
    for (i = 0; i < named; i++) {
        lamps->track[lamps->count++].lamp = names[i];
    }
    if (lamps->mode == CDJ_LAMPS_LOG_ALL) {
        const uint8_t *bytes = cdj2000_lamp_bytes;
        unsigned nbytes = sizeof(cdj2000_lamp_bytes);
        unsigned b, bit, j;

        for (b = 0; b < nbytes; b++) {
            for (bit = 0; bit < 8 && lamps->count < CDJ_LAMPS_MAX; bit++) {
                CdjLampTrack *t = &lamps->track[lamps->count];
                bool is_named = false;

                for (j = 0; j < named; j++) {
                    is_named |= names[j].byte == bytes[b]
                                && (names[j].mask & (1u << bit));
                }
                if (is_named) {
                    continue;
                }
                t->lamp.byte = bytes[b];
                t->lamp.mask = 1u << bit;
                snprintf(t->unnamed, sizeof(t->unnamed), "%u.%u", bytes[b], bit);
                t->lamp.name = t->unnamed;
                lamps->count++;
            }
        }
    }
    for (i = 0; i < lamps->count; i++) {
        lamps->track[i].shown = -1;
        lamps->track[i].last_edge_ns = lamps->track[i].prev_edge_ns = INT64_MIN / 2;
    }
}

static unsigned cdj_lamp_value(const CdjPanelLamp *lamp, const uint8_t *payload)
{
    unsigned value = payload[lamp->byte] & lamp->mask;
    unsigned mask = lamp->mask;

    while (mask && !(mask & 1)) {
        mask >>= 1;
        value >>= 1;
    }
    return value;
}

static bool cdj_lamp_is_field(const CdjPanelLamp *lamp)
{
    return lamp->mask & (lamp->mask - 1);
}

/*
 * CDJ_PANEL_LAMPS=1: one line per lamp change, "on"/"off" (or the 2-bit
 * level), and "blink N ms" once a lamp toggles twice within 1.2 s -- a
 * blinking lamp is one change, not one line per toggle.  =all also names the
 * other bits of the lamp bytes by byte.bit.
 */
bool cdj_lamps_update(CdjLamps *lamps, const uint8_t *payload, int64_t now_ns,
                      FILE *log)
{
    bool changed = false;
    unsigned i;

    if (lamps->mode == CDJ_LAMPS_OFF) {
        return false;
    }
    for (i = 0; i < lamps->count; i++) {
        CdjLampTrack *t = &lamps->track[i];
        unsigned value = cdj_lamp_value(&t->lamp, payload);
        int shown;

        if (value != t->value || t->shown < 0) {
            if (t->shown >= 0) {
                t->prev_edge_ns = t->last_edge_ns;
                t->last_edge_ns = now_ns;
            }
            t->value = value;
        }
        shown = now_ns - t->last_edge_ns < CDJ_LAMPS_BLINK_NS
                && t->last_edge_ns - t->prev_edge_ns < CDJ_LAMPS_BLINK_NS
                ? CDJ_LAMP_BLINKING : (int)value;
        if (shown == t->shown) {
            continue;
        }
        changed = true;
        if (lamps->mode >= CDJ_LAMPS_LOG && t->shown >= 0 && log) {
            if (shown == CDJ_LAMP_BLINKING) {
                fprintf(log, "cdj2000-lamp t=%.3f %s blink %u ms\n",
                        now_ns / 1e9, t->lamp.name,
                        (unsigned)((t->last_edge_ns - t->prev_edge_ns)
                                   / 1000000));
            } else if (cdj_lamp_is_field(&t->lamp)) {
                fprintf(log, "cdj2000-lamp t=%.3f %s %d\n", now_ns / 1e9,
                        t->lamp.name, shown);
            } else {
                fprintf(log, "cdj2000-lamp t=%.3f %s %s\n", now_ns / 1e9,
                        t->lamp.name, shown ? "on" : "off");
            }
        }
        t->shown = shown;
    }
    return changed;
}

size_t cdj_lamps_summary(const CdjLamps *lamps, char *out, size_t size)
{
    size_t used = 0;
    unsigned i;

    if (size) {
        out[0] = 0;
    }
    for (i = 0; i < lamps->count; i++) {
        const CdjLampTrack *t = &lamps->track[i];
        char how[16];
        int wrote;

        if (t->shown <= 0) {
            continue;
        }
        if (t->shown == CDJ_LAMP_BLINKING) {
            snprintf(how, sizeof(how), "blink");
        } else if (cdj_lamp_is_field(&t->lamp)) {
            snprintf(how, sizeof(how), "%d", t->shown);
        } else {
            snprintf(how, sizeof(how), "on");
        }
        wrote = snprintf(used < size ? out + used : NULL,
                         used < size ? size - used : 0, "%s%s:%s",
                         used ? "," : "", t->lamp.name, how);
        used += wrote > 0 ? (size_t)wrote : 0;
    }
    if (!used) {
        used = (size_t)snprintf(out, size, "-");
    }
    return used;
}
