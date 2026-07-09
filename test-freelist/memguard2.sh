#!/bin/bash
# memguard2.sh — SYSTEM-level memory watchdog.
#
# Lesson (2026-07-09): the RSS-based memguard.sh MISSED a 43 GB balloon
# because macOS compresses/swaps to keep RSS low while committed footprint
# explodes. RSS and even VSZ are useless here (HVM reserves a huge arena).
# The only reliable signals are SYSTEM-WIDE: free RAM collapsing and swap
# growing. This guard kills the whole process group when EITHER trips.
#
# Usage: memguard2.sh <min_free_mb> <max_swap_used_mb> <command...>
#   e.g. memguard2.sh 2500 14000 hvm run x.hvml -c
# Kills if free RAM < min_free_mb OR swap-used > max_swap_used_mb.

set -u
MIN_FREE_MB="$1"; shift
MAX_SWAP_MB="$1"; shift

"$@" &
CMD_PID=$!

free_mb () { vm_stat | awk '/Pages free/{f=$3}/Pages inactive/{i=$3}/Pages speculative/{s=$3} END{gsub(/\./,"",f);gsub(/\./,"",i);gsub(/\./,"",s);print int((f+i+s)*16384/1048576)}'; }
swap_mb () { sysctl -n vm.swapusage | awk '{for(j=1;j<=NF;j++) if($j=="used"){v=$(j+2); sub(/M$/,"",v); print int(v)}}'; }

while kill -0 "$CMD_PID" 2>/dev/null; do
  fm=$(free_mb); sw=$(swap_mb)
  if [ "${fm:-0}" -lt "$MIN_FREE_MB" ] || [ "${sw:-0}" -gt "$MAX_SWAP_MB" ]; then
    echo "MEMGUARD2-KILL: free=${fm}MB (min ${MIN_FREE_MB}) swap_used=${sw}MB (max ${MAX_SWAP_MB}) — SIGKILL group $CMD_PID" >&2
    pkill -9 -P "$CMD_PID" 2>/dev/null
    kill -9 "$CMD_PID" 2>/dev/null
    wait "$CMD_PID" 2>/dev/null
    exit 137
  fi
  sleep 1
done
wait "$CMD_PID"; rc=$?
echo "MEMGUARD2-OK: exited rc=$rc; free=$(free_mb)MB swap_used=$(swap_mb)MB" >&2
exit $rc
