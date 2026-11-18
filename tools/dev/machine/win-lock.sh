#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# A copy of a helper from the development machine's scratch folder (C:\blab), kept so it is not lost
# (docs/handoff/MACHINE.md). Every path in it (/c/blab/...) is THAT machine's: change them before use elsewhere.
# Nothing here is built, shipped or run by CI.
#
# The hub's lock for Windows compiles (2026-11-16): 13 sessions compiling full trees at once (/MP, 333 cl.exe) ran the
# 32 GB machine out of commit memory and stalled every link. At most TWO Windows compiles run at a time; tests need none.
# Usage (from Git Bash):  bash /c/blab/win-lock.sh take <who>   -> waits its turn for a free slot, then takes it
#                         bash /c/blab/win-lock.sh drop <who>   -> frees the caller's slot (run it in the same command
#                                                                  as the build, after `;`, so it runs on failure too)
#                         bash /c/blab/win-lock.sh show
# First come, first served: a waiter takes a free slot only when no one queued before it is still waiting (a queue entry
# whose waiter has not refreshed it for 5 minutes is dropped: that waiter died). One slot per owner.
# A slot is treated as abandoned and freed by the next taker when it is older than 90 minutes, or older than 10 minutes
# while no cl.exe or link.exe runs anywhere (a holder that forgot to drop, or a build that hung before compiling).
SLOTS="/c/blab/win-build-slot1 /c/blab/win-build-slot2"
Q=/c/blab/win-build-queue
idle() { [ "$(tasklist 2>/dev/null | grep -icE "^(cl.exe|link.exe)")" = "0" ]; }
stale() { [ -f "$1/owner" ] && { [ -n "$(find "$1/owner" -mmin +90 2>/dev/null)" ] || { [ -n "$(find "$1/owner" -mmin +10 2>/dev/null)" ] && idle; }; }; }
holds() { for s in $SLOTS; do [ "$(cut -d' ' -f1 "$s/owner" 2>/dev/null)" = "$1" ] && return 0; done; return 1; }
case "$1" in
  take)
    mkdir -p "$Q"
    me="$Q/$(date +%s)-$$-$2"
    : > "$me"
    trap 'rm -f "$me"' EXIT
    while :; do
      touch "$me"                                           # (still waiting: keeps the place in the queue)
      find "$Q" -type f -mmin +5 -delete 2>/dev/null        # (waiters that died)
      # (the first waiter whose owner holds no slot yet: an owner that already holds one waits without blocking the rest)
      first=""; for e in $(ls "$Q" 2>/dev/null | sort -n); do holds "${e#*-*-}" || { first="$e"; break; }; done
      if [ "$Q/$first" = "$me" ]; then
        for s in $SLOTS; do
          stale "$s" && { rm -f "$s/owner"; rmdir "$s" 2>/dev/null; }
          if mkdir "$s" 2>/dev/null; then echo "$2 $(date '+%m-%d %H:%M')" > "$s/owner"; echo "slot $(basename "$s") taken by $2"; exit 0; fi
        done
      fi
      sleep 20
    done ;;
  drop)
    for s in $SLOTS; do
      if [ -f "$s/owner" ] && [ "$(cut -d' ' -f1 "$s/owner")" = "$2" ]; then rm -f "$s/owner"; rmdir "$s"; echo "slot $(basename "$s") dropped"; exit 0; fi
    done
    echo "no slot held by $2" ;;
  show)
    for s in $SLOTS; do echo "$(basename "$s"): $(cat "$s/owner" 2>/dev/null || echo free)"; done
    echo "queue: $(ls "$Q" 2>/dev/null | sort -n | sed 's/^[0-9]*-[0-9]*-//' | tr '\n' ' ')" ;;
esac
