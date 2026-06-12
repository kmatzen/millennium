#ifndef CLI_H
#define CLI_H

#include <stdio.h>

/* What the parsed command line tells main() to do. */
typedef enum {
    CLI_MODE_RUN = 0,   /* start the daemon normally */
    CLI_MODE_HELP,      /* print usage and exit 0 */
    CLI_MODE_VERSION,   /* print version and exit 0 */
    CLI_MODE_ERROR      /* bad arguments; print error+usage, exit 2 */
} cli_mode_t;

typedef struct {
    cli_mode_t mode;
    const char* config_file;   /* path from --config, or NULL for the default */
    char error[128];           /* human-readable reason when mode == CLI_MODE_ERROR */
} cli_options_t;

/* Parse argv into opts. Never exits or prints; the caller inspects opts.mode
   and acts. Recognizes --help/-h, --version/-v, and --config <path> (also the
   --config=<path> form). The first --help/--version wins; an unknown flag or a
   --config with no value yields CLI_MODE_ERROR with a message in opts.error. */
void cli_parse_args(int argc, char* const argv[], cli_options_t* opts);

/* Write the usage/help text to `out`. `prog` is argv[0] (NULL falls back to a
   sensible default program name). */
void cli_print_usage(FILE* out, const char* prog);

#endif /* CLI_H */
