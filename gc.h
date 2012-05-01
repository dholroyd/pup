#ifndef _GC_H
#define _GC_H

#include "heap.h"

struct PupGCState;
struct PupObject;

void pup_gc_scan_stack(struct PupGCState *state);
struct PupGCState *pup_gc_state_create(void);
void pup_gc_state_destroy(struct PupGCState *state);
void pup_gc_scan_heap(struct PupGCState *state);
bool pup_gc_mark_reachable(struct PupGCState *state, struct PupObject *ref);
int pup_gc_get_current_mark(const struct PupGCState *state);
bool pup_gc_is_live_mark(const struct PupGCState *state, const int mark_value);
void pup_gc_inc_garbage_count(struct PupGCState *state);
void pup_gc_inc_live_count(struct PupGCState *state);
void pup_gc_period_start(struct PupGCState *state);
void pup_gc_period_end(struct PupGCState *state);
void *pup_gc_alloc_for_copy(struct PupGCState *state,
                            struct PupHeap *heap,
                            size_t size,
                            enum PupHeapKind kind);
void pup_gc_queue_for_marking(struct PupGCState *state, void **ref);

#endif  // _GC_H
