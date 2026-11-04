#include "cli.h"

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/log.h"
#include "util/net.h"
#include "util/str.h"

#define PE_MAX_CONFIG_SIZE (256 * 1024)

enum {
    OPT_PORT = 1000,
    OPT_TUNNEL_HOST,
    OPT_TUNNEL_PORT,
    OPT_FORCE_ADB_FORWARD,
    OPT_KILL_ADB_ON_CLOSE,
    OPT_PAUSE_ON_EXIT,
    OPT_DEBUG_LOG,
    OPT_SCRIPT,
    OPT_DRY_RUN,
    OPT_LIST_MONITORS,
    // The edge settings (see EDGE_SETTINGS), one option each
    OPT_EDGE_BASE = 2000,
};

// The edge settings that can be given on the command line: the same names as
// in the .ini file (with '-' instead of '_')
static const char *const EDGE_SETTINGS[] = {
    "edge",
    "monitor",
    "phone-size",
    "phone-rotation",
    "pointer-scale",
    "sensitivity",
    "wheel-scale",
    "hotkey",
    "edge-push",
    "leave-margin",
    "entry-inset",
    "return-inset",
    "flush-ms",
    "enter-guard-ms",
    "reenter-ms",
    "anchor-extra",
    "place-delay-ms",
    "clip-cursor",
};
#define EDGE_SETTINGS_COUNT (sizeof(EDGE_SETTINGS) / sizeof(EDGE_SETTINGS[0]))

#define EDGE_OPT(name, index) {name, required_argument, NULL, OPT_EDGE_BASE + index}

static const struct option long_options[] = {
    {"config",             required_argument, NULL, 'c'},
    {"debug-log",          required_argument, NULL, OPT_DEBUG_LOG},
    {"dry-run",            no_argument,       NULL, OPT_DRY_RUN},
    {"force-adb-forward",  no_argument,       NULL, OPT_FORCE_ADB_FORWARD},
    {"help",               no_argument,       NULL, 'h'},
    {"kill-adb-on-close",  no_argument,       NULL, OPT_KILL_ADB_ON_CLOSE},
    {"list-monitors",      no_argument,       NULL, OPT_LIST_MONITORS},
    {"pause-on-exit",      no_argument,       NULL, OPT_PAUSE_ON_EXIT},
    {"port",               required_argument, NULL, OPT_PORT},
    {"script",             required_argument, NULL, OPT_SCRIPT},
    {"serial",             required_argument, NULL, 's'},
    {"stay-awake",         no_argument,       NULL, 'w'},
    {"tunnel-host",        required_argument, NULL, OPT_TUNNEL_HOST},
    {"tunnel-port",        required_argument, NULL, OPT_TUNNEL_PORT},
    {"verbosity",          required_argument, NULL, 'V'},
    {"version",            no_argument,       NULL, 'v'},
    EDGE_OPT("edge", 0),
    EDGE_OPT("monitor", 1),
    EDGE_OPT("phone-size", 2),
    EDGE_OPT("phone-rotation", 3),
    EDGE_OPT("pointer-scale", 4),
    EDGE_OPT("sensitivity", 5),
    EDGE_OPT("wheel-scale", 6),
    EDGE_OPT("hotkey", 7),
    EDGE_OPT("edge-push", 8),
    EDGE_OPT("leave-margin", 9),
    EDGE_OPT("entry-inset", 10),
    EDGE_OPT("return-inset", 11),
    EDGE_OPT("flush-ms", 12),
    EDGE_OPT("enter-guard-ms", 13),
    EDGE_OPT("reenter-ms", 14),
    EDGE_OPT("anchor-extra", 15),
    EDGE_OPT("place-delay-ms", 16),
    EDGE_OPT("clip-cursor", 17),
    {NULL,                 0,                 NULL, 0},
};

void
scrcpy_print_usage(const char *arg0) {
    printf(
"Uso: %s [opções]\n"
"\n"
"Move o mouse e o teclado do PC para um celular Android ao empurrar o cursor\n"
"pela borda do monitor. Configuração: arquivo .ini (--config) e/ou opções\n"
"abaixo (as opções têm prioridade sobre o arquivo).\n"
"\n"
"Geral:\n"
"    -h, --help                  Mostra esta ajuda.\n"
"    -v, --version               Mostra a versão.\n"
"    -c, --config=ARQUIVO        Arquivo .ini (padrão: phoneedge.ini ao lado do\n"
"                                executável, se existir).\n"
"        --list-monitors         Lista os monitores e seus números.\n"
"        --dry-run               Sem celular: testa só o lado do PC (borda,\n"
"                                retorno, atalho), registrando o que seria enviado.\n"
"        --script=ARQUIVO        Entrada vinda de um roteiro, em vez do mouse e\n"
"                                teclado do PC (teste do celular sem ganchos).\n"
"        --debug-log=ARQUIVO     Grava um log detalhado (entrada/saída de borda,\n"
"                                posição estimada, erros) para diagnóstico.\n"
"    -V, --verbosity=NÍVEL       verbose, debug, info, warn ou error.\n"
"        --pause-on-exit         Espera Enter antes de fechar a janela.\n"
"\n"
"Celular (adb):\n"
"    -s, --serial=SÉRIE          Número de série do celular (adb devices).\n"
"    -w, --stay-awake            Mantém o celular com a tela ligada.\n"
"        --port=PORTA[:PORTA]    Faixa de portas TCP locais do túnel adb.\n"
"        --force-adb-forward     Usa \"adb forward\" em vez de \"adb reverse\".\n"
"        --tunnel-host=IP        IP do host do túnel adb.\n"
"        --tunnel-port=PORTA     Porta do túnel adb.\n"
"        --kill-adb-on-close     Encerra o servidor adb ao sair.\n"
"\n"
"Borda e ponteiro (os mesmos nomes do .ini, com '-' no lugar de '_'):\n"
"        --edge=left|right|top|bottom   Borda do monitor (padrão: right).\n"
"        --monitor=N                    Monitor (0 = principal; veja --list-monitors).\n"
"        --phone-size=auto|LxA          Tamanho da tela do celular (padrão: auto,\n"
"                                       lido com adb; fallback 1080x2340).\n"
"        --phone-rotation=0|90|180|270  Rotação, se o tamanho for manual.\n"
"        --pointer-scale=X              Pixels do celular por unidade enviada (>= 0.25;\n"
"                                       calibração, padrão 1.0).\n"
"        --sensitivity=X                Unidades enviadas por contagem do mouse.\n"
"        --wheel-scale=X                Multiplicador da roda.\n"
"        --hotkey=ctrl+alt+pause        Atalho de emergência.\n"
"        --edge-push, --leave-margin, --entry-inset, --return-inset, --flush-ms,\n"
"        --enter-guard-ms, --reenter-ms, --anchor-extra, --place-delay-ms,\n"
"        --clip-cursor=yes|no           Ajustes finos (veja o phoneedge.ini).\n",
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

// ---------------------------------------------------------------------------
// .ini file

static void
normalize(const char *in, char *out, size_t len) {
    size_t i = 0;
    for (; in[i] && i + 1 < len; ++i) {
        char c = in[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char) (c - 'A' + 'a');
        }
        out[i] = c == '-' ? '_' : c;
    }
    out[i] = '\0';
}

// Settings of the .ini that are not edge settings
static bool
ini_apply(void *userdata, const char *key_in, const char *value, char *err,
          unsigned errlen) {
    struct scrcpy_options *opts = userdata;

    enum pe_set_result r = pe_config_set(&opts->edge, key_in, value, err,
                                         errlen);
    if (r == PE_SET_OK) {
        return true;
    }
    if (r == PE_SET_INVALID_VALUE) {
        return false;
    }

    char key[40];
    normalize(key_in, key, sizeof(key));

    if (!strcmp(key, "serial")) {
        opts->serial = *value ? strdup(value) : NULL;
        return true;
    }
    if (!strcmp(key, "stay_awake")) {
        if (!pe_parse_bool(value, &opts->stay_awake)) {
            snprintf(err, errlen, "stay_awake: expected yes or no");
            return false;
        }
        return true;
    }
    if (!strcmp(key, "force_adb_forward")) {
        if (!pe_parse_bool(value, &opts->force_adb_forward)) {
            snprintf(err, errlen, "force_adb_forward: expected yes or no");
            return false;
        }
        return true;
    }
    if (!strcmp(key, "kill_adb_on_close")) {
        if (!pe_parse_bool(value, &opts->kill_adb_on_close)) {
            snprintf(err, errlen, "kill_adb_on_close: expected yes or no");
            return false;
        }
        return true;
    }
    if (!strcmp(key, "verbosity")) {
        if (!parse_log_level(value, &opts->log_level)) {
            snprintf(err, errlen, "verbosity: invalid level \"%s\"", value);
            return false;
        }
        opts->log_level_set = true;
        return true;
    }
    if (!strcmp(key, "port")) {
        if (!parse_port_range(value, &opts->port_range)) {
            snprintf(err, errlen, "port: invalid value \"%s\"", value);
            return false;
        }
        return true;
    }

    snprintf(err, errlen, "unknown setting \"%s\"", key_in);
    return false;
}

bool
scrcpy_load_config_text(struct scrcpy_options *opts, const char *text,
                        char *err, unsigned errlen) {
    return pe_ini_parse(text, ini_apply, opts, err, errlen);
}

static bool
load_config_file(struct scrcpy_options *opts, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        LOGE("Could not open the configuration file %s: %s", path,
             strerror(errno));
        return false;
    }

    char *text = malloc(PE_MAX_CONFIG_SIZE + 1);
    if (!text) {
        LOG_OOM();
        fclose(f);
        return false;
    }
    size_t n = fread(text, 1, PE_MAX_CONFIG_SIZE + 1, f);
    fclose(f);
    if (n > PE_MAX_CONFIG_SIZE) {
        LOGE("The configuration file %s is too big", path);
        free(text);
        return false;
    }
    text[n] = '\0';

    char err[256];
    bool ok = scrcpy_load_config_text(opts, text, err, sizeof(err));
    free(text);
    if (!ok) {
        LOGE("%s: %s", path, err);
        return false;
    }
    LOGI("Configuration loaded from %s", path);
    return true;
}

// Find --config / -c in the arguments, before the real parsing: the file must
// be applied first so that the command line overrides it
static const char *
find_config_arg(int argc, char *argv[]) {
    const char *found = NULL;
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!strcmp(a, "--")) {
            break;
        }
        if (!strncmp(a, "--config=", 9)) {
            found = a + 9;
        } else if (!strcmp(a, "--config") || !strcmp(a, "-c")) {
            if (i + 1 < argc) {
                found = argv[++i];
            }
        } else if (!strncmp(a, "-c", 2) && a[2] && a[2] != '-') {
            found = a + 2;
        }
    }
    return found;
}

bool
scrcpy_parse_args(struct scrcpy_cli_args *args, int argc, char *argv[]) {
    struct scrcpy_options *opts = &args->opts;

    const char *explicit_config = find_config_arg(argc, argv);
    if (explicit_config) {
        opts->config_path = explicit_config;
    }
    if (opts->config_path && !load_config_file(opts, opts->config_path)) {
        return false;
    }

    optind = 0; // reset to start from the beginning
    opterr = 0; // do not print getopt errors, we log our own

    int c;
    while ((c = getopt_long(argc, argv, "c:hs:vV:w", long_options, NULL))
            != -1) {
        if (c >= OPT_EDGE_BASE
                && c < OPT_EDGE_BASE + (int) EDGE_SETTINGS_COUNT) {
            char err[256];
            const char *name = EDGE_SETTINGS[c - OPT_EDGE_BASE];
            if (pe_config_set(&opts->edge, name, optarg, err, sizeof(err))
                    != PE_SET_OK) {
                LOGE("--%s: %s", name, err);
                return false;
            }
            continue;
        }

        switch (c) {
            case 'c':
                // already applied
                break;
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
                opts->log_level_set = true;
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
            case OPT_DEBUG_LOG:
                opts->debug_log = optarg;
                break;
            case OPT_SCRIPT:
                opts->script_path = optarg;
                break;
            case OPT_DRY_RUN:
                opts->dry_run = true;
                break;
            case OPT_LIST_MONITORS:
                opts->list_monitors = true;
                break;
            default:
                LOGE("Opção desconhecida ou inválida (use --help)");
                return false;
        }
    }

    if (optind < argc) {
        LOGE("Argumento inesperado: %s", argv[optind]);
        return false;
    }

    // Detailed logs are the point of --debug-log
    if (opts->debug_log && !opts->log_level_set) {
        opts->log_level = SC_LOG_LEVEL_DEBUG;
    }

    return true;
}
