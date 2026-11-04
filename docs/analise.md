# PhoneEdge — Fase 0: análise e decisões

Escopo: spikes e leitura de código. Nenhuma linha de código do cliente foi alterada nesta fase.
Base: fork do scrcpy **v5.0.1** (C11, SDL3, meson). Branch base do fork: `master`.

## 1. Resumo das decisões

| # | Decisão do prompt | Veredito | Observação |
|---|---|---|---|
| 1 | Entrada no celular via `--mouse=uhid` / `--keyboard=uhid` | **Mantida** | Nenhum driver USB no Windows; usa só ADB. Ver §4 para as armadilhas. |
| 2 | Estimar posição do ponteiro somando deltas | **Mantida** | HID é relativo e o Android aplica aceleração; haverá deriva (§6, R3). |
| 3 | Captura global: Raw Input + `WH_MOUSE_LL` | **Mantida, com 1 risco a validar no Windows** | Se o Raw Input continuar chegando com o mouse engolido pelo hook (esperado, mas não verificável aqui) está tudo certo; plano B no §5.3. |
| 4 | Captura dentro do loop SDL? | **Não: módulo separado, em thread própria** | Ver §5.1. |
| 5 | Vídeo | **`--no-video --no-audio --no-window` (só controle)** | Ver §3. Sem janela, sem decodificação, e dá para remover o FFmpeg inteiro na Fase 1. |
| 6 | Server pré-compilado v5.0.1 | **Mantido** | Release existe e é baixável do GitHub (HTTP 200 verificado). Sem mudança no servidor Java. |
| 7 | AOA | **Fora do protótipo** | Só adicionaria libusb + troca de driver (Zadig). |

Nenhuma decisão caiu. Ajustes de detalhe que **mudam o plano**:

- **Sem janela, o `input_manager` não existe.** Ele é instanciado dentro de `sc_screen_init()`, que só roda se `options->window` (`scrcpy.c:815-848`). Logo o nosso módulo chama os *processors* (`kp`/`mp`) diretamente, não passa por SDL. Isso é uma simplificação, não um problema (§3).
- **`hid_mouse.c` trunca deltas em ±127** (`hid_mouse.c:182-183`, `CLAMP`), não divide. O nosso módulo precisa fatiar deltas grandes em vários relatórios (necessário também para a âncora).
- **A fila de controle tem limite de 60 mensagens e `UHID_INPUT` é descartável** (`controller.c:8`, `control_msg.c:358-366`). Mouse a 1000 Hz estoura a fila. É preciso agregar deltas e enviar a ≤ ~125–250 Hz.
- **`Ctrl+Alt+Pause` é, na verdade, Ctrl+Break** para o Windows (com Ctrl pressionado a tecla Pause gera `VK_CANCEL`, scancode 0xE0 0x46, e não `VK_PAUSE`). O detector deve aceitar os dois. Além disso, num console, Ctrl+Break dispara `CTRL_BREAK_EVENT` e o handler existente encerra o app (`scrcpy.c:100-107`), por isso o hook precisa engolir a combinação.
- **Para os testes em Linux**, o SDL3 não está no apt do Ubuntu 24.04. Foi compilado a partir do tarball do GitHub (3.2.20) no ambiente de desenvolvimento. O CI do Windows usa o pacote MSYS2.

## 2. Estado do build (verificado)

Linux (Ubuntu 24.04, ambiente de nuvem):

```bash
# SDL3 3.2.20 compilado de https://github.com/libsdl-org/SDL (release-3.2.20), prefixo local
export PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig LD_LIBRARY_PATH=$PREFIX/lib
meson setup build -Dcompile_server=false
ninja -C build
meson test -C build        # Ok: 12  Fail: 0
```

Pacotes apt usados: `libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libavdevice-dev libusb-1.0-0-dev libdrm-dev libva-dev` + `meson` (pip).
O upstream exige `libavdevice`, `libdrm` e `libva` só por causa de v4l2/VA-API; todos vão sair na Fase 1.

### Dependências do build Windows

Upstream (`release/build_windows.sh`): SDL3, FFmpeg, dav1d, libusb e `adb` (platform-tools), todos compilados por scripts em `app/deps/`.
Problema: `ffmpeg.org`, `libsdl.org` e `dl.google.com` **não** são acessíveis da nuvem (HTTP 000 verificado), mas só importam para o CI, que roda no runner do GitHub com internet.

Plano para o CI (Fase 4), bem mais leve que o upstream:

| Item | Origem | Motivo |
|---|---|---|
| Compilador, meson, ninja | MSYS2 (`mingw-w64-x86_64-toolchain`, `meson`) | `windows-latest` já tem MSYS2 |
| SDL3 | pacote MSYS2 (`mingw-w64-x86_64-SDL3`) | usado só para threads, eventos e timers (§3) |
| `scrcpy-server` | `github.com/Genymobile/scrcpy/releases/download/v5.0.1/scrcpy-server-v5.0.1` | verificado HTTP 200 |
| `adb.exe` + `AdbWinApi.dll` + `AdbWinUsbApi.dll` | `scrcpy-win64-v5.0.1.zip` do mesmo release (HTTP 200) ou `dl.google.com` no runner | evita compilar/baixar platform-tools à parte; conferir licença |
| FFmpeg, dav1d, libusb | **não necessários** depois da Fase 1 | sem vídeo, sem áudio, sem AOA |

## 3. `--no-video`: o que muda

Avaliadas duas opções:

- **A. Vídeo ligado, janela minimizada.** Mantém FFmpeg, decoder, OpenGL/D3D, `screen.c`, e uma janela que o usuário pode fechar sem querer. O decoder gasta CPU e o ganho (saber o tamanho/rotação da tela) é obtido de graça pelo `adb`.
- **B. `--no-video --no-audio --no-window` (escolhida).** O servidor só abre o socket de controle (`server.c:692-693`: sem vídeo e sem áudio o primeiro socket já é o de controle). A conexão ADB, o envio do servidor e o canal de controle seguem iguais.

Consequências de B, todas verificadas no código:

1. `options->window == false` ⇒ `sc_screen_init()` não roda ⇒ **não há `input_manager`** ⇒ o nosso módulo conversa direto com `sc_mouse_uhid` / `sc_keyboard_uhid` (criados em `scrcpy.c:764-790`, independentemente de haver janela).
2. `cli.c:3156-3159`: sem `video_playback`, o mouse `auto` vira `disabled` e `--mouse=sdk` é recusado. `--mouse=uhid` é aceito. Nada a mudar na CLI.
3. A desconexão do celular continua sendo detectada: o receiver do canal de controle empurra `SC_EVENT_DEVICE_DISCONNECTED` (`scrcpy.c:250-255`), tratado em `event_loop()` (`scrcpy.c:125`). Esse é o gatilho para devolver o mouse ao PC (R5).
4. **Tamanho/rotação:** o servidor não envia tamanho sem vídeo. Fica por conta do cliente: `adb shell wm size` (formato `Physical size: 1080x2340`, com possível linha `Override size:` que vale mais). Rotação: leitura opcional (`dumpsys`) e **a validar no S22**; padrão: retrato, parâmetro `phone_rotation` e `phone_size` manuais. Parser em C puro, testável em Linux no estilo de `app/tests/test_adb_parser.c`.
5. `sc_keyboard_uhid_synchronize_mod()` usa `SDL_GetModState()` (`keyboard_uhid.c:34`), que sem janela de vídeo não reflete o teclado real. Impacto: o LED de Caps/Num pode ficar dessincronizado. Não bloqueia; tratar na Fase 3 passando o estado de mods vindo do Windows.
6. O vídeo some, então **o usuário não vê a tela do celular no PC** (só no próprio aparelho, ao lado do monitor, que é o cenário pedido).

## 4. Fluxo real de eventos (upstream)

```
SDL event ──► sc_screen_handle_event()            screen.c
                 ├─► sc_mouse_capture_handle_event()   mouse_capture.c:20   (modo relativo SDL)
                 └─► sc_input_manager_handle_event()   input_manager.c:1167
                        ├─ MOUSE_MOTION  → process_mouse_motion  :747  → mp->ops->process_mouse_motion
                        ├─ MOUSE_BUTTON  → process_mouse_button  :837  → mp->ops->process_mouse_click
                        ├─ MOUSE_WHEEL   → process_mouse_wheel   :1010 → mp->ops->process_mouse_scroll
                        └─ KEY_DOWN/UP   → process_key           :~700 → kp->ops->process_key
mp = sc_mouse_uhid                              uhid/mouse_uhid.c
        └─► hid_mouse.c: relatório de 5 bytes [botões, dx, dy, roda, pan]  (dx,dy com CLAMP ±127)
        └─► sc_controller_push_msg(UHID_INPUT)      controller.c:96   (fila de 60, mutex, descartável)
                └─► thread do controller → socket de controle → servidor Android → /dev/uhid
```

Pontos que importam para o protótipo:

- **O SDL só entra na ponta de entrada.** Do `mp`/`kp` para baixo (UHID + controller) não há dependência de janela. É por onde o nosso módulo vai se plugar.
- `mouse_capture.c` só liga `SDL_SetWindowRelativeMouseMode` (precisa de janela com foco) — **não serve** para o nosso caso (captura global fora de uma janela).
- `sc_mouse_uhid_init()` envia `UHID_CREATE` uma vez na inicialização; o dispositivo virtual existe desde o início, mesmo com o controle no PC. O Android pode mostrar o ponteiro do mouse mesmo antes da transferência; **a validar** (ver R6).
- Teclado: `sc_key_event` usa `scancode` no padrão USB HID (`SC_SCANCODE_* = SDL_SCANCODE_*`, `input_events.h:198`). O keyboard UHID só precisa de `scancode` + `action` + `mods`; precisamos de uma tabela **Windows scancode (set 1) → USB HID**, em C puro e testável.
- A rolagem em `hid_mouse.c:211` acumula resíduo e trabalha em "cliques"; Raw Input/`WM_MOUSEWHEEL` usa 120 por clique, então dividimos por 120.

## 5. A captura global cabe no loop SDL?

### 5.1 Não. Módulo separado, em thread própria.

- O SDL não oferece captura global de mouse; o modo relativo precisa de uma janela com foco, que não existe em `--no-window` e que perderíamos exatamente quando o usuário empurra o mouse para fora do monitor.
- Raw Input (`WM_INPUT`) exige um `HWND` (janela message-only) com **message loop próprio**; hooks `WH_*_LL` também exigem message loop na thread que os instala. `SDL_WaitEvent()` na thread principal não bombeia essas mensagens do jeito que precisamos e bloquearia o hook.
- O hook deve ser curto: o Windows remove hooks que passam do `LowLevelHooksTimeout` (configurável no registro; ~300 ms por padrão, do que me lembro: **a validar**). Por isso a thread do hook só roda a máquina de estados (microssegundos) e enfileira mensagens; nunca faz E/S.
- A thread principal continua rodando o `event_loop()` de `scrcpy.c` apenas para o ciclo de vida: `SC_EVENT_DEVICE_DISCONNECTED`, `SDL_EVENT_QUIT` (Ctrl+C), erro do controller.

Arquitetura proposta (Fases 2–3):

```
 [thread input Win32]                                [thread principal]
  Raw Input (WM_INPUT, INPUTSINK)  ─ deltas reais     event_loop(): disconnect / quit / erro
  WH_MOUSE_LL / WH_KEYBOARD_LL     ─ engolir eventos  └─► edge_release()  (atômico) ─┐
        │                                                                          │
        ▼                                                                          ▼
  edge/ (C puro): máquina de estados ──► ações ──► sc_mouse_uhid / sc_keyboard_uhid ─► controller
```

### 5.2 Detecção de borda

`GetCursorPos` + retângulo do monitor (`EnumDisplayMonitors`/`GetMonitorInfo`, com DPI awareness per-monitor v2, que o manifest já declara em `app/scrcpy-windows.manifest`). Condição: cursor na borda externa do monitor escolhido (borda que não encosta noutro monitor) e delta do Raw Input empurrando para fora. O Raw Input dá o delta real mesmo com o cursor parado na borda; só o hook (que vê a posição) não daria.

### 5.3 Plano B se o Raw Input parar de chegar quando o hook engole o mouse

Não consegui confirmar o comportamento sem Windows. Se acontecer: não engolir o movimento no hook; em vez disso `ClipCursor()` num retângulo de 1×1 px na borda enquanto o celular estiver ativo, e engolir só botões e roda no hook. A interface Win32 (Fase 3) deve esconder essa escolha atrás de uma função (`capture_set_mode()`), e a Fase 2 não depende disso.

### 5.4 Garantias de "nunca deixar o PC sem mouse" (critério 4)

1. **Processo morre (crash, kill, fechar janela do console):** o Windows remove os hooks do processo automaticamente. Sem ação nossa.
2. **Celular desconecta / controller falha / Ctrl+C:** `event_loop()` chama `release()`, que zera um `atomic_bool phone_active`. O callback do hook lê esse atômico a cada evento e deixa de engolir imediatamente, sem depender de message loop ou da thread principal.
3. **Atalho de emergência:** tratado **dentro do hook de teclado**, na thread de input, sem passar por fila nem pela thread principal. Funciona mesmo com a thread principal travada.
4. **Hook lento:** o Windows o desativa sozinho; o mouse volta.
5. **`atexit` + `SetUnhandledExceptionFilter`:** `UnhookWindowsHookEx` e `ClipCursor(NULL)` como cinto e suspensório.
6. **Watchdog opcional (Fase 3):** se o controller não consumir mensagens por N ms durante o modo celular, voltar ao PC.

## 6. Riscos (dependem de teste no aparelho/Windows)

| ID | Risco | Mitigação / o que o usuário precisa validar |
|---|---|---|
| R1 | Raw Input vs. hook engolindo o mouse (§5.3) | Plano B pronto; testar no Windows |
| R2 | Overflow da fila de controle (60) a 1000 Hz | Agregar deltas; enviar a 125–250 Hz; `--debug-log` conta mensagens descartadas |
| R3 | **Deriva da posição estimada.** O Android aplica velocidade/aceleração ao mouse HID e não informa a posição do ponteiro | Fator de escala configurável (`pointer_scale`); âncora no canto ao entrar; velocidade do ponteiro fixável com `adb shell settings put system pointer_speed 0` (**a validar** que a One UI respeita); sem aceleração desativável garantida. Procedimento de calibração vai no README (Fase 5) |
| R4 | One UI: Auto Blocker bloqueia comandos por USB; driver Samsung; autorização ADB | Checklist no README (Fase 4/5) |
| R5 | Detecção de desconexão depende do socket de controle | Verificado no código (§3.3); testar puxando o cabo |
| R6 | O ponteiro pode aparecer/sumir no Android independente da transferência (UHID criado no início) | Avaliar criar o UHID só ao entrar no celular e destruir ao sair (UHID_CREATE/DESTROY existem no protocolo); decidir na Fase 3 depois de ver o comportamento |
| R7 | **Layout de teclado no Android** (UHID é teclado físico): acentos/`ç` de ABNT2 dependem de "Teclado físico → layout" nas configurações do S22 | Documentar no README; usuário precisa escolher o layout |
| R8 | Rotação da tela do celular não é conhecida sem vídeo | Padrão retrato; `phone_rotation`/`phone_size` manuais; leitura automática por `dumpsys` a validar |
| R9 | `Ctrl+Pause` ≠ `VK_PAUSE` (§1) | Detectar `VK_PAUSE` e `VK_CANCEL`; testar |
| R10 | Mods (Caps/Num) sem `SDL_GetModState()` | Passar estado do Windows; não bloqueia |
| R11 | Licença: `NOTICE`, renomear binário/ícone, remover marca scrcpy | Fase 1 |
| R12 | `adb.exe` redistribuído no zip | Licença do platform-tools (Android SDK); mencionar no `NOTICE` |

## 7. Plano resumido das próximas fases

1. **Fase 1 — enxugar:** remover áudio, câmera, gravação, v4l2, gamepad, display virtual, AOA/libusb, FFmpeg, OpenGL/screen/input_manager, mac/Linux na build final, `release/`, `doc/` upstream. Manter: ADB, push do server, controller, UHID mouse/teclado, `adb shell wm size`. Renomear para `phoneedge`. `NOTICE` + `docs/removido.md`.
2. **Fase 2 — `app/src/edge/` em C puro** (máquina de estados, posição virtual com clamp, âncora, agregação de deltas, mapa de scancodes) + testes meson.
3. **Fase 3 — `app/src/win/`**: Raw Input, hooks, bloqueio/liberação, teclado, limpeza em todas as saídas.
4. **Fase 4 — CI/Release** no GitHub Actions (`windows-latest`, MSYS2).
5. **Fase 5 — README em português e `--debug-log`.**

## 8. O que **não** foi testado nesta fase

Tudo que envolve Windows ou o S22: Raw Input com hook, `WH_*_LL`, `UHID` no Android, `wm size`, deriva do ponteiro, One UI. Nesta fase só foi verificado: o build Linux, os testes existentes (12/12), o fluxo de código descrito acima, e a existência/acessibilidade dos artefatos do release v5.0.1 no GitHub.
