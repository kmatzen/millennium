#define _POSIX_C_SOURCE 200112L
#include "display_manager.h"
#include <string.h>
#include <stdio.h>

#define MAX_TEXT_LEN 256

static millennium_client_t *dm_client = NULL;

static char dm_line1_full[MAX_TEXT_LEN];
static char dm_line2_full[MAX_TEXT_LEN];
static int dm_line1_len;
static int dm_line2_len;
static int dm_scroll1_pos;
static int dm_scroll2_pos;
static int dm_line1_scrolling;
static int dm_line2_scrolling;
static int dm_line1_hold;  /* ticks remaining to pause at start of line 1 scroll */
static int dm_line2_hold;  /* ticks remaining to pause at start of line 2 scroll */

/*
 * Replace C0 control characters (bytes below 0x20) and DEL (0x7F) with spaces,
 * in place. The Beta firmware forwards every display byte straight to the VFD
 * module, which interprets these low bytes as commands (cursor moves, scroll
 * mode, display on/off) and treats 0x0A as a line break. The display manager
 * inserts its own single 0x0A line separator when packing the two lines, so any
 * control byte arriving in plugin-supplied text could only corrupt the layout
 * or issue an unintended VFD command. Printable ASCII and high-bit bytes (which
 * may map to the VFD's extended glyphs) are left untouched.
 */
static void dm_sanitize(char *s) {
    size_t i;
    for (i = 0; s[i] != '\0'; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7F) {
            s[i] = ' ';
        }
    }
}

static void dm_send_display(void) {
    char display_bytes[100];
    char visible1[DISPLAY_WIDTH + 1];
    char visible2[DISPLAY_WIDTH + 1];
    size_t pos = 0;
    int i;

    if (!dm_client) return;

    /* Build visible line 1 */
    if (dm_line1_scrolling) {
        int total = dm_line1_len + DISPLAY_SCROLL_GAP;
        for (i = 0; i < DISPLAY_WIDTH; i++) {
            int idx = (dm_scroll1_pos + i) % total;
            if (idx < dm_line1_len) {
                visible1[i] = dm_line1_full[idx];
            } else {
                visible1[i] = ' ';
            }
        }
    } else {
        for (i = 0; i < DISPLAY_WIDTH; i++) {
            visible1[i] = (i < dm_line1_len) ? dm_line1_full[i] : ' ';
        }
    }
    visible1[DISPLAY_WIDTH] = '\0';

    /* Build visible line 2 */
    if (dm_line2_scrolling) {
        int total = dm_line2_len + DISPLAY_SCROLL_GAP;
        for (i = 0; i < DISPLAY_WIDTH; i++) {
            int idx = (dm_scroll2_pos + i) % total;
            if (idx < dm_line2_len) {
                visible2[i] = dm_line2_full[idx];
            } else {
                visible2[i] = ' ';
            }
        }
    } else {
        for (i = 0; i < DISPLAY_WIDTH; i++) {
            visible2[i] = (i < dm_line2_len) ? dm_line2_full[i] : ' ';
        }
    }
    visible2[DISPLAY_WIDTH] = '\0';

    /* Pack into display bytes: line1 + LF + line2 + NUL */
    for (i = 0; i < DISPLAY_WIDTH; i++) {
        display_bytes[pos++] = visible1[i];
    }
    display_bytes[pos++] = 0x0A;
    for (i = 0; i < DISPLAY_WIDTH; i++) {
        display_bytes[pos++] = visible2[i];
    }
    display_bytes[pos] = '\0';

    millennium_client_set_display(dm_client, display_bytes);
}

void display_manager_init(millennium_client_t *client) {
    dm_client = client;
    dm_line1_full[0] = '\0';
    dm_line2_full[0] = '\0';
    dm_line1_len = 0;
    dm_line2_len = 0;
    dm_scroll1_pos = 0;
    dm_scroll2_pos = 0;
    dm_line1_scrolling = 0;
    dm_line2_scrolling = 0;
    dm_line1_hold = 0;
    dm_line2_hold = 0;
}

void display_manager_set_text(const char *line1, const char *line2) {
    char new1[MAX_TEXT_LEN];
    char new2[MAX_TEXT_LEN];

    /* Normalize NULL (clear) to an empty string so comparison is uniform, and
     * scrub control characters (bytes below 0x20, plus DEL 0x7F) before storage
     * and comparison: the VFD treats those bytes as commands or line breaks, so
     * they would otherwise corrupt the display, and sanitizing up front lets a
     * repaint of the same logical text compare equal and keep its scroll. */
    if (line1) {
        strncpy(new1, line1, MAX_TEXT_LEN - 1);
        new1[MAX_TEXT_LEN - 1] = '\0';
    } else {
        new1[0] = '\0';
    }
    dm_sanitize(new1);
    if (line2) {
        strncpy(new2, line2, MAX_TEXT_LEN - 1);
        new2[MAX_TEXT_LEN - 1] = '\0';
    } else {
        new2[0] = '\0';
    }
    dm_sanitize(new2);

    /*
     * Only reset the scroll position when a line's content actually changes.
     * A plugin that idempotently repaints its current state on every tick (the
     * canonical pattern) would otherwise snap a long, scrolling line back to
     * its start on each repaint and never advance. Re-sending identical text
     * now preserves the in-progress scroll animation. When a line does change
     * and is long enough to scroll, the start of the line is held in view for
     * DISPLAY_SCROLL_HOLD_TICKS before scrolling begins so it stays readable.
     */
    if (strcmp(new1, dm_line1_full) != 0) {
        strcpy(dm_line1_full, new1);
        dm_line1_len = (int)strlen(dm_line1_full);
        dm_scroll1_pos = 0;
        dm_line1_scrolling = (dm_line1_len > DISPLAY_WIDTH);
        dm_line1_hold = dm_line1_scrolling ? DISPLAY_SCROLL_HOLD_TICKS : 0;
    }
    if (strcmp(new2, dm_line2_full) != 0) {
        strcpy(dm_line2_full, new2);
        dm_line2_len = (int)strlen(dm_line2_full);
        dm_scroll2_pos = 0;
        dm_line2_scrolling = (dm_line2_len > DISPLAY_WIDTH);
        dm_line2_hold = dm_line2_scrolling ? DISPLAY_SCROLL_HOLD_TICKS : 0;
    }

    dm_send_display();
}

void display_manager_tick(void) {
    int changed = 0;

    if (dm_line1_scrolling) {
        if (dm_line1_hold > 0) {
            /* Holding the start of the message in view; don't advance yet. */
            dm_line1_hold--;
        } else {
            dm_scroll1_pos = (dm_scroll1_pos + 1) % (dm_line1_len + DISPLAY_SCROLL_GAP);
            changed = 1;
            if (dm_scroll1_pos == 0) {
                /* Wrapped back to the start — pause again so it stays readable. */
                dm_line1_hold = DISPLAY_SCROLL_HOLD_TICKS;
            }
        }
    }
    if (dm_line2_scrolling) {
        if (dm_line2_hold > 0) {
            dm_line2_hold--;
        } else {
            dm_scroll2_pos = (dm_scroll2_pos + 1) % (dm_line2_len + DISPLAY_SCROLL_GAP);
            changed = 1;
            if (dm_scroll2_pos == 0) {
                dm_line2_hold = DISPLAY_SCROLL_HOLD_TICKS;
            }
        }
    }

    if (changed) {
        dm_send_display();
    }
}

void display_manager_refresh(void) {
    dm_send_display();
}

void display_manager_get_text(char *line1, size_t line1_size,
                              char *line2, size_t line2_size) {
    if (line1 && line1_size > 0) {
        strncpy(line1, dm_line1_full, line1_size - 1);
        line1[line1_size - 1] = '\0';
    }
    if (line2 && line2_size > 0) {
        strncpy(line2, dm_line2_full, line2_size - 1);
        line2[line2_size - 1] = '\0';
    }
}
