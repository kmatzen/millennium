#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include "millennium_sdk.h"

#define DISPLAY_WIDTH 20
#define DISPLAY_SCROLL_GAP 3  /* spaces between end and start during scroll */
/*
 * Ticks to pause with the start of a long line held in view before the line
 * begins scrolling. The hold is re-armed every time the scroll wraps back to
 * the beginning, so on each loop the reader gets a readable beat at the start
 * of the message instead of it sliding away immediately. At the ~300ms tick
 * cadence, 4 ticks is a ~1.2s pause.
 */
#define DISPLAY_SCROLL_HOLD_TICKS 4

/*
 * Initialize the display manager with the SDK client handle.
 * Must be called before any other display_manager functions.
 */
void display_manager_init(millennium_client_t *client);

/*
 * Set display text. Lines longer than 20 characters scroll automatically on
 * each tick, pausing briefly (DISPLAY_SCROLL_HOLD_TICKS) with the start of the
 * line in view at the beginning of every loop so it stays readable. Short
 * lines are displayed statically. Passing NULL for a line clears that line.
 *
 * Control characters (bytes below 0x20, plus DEL 0x7F) in the supplied text
 * are replaced with spaces before storage: the VFD treats those bytes as
 * commands or line breaks, so they would otherwise corrupt the display. Plugin
 * authors can therefore pass arbitrary text without sanitizing it themselves.
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
