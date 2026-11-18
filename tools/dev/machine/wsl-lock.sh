#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# A copy of a helper from the development machine's scratch folder (C:\blab), kept so it is not lost
# (docs/handoff/MACHINE.md). Every path in it (/c/blab/...) is THAT machine's: change them before use elsewhere.
# Nothing here is built, shipped or run by CI.
#
# The hub's lock for Linux compiles in WSL (2026-11-14): WSL has 15 GB, so only ONE compile may run at a time.
# Usage (from Git Bash):  bash /c/blab/wsl-lock.sh take <who>   -> waits until the lock is free, then takes it
#                         bash /c/blab/wsl-lock.sh drop <who>   -> frees it (only its owner); run it after `;` in the
#                                                                  same command as the build, so it runs on failure too
#                         bash /c/blab/wsl-lock.sh show
# 2026-11-16: a lock older than 10 minutes while nothing compiles in WSL (no cc1plus / ld) is abandoned and taken over;
# so is any lock older than 2 hours. (s3-533 held it 02:35-04:45 with nothing running.)
L=/c/blab/wsl-build.lock
wsl_idle() { [ "$(wsl -e bash -lc "pgrep -c 'cc1plus|collect2|ld$|blendity_' 2>/dev/null" 2>/dev/null | tr -d '\r\0')" = "0" ]; }
stale() { [ -f "$L/owner" ] && { [ -n "$(find "$L/owner" -mmin +120 2>/dev/null)" ] || { [ -n "$(find "$L/owner" -mmin +10 2>/dev/null)" ] && wsl_idle; }; }; }
case "$1" in
  take)
    while :; do
      stale && { echo "taking over an abandoned lock: $(cat "$L/owner")"; rm -f "$L/owner"; rmdir "$L" 2>/dev/null; }
      if mkdir "$L" 2>/dev/null; then echo "$2 $(date '+%m-%d %H:%M')" > "$L/owner"; echo "lock taken by $2"; exit 0; fi
      sleep 30
    done ;;
  drop)
    if [ -f "$L/owner" ] && [ "$(cut -d' ' -f1 "$L/owner")" = "$2" ]; then rm -f "$L/owner"; rmdir "$L"; echo "lock dropped"; else echo "not yours: $(cat "$L/owner" 2>/dev/null)"; fi ;;
  show) cat "$L/owner" 2>/dev/null || echo free ;;
esac
