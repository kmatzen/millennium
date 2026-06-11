#include "cli.h"

#include <string.h>
#include <stddef.h>

/* Strip any leading directory components from argv[0] so usage reads as the
   bare program name regardless of how it was invoked. */
static const char* cli_basename(const char* prog) {
    const char* slash;
    if (prog == NULL || prog[0] == '\0') {
        return "millennium-daemon";
    }
    slash = strrchr(prog, '/');
    return slash ? slash + 1 : prog;
}

static void cli_set_error(cli_options_t* opts, const char* fmt_arg, const char* arg) {
    /* No printf-family formatting needed: messages are "<text><arg>" at most. */
    size_t n;
    opts->mode = CLI_MODE_ERROR;
    opts->error[0] = '\0';
    strncpy(opts->error, fmt_arg, sizeof(opts->error) - 1);
    opts->error[sizeof(opts->error) - 1] = '\0';
    if (arg != NULL) {
        n = strlen(opts->error);
        strncpy(opts->error + n, arg, sizeof(opts->error) - 1 - n);
        opts->error[sizeof(opts->error) - 1] = '\0';
    }
}

void cli_parse_args(int argc, char* const argv[], cli_options_t* opts) {
    int i;

    opts->mode = CLI_MODE_RUN;
    opts->config_file = NULL;
    opts->error[0] = '\0';

    for (i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            opts->mode = CLI_MODE_HELP;
            return;
        }
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            opts->mode = CLI_MODE_VERSION;
            return;
        }
        if (strcmp(arg, "-c") == 0 || strcmp(arg, "--config") == 0) {
            if (i + 1 >= argc) {
                cli_set_error(opts, "missing path after ", arg);
                return;
            }
            opts->config_file = argv[++i];
            continue;
        }
        if (strncmp(arg, "--config=", 9) == 0) {
            if (arg[9] == '\0') {
                cli_set_error(opts, "missing path after ", "--config=");
                return;
            }
            opts->config_file = arg + 9;
            continue;
        }

        cli_set_error(opts, "unknown argument: ", arg);
        return;
    }
}

void cli_print_usage(FILE* out, const char* prog) {
    const char* name = cli_basename(prog);
    fprintf(out, "Usage: %s [options]\n", name);
    fprintf(out, "\n");
    fprintf(out, "Millennium payphone daemon.\n");
    fprintf(out, "\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -c, --config <path>   Read configuration from <path>\n");
    fprintf(out, "                        (default: /etc/millennium/daemon.conf)\n");
    fprintf(out, "  -v, --version         Print version and build info, then exit\n");
    fprintf(out, "  -h, --help            Print this help, then exit\n");
}
