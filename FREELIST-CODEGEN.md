# Compiled-mode node reclamation (freelist-in-codegen) for HVM3

Status: **S1 complete and validated; S2 (collect dropped subtrees) pending.**
Branch: `freelist`. Base: `bff58a3` (upstream `HigherOrderCO/HVM3`).
Author of this layer: shen-inets work, 2026-07-09.

## Why

HVM3's allocator only bumps a pointer; nothing is ever freed. Resident
memory therefore tracks **cumulative interactions ever performed**
(≈ 5 bytes/interaction), not the live working set. For long/large programs
this is fatal: booting the Shen kernel under `hvm run -c` drove the process
to a **~43 GB footprint** on a 24 GB machine and was OS-killed before boot
finished.

The pre-existing `freelist` branch (commits `300cc93`..`bff58a3`) added a
per-arity global freelist to `heap.c` and calls at the interpreted death
sites in `reduce/*.c` — but it is **interpreted-mode only**. `hvm run -c`
generates C from `Compile.hs` that inlines the reduction rules with its own
allocation paths and never calls the freelist. This layer wires the freelist
into that generated C.

## Architecture notes that shaped the design

- **The compiled `.so` has its own runtime-state copy.** `initBook`
  (`API.hs`) compiles the book to a `.so`, `dlopen`s it, and shares only the
  `State` struct (heap array + `size`/`itrs` pointers) via `hvm_set_state`.
  The freelist statics in `heap.c` (`HVM_REUSE_ON`, the free lists,
  `HVM_FREED_CELLS`) are file-static and compiled **separately** into the
  main binary and the `.so`. All reduction in compiled mode runs inside the
  `.so` (`reduceCAt`), so the freelist that matters is the `.so`'s.
- **`Compile.hs` already had intra-rule reuse** (`compileFastAlloc`, the
  `reus` map): when a rule consumes the redex, its cells are parked for the
  rule's own allocations. That is the "static block reuse" the code comments
  mention. It does **not** free anything across rules, and it strands cells a
  rule consumes but does not re-allocate.
- **HVM's DUP is lazy.** A read that duplicates a structure only copies the
  *forced prefix*; untaken match arms collapse against erasers (DUP-ERA). Any
  reclamation scheme must work *with* this, not against it (see the rejected
  approach below).

## What this layer changes

All gated on the env var **`HVM_REUSE_C`** (off by default → byte-identical
to stock `-c`).

1. **`state.c` — enable the `.so`'s freelist.** `hvm_set_state` (the one init
   point that runs inside the `.so`) calls `hvm_set_reuse(1)` when
   `HVM_REUSE_C` is set.
2. **`Compile.hs` — free dead cells in the generated rules:**
   - **`compileFastBody`, both CTR-mat paths (plain and IfLet):** extract the
     matched fields, then for a **TCO** function `free_node` the consumed
     scrutinee node immediately (it is dead once its fields are copied out;
     its fields point at *other* nodes). Non-TCO keeps direct intra-rule
     reuse. This is the load-bearing fix — see the W32 note below.
   - **`compileFastAlloc`, TCO branch:** on iterations ≥2 (where the
     `fst_iter` reuse of a parked cell is *not* taken), `free_node` that cell
     unless it is the argument frame (`term_loc(ref)`), which stays live
     across the loop.
   - **`flushReuse` at rule terminals:** free any remaining parked `reus`
     cells at a plain `return` (frame included, it is dead) and at the TCO
     loop-back (frame *protected*).
3. **`heap.c` — `.so` counter visibility.** An `atexit`-registered reporter
   (registered from `hvm_set_reuse`, gated on `HVM_REUSE_STATS`) prints
   `SO_REUSE: on=.. freed=.. reused=..` from the local translation unit, so
   the `.so`'s real numbers are visible (the FFI `get_frees`/`get_reuses` read
   the main binary's copy, always ~0 in compiled mode).

## Debugging findings (load-bearing, don't relearn the hard way)

- **Frame-free corruption.** Freeing the argument frame (`term_loc(ref)`) at a
  TCO loop-back corrupts the next iteration (frame is the live loop state) →
  `GOT 0`. Terminals must distinguish: free the frame at a plain return, keep
  it at the loop-back.
- **The W32 fast-path leak (root cause of the low initial yield).** A `~2%`
  reuse rate on the churn test came from this: `compileFastAlloc`
  *speculatively* parks the consumed scrutinee for an allocation (e.g. the
  `OP2` of `+`), but a runtime fast path (both operands `W32` →
  `term_new(W32)`, no allocation) skips that allocation, so the cell is
  neither reused nor freed and leaks every iteration. Fix: free the scrutinee
  right after field extraction, independent of any body allocation; the body
  recycles it via `alloc_node`/the freelist when it does allocate.
- **Rejected approach — "thread the store back".** An earlier idea (return
  `(value . G)` so a global read doesn't duplicate the globals map) measured
  **21× worse**: HVM's lazy DUP already copies only the forced prefix, so
  eager spine rebuild is a pessimization. Documented so nobody retries it.
- **`take`/`partition` are exported by both `Data.List` and `HVM.Foreign`** —
  use `isPrefixOf`/explicit filters in `Compile.hs` to avoid ambiguity errors.
- **The generated `.so` C does not preserve `heap.c`'s definition order** —
  functions referenced across it (e.g. the atexit reporter) need forward
  declarations near the top of `heap.c`.
- **`atexit` reporter must not touch shared state** (`*HVM.size`) — it is torn
  down by the time atexit fires (segfault). Print only local statics.

## Validation (all `-c`, on the fork binary)

Correctness bar: identical result **and** identical WORK vs stock `-c`
(freelist frees are bookkeeping, not interactions), plus bounded memory.

| test | result | WORK | SIZE (nodes) | peak RSS |
|------|--------|------|--------------|----------|
| factorial / tsum / map / supfact | identical | identical | — | tiny |
| churn (10M cons built+consumed) OFF | 495000000 | 121200004 | 20,600,003 | 176 MB |
| churn ON (`HVM_REUSE_C=1`) | 495000000 ✓ | 121200004 ✓ | **600,201** | **65 MB** |

`.so` counters on churn ON: `freed=20,400,002 reused=19,999,802` — nearly
every dead cell freed and recycled. **34× fewer nodes; correct output.**

## Known gap (S2)

Kernel boot `-c` still balloons (guard-killed at ~14.5 GB, down from 43 GB).
Churn only exercises linear consume (S1's pattern). The kernel additionally
**drops subtrees** — old world/assoc spines from every `kl.aset`, discarded
match branches, erasures — which never enter `reus` and so are never freed.
S2 must emit `collect()`/`free_node` in the generated C at the death sites the
interpreted `reduce/*.c` already handle (`dup_ctr`, `mat_ctr`, `*_era`, dup
paths). Validate on tiny programs that drop subtrees, then re-test kernel boot
on a machine with clean swap.

## S2 resume checklist (do after a reboot / clean swap)

The scratchpad worktree and built binary are ephemeral (`/private/tmp`), but
this branch is pushed to `pyrex41/HVM3`. To resume:

1. **Re-establish the worktree** (branch is safe in `~/projects/HVM3/.git` and
   on the fork): `git -C ~/projects/HVM3 worktree add <persistent-path> freelist`
   (prefer a persistent path this time, not `/private/tmp`). `cabal build exe:hvm`.
2. **Recreate the tiny harness:** the `churn.hvm` program is in the validation
   table above; `memguard2.sh` = system-level watchdog (kills on low free-RAM /
   high swap — RSS is useless here). Oracle: factorial=3628800/108,
   tsum=523776/29677, map=30/467, supfact=`&3{6 120}`/147.
3. **Implement S2 — emit collect()/free_node at codegen death sites.** The
   interpreted freelist already frees at these sites; mirror each into the
   generated C in `Compile.hs`. Files the interpreted layer touched (the
   death-site catalogue): `dup_ctr.c`, `dup_lam.c`, `dup_ref.c`, `dup_sup.c`,
   `mat_ctr.c`, `mat_era.c`, `mat_w32.c`, `opx_era.c`, `opy_era.c`,
   `opy_w32.c`, `app_era.c`, `app_lam.c`, `let.c`. The dominant kernel source
   is **dropped assoc/world spines**: `kl.aset` rebuilds a spine and drops the
   old one; those nodes are unreachable but never entered `reus`. A `collect()`
   walk (already in `heap.c` for interpreted) frees a dropped subtree
   recursively — the codegen needs to call it where a rule erases/discards a
   subterm.
4. **Same hazards as S1:** never free a live cell (frame, or a subterm still
   referenced) → `GOT 0`. Beware runtime fast paths that skip an allocation
   (the W32-OP2 class). Validate each interaction type on a tiny program that
   *drops* a subtree (e.g. repeatedly `set` a global to shrink a stored list;
   build-and-discard trees) — SIZE must stay bounded and output correct.
5. **Only then** re-test kernel `--smoke -c` with `HVM_REUSE_C=1` under
   `memguard2.sh` (free-RAM min ~2.5G, swap max ~14G). Target: bounded SIZE /
   peak (was 43 GB stock, ~14.5 GB after S1) and boot completes `(+ 1 2)=3`.
   If still large, profile which death site dominates before adding more.

## Build / use

```
cabal build exe:hvm                       # rebuild after editing Compile.hs/*.c
HVM_REUSE_C=1 <hvm> run prog.hvml -c -s    # compiled run with reclamation
HVM_REUSE_C=1 HVM_REUSE_STATS=1 <hvm> ...  # + SO_REUSE freed/reused line
```
Off (no `HVM_REUSE_C`) is stock behavior. Interpreted mode is unchanged
(`HVM_REUSE=1` still drives the original interpreted freelist).

## Provenance / upstreaming

Forked from upstream `HigherOrderCO/HVM3` at `bff58a3`. The interpreted
freelist (`300cc93`..`bff58a3`) is prior work on this branch; this layer adds
the compiled-mode wiring on top.

Hosted on our GitHub fork **`pyrex41/HVM3`** (branch `freelist`). Local repo
`~/projects/HVM3` has `origin` = our fork, `upstream` = `HigherOrderCO/HVM3`.
Because `HVM_REUSE_C` gating and the `.so`-state enable keep stock `-c`
byte-identical, the codegen `free_node` emission is a plausible upstream PR
later (open one from `pyrex41:freelist` → `HigherOrderCO:main` if desired) —
but that decision is deferred.
