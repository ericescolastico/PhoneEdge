#!/bin/bash
S=/tmp/claude-0/-home-user-PhoneEdge/a00d35f7-1556-5d60-85fe-787653c56568/scratchpad
export WINEPREFIX=$S/wine WINEDEBUG=-all DISPLAY=:78
cd $S/build-win/app
rm -f $S/wine-log2.txt
Xvfb :78 -screen 0 1280x1024x24 >/dev/null 2>&1 &
XV=$!
sleep 2
/usr/lib/wine/wine64 explorer /desktop=pe,1280x1024 ./phoneedge.exe --dry-run --debug-log "Z:$S/wine-log2.txt" -V debug "$@" >$S/wine-out2.txt 2>&1 &
WP=$!
sleep 6
WID=$(xdotool search --name "pe" 2>/dev/null | head -1); [ -n "$WID" ] && xdotool windowfocus $WID 2>/dev/null; xdotool mousemove 1100 500
sleep 0.3
for i in $(seq 1 60); do xdotool mousemove_relative -- 15 0; sleep 0.01; done
sleep 0.8
# type some keys while on the phone, then the emergency shortcut
xdotool key a
sleep 0.2
xdotool keydown ctrl keydown alt key Pause keyup alt keyup ctrl
sleep 1
xdotool key b
sleep 0.5
kill $WP 2>/dev/null; sleep 1; kill -9 $WP 2>/dev/null
kill $XV 2>/dev/null
grep -v "hook mouse\|raw input\|phone move\|dry-run) mouse motion" $S/wine-log2.txt | tail -25
