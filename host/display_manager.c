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
}

void display_manager_set_text(const char *line1, const char *line2) {
    if (line1) {
        strncpy(dm_line1_full, line1, MAX_TEXT_LEN - 1);
        dm_line1_full[MAX_TEXT_LEN - 1] = '\0';
        dm_line1_len = (int)strlen(dm_line1_full);
    } else {
        dm_line1_full[0] = '\0';
        dm_line1_len = 0;
    }
    dm_scroll1_pos = 0;
    dm_line1_scrolling = (dm_line1_len > DISPLAY_WIDTH);

    if (line2) {
        strncpy(dm_line2_full, line2, MAX_TEXT_LEN - 1);
        dm_line2_full[MAX_TEXT_LEN - 1] = '\0';
        dm_line2_len = (int)strlen(dm_line2_full);
    } else {
        dm_line2_full[0] = '\0';
        dm_line2_len = 0;
    }
    dm_scroll2_pos = 0;
    dm_line2_scrolling = (dm_line2_len > DISPLAY_WIDTH);

    dm_send_display();
}

void display_manager_tick(void) {
    int changed = 0;

    if (dm_line1_scrolling) {
        dm_scroll1_pos = (dm_scroll1_pos + 1) % (dm_line1_len + DISPLAY_SCROLL_GAP);
        changed = 1;
    }
    if (dm_line2_scrolling) {
        dm_scroll2_pos = (dm_scroll2_pos + 1) % (dm_line2_len + DISPLAY_SCROLL_GAP);
        changed = 1;
    }

    if (changed) {
        dm_send_display();
    }
}

void display_manager_refresh(void) {
    dm_send_display();
}
