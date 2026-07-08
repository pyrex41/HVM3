#include "Runtime.h"

// ! &L{x y} = #{a b c ...}
// ------------------------ DUP-CTR
// ! &L{a0 a1} = a
// ! &L{b0 b1} = b
// ! &L{c0 c1} = c
// ...
// x <- #{a0 b0 c0 ...} 
// y <- #{a1 b1 c1 ...}
Term reduce_dup_ctr(Term dup, Term ctr) {
  inc_itr();
  Loc dup_loc = term_loc(dup);
  Lab dup_lab = term_lab(dup);
  Loc ctr_loc = term_loc(ctr);
  Lab ctr_lab = term_lab(ctr);
  u64 ctr_ari = HVM.cari[ctr_lab];

  // Allocated as separate extents (ctr block + one cell per dup node) so
  // that, under reuse, each piece is requested at the size class it will
  // later be freed at: ctr1 dies as a CTR(ctr_ari) free in mat_ctr, each
  // dup cell dies as a 1-cell free at DP deref. A fused 2*ctr_ari extent
  // can never be re-formed from those fragments (no coalescing), which
  // made every dup of a 2-field ctor bump fresh heap forever. Under pure
  // bump (reuse off) consecutive allocs are contiguous, so the layout is
  // identical to the original fused alloc_node(ctr_ari * 2).
  Loc ctr1    = alloc_node(ctr_ari);
  Loc ctr0    = ctr_loc;
  for (u64 i = 0; i < ctr_ari; i++) {
    Loc du0 = alloc_node(1);
    set(du0 + 0, got(ctr_loc + i));
    set(ctr0 + i, term_new(DP0, dup_lab, du0));
    set(ctr1 + i, term_new(DP1, dup_lab, du0));
  }
  if (term_tag(dup) == DP0) {
    sub(dup_loc + 0, term_new(CTR, ctr_lab, ctr1));
    return term_new(CTR, ctr_lab, ctr0);
  } else {
    sub(dup_loc + 0, term_new(CTR, ctr_lab, ctr0));
    return term_new(CTR, ctr_lab, ctr1);
  }
}
