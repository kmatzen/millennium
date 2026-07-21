#ifndef CONTROL_COMMANDS_H
#define CONTROL_COMMANDS_H

/* Parses and executes one POST /api/control command string, e.g.
 * "keypad_press:5" or "reset_system". Returns 1 on success, 0 on a rejected
 * or unrecognized command (bad argument, guard not satisfied, NULL input).
 *
 * Pulled out of daemon.c (#248) so the actual web-control dispatch logic --
 * previously only reachable by hand-testing against real hardware or the web
 * portal -- can be linked into the simulator and unit tests instead of being
 * hand-mirrored there. Operates on the same daemon_state / event_processor /
 * daemon_state_mutex globals each binary (daemon, simulator, unit_tests)
 * already defines for itself; this file only takes `extern` references to
 * them, same convention as plugins.c / plugin_sdk.c.
 *
 * Not thread-safe on its own -- daemon.c's send_control_command() serializes
 * calls under engine_mutex against the main loop. Callers outside daemon.c
 * (tests, the simulator) are single-threaded, so no extra locking is needed
 * there. */
int dispatch_control_command(const char *action);

#endif /* CONTROL_COMMANDS_H */
