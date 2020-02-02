#ifndef GC_H
#define GC_H

#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

typedef struct GC_ptr {
  void* ptr;
  size_t size;
  int marked;
} GC_ptr;

typedef struct GC {
  void* bottom;
  GC_ptr* hmap;
  size_t n_items;
  size_t n_slots;
  double lf_max, lf_min;
} GC;

void* GC_malloc(GC* gc, size_t size);
void GC_free(GC* gc, void* ptr);
void GC_run(GC* gc);

#endif