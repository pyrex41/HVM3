#include "Runtime.h"
#include <string.h>

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
// Large tier: exact-size buckets for blocks of 64..REUSE_L_MAX-1 cells.
// Shen-scale programs churn big CTR blocks (absvectors: symbol tables,
// property vectors, dicts) whose functional update copies the whole
// vector; leaking those (the old "arity >= 64 is leaked" rule) made
// suite memory grow by the full vector size on every update. Vector
// sizes recur exactly, so exact-size buckets almost always hit; a
// bitmap scan finds the smallest larger block otherwise (split like
// the small tier).
#define REUSE_L_MAX     65536

static bool HVM_REUSE_ON = false;
static Loc  HVM_FREE_HEAD[REUSE_MAX_ARITY];
static u64  HVM_REUSE_MASK   = 0; // bit i set iff HVM_FREE_HEAD[i] != 0
static u64  HVM_FREED_CELLS  = 0;
static u64  HVM_REUSED_CELLS = 0;
static u64  HVM_BUMPS[REUSE_MAX_ARITY];
static u64  HVM_NFREE[REUSE_MAX_ARITY]; // blocks currently on each list
static Loc  HVM_FREE_HEAD_L[REUSE_L_MAX];
static u64  HVM_REUSE_MASK_L[REUSE_L_MAX / 64]; // bit (a&63) of word a>>6
static u64  HVM_NFREE_L       = 0; // total blocks in the large tier
static u64  HVM_BUMP_L_BLOCKS = 0;
static u64  HVM_BUMP_L_CELLS  = 0;
static int  HVM_TRACE_ON      = -1;         // lazy getenv(HVM_REUSE_TRACE)
static u64  HVM_TRACE_MARK    = 1ULL << 27; // next size threshold (~1GB)

void hvm_set_reuse(u64 on) { HVM_REUSE_ON = on != 0; }
bool reuse_enabled() { return HVM_REUSE_ON; }

// Drops all freelist entries. Must be called whenever the bump pointer is
// rolled back (set_len) or the heap is remapped (hvm_init): stale entries
// above the new top would alias future bump allocations.
void reuse_reset() {
  for (u64 i = 0; i < REUSE_MAX_ARITY; i++) {
    HVM_FREE_HEAD[i] = 0;
  }
  HVM_REUSE_MASK = 0;
  memset(HVM_FREE_HEAD_L, 0, sizeof(HVM_FREE_HEAD_L));
  memset(HVM_REUSE_MASK_L, 0, sizeof(HVM_REUSE_MASK_L));
  HVM_NFREE_L = 0;
}

u64 get_frees()  { return HVM_FREED_CELLS; }
u64 get_reuses() { return HVM_REUSED_CELLS; }

// Free-list links are stored in the first cell of each freed block as a
// SUB-tagged term (tag 0x03 is never dispatched on at runtime, and the
// sub bit is 0) with the next block's loc in the loc field. This keeps
// freed cells non-zero for got()/swap(), and guarantees that a stale read
// of a freed cell (e.g. collect() walking a dropped lambda whose bound VAR
// points back at the already-freed binder cell) sees bit=0 and an inert
// tag instead of a plausible-looking term.
static void reuse_push(Loc loc, Loc cls) {
  HVM.heap[loc] = term_new(SUB, 0, HVM_FREE_HEAD[cls]);
  HVM_FREE_HEAD[cls] = loc;
  HVM_REUSE_MASK |= 1ULL << cls;
  HVM_NFREE[cls]++;
}

static Loc reuse_pop(Loc cls) {
  Loc head = HVM_FREE_HEAD[cls];
  HVM_FREE_HEAD[cls] = term_loc(HVM.heap[head]);
  if (HVM_FREE_HEAD[cls] == 0) HVM_REUSE_MASK &= ~(1ULL << cls);
  HVM_NFREE[cls]--;
  return head;
}

// Large-tier list ops (same chaining scheme as the small tier).
static void reuse_push_l(Loc loc, Loc cls) {
  HVM.heap[loc] = term_new(SUB, 0, HVM_FREE_HEAD_L[cls]);
  HVM_FREE_HEAD_L[cls] = loc;
  HVM_REUSE_MASK_L[cls >> 6] |= 1ULL << (cls & 63);
  HVM_NFREE_L++;
}

static Loc reuse_pop_l(Loc cls) {
  Loc head = HVM_FREE_HEAD_L[cls];
  HVM_FREE_HEAD_L[cls] = term_loc(HVM.heap[head]);
  if (HVM_FREE_HEAD_L[cls] == 0) HVM_REUSE_MASK_L[cls >> 6] &= ~(1ULL << (cls & 63));
  HVM_NFREE_L--;
  return head;
}

// Push to whichever tier fits; blocks >= REUSE_L_MAX are leaked.
static void reuse_push_any(Loc loc, Loc cls) {
  if (cls == 0) return;
  if (cls < REUSE_MAX_ARITY) reuse_push(loc, cls);
  else if (cls < REUSE_L_MAX) reuse_push_l(loc, cls);
}

// Smallest large-tier class >= arity, or 0 if none.
static Loc reuse_find_l(Loc arity) {
  u64 w = arity >> 6;
  u64 m = HVM_REUSE_MASK_L[w] & (~0ULL << (arity & 63));
  if (m != 0) return (Loc)((w << 6) + __builtin_ctzll(m));
  for (w++; w < REUSE_L_MAX / 64; w++) {
    if (HVM_REUSE_MASK_L[w] != 0) return (Loc)((w << 6) + __builtin_ctzll(HVM_REUSE_MASK_L[w]));
  }
  return 0;
}

// Returns a dead block of `arity` cells at `loc` to the freelist.
// The block must be fully unreachable (see the per-rule lifecycle map).
// Blocks >= REUSE_L_MAX cells are leaked (missed frees only cost memory).
void free_node(Loc loc, Loc arity) {
  if (!HVM_REUSE_ON || arity == 0 || arity >= REUSE_L_MAX) return;
  reuse_push_any(loc, arity);
  HVM_FREED_CELLS += arity;
}

// Diagnostics: per-ctor-label counters for DUP-CTR and MAT-CTR (who is
// being duplicated/matched); bumped from dup_ctr.c / mat_ctr.c.
u64 HVM_DUPC_BY_LAB[65536];
u64 HVM_MATC_BY_LAB[65536];

static void dump_top_labs(const char *name, u64 *tab) {
  for (int k = 0; k < 4; k++) {
    u64 best = 0, besti = 0;
    for (u64 i = 0; i < 65536; i++) {
      if (tab[i] > best) { best = tab[i]; besti = i; }
    }
    if (best == 0) break;
    fprintf(stderr, "  %s lab=%llu: %llu\n", name, (unsigned long long)besti, (unsigned long long)best);
    tab[besti] = 0; // destructive top-k; totals keep accumulating after
  }
}

// Optional growth trace: with HVM_REUSE_TRACE set, print one line to
// stderr every time the bump watermark crosses another 128M cells.
static void reuse_trace_maybe() {
  if (HVM_TRACE_ON < 0) HVM_TRACE_ON = getenv("HVM_REUSE_TRACE") != NULL;
  if (!HVM_TRACE_ON || *HVM.size < HVM_TRACE_MARK) return;
  HVM_TRACE_MARK = *HVM.size + (1ULL << 27);
  fprintf(stderr, "REUSE_TRACE size=%llu freed=%llu reused=%llu bumpL=%llu blocks/%llu cells nfreeL=%llu\n",
          (unsigned long long)*HVM.size, (unsigned long long)HVM_FREED_CELLS,
          (unsigned long long)HVM_REUSED_CELLS, (unsigned long long)HVM_BUMP_L_BLOCKS,
          (unsigned long long)HVM_BUMP_L_CELLS, (unsigned long long)HVM_NFREE_L);
  for (u64 i = 0; i < REUSE_MAX_ARITY; i++) {
    if (HVM_BUMPS[i] > 1000000) fprintf(stderr, "  BUMP[%llu]: %llu\n", (unsigned long long)i, (unsigned long long)HVM_BUMPS[i]);
  }
  dump_top_labs("DUP-CTR", HVM_DUPC_BY_LAB);
  dump_top_labs("MAT-CTR", HVM_MATC_BY_LAB);
}

// Allocation and accounting
Loc alloc_node(Loc arity) {
  if (HVM_REUSE_ON && arity > 0 && arity < REUSE_L_MAX) {
    if (arity < REUSE_MAX_ARITY) {
      // Smallest sufficient size class (exact class first); split the tail
      // back onto the freelist. Blocks are plain extents, so any k-extent
      // serves as an a-extent plus a (k-a)-extent.
      u64 cand = HVM_REUSE_MASK >> arity;
      if (cand != 0) {
        Loc cls  = arity + (Loc)__builtin_ctzll(cand);
        Loc head = reuse_pop(cls);
        if (cls > arity) reuse_push(head + arity, cls - arity);
        HVM_REUSED_CELLS += arity;
        return head;
      }
    }
    // Large tier (also serves small requests when the small tier is dry).
    if (HVM_NFREE_L > 0) {
      Loc cls = reuse_find_l(arity);
      if (cls != 0) {
        Loc head = reuse_pop_l(cls);
        if (cls > arity) reuse_push_any(head + arity, cls - arity);
        HVM_REUSED_CELLS += arity;
        return head;
      }
    }
  }
  if (*HVM.size + arity > MAX_HEAP_SIZE) {
    printf("Heap memory limit exceeded\n");
    exit(1);
  }
  if (HVM_REUSE_ON) {
    if (arity < REUSE_MAX_ARITY) HVM_BUMPS[arity]++;
    else { HVM_BUMP_L_BLOCKS++; HVM_BUMP_L_CELLS += arity; }
    reuse_trace_maybe();
  }
  u64 old = *HVM.size;
  *HVM.size += arity;
  return old;
}

void reuse_dump() {
  for (u64 i = 0; i < REUSE_MAX_ARITY; i++) {
    if (HVM_BUMPS[i] > 0) fprintf(stderr, "BUMP[%llu]: %llu\n", (unsigned long long)i, (unsigned long long)HVM_BUMPS[i]);
    if (HVM_NFREE[i] > 0) fprintf(stderr, "FREELIST[%llu]: %llu blocks\n", (unsigned long long)i, (unsigned long long)HVM_NFREE[i]);
  }
  if (HVM_BUMP_L_BLOCKS > 0) fprintf(stderr, "BUMP_LARGE: %llu blocks / %llu cells\n", (unsigned long long)HVM_BUMP_L_BLOCKS, (unsigned long long)HVM_BUMP_L_CELLS);
  if (HVM_NFREE_L > 0) fprintf(stderr, "FREELIST_LARGE: %llu blocks\n", (unsigned long long)HVM_NFREE_L);
}

// Eraser propagation (collect)
//
// Frees a DROPPED subtree: a subgraph whose one owning occurrence was
// discarded by an interaction (unselected match arms, APP-ERA'd args,
// unused REF args). Walks the tree iteratively and returns every owned
// node block to the freelist. Conservative at binder boundaries:
// - VAR/DP0/DP1 with sub bit unset: the binder/dup cell is still owned by
//   a live LAM/DUP node; leave it (at worst a 1-cell leak if it later
//   gets substituted with no reader).
// - VAR/DP0/DP1 with sub bit set: this occurrence is the unique reader of
//   the sub cell (linearity); free the cell and collect the value.
// No-op unless reuse is enabled, so compiled mode is unaffected.
static Term* CLT_BUF = NULL;
static u64   CLT_CAP = 0;
static u64   CLT_POS = 0;

static void collect_push(Term t) {
  if (CLT_POS == CLT_CAP) {
    CLT_CAP = CLT_CAP ? CLT_CAP * 2 : 4096;
    CLT_BUF = realloc(CLT_BUF, CLT_CAP * sizeof(Term));
    if (CLT_BUF == NULL) {
      printf("collect stack alloc failed\n");
      exit(1);
    }
  }
  CLT_BUF[CLT_POS++] = t;
}

void collect(Term term) {
  if (!HVM_REUSE_ON) return;
  CLT_POS = 0;
  collect_push(term);
  while (CLT_POS > 0) {
    Term t = CLT_BUF[--CLT_POS];
    if (term_get_bit(t) != 0) continue;
    Tag tag = term_tag(t);
    Lab lab = term_lab(t);
    Loc loc = term_loc(t);
    switch (tag) {
      case ERA: case W32: case CHR: break;
      case VAR: case DP0: case DP1: {
        Term s = got(loc);
        if (term_get_bit(s) != 0) {
          free_node(loc, 1);
          collect_push(term_rem_bit(s));
        }
        break;
      }
      case LAM: {
        Term bod = got(loc);
        if (term_get_bit(bod) == 0) {
          collect_push(bod);
          free_node(loc, 1);
        }
        break;
      }
      case LET: {
        Term val = got(loc);
        if (term_get_bit(val) == 0) {
          collect_push(val);
          collect_push(got(loc + 1));
          free_node(loc, 2);
        }
        break;
      }
      case APP: case SUP: case OPX: case OPY: {
        collect_push(got(loc + 0));
        collect_push(got(loc + 1));
        free_node(loc, 2);
        break;
      }
      case INC: case DEC: {
        collect_push(got(loc + 0));
        free_node(loc, 1);
        break;
      }
      case CTR: {
        u64 ari = HVM.cari[lab];
        for (u64 i = 0; i < ari; i++) collect_push(got(loc + i));
        free_node(loc, ari);
        break;
      }
      case MAT: case IFL: case SWI: {
        u64 len = tag == SWI ? lab : tag == IFL ? 2 : HVM.clen[lab];
        for (u64 i = 0; i <= len; i++) collect_push(got(loc + i));
        free_node(loc, 1 + len);
        break;
      }
      case REF: {
        u64 ari = HVM.fari[lab];
        for (u64 i = 0; i < ari; i++) collect_push(got(loc + i));
        free_node(loc, ari);
        break;
      }
      default: break; // SUB or unknown: leave untouched
    }
  }
}

// Collects the subtree referenced from a heap slot (gated: no reads when
// reuse is off, keeping the disabled path identical to stock).
void collect_at(Loc loc) {
  if (!HVM_REUSE_ON) return;
  collect(got(loc));
}

void inc_itr() { (*HVM.itrs)++; }

