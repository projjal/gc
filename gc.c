#include "gc.h"

static size_t GC_hash(void* ptr) {
  // https://stackoverflow.com/questions/3442639/hashing-of-pointer-values
  // https://stackoverflow.com/questions/664014/what-integer-hash-function-are-good-that-accepts-an-integer-hash-key
  return ((uintptr_t) ptr * 2654435761) % 1<<32;
}

static void GC_add_ptr(GC* gc, void* ptr, size_t size) {
  gc->n_items++;

  size_t index = GC_hash(ptr) % gc->n_slots;

  GC_ptr gc_ptr;
  gc_ptr.ptr = ptr;
  gc_ptr.size = size;
  gc_ptr.marked = 0;

  while(gc->hmap[index].ptr != 0) index = (index + 1) % gc->n_slots;

  gc->hmap[index] = gc_ptr;
}

static void GC_rem_ptr(GC* gc, void* ptr) {
  size_t hash_idx = GC_hash(ptr) % gc->n_slots, i = hash_idx, index;
  while (1) {
    if (gc->hmap[i].ptr == 0) return;
    if (gc->hmap[i].ptr == ptr) {
      memset(&gc->hmap[i], 0, sizeof(GC_ptr));
      index = i;
      i = (i + 1) % gc->n_slots;
      break;
    }
    i = (i + 1) % gc->n_slots;
  }

  // rehash all entries in the same cluster
  while(gc->hmap[i].ptr != 0) {
    GC_ptr gc_ptr = gc->hmap[i];

    // delete entry
    memset(&gc->hmap[i], 0, sizeof(GC_ptr));
    
    // reinsert
    size_t index = GC_hash(gc_ptr.ptr) % gc->n_slots;
    while(gc->hmap[index].ptr != 0) index = (index + 1) % gc->n_slots;

    gc->hmap[index] = gc_ptr;

    i = (i + 1) % gc->n_slots;
  }

  gc->n_items--;
}

static void GC_rehash(GC* gc, size_t new_size) {
  GC_ptr* old_map = gc->hmap;
  size_t old_size = gc->n_slots;

  gc->n_slots = new_size;
  gc->hmap = calloc(new_size, sizeof(GC_ptr));
  if (gc->hmap == NULL) {
    gc->n_slots = old_size;
    gc->hmap = old_map;
    return;
  }

  for (size_t i = 0; i < old_size; i++) {
    if (old_map[i].ptr == 0) continue;
    GC_add_ptr(gc, old_map[i].ptr, old_map[i].size);
  }

  free(old_map);

}

static void GC_resize_if_needed(GC* gc) {
  double lf = (double) gc->n_items / (double) gc->n_slots;

  if (lf > gc->lf_max) GC_rehash(gc, gc->n_slots * 2);
  if (lf > 0 && lf < gc->lf_min) GC_rehash(gc, gc->n_slots/2);
}

void* GC_malloc(GC* gc, size_t size) {
  void *ptr = malloc(size);
  // if allocation fails retry after running gc
  if (ptr == NULL) {
      GC_run(gc);
      ptr = malloc(size);
  }
  if (ptr != NULL) {
      GC_add_ptr(gc, ptr, size);
      GC_resize_if_needed(gc);
  }
  return ptr;
}

void GC_free(GC* gc, void* ptr) {
  free(ptr);
  GC_rem_ptr(gc, ptr);
}

static void GC_mark_ptr(GC* gc, void* ptr) { // assume ptr is aligned
  size_t index = GC_hash(ptr) % gc->n_slots;

  while (1) {
    if (gc->hmap[index].ptr == 0) return;
    if (gc->hmap[index].ptr == ptr) {
      if (gc->hmap[index].marked != 1) {
        gc->hmap[index].marked = 1;
        // recursively mark all the potentially reachable pointers
        for (void* p = *(void**)ptr;
            p <= *(void**)ptr + gc->hmap[index].size - sizeof(void*);
            p += sizeof(void*)) {
          GC_mark_ptr(gc, p);
        }
      }
      return;
    }
    index = (index + 1) % gc->n_slots;
  }
}

static void GC_mark_stack(GC* gc) {
  void* dummy;
  void* bottom = gc->bottom;
  void* top = &dummy;
  for (void* p = top; p <= bottom; p += sizeof(void*)) { // stack grows downwards
    GC_mark_ptr(gc, p);
  }
}

static void GC_mark(GC* gc) {
  if (gc->n_items == 0) return;

  void (*volatile mark_stack)(GC*) = GC_mark_stack;

  jmp_buf env;
  memset(&env, 0, sizeof(env));
  setjmp(env);

  mark_stack(gc);
}

static void GC_sweep(GC* gc) {
  if (gc->n_items == 0) return;

  for (size_t i = 0; i < gc->n_slots; ++i) {
    if (gc->hmap[i].ptr == 0) continue;
    if (gc->hmap[i].marked == 1) continue;

    free(gc->hmap[i].ptr);
    GC_rem_ptr(gc, gc->hmap[i].ptr);
  }

  GC_resize_if_needed(gc);
}

void GC_run(GC* gc) {
  GC_mark(gc);
  GC_sweep(gc);
}