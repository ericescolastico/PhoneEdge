#!/bin/bash
# Smoke test of the Windows build under Wine + Xvfb (hooks, Raw Input, dry-run):
# enters the "phone" by pushing the mouse against the right edge, types, then
# uses the emergency shortcut. Needs: wine64, Xvfb, xdotool.
#
# Usage: tests/wine/smoke_hotkey.sh DIR_WITH_phoneedge.exe_AND_SDL3.dll
set -e
DIR=$(cd "${1:?usage: $0 DIR}" && pwd)
export WINEPREFIX=${WINEPREFIX:-$HOME/.wine-phoneedge} WINEDEBUG=-all DISPLAY=:78
WINE=${WINE:-/usr/lib/wine/wine64}
LOG=$(mktemp)
Xvfb :78 -screen 0 1280x1024x24 >/dev/null 2>&1 &
XV=$!
trap 'kill $XV 2>/dev/null' EXIT
sleep 2
cd "$DIR"
$WINE explorer /desktop=pe,1280x1024 ./phoneedge.exe --dry-run --debug-log "Z:$LOG" -V debug >/dev/null 2>&1 &
WP=$!
sleep 6
WID=$(xdotool search --name pe 2>/dev/null | head -1) && xdotool windowfocus "$WID" 2>/dev/null || true
xdotool mousemove 1100 500; sleep 0.3
for i in $(seq 1 60); do xdotool mousemove_relative -- 15 0; sleep 0.01; done
sleep 0.8
xdotool key a; sleep 0.2
xdotool keydown ctrl keydown alt key Pause keyup alt keyup ctrl
sleep 1
kill $WP 2>/dev/null || true
grep -q "Control moved to the phone" "$LOG" && grep -q "Control back to the PC: emergency hotkey" "$LOG" \
    && echo "OK: entered the phone and the emergency hotkey brought the control back" \
    || { echo "FAILED"; grep -v "hook mouse\|raw input\|dry-run" "$LOG" | tail -20; exit 1; }
