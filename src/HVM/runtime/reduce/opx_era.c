#include "Runtime.h"

// <op(* b)
// -------- OPX-ERA
// *
Term reduce_opx_era(Term opx, Term era) {
  inc_itr();
  collect_at(term_loc(opx) + 1); // dropped second operand
  free_node(term_loc(opx), 2);   // OPX node dead
  return era;
}
