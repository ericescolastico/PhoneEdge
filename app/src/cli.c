#include "cli.h"

#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/log.h"
#include "util/net.h"
#include "util/str.h"

enum {
    OPT_PORT = 1000,
    OPT_TUNNEL_HOST,
    OPT_TUNNEL_PORT,
    OPT_FORCE_ADB_FORWARD,
    OPT_KILL_ADB_ON_CLOSE,
    OPT_PAUSE_ON_EXIT,
};

static const struct option long_options[] = {
    {"force-adb-forward",  no_argument,       NULL, OPT_FORCE_ADB_FORWARD},
    {"help",               no_argument,       NULL, 'h'},
    {"kill-adb-on-close",  no_argument,       NULL, OPT_KILL_ADB_ON_CLOSE},
    {"pause-on-exit",      no_argument,       NULL, OPT_PAUSE_ON_EXIT},
    {"port",               required_argument, NULL, OPT_PORT},
    {"serial",             required_argument, NULL, 's'},
    {"stay-awake",         no_argument,       NULL, 'w'},
    {"tunnel-host",        required_argument, NULL, OPT_TUNNEL_HOST},
    {"tunnel-port",        required_argument, NULL, OPT_TUNNEL_PORT},
    {"verbosity",          required_argument, NULL, 'V'},
    {"version",            no_argument,       NULL, 'v'},
    {NULL,                 0,                 NULL, 0},
};

void
scrcpy_print_usage(const char *arg0) {
    printf(
"Usage: %s [options]\n"
"\n"
"Options:\n"
"    -h, --help                  Show this help.\n"
"    -v, --version               Show the version.\n"
"    -s, --serial=SERIAL         Serial number of the Android device.\n"
"    -w, --stay-awake            Keep the device awake while connected.\n"
"    -V, --verbosity=LEVEL       verbose, debug, info, warn or error.\n"
"        --port=PORT[:PORT]      Local TCP port range for the adb tunnel.\n"
"        --force-adb-forward     Use \"adb forward\" instead of \"adb reverse\".\n"
"        --tunnel-host=IP        IP of the adb tunnel host.\n"
"        --tunnel-port=PORT      Port of the adb tunnel.\n"
"        --kill-adb-on-close     Kill the adb server on exit.\n"
"        --pause-on-exit         Wait for Enter before exiting.\n",
    arg0);
}

static bool
parse_integer_arg(const char *s, long *out, long min, long max,
                  const char *name) {
    long value;
    if (!sc_str_parse_integer(s, &value)) {
        LOGE("Could not parse %s: %s", name, s);
        return false;
    }
    if (value < min || value > max) {
        LOGE("Could not parse %s: value (%ld) out-of-range (%ld; %ld)",
             name, value, min, max);
        return false;
    }
    *out = value;
    return true;
}

static bool
parse_port_range(const char *s, struct sc_port_range *port_range) {
    long values[2];
    size_t count = sc_str_parse_integers(s, ':', 2, values);
    if (!count) {
        LOGE("Could not parse port: %s", s);
        return false;
    }

    for (size_t i = 0; i < count; ++i) {
        if (values[i] < 0 || values[i] > 0xFFFF) {
            LOGE("Could not parse port: value (%ld) out-of-range (0; 65535)",
                 values[i]);
            return false;
        }
    }

    uint16_t v0 = (uint16_t) values[0];
    if (count == 1) {
        port_range->first = v0;
        port_range->last = v0;
        return true;
    }

    assert(count == 2);
    uint16_t v1 = (uint16_t) values[1];
    port_range->first = MIN(v0, v1);
    port_range->last = MAX(v0, v1);
    return true;
}

static bool
parse_log_level(const char *s, enum sc_log_level *log_level) {
    static const struct {
        const char *name;
        enum sc_log_level level;
    } levels[] = {
        {"verbose", SC_LOG_LEVEL_VERBOSE},
        {"debug", SC_LOG_LEVEL_DEBUG},
        {"info", SC_LOG_LEVEL_INFO},
        {"warn", SC_LOG_LEVEL_WARN},
        {"error", SC_LOG_LEVEL_ERROR},
    };
    for (size_t i = 0; i < ARRAY_LEN(levels); ++i) {
        if (!strcmp(s, levels[i].name)) {
            *log_level = levels[i].level;
            return true;
        }
    }

    LOGE("Could not parse log level: %s", s);
    return false;
}

bool
scrcpy_parse_args(struct scrcpy_cli_args *args, int argc, char *argv[]) {
    struct scrcpy_options *opts = &args->opts;

    optind = 0; // reset to start from the beginning
    opterr = 0; // do not print getopt errors, we log our own

    int c;
    while ((c = getopt_long(argc, argv, "hs:vV:w", long_options, NULL)) != -1) {
        switch (c) {
            case 'h':
                args->help = true;
                break;
            case 'v':
                args->version = true;
                break;
            case 's':
                opts->serial = optarg;
                break;
            case 'w':
                opts->stay_awake = true;
                break;
            case 'V':
                if (!parse_log_level(optarg, &opts->log_level)) {
                    return false;
                }
                break;
            case OPT_PORT:
                if (!parse_port_range(optarg, &opts->port_range)) {
                    return false;
                }
                break;
            case OPT_FORCE_ADB_FORWARD:
                opts->force_adb_forward = true;
                break;
            case OPT_TUNNEL_HOST:
                if (!net_parse_ipv4(optarg, &opts->tunnel_host)) {
                    LOGE("Invalid tunnel host: %s", optarg);
                    return false;
                }
                break;
            case OPT_TUNNEL_PORT: {
                long value;
                if (!parse_integer_arg(optarg, &value, 0, 0xFFFF,
                                       "tunnel port")) {
                    return false;
                }
                opts->tunnel_port = (uint16_t) value;
                break;
            }
            case OPT_KILL_ADB_ON_CLOSE:
                opts->kill_adb_on_close = true;
                break;
            case OPT_PAUSE_ON_EXIT:
                args->pause_on_exit = true;
                break;
            default:
                LOGE("Unexpected or invalid option (use --help)");
                return false;
        }
    }

    if (optind < argc) {
        LOGE("Unexpected additional argument: %s", argv[optind]);
        return false;
    }

    return true;
}
