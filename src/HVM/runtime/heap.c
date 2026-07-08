#include "Runtime.h"

// Heap counters
void set_len(u64 size) { *HVM.size = size; reuse_reset(); }
void set_itr(u64 itrs) { *HVM.itrs = itrs; }
u64  get_len() { return *HVM.size; }
u64  get_itr() { return *HVM.itrs; }
u64  fresh()   { return (*HVM.frsh)++; }

// Atomics
Term swap(Loc loc, Term term) {
  Term val = HVM.heap[loc];
  HVM.heap[loc] = term;
  if (val == 0) {
    printf("SWAP 0 at %08llx\n", (u64)loc);
    exit(0);
  }
  return val;
}

Term got(Loc loc) {
  Term val = HVM.heap[loc];
  if (val == 0) {
    printf("GOT 0 at %08llx\n", (u64)loc);
    exit(0);
  }
  return val;
}

void set(Loc loc, Term term) { HVM.heap[loc] = term; }
void sub(Loc loc, Term term) { set(loc, term_set_bit(term)); }
Term take(Loc loc) { return swap(loc, VOID); }

// Node reuse (freelist)
//
// Per-size free lists of dead heap blocks, so memory tracks the LIVE set
// instead of total interactions. Disabled by default: the Haskell driver
// enables it (hvm_set_reuse) for interpreted runs only. The compiled .so
// embeds its own copy of this file with its own static state, where the
// flag is never set, so compiled-mode behavior is bit-identical to stock
// (fast-mode codegen already does its own static block reuse).
//
// Freed blocks chain through their first cell. The link is encoded as
// (next_loc << 1) | 1 so a freed cell is never 0 (got()/swap() treat a
// zero cell as corruption). Loc 0 is the root and is never freed, so 0
// works as the empty-list sentinel in HVM_FREE_HEAD.
#define REUSE_MAX_ARITY 64

static bool HVM_REUSE_ON = false;
static Loc  HVM_FREE_HEAD[REUSE_MAX_ARITY];
static u64  HVM_FREED_CELLS  = 0;
static u64  HVM_REUSED_CELLS = 0;

void hvm_set_reuse(u64 on) { HVM_REUSE_ON = on != 0; }

// Drops all freelist entries. Must be called whenever the bump pointer is
// rolled back (set_len) or the heap is remapped (hvm_init): stale entries
// above the new top would alias future bump allocations.
void reuse_reset() {
  for (u64 i = 0; i < REUSE_MAX_ARITY; i++) {
    HVM_FREE_HEAD[i] = 0;
  }
}

u64 get_frees()  { return HVM_FREED_CELLS; }
u64 get_reuses() { return HVM_REUSED_CELLS; }

// Returns a dead block of `arity` cells at `loc` to the freelist.
// The block must be fully unreachable (see the per-rule lifecycle map).
// Oversized blocks are leaked (missed frees only cost memory).
void free_node(Loc loc, Loc arity) {
  if (!HVM_REUSE_ON || arity == 0 || arity >= REUSE_MAX_ARITY) return;
  HVM.heap[loc] = ((Term)HVM_FREE_HEAD[arity] << 1) | 1;
  HVM_FREE_HEAD[arity] = loc;
  HVM_FREED_CELLS += arity;
}

// Allocation and accounting
Loc alloc_node(Loc arity) {
  if (HVM_REUSE_ON && arity > 0 && arity < REUSE_MAX_ARITY) {
    Loc head = HVM_FREE_HEAD[arity];
    if (head != 0) {
      HVM_FREE_HEAD[arity] = (Loc)(HVM.heap[head] >> 1);
      HVM_REUSED_CELLS += arity;
      return head;
    }
  }
  if (*HVM.size + arity > MAX_HEAP_SIZE) {
    printf("Heap memory limit exceeded\n");
    exit(1);
  }
  u64 old = *HVM.size;
  *HVM.size += arity;
  return old;
}

void inc_itr() { (*HVM.itrs)++; }

