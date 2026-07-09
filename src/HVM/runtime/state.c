#include "Runtime.h"
#include <stdlib.h>

State HVM = {
  .sbuf = NULL,
  .spos = NULL,
  .heap = NULL,
  .size = NULL,
  .itrs = NULL,
  .frsh = NULL,
  .book = {NULL},
  .cari = {0},
  .clen = {0},
  .cadt = {0},
  .fari = {0},
};

State* hvm_get_state() {
  return &HVM;
}

void hvm_set_state(State* hvm) {
  HVM.sbuf = hvm->sbuf;
  HVM.spos = hvm->spos;
  HVM.heap = hvm->heap;
  HVM.size = hvm->size;
  HVM.itrs = hvm->itrs;
  HVM.frsh = hvm->frsh;
  for (int i = 0; i < 65536; i++) {
    HVM.book[i] = hvm->book[i];
    HVM.fari[i] = hvm->fari[i];
    HVM.cari[i] = hvm->cari[i];
    HVM.clen[i] = hvm->clen[i];
    HVM.cadt[i] = hvm->cadt[i];
  }
  // Compiled-mode freelist (opt-in). This runs inside the dlopen'd .so, so it
  // toggles the .so's OWN copy of the freelist statics (the main process and
  // the .so each compile heap.c separately; only the HVM struct / heap array
  // above is shared). Gated on HVM_REUSE_C so compiled reuse is A/B testable
  // and off by default. The reduction rules in the generated C free dead
  // cells via free_node(); without this flag those calls are no-ops.
  if (getenv("HVM_REUSE_C") != NULL) {
    hvm_set_reuse(1);
  }
}

void hvm_define(u16 fid, Term (*func)()) {
  HVM.book[fid] = func;
}

void hvm_set_cari(u16 cid, u16 arity) {
  HVM.cari[cid] = arity;
}

void hvm_set_fari(u16 fid, u16 arity) {
  HVM.fari[fid] = arity;
}

void hvm_set_clen(u16 cid, u16 cases) {
  HVM.clen[cid] = cases;
}

void hvm_set_cadt(u16 cid, u16 adt) {
  HVM.cadt[cid] = adt;
}

