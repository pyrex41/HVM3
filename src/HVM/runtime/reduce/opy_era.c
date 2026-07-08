#include "Runtime.h"

// >op(a *)
// -------- OPY-ERA
// *
Term reduce_opy_era(Term opy, Term era) {
  inc_itr();
  free_node(term_loc(opy), 2); // OPY node dead (stored operand is an atom)
  return era;
}
