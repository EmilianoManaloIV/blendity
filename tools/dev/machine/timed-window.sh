#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# A copy of a helper from the development machine's scratch folder (C:\blab), kept so it is not lost
# (docs/handoff/MACHINE.md). Every path in it (/c/blab/...) is THAT machine's: change them before use elsewhere.
# Nothing here is built, shipped or run by CI.
#
# Hub's locked timed window:  bash /c/blab/timed-window.sh <log-name> <script> [<script> ...]
# Takes BOTH Windows build slots (hub-window1, hub-window2) and the WSL lock (hub-window), keeps them alive from the
# first take on (a slot idle for 10 minutes is otherwise reclaimed), waits for a quiet machine, runs the scripts one
# after another at normal priority, and logs any build or test process seen during the window ("TAINT").
L=/c/blab/$1.log; shift
: > "$L"
log() { echo "$(date +%H:%M:%S) $*" | tee -a "$L"; }
( while true; do
    for s in /c/blab/win-build-slot1 /c/blab/win-build-slot2 /c/blab/wsl-build.lock; do
      [ -f "$s/owner" ] && case "$(cut -d' ' -f1 "$s/owner")" in hub-window*) touch "$s/owner";; esac
    done; sleep 30
  done ) & keep=$!
bash /c/blab/win-lock.sh take hub-window1 >> "$L" 2>&1 & t1=$!
bash /c/blab/win-lock.sh take hub-window2 >> "$L" 2>&1 & t2=$!; wait $t1 $t2
bash /c/blab/wsl-lock.sh take hub-window >> "$L" 2>&1
held=$(bash /c/blab/win-lock.sh show | grep -c "hub-window")
log "slots held by the hub: $held of 2"
quiet=0
for i in $(seq 1 540); do
  n=$(tasklist 2>/dev/null | grep -icE '^(cl\.exe|link\.exe|blendity_tests|blendity_stress|msbuild)')
  w=$(wsl -e bash -lc "pgrep -c 'cc1plus|blendity_' 2>/dev/null" 2>/dev/null | tr -d '\r\0'); case "$w" in ''|*[!0-9]*) w=0;; esac
  if [ "$n" = "0" ] && [ "$w" = "0" ]; then quiet=$((quiet+1)); else quiet=0; fi
  [ $quiet -ge 6 ] && break
  sleep 10
done
log "quiet=$quiet"
if [ $quiet -ge 6 ] && [ "$held" = "2" ]; then
  ( while true; do b=$(tasklist 2>/dev/null | grep -icE '^(cl\.exe|link\.exe|blendity_tests|msbuild)'); [ "$b" != "0" ] && echo "$(date +%H:%M:%S) TAINT: $b build/test processes" >> "$L"; sleep 5; done ) & samp=$!
  log "WINDOW START"
  for s in "$@"; do
    log "start $s"
    bash "$s" > "${s%.sh}.hub.out" 2>&1
    log "end $s exit=$?"
  done
  log "WINDOW END"
  kill $samp 2>/dev/null
else
  log "NOT RUN (quiet=$quiet, slots held=$held)"
fi
kill $keep 2>/dev/null
bash /c/blab/win-lock.sh drop hub-window1 >> "$L" 2>&1
bash /c/blab/win-lock.sh drop hub-window2 >> "$L" 2>&1
bash /c/blab/wsl-lock.sh drop hub-window >> "$L" 2>&1
log "locks dropped; taint lines: $(grep -c TAINT "$L")"
