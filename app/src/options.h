#ifndef SCRCPY_OPTIONS_H
#define SCRCPY_OPTIONS_H

#include "common.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "edge/pe_config.h"

enum sc_log_level {
    SC_LOG_LEVEL_VERBOSE,
    SC_LOG_LEVEL_DEBUG,
    SC_LOG_LEVEL_INFO,
    SC_LOG_LEVEL_WARN,
    SC_LOG_LEVEL_ERROR,
};

struct sc_port_range {
    uint16_t first;
    uint16_t last;
};

struct scrcpy_options {
    // Connection to the device
    const char *serial;
    enum sc_log_level log_level;
    bool log_level_set; // explicitly requested (else --debug-log implies debug)
    struct sc_port_range port_range;
    uint32_t tunnel_host;
    uint16_t tunnel_port;
    bool force_adb_forward;
    bool kill_adb_on_close;
    bool stay_awake;

    // Edge handoff
    struct pe_config edge;
    const char *config_path;  // .ini file (NULL: phoneedge.ini next to the exe)
    const char *debug_log;    // file receiving a copy of the logs
    const char *script_path;  // input from a script instead of the PC hooks
    bool dry_run;             // no phone: only test the PC side
    bool list_monitors;
};

// Fill `opts` with the default values
void
scrcpy_options_init(struct scrcpy_options *opts);

#endif
