#include "Runtime.h"

// ~ * {K0 K1 K2 ...} 
// ------------------ MAT-ERA
// *
Term reduce_mat_era(Term mat, Term era) {
  inc_itr();
  Tag mat_tag = term_tag(mat);
  Lab mat_lab = term_lab(mat);
  u64 mat_len = mat_tag == SWI ? mat_lab : mat_tag == IFL ? 2 : HVM.clen[mat_lab];
  free_node(term_loc(mat), 1 + mat_len); // MAT node dead (arm subtrees leaked)
  return era;
}
