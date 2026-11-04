# O que foi removido do scrcpy (Fase 1)

O PhoneEdge usa só o canal de controle do scrcpy: conecta por ADB, envia o
`scrcpy-server` pré-compilado, abre o socket de controle e injeta mouse e
teclado como dispositivos HID virtuais (UHID). Sem vídeo, sem áudio, sem janela.

Cada remoção foi feita em um commit separado, com o build e os testes verdes
(`git log --oneline`).

## Repositório

| Removido | Motivo |
|---|---|
| `server/`, `build.gradle`, `settings.gradle`, `gradle*`, `config/` | O servidor Java não é modificado; usamos o `scrcpy-server` v5.0.1 do release oficial. O código continua no upstream. |
| `release/`, `install_release.sh`, `bump_version`, `run` | Scripts de empacotamento/release do upstream (Linux, macOS, Windows, servidor). O CI próprio vem na Fase 4. |
| `doc/`, `assets/`, `FAQ.md`, README do upstream, `app/scrcpy.1` | Documentação do upstream. |
| `app/data/` (ícones, `.desktop`, completions, `.bat`/`.vbs`) | Ícone e marca do scrcpy não devem ser reutilizados. |
| `app/deps/` | Scripts que compilam SDL, FFmpeg, dav1d, libusb e baixam o adb. O CI usa MSYS2 + release do upstream. |
| `.github/ISSUE_TEMPLATE`, `FUNDING.yml`, `workflows/release.yml` | Do upstream. |
| `cross_win32.txt` | Só Windows x64. |

## Código do cliente (`app/src/`)

| Removido | Funcionalidade |
|---|---|
| `screen.*`, `opengl.*`, `egl.*`, `texture.*`, `icon.*`, `fps_counter.*`, `sdl_hints.*`, `disconnect.*`, `interop/` | Janela e renderização |
| `decoder.*`, `demuxer.*`, `hwdec.*`, `frame_buffer.*`, `video_regulator.*`, `vaapi_shim.c`, `trait/frame_*`, `trait/sink.h` | Decodificação de vídeo (FFmpeg, VA-API, D3D11VA, VideoToolbox) |
| `audio_player.*`, `audio_regulator.*`, `util/audiobuf.*` | Áudio |
| `recorder.*`, `packet_merger.*`, `trait/packet_*` | Gravação |
| `v4l2_sink.*` | v4l2 |
| `usb/` (AOA, OTG, libusb), `util/acksync.*` | HID sobre USB (exigiria trocar o driver do celular no Windows) |
| `hid/hid_gamepad.*`, `uhid/gamepad_uhid.*`, `trait/gamepad_processor.h` | Gamepad |
| `input_manager.*`, `mouse_capture.*`, `mouse_sdk.*`, `keyboard_sdk.*`, `shortcut_mod.h` | Entrada via janela SDL e injeção SDK |
| `file_pusher.*` | Envio de arquivos/APKs |
| `uhid/uhid_output.*`, `util/intmap.*`, `util/average.*`, `util/sdl.*`, `util/term.*`, `util/timeout.*`, `clock.*`, `version.*` | Utilitários sem uso |
| Câmera, display virtual, `--otg`, `--record`, `--tcpip`, `--list-*`, atalhos, clipboard e centenas de opções de `cli.c` | Reescritos: `cli.c` agora aceita só `--serial`, `--stay-awake`, `--verbosity`, `--port`, `--tunnel-*`, `--force-adb-forward`, `--kill-adb-on-close`, `--pause-on-exit` |

Dependências que saíram do build: **FFmpeg** (libavcodec/format/util/swresample/avdevice), **libusb**,
**libdrm/libva**. Resta apenas **SDL3** (eventos, threads, log).

## Alterações de comportamento

- `server.c` pede ao servidor `video=false audio=false clipboard_autosync=false`
  (só controle) e informa a versão **`SCRCPY_SERVER_VERSION=5.0.1`**, independente da
  versão do PhoneEdge: o servidor recusa clientes de versão diferente.
- O `receiver` ignora mensagens de clipboard e de saída HID (LEDs do teclado).
- `keyboard_uhid` deixou de depender de SDL e da thread principal (quem injeta teclas
  será a thread de entrada do Windows).
- Binário: `phoneedge` (`phoneedge.exe`).

## O que foi mantido (de propósito)

- `adb/` (ADB: seleção de dispositivo, push do servidor, túnel), `server.c`, `controller.*`,
  `receiver.*`, `control_msg.*` (serialização completa; só UHID é usado), `device_msg.*`.
- `hid/hid_mouse.*`, `hid/hid_keyboard.*`, `uhid/mouse_uhid.*`, `uhid/keyboard_uhid.*`.
- `sys/unix/`: só para compilar e rodar os testes no Linux/CI; o produto final é Windows.
- SDL3: eventos, threads e log (sem subsistema de vídeo).

## Pendências (fases seguintes)

- `control_msg.c` e `input_events.h` ainda carregam tipos que o protótipo não usa
  (teclas Android, orientação...). Podem ser podados depois, sem urgência.
