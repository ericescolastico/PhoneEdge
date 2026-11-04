#ifndef SCRCPY_CLI_H
#define SCRCPY_CLI_H

#include "common.h"

#include <stdbool.h>

#include "options.h"

struct scrcpy_cli_args {
    struct scrcpy_options opts;
    bool help;
    bool version;
    bool pause_on_exit;
};

void
scrcpy_print_usage(const char *arg0);

bool
scrcpy_parse_args(struct scrcpy_cli_args *args, int argc, char *argv[]);

// Apply the content of a .ini file to the options (exposed for the tests)
bool
scrcpy_load_config_text(struct scrcpy_options *opts, const char *text,
                        char *err, unsigned errlen);

#endif
