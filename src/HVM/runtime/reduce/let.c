#include "Runtime.h"

// ! x = val
// bod
// --------- LET
// x <- val
// bod
Term reduce_let(Term let, Term val) {
  inc_itr();
  Loc let_loc = term_loc(let);
  Term bod    = got(let_loc + 1);
  sub(let_loc + 0, val);
  free_node(let_loc + 1, 1); // body cell dead; let_loc+0 lives on as a sub cell
  return bod;
}
