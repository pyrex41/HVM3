#!/bin/bash
# Per-edit gate. Usage: bash gate.sh
# NO kernel/smoke/suite. Every run tiny via memguard2.sh (min_free 1500, max_swap 18000).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
HVM=dist-newstyle/build/aarch64-osx/ghc-9.12.4/HVM-0.1.0.0/x/hvm/opt/build/hvm/hvm
G2=test-freelist/memguard2.sh
RAW=test-freelist/rawlog
: > "$RAW"
norm() { grep -vE 'MEMGUARD2|^TIME:|^PERF:|^$'; }
stable() { grep -E '=|^WORK:|^SIZE:|^a$'; }  # result+WORK+SIZE
rwork() { grep -E '=|^WORK:|^a$'; }          # result+WORK (ignore SIZE)
FAIL=0

echo "=== (1) ORACLE OFF==BASELINE, ON result+WORK==OFF ==="
for f in factorial map supfact tsum; do
  rm -rf .build
  bash $G2 1500 18000 $HVM run test-freelist/oracle/$f.hvml -c -s >/tmp/g_off.raw 2>/dev/null
  cat /tmp/g_off.raw >>"$RAW"
  cat /tmp/g_off.raw | norm | stable >/tmp/g_off.txt
  rm -rf .build
  bash $G2 1500 18000 env HVM_REUSE_C=1 $HVM run test-freelist/oracle/$f.hvml -c -s >/tmp/g_on.raw 2>/dev/null
  cat /tmp/g_on.raw >>"$RAW"
  if diff -q test-freelist/baseline/$f.txt /tmp/g_off.txt >/dev/null; then
    o="OFF==BASE"; else o="OFF!=BASE"; FAIL=1; fi
  if diff -q <(norm </tmp/g_off.raw | rwork) <(norm </tmp/g_on.raw | rwork) >/dev/null; then
    n="ON==OFF(r+w)"; else n="ON!=OFF"; FAIL=1; fi
  offsize=$(norm </tmp/g_off.raw | grep '^SIZE:'); onsize=$(norm </tmp/g_on.raw | grep '^SIZE:')
  echo "  $f: $o $n | off $offsize / on $onsize"
done

echo "=== (2) NO corruption (grep GOT 0 / invalid over all stdout) ==="
if grep -nE 'GOT 0|invalid:' "$RAW"; then echo "  CORRUPTION FOUND"; FAIL=1; else echo "  clean"; fi

echo "=== (3) CHURN correct+bounded ==="
rm -rf .build
bash $G2 1500 18000 $HVM run test-freelist/churn.hvm -c -s >/tmp/c_off.raw 2>/dev/null
rm -rf .build
bash $G2 1500 18000 env HVM_REUSE_C=1 $HVM run test-freelist/churn.hvm -c -s >/tmp/c_on.raw 2>/dev/null
cat /tmp/c_off.raw /tmp/c_on.raw >>"$RAW"
cres=$(norm </tmp/c_off.raw | grep '=' ); cwork=$(norm </tmp/c_off.raw | grep '^WORK:')
cres2=$(norm </tmp/c_on.raw | grep '='); cwork2=$(norm </tmp/c_on.raw | grep '^WORK:')
csoff=$(norm </tmp/c_off.raw | grep '^SIZE:'); cson=$(norm </tmp/c_on.raw | grep '^SIZE:')
echo "  OFF: $cres $cwork $csoff"
echo "  ON : $cres2 $cwork2 $cson"
[ "$cres" = "! a = 495000000" ] && [ "$cwork" = "WORK: 121200004 interactions" ] || { echo "  CHURN OFF WRONG"; FAIL=1; }
[ "$cres2" = "! a = 495000000" ] && [ "$cwork2" = "WORK: 121200004 interactions" ] || { echo "  CHURN ON WRONG"; FAIL=1; }
if grep -qE 'GOT 0|invalid:' /tmp/c_off.raw /tmp/c_on.raw; then echo "  CHURN CORRUPTION"; FAIL=1; fi

echo "=== GATE $([ $FAIL -eq 0 ] && echo PASS || echo FAIL) ==="
exit $FAIL
