#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include "millennium_sdk.h"

#define DISPLAY_WIDTH 20
#define DISPLAY_SCROLL_GAP 3  /* spaces between end and start during scroll */

/*
 * Initialize the display manager with the SDK client handle.
 * Must be called before any other display_manager functions.
 */
void display_manager_init(millennium_client_t *client);

/*
 * Set display text. Lines longer than 20 characters will scroll
 * automatically on each tick. Short lines are displayed statically.
 * Passing NULL for a line clears that line.
 *
 * Re-setting a line to text identical to what it already shows preserves the
 * in-progress scroll position, so a plugin may safely repaint its current
 * state every tick without freezing a scrolling line at its start.
 */
void display_manager_set_text(const char *line1, const char *line2);

/*
 * Advance the scroll animation by one step.
 * Call this periodically (e.g., every 300ms) from the main loop.
 * No-op if no lines are currently scrolling.
 */
void display_manager_tick(void);

/*
 * Force an immediate display refresh (useful after set_text).
 * Normally set_text already sends to display, so this is for
 * re-sending the current state.
 */
void display_manager_refresh(void);

/*
 * Copy the current (full, un-scrolled) display text into the caller's
 * buffers. Either pointer may be NULL. Useful for surfacing the VFD in the
 * web API / dashboard so plugins (e.g. games) are visible remotely.
 */
void display_manager_get_text(char *line1, size_t line1_size,
                              char *line2, size_t line2_size);

#endif /* DISPLAY_MANAGER_H */
