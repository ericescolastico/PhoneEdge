#include "options.h"

void
scrcpy_options_init(struct scrcpy_options *opts) {
    *opts = (struct scrcpy_options) {
        .serial = NULL,
        .log_level = SC_LOG_LEVEL_INFO,
        .log_level_set = false,
        .port_range = {
            .first = DEFAULT_LOCAL_PORT_RANGE_FIRST,
            .last = DEFAULT_LOCAL_PORT_RANGE_LAST,
        },
        .tunnel_host = 0,
        .tunnel_port = 0,
        .force_adb_forward = false,
        .kill_adb_on_close = false,
        .stay_awake = false,
        .config_path = NULL,
        .debug_log = NULL,
        .script_path = NULL,
        .dry_run = false,
        .list_monitors = false,
    };
    pe_config_defaults(&opts->edge);
}
