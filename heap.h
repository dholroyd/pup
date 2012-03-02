#include <pthread.h>

struct PupHeapRegion;

struct PupHeap {
	pthread_key_t threadlocal_region;
	struct PupHeapRegion *region_list;
	pthread_t gc_thread;
};

int pup_heap_init(struct PupHeap *heap);

enum PupHeapKind {
	PUP_KIND_OBJ,
	PUP_KIND_ATTR
};

/**
 * Must be called from the same thread that called pup_heap_init()
 */
void pup_heap_destroy(struct PupHeap *heap);

void *pup_heap_alloc(struct PupHeap *heap, size_t size, enum PupHeapKind kind);
