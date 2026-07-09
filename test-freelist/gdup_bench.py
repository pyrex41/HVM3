#!/usr/bin/env python3
"""gdup_bench.py — isolate the globals-DUP cost.

Seeds G with many globals, then reads one repeatedly through `value`
(→ kl.value → kl.aget[-t]). With the old code each read DUPs the whole G
spine; with the threading fix it does not. We report the HVM interaction
count (WORK), which is memory-independent and deterministic, so this needs
no big run. Prelude-only compile (no kernel) → tiny + fast.
"""
import os, re, subprocess, sys, tempfile
sys.setrecursionlimit(10_000_000)
HERE = "/Users/reuben/projects/shen/inets/shen-inets"
sys.path.insert(0, HERE)
from shen_inets.kl import parse_all
from shen_inets.compile import boxed_compiler
from shen_inets.emit_hvml import emit_book

FORK = ("/private/tmp/claude-502/-Users-reuben-projects-shen-inets/"
        "fa7dc937-b1fd-4652-b1e1-731184c07803/scratchpad/forkbin/hvm")

NGLOB = int(sys.argv[1]) if len(sys.argv) > 1 else 60
NREAD = int(sys.argv[2]) if len(sys.argv) > 2 else 300

# build: (set g0 0) ... (set g{N-1} 0) then a recursive reader of g0
sets = "".join(f"(set g{i} {i})" for i in range(NGLOB))
# nest the sets as a do-chain ending in the read loop
deep = f"g{NGLOB-1}"   # last key set → tail of the spine → full O(|G|) walk
prog = f"(defun rd (K) (if (= K 0) (value {deep}) (do (value {deep}) (rd (- K 1)))))"
body = f"(rd {NREAD})"
for i in reversed(range(NGLOB)):
    body = f"(do (set g{i} {i}) {body})"
src = prog + body

comp = boxed_compiler()
forms = parse_all(src)
comp.compile_program(forms)
main = comp.compile_main(
    [f for f in forms if not (isinstance(f, list) and f and f[0] == "defun")][-1])
comp.finalize()
with tempfile.NamedTemporaryFile("w", suffix=".hvml", delete=False) as f:
    f.write(emit_book(comp.book, main)); path = f.name

try:
    out = subprocess.run([FORK, "run", path, "-s"],
                         capture_output=True, text=True, timeout=300,
                         env={**os.environ, "HVM_REUSE": "1"})
    txt = out.stdout + out.stderr
    if "invalid:" in txt or "GOT 0" in txt:
        print("ABORT:", txt[:200]); sys.exit(1)
    # HVM -s prints interaction/work stats; capture whatever integers it reports
    print(f"NGLOB={NGLOB} NREAD={NREAD}")
    for line in txt.splitlines():
        if re.search(r"(ITRS|WORK|itrs|work|interactions|COST|TIME|time)", line, re.I):
            print("  ", line.strip())
    # also show the result head to confirm it ran
    res = [l for l in txt.splitlines() if l.startswith("! a =") or l.startswith("#") or l.startswith("[")]
    print("   result:", (res[0][:60] if res else txt.strip()[:60]))
finally:
    os.unlink(path)
