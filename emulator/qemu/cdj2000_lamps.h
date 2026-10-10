/*
 * Copyright (C) 2026 LycheeAPPF
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
/*
 * The panel's lamps, by name, from the frame MAIN sends the panel board.
 *
 * MAIN tells its panel which lamps to light in the same 24-byte exchange that
 * brings the keys back: the frame it transmits (DMA channel 4, cdj2000_main.c)
 * carries the lamps bit by bit.  This file names those bits, for the CDJ-2000
 * and for the NXS, and follows them over time: what each lamp shows (off, on,
 * a two-bit level, or blinking) and when that changes.  It knows nothing of
 * QEMU, so tests/test_panel_lamps.py drives it with frames of its own.
 */
#ifndef CDJ2000_LAMPS_H
#define CDJ2000_LAMPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* The bytes of the frame that carry lamps: its 22 bytes of payload. */
#define CDJ_LAMPS_PAYLOAD 22

typedef struct CdjPanelLamp {
    uint8_t byte;
    uint8_t mask;                   /* one bit, or a field of two */
    const char *name;
} CdjPanelLamp;

typedef enum CdjLampsMode {
    CDJ_LAMPS_OFF,                  /* nothing followed */
    CDJ_LAMPS_TRACK,                /* followed for `state`, nothing logged */
    CDJ_LAMPS_LOG,                  /* and one line per change */
    CDJ_LAMPS_LOG_ALL,              /* and the unnamed bits too, by byte.bit */
} CdjLampsMode;

/* What a lamp shows when it is blinking rather than holding a value. */
#define CDJ_LAMP_BLINKING 0x100

#define CDJ_LAMPS_MAX 128

typedef struct CdjLampTrack {
    CdjPanelLamp lamp;
    char unnamed[8];
    unsigned value;
    int64_t last_edge_ns;
    int64_t prev_edge_ns;
    int shown;                      /* -1 unknown, the value, or CDJ_LAMP_BLINKING */
} CdjLampTrack;

typedef struct CdjLamps {
    CdjLampsMode mode;
    unsigned count;
    CdjLampTrack track[CDJ_LAMPS_MAX];
} CdjLamps;

/*
 * The mode CDJ_PANEL_LAMPS asks for ("1" log, "all" log all, unset or "0"
 * nothing), given CDJ_INPUT_PORT: with the control channel open the lamps are
 * followed either way, so that `state` can report them.
 */
CdjLampsMode cdj_lamps_mode(const char *panel_lamps, const char *input_port);

/* The named lamps of the CDJ-2000's panel, or of the NXS's. */
const CdjPanelLamp *cdj_lamps_names(bool nxs, unsigned *count);

void cdj_lamps_init(CdjLamps *lamps, bool nxs, CdjLampsMode mode);

/*
 * One frame MAIN sent the panel, its payload at least, at guest time now_ns.
 * In the LOG modes each change of what a lamp shows is one line on `log`.
 * Returns whether anything a lamp shows changed.
 */
bool cdj_lamps_update(CdjLamps *lamps, const uint8_t *payload, int64_t now_ns,
                      FILE *log);

/*
 * What the lamps show, as `state` reports it: "CUE:on,PLAY_PAUSE:blink,
 * SOURCE_SD:3", the lamps that are dark left out, "-" when all are.  Written
 * into `out` (truncated to `size`); returns the length it needed.
 */
size_t cdj_lamps_summary(const CdjLamps *lamps, char *out, size_t size);

#endif /* CDJ2000_LAMPS_H */
