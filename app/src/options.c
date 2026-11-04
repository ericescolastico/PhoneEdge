#include "options.h"

const struct scrcpy_options scrcpy_options_default = {
    .serial = NULL,
    .log_level = SC_LOG_LEVEL_INFO,
    .port_range = {
        .first = DEFAULT_LOCAL_PORT_RANGE_FIRST,
        .last = DEFAULT_LOCAL_PORT_RANGE_LAST,
    },
    .tunnel_host = 0,
    .tunnel_port = 0,
    .force_adb_forward = false,
    .kill_adb_on_close = false,
    .stay_awake = false,
};
