#include "Runtime.h"

// (* a)
// ------- APP-ERA
// *
Term reduce_app_era(Term app, Term era) {
  inc_itr();
  free_node(term_loc(app), 2); // APP node dead (arg subtree is leaked)
  return era;
}
