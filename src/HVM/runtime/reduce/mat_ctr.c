#include "Runtime.h"

Term reduce_mat_ctr(Term mat, Term ctr) {
  inc_itr();
  Tag mat_tag = term_tag(mat);
  Loc mat_loc = term_loc(mat);
  Lab mat_lab = term_lab(mat);
  // If-Let
  if (mat_tag == IFL) {
    Loc ctr_loc = term_loc(ctr);
    Lab ctr_lab = term_lab(ctr);
    u64 mat_ctr = mat_lab;
    u64 ctr_num = ctr_lab;
    u64 ctr_ari = HVM.cari[ctr_num];
    if (mat_ctr == ctr_num) {
      Term app = got(mat_loc + 1);
      Loc loc = alloc_node(ctr_ari * 2);
      for (u64 i = 0; i < ctr_ari; i++) {
        Loc new_app = loc + i * 2;
        set(new_app + 0, app);
        set(new_app + 1, got(ctr_loc + i));
        app = term_new(APP, 0, new_app);
      }
      collect_at(mat_loc + 2);     // dropped else arm
      free_node(mat_loc, 3);       // IFL node dead
      free_node(ctr_loc, ctr_ari); // CTR fields copied out
      return app;
    } else {
      Term app = got(mat_loc + 2);
      Loc new_app;
      if (reuse_enabled()) {
        collect_at(mat_loc + 1);  // dropped then-arm
        new_app = alloc_node(2);  // alloc first so the free below survives whole
        free_node(mat_loc, 3);
      } else {
        new_app = mat_loc;        // stock path: reuse the IFL node in place
      }
      set(new_app + 0, app);
      set(new_app + 1, ctr);
      app = term_new(APP, 0, new_app);
      return app;
    }
  // Match
  } else {
    Loc ctr_loc = term_loc(ctr);
    Lab ctr_lab = term_lab(ctr);
    u64 ctr_num = ctr_lab;
    u64 ctr_ari = HVM.cari[ctr_num];
    u64 mat_ctr = mat_lab;
    u64 cadt = HVM.cadt[mat_ctr];
    u64 clen = HVM.clen[mat_ctr];
    if (ctr_num < cadt || ctr_num >= cadt + clen) {
      printf("invalid:mat-ctr(%llu, %llu)\n", (unsigned long long)ctr_num, (unsigned long long)cadt);
      exit(1);
    }
    u64 cse_idx = ctr_num - mat_ctr;
    Term app = got(mat_loc + 1 + cse_idx);
    Loc loc = alloc_node(ctr_ari * 2);
    for (u64 i = 0; i < ctr_ari; i++) {
      Loc new_app = loc + i * 2;
      set(new_app + 0, app);
      set(new_app + 1, got(ctr_loc + i));
      app = term_new(APP, 0, new_app);
    }
    for (u64 i = 1; i <= clen; i++) {
      if (i != 1 + cse_idx) collect_at(mat_loc + i); // dropped arms
    }
    free_node(mat_loc, 1 + clen); // MAT node dead
    free_node(ctr_loc, ctr_ari);  // CTR fields copied out
    return app;
  }
}
