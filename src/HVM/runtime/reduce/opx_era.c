#include "Runtime.h"

// <op(* b)
// -------- OPX-ERA
// *
Term reduce_opx_era(Term opx, Term era) {
  inc_itr();
  free_node(term_loc(opx), 2); // OPX node dead (other operand leaked)
  return era;
}
