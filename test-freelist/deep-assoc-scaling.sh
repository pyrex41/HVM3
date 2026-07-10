#!/bin/bash
# Focused N,2N,4N guard for delayed DUP-projection reclamation.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
HVM=dist-newstyle/build/aarch64-osx/ghc-9.12.4/HVM-0.1.0.0/x/hvm/opt/build/hvm/hvm
SRC=test-freelist/shen-deep-assoc-get.hvm

sizes=()
for n in 1000 2000 4000; do
  case_file="/tmp/hvm-deep-assoc-$n.hvm"
  sed "s/@loop(5000 /@loop($n /" "$SRC" >"$case_file"
  rm -rf .build
  "$HVM" run "$case_file" -c -s >"/tmp/hvm-deep-off-$n"
  rm -rf .build
  HVM_REUSE_C=1 HVM_DEATH_PROFILE=1 "$HVM" run "$case_file" -c -s \
    >"/tmp/hvm-deep-on-$n" 2>"/tmp/hvm-deep-profile-$n"
  diff -u \
    <(grep -E '^! a =|^WORK:' "/tmp/hvm-deep-off-$n") \
    <(grep -E '^! a =|^WORK:' "/tmp/hvm-deep-on-$n")
  sizes+=("$(awk '/^SIZE:/ { print $2 }' "/tmp/hvm-deep-on-$n")")
done

d1=$((sizes[1] - sizes[0]))
d2=$((sizes[2] - sizes[1]))
printf 'deep-assoc ON SIZE N=%s 2N=%s 4N=%s\n' "${sizes[@]}"
# Doubling N must grow by materially less than the preceding linear doubling.
((d2 * 2 < d1 * 3))
grep -Eq 'HVM_DEATH_COLLECT .*name=DP[01].*reclaimed_cells=[1-9]' \
  /tmp/hvm-deep-profile-4000
