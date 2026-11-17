#!/bin/sh
# The Sochi window on a LAN table, both ways, on this computer (UDP ports
# 33391 and 33392 on 127.0.0.1). Needs a GUI-enabled ussr. Run from the
# repository root:  sh tests/sochi_lan/run.sh [ussr]
ussr=${1:-./ussr}
[ -x "$ussr" ] || ussr=./ussr.exe
# 1. the window hosts; two terminal bots join it
"$ussr" tests/sochi_lan/gui_host.su </dev/null &
host=$!
sleep 2
"$ussr" src/sochi/sochi.su join 127.0.0.1 --port 33391 --bot --fast --name TermA </dev/null >/dev/null 2>&1 &
a=$!
sleep 1
"$ussr" src/sochi/sochi.su join 127.0.0.1 --port 33391 --bot --fast --name TermB </dev/null >/dev/null 2>&1 &
b=$!
wait $host
kill $a $b 2>/dev/null
# 2. a terminal bot hosts with one bot; the window joins it
"$ussr" src/sochi/sochi.su host --port 33392 --bot --fast --bots 1 --pool 2 --name TermHost </dev/null >/dev/null 2>&1 &
h=$!
sleep 2
"$ussr" tests/sochi_lan/gui_join.su </dev/null
kill $h 2>/dev/null
wait 2>/dev/null
