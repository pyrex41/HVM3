#include "Runtime.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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
  // Every attachment may point at a different/remapped heap. Drop this .so's
  // stale local free-list links before enabling or disabling compiled reuse.
  // Treat conventional false spellings as false rather than enabling merely
  // because the variable exists (notably HVM_REUSE_C=0).
  reuse_reset();
  const char* reuse = getenv("HVM_REUSE_C");
  bool enabled = reuse != NULL && reuse[0] != '\0'
              && strcmp(reuse, "0") != 0
              && strcasecmp(reuse, "false") != 0
              && strcasecmp(reuse, "no") != 0
              && strcasecmp(reuse, "off") != 0;
  hvm_set_reuse(enabled);
  hvm_profile_init();
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

