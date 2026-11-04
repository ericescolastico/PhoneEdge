# PhoneEdge

Use o mouse e o teclado do **PC Windows** no **celular Android** (testado em mente para o Galaxy S22) empurrando o
cursor pela borda do monitor, como o "Mouse Without Borders". O ponteiro aparece no celular, o mouse e o teclado do PC
passam a controlá-lo, e ao empurrar o ponteiro pela borda oposta do celular (ou com um atalho de emergência) o
controle volta para o PC.

É um **protótipo**, fork do [scrcpy](https://github.com/Genymobile/scrcpy) (Apache 2.0, ver `LICENSE` e `NOTICE`).
Sem janela, sem vídeo: o celular é controlado por um mouse e um teclado HID virtuais (UHID) via ADB.

> **Estado:** compila e passa nos testes automáticos (lógica, integração com um "celular falso" e o executável
> Windows rodando no Wine), mas **ainda não foi validado em um Windows real com um S22 real**. Este guia é justamente
> o passo a passo para você fazer isso. Quando algo depender do aparelho, está marcado como **[a validar]**.

## O que vem no zip

| Arquivo | Para quê |
|---|---|
| `phoneedge.exe` | o programa |
| `scrcpy-server` | servidor Android do scrcpy v5.0.1, **sem modificações** (enviado ao celular pelo ADB) |
| `adb.exe`, `AdbWinApi.dll`, `AdbWinUsbApi.dll` | ADB (Android Platform-Tools), **sem modificações** |
| `SDL3.dll` e outras `.dll` | bibliotecas do programa |
| `phoneedge.ini` | configuração de exemplo (lida automaticamente se estiver ao lado do `.exe`) |
| `teste-celular.txt` | roteiro para testar o celular sem usar o mouse do PC (etapa 3) |

## 1. Preparar o celular (S22 / One UI)

1. **Opções do desenvolvedor**: *Configurações → Sobre o telefone → Informações do software* e toque 7 vezes em
   *Número da versão*.
2. *Configurações → Opções do desenvolvedor* → ative **Depuração USB**.
3. **Auto Blocker** (One UI): se estiver ativo, desative **"Bloquear comandos por cabo USB"**
   (*Configurações → Segurança e privacidade → Auto Blocker*). Ele pode impedir o ADB.
4. Conecte o cabo ao PC e **aceite a autorização de depuração USB** no celular ("Sempre permitir neste computador").
5. Deixe o celular **desbloqueado e com a tela ligada** durante o teste (use `--stay-awake` para ele não apagar).
6. **Teclado físico / acentos**: o teclado do PC aparece no Android como um teclado físico. Para acentuar certo no
   ABNT2, escolha o layout em *Configurações → Gerenciamento geral → Teclado Samsung → Teclado físico* (ou
   *Lista de teclados e padrões → Teclado físico → Configurar layouts de teclado*) → **Português (Brasil)**.
   **[a validar]**: os caminhos dos menus variam entre versões da One UI.

## 2. Preparar o Windows

1. Extraia o zip em uma pasta (por exemplo `C:\PhoneEdge`). **Não** misture com outra instalação do scrcpy.
2. Abra um terminal (PowerShell ou `cmd`) **dentro dessa pasta**.
3. Rode `.\adb.exe devices`. O celular deve aparecer como `device`. Se não aparecer, instale o **driver USB da
   Samsung** (Samsung USB Driver for Mobile Phones) e reconecte o cabo. Se aparecer `unauthorized`, aceite a
   autorização no celular.
4. O Windows SmartScreen pode alertar que o `.exe` não é assinado ("Mais informações → Executar assim mesmo").

## 3. Passo a passo de teste (do mais seguro ao completo)

Faça na ordem: cada etapa isola uma parte, e se algo falhar você sabe onde.

### Etapa 1: só o lado do PC, sem celular (`--dry-run`)

```powershell
.\phoneedge.exe --list-monitors
.\phoneedge.exe --dry-run --debug-log log-etapa1.txt
```

- `--list-monitors` mostra os monitores e seus números (0 é o principal). Use `--monitor=N` (ou `monitor = N` no
  `.ini`) para escolher e `--edge=left|right|top|bottom` para a borda onde o celular está.
- Em `--dry-run` nada é enviado a nenhum celular. **Empurre o mouse contra a borda configurada** (padrão: direita do
  monitor 0): o cursor do Windows fica parado na borda, o console mostra `Control moved to the phone`, e o mouse e o
  teclado do PC deixam de agir no Windows. Para voltar: empurre o mouse no sentido oposto até
  `Control back to the PC`, ou use o atalho **Ctrl+Alt+Pause** (que sempre funciona).
- **Se o cursor do Windows continuar andando enquanto o programa diz que está com o celular**, é o ponto de risco
  principal (**[a validar]**: o Raw Input continuar chegando com o mouse "engolido"): tente
  `--clip-cursor=yes` e me envie o log.
- Feche com **Ctrl+C**.

### Etapa 2: celular conectado, sem mouse

```powershell
.\phoneedge.exe --stay-awake --debug-log log-etapa2.txt
```

O console deve mostrar `Connected to "..."` e `Phone screen: 1080x2340 (...)`. Nada deve acontecer no celular até
você empurrar o mouse pela borda. **[a validar]**: se o Android já mostra um ponteiro de mouse flutuando assim que o
programa conecta (os dispositivos virtuais são criados na conexão).

### Etapa 3: testar o celular sem os ganchos do Windows (`--script`)

```powershell
.\phoneedge.exe --stay-awake --script teste-celular.txt --debug-log log-etapa3.txt
```

O roteiro entra no celular, move o ponteiro, clica, rola e digita, sem usar o mouse nem o teclado do PC. Se o
ponteiro aparecer e se mover, o caminho ADB → UHID → Android funciona. Abra um app (por exemplo, o Chrome) antes para
ver o efeito dos cliques e da digitação.

### Etapa 4: uso real

```powershell
.\phoneedge.exe --stay-awake --debug-log log.txt
```

- Empurre o mouse contra a borda: o ponteiro aparece **na borda oposta do celular, na mesma proporção de altura**.
  Mova, clique (esquerdo/direito), role a roda e digite normalmente.
- Para **voltar**: leve o ponteiro até a borda do celular que fica de frente para o PC e empurre mais um pouco; o
  cursor reaparece no monitor, na altura proporcional.
- **Atalho de emergência: Ctrl+Alt+Pause** (em teclados sem a tecla Pause, mude com `--hotkey`, por exemplo
  `--hotkey=ctrl+alt+f12`). Funciona em qualquer estado.
- Enquanto um botão do mouse está pressionado (arrastar uma janela), o cursor **não** atravessa a borda.

### Etapa 5: calibrar o ponteiro

O HID é relativo e o Android **não informa onde o ponteiro está**. O PhoneEdge estima a posição somando os
movimentos que enviou. Além disso, o **Android acelera o mouse** (quanto mais rápido você mexe, mais o ponteiro anda
por unidade), e isso causa os "pulos" e a sensação de sensibilidade irregular. Há dois remédios, e você pode usar os
dois:

1. **Desligar a aceleração e fixar a velocidade no Android, só enquanto o PhoneEdge roda** (é restaurado ao sair):

   ```powershell
   .\phoneedge.exe --stay-awake --android-accel-off=yes --android-pointer-speed=0 --place-scale=1 --debug-log log.txt
   ```

   **[a validar]** que a chave de aceleração existe na sua versão do Android (em versões antigas é ignorada sem
   efeito). Se o programa fechar de forma anormal, restaure na mão:
   `adb shell settings delete system mouse_pointer_acceleration_enabled` e
   `adb shell settings put system pointer_speed 0`.

2. **Calibrar `place_scale`**: é quantos pixels o ponteiro anda por unidade **nos movimentos rápidos** de
   posicionamento ao entrar (com a aceleração ligada, o padrão é `3.0`; com ela desligada, use `1.0`). Teste: entre
   pela borda **no meio da altura** do monitor; o ponteiro deve surgir **no meio da altura** do celular.

| Sintoma | O que fazer |
|---|---|
| Ao entrar, o ponteiro surge **mais abaixo** (ou no canto de baixo) do que a altura em que você entrou | **diminua** `place_scale` (3 → 2 → 1.5 → 1) |
| Ao entrar, surge **mais acima** | **aumente** `place_scale` |
| O ponteiro "voa até o canto" a cada entrada | já é amenizado: ele só reancora a cada `reanchor_entries` entradas (padrão 6) ou 2 min; use `--reanchor-entries=0` para sempre ancorar, ou valores maiores para menos voos |
| O cursor volta ao PC antes do ponteiro chegar à borda do celular | **diminua** `pointer_scale` (ex.: 0.8) |
| É preciso empurrar muito para voltar | **aumente** `pointer_scale` (ex.: 1.3) |
| Volta ao PC sem querer ao mexer perto da borda esquerda do celular | aumente `leave_margin` (padrão 100) |
| Ponteiro rápido/lento demais | ajuste `sensitivity` (>1 acelera) ou `--android-pointer-speed` |

Teste com `--debug-log`: cada saída mostra a posição estimada (`estimated pointer x,y`) para comparar com o que você
viu na tela.

## 4. Opções

Todas existem na linha de comando (`--nome-da-opcao`) e no `.ini` (`nome_da_opcao`). A linha de comando tem
prioridade. Veja `phoneedge.ini` (comentado) e `.\phoneedge.exe --help`.

| Opção | Padrão | O que faz |
|---|---|---|
| `edge` | `right` | borda do monitor do PC pela qual o cursor passa (`left`, `right`, `top`, `bottom`) |
| `monitor` | `0` | número do monitor (`--list-monitors`) |
| `phone_size` | `auto` | tamanho da tela do celular; `auto` lê com ADB (`dumpsys input` / `wm size`); ou `1080x2340` |
| `phone_rotation` | `0` | rotação (0/90/180/270) se o tamanho for manual |
| `pointer_scale` | `1.0` | pixels do celular por unidade enviada (calibração; mínimo 0.25) |
| `sensitivity` | `1.0` | unidades enviadas por contagem do mouse |
| `wheel_scale` | `1.0` | multiplicador da roda |
| `hotkey` | `ctrl+alt+pause` | atalho de emergência (ctrl, alt, shift, win + uma tecla) |
| `place_scale` | `3.0` | pixels por unidade nos movimentos rápidos de posicionamento (aceleração do Android) |
| `reanchor_entries`, `reanchor_s` | `6`, `120` | reancora no canto a cada N entradas / após N segundos (0 = sempre) |
| `altgr` | `yes` | Ctrl+Alt (que no Windows ABNT2 é o AltGr) vira o AltGr de verdade no celular: `?` = Ctrl+Alt+W |
| `android_accel_off`, `android_pointer_speed` | `no`, `keep` | desligam a aceleração / fixam a velocidade do ponteiro no Android enquanto roda (restaura ao sair) |
| `clip_cursor` | `no` | também prende o cursor em 1 pixel enquanto o celular tem o controle (plano B) |
| `serial` | (único) | número de série do celular, se houver mais de um |
| `stay_awake` | `no` | mantém o celular com a tela ligada |
| `edge_push`, `leave_margin` (padrão 100), `entry_inset` (padrão 60), `entry_inset`, `return_inset`, `flush_ms`, `enter_guard_ms`, `reenter_ms`, `anchor_extra`, `place_delay_ms` | ver `phoneedge.ini` | ajustes finos |

Outras: `--dry-run`, `--script=ARQ`, `--debug-log=ARQ`, `--config=ARQ`, `--list-monitors`, `--pause-on-exit`.

## 5. Segurança: o mouse do PC nunca deve ficar preso

- O mouse e o teclado só são "engolidos" por **ganchos de baixo nível do Windows**, que o **próprio Windows remove**
  se o programa fechar, travar, for encerrado ou demorar demais para responder.
- Voltam ao PC sozinhos: celular desconectado, erro de comunicação, `Ctrl+C`, fechar a janela do console, falha do
  programa. O atalho de emergência é tratado dentro do gancho, mesmo que o resto do programa trave.
- Se mesmo assim algo ficar estranho, nesta ordem: pressione **Ctrl+Alt+Pause**; feche a janela do console (o
  mouse e o teclado voltam na hora); encerre o `phoneedge.exe` no Gerenciador de Tarefas. Enquanto o celular tem o
  controle, o teclado do PC está redirecionado (por isso Ctrl+Shift+Esc não abre nada), mas **Ctrl+Alt+Del** é
  tratado pelo próprio Windows **[a validar]**.
- **[a validar]**: o comportamento exato do Windows ao puxar o cabo / matar o processo no meio do controle do
  celular. É o item que mais vale testar com calma na primeira vez (etapa 4).

## 6. Limitações conhecidas

- **Deriva do ponteiro** (etapa 5): é inerente ao HID relativo; o ponteiro é re-ancorado a cada entrada.
- **Sem vídeo**: o celular precisa estar à vista ao lado do monitor.
- Funciona com **um** celular; um monitor de entrada por vez; só Windows 10/11 x64.
- Janelas abertas **como administrador** e a tela de bloqueio/UAC não passam pelos ganchos do mesmo jeito.
- Teclas de mídia e teclas sem equivalente HID continuam agindo no PC.
- Um jogo/aplicativo que lê o mouse diretamente (Raw Input exclusivo) pode se comportar de forma diferente.

## 7. Coletar informações para diagnóstico

Rode com `--debug-log log.txt` e envie o arquivo. Ele registra a configuração efetiva, o tamanho da tela lido do
celular, cada **entrada** e **saída** de borda (com o motivo), a **posição estimada do ponteiro**, o destino do
cursor ao voltar, teclas, botões e erros. Com `-V verbose` registra também cada movimento do mouse (muito grande).

## 8. Compilar e testar (desenvolvedores)

Dependência: SDL3 (≥ 3.2) e Meson.

```bash
meson setup build
ninja -C build
meson test -C build            # testes unitários (lógica portátil, sessão, remetente, CLI, HID)
python3 tests/e2e/run_e2e.py build/app/phoneedge   # integração com "celular" e adb falsos
```

O `.github/workflows/build.yml` compila no `windows-latest` (MSYS2), roda os testes e publica o zip; ao criar uma tag
`v*`, publica o Release. A estrutura e as decisões estão em `docs/analise.md`; o que foi removido do scrcpy, em
`docs/removido.md`.

## Licença

Apache 2.0, fork modificado do scrcpy (Genymobile / Romain Vimont). Veja `LICENSE` e `NOTICE`. O `scrcpy-server` e o
`adb` do zip são redistribuídos sem modificações.
