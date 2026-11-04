#ifndef SCRCPY_OPTIONS_H
#define SCRCPY_OPTIONS_H

#include "common.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

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
    const char *serial;
    enum sc_log_level log_level;
    struct sc_port_range port_range;
    uint32_t tunnel_host;
    uint16_t tunnel_port;
    bool force_adb_forward;
    bool kill_adb_on_close;
    bool stay_awake;
};

extern const struct scrcpy_options scrcpy_options_default;

#endif
