#include "Runtime.h"

// (* a)
// ------- APP-ERA
// *
Term reduce_app_era(Term app, Term era) {
  inc_itr();
  collect_at(term_loc(app) + 1); // dropped arg subtree
  free_node(term_loc(app), 2);   // APP node dead
  return era;
}
