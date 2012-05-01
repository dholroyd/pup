#ifndef _HEAP_H
#define _HEAP_H

#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>
#include <atomic_ops.h>

struct PupHeapRegion;
struct PupTheadInfo;

struct PupHeap {
	pthread_key_t this_thread_info;
	struct PupHeapRegion *region_list;
	pthread_t gc_thread;
	// actually a 'struct PupThreadInfo *',
	volatile AO_t thread_list;
	// barrier used for each attempt by the gc thread to coordinate with
	// a mutator thread at a mutator safepoint,
	pthread_barrier_t safepoint_barrier;
	// actually a 'struct PupGCState *'
	volatile AO_t gc_state;
	// actually a 'struct PupVolitileHeapRegion *'
	volatile AO_t current_global_allocation;
};

int pup_heap_init(struct PupHeap *heap);
struct PupHeapRegion *pup_heap_region_allocate();

enum PupHeapKind {
	PUP_KIND_OBJ,
	PUP_KIND_ATTR
};

bool pup_heap_region_have_room_for(struct PupHeapRegion *region, size_t size);

/**
 * Must be called from the same thread that called pup_heap_init()
 */
void pup_heap_destroy(struct PupHeap *heap);

void *pup_heap_alloc(struct PupHeap *heap, size_t size, enum PupHeapKind kind);

void *pup_heap_alloc_for_gc_copy(struct PupHeap *heap,
                                 size_t size,
                                 enum PupHeapKind kind);

void pup_heap_add_to_global_heap(struct PupHeap *heap,
                                 struct PupHeapRegion *region);

void *pup_heap_region_make_room_for(struct PupHeapRegion *region,
                                    const size_t size,
                                    const enum PupHeapKind kind);

/**
 * setup the calling thread to participate in heap usage
 */
int pup_heap_thread_init(struct PupHeap *heap);

/**
 * generated code should probably just use pup_safepoint(ENV)
 */
void pup_heap_safepoint(struct PupHeap *heap);

#endif  // _HEAP_H
