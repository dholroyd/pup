#include <pthread.h>
#include <semaphore.h>

struct PupHeapRegion;
struct PupTheadInfo;

struct PupHeap {
	pthread_key_t this_thread_info;
	struct PupHeapRegion *region_list;
	pthread_t gc_thread;
	struct PupThreadInfo *thread_list;
	sem_t safepoint_sem;
	struct PupGCState *gc_state;
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

/**
 * setup the calling thread to participate in heap usage
 */
int pup_heap_thread_init(struct PupHeap *heap);

/**
 * generated code should probably just use pup_safepoint(ENV)
 */
void pup_heap_safepoint(struct PupHeap *heap);
