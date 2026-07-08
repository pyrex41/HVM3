#include "Runtime.h"

// ~ num {K0 K1 K2 ... KN}
// ----------------------- MAT-W32
// if n < N: Kn
// else    : KN(num-N)
Term reduce_mat_w32(Term mat, Term w32) {
  inc_itr();
  Loc mat_loc = term_loc(mat);
  Lab mat_lab = term_lab(mat);
  u64 mat_len = mat_lab;
  u64 w32_val = term_loc(w32);
  if (w32_val < mat_len - 1) {
    Term arm = got(mat_loc + 1 + w32_val);
    for (u64 i = 1; i <= mat_len; i++) {
      if (i != 1 + w32_val) collect_at(mat_loc + i); // dropped arms
    }
    free_node(mat_loc, 1 + mat_len); // SWI node dead
    return arm;
  } else {
    Term fn = got(mat_loc + mat_len);
    for (u64 i = 1; i < mat_len; i++) collect_at(mat_loc + i); // dropped numeric arms
    Loc app;
    if (reuse_enabled()) {
      // Place the APP in a fresh (recycled) 2-node and free the whole SWI
      // block, so full-size blocks flow back to the next SWI allocation
      // instead of being torn into stranded size classes. Alloc first:
      // freeing first would let this 2-alloc split the just-freed block.
      app = alloc_node(2);
      free_node(mat_loc, 1 + mat_len);
    } else {
      app = mat_loc; // stock path: reuse the SWI node in place
    }
    set(app + 0, fn);
    set(app + 1, term_new(W32, 0, w32_val - (mat_len - 1)));
    return term_new(APP, 0, app);
  }
}
