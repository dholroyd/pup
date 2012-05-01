#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <atomic_ops.h>
#include <valgrind/drd.h>
#include "abortf.h"
#include "heap.h"
#include "object.h"
#include "gc.h"

#define REGION_SIZE 0x100000
#define MAX_REGION_ALLOCATION 0x1000

struct PupHeapRegion {
	void *region;
	void *end;
	void *allocated;
	// actually a 'struct PupHeapRegion *',
	AO_t next;
	int read_only;
};

struct PupThreadInfo {
	AO_t tid;
	struct PupHeapRegion *local_region;
	// actually a 'struct PupThreadInfo *',
	AO_t next;
	// these values get modified in signal-handler context, therefore they
	// are marked volatile,
	volatile int gc_waiting;
	volatile int current_gc_mark;
};

// TODO: optimise HeapObject layout
struct HeapObject {
	size_t object_size;  // the requested size (NB not HeapObject's size)
	unsigned int kind : 1;  // is it an object or an AttrListEntry
	char data[0];  // actual object data starts from here
};

struct PupHeapRegion *pup_heap_region_allocate(void)
{
	struct PupHeapRegion *region = malloc(sizeof(struct PupHeapRegion));
	if (!region) {
		return NULL;
	}
	region->next = 0;
	// TODO: assert REGION_SIZE is a multiple of page-size
	region->region = mmap(NULL,
	                    REGION_SIZE,
	                    PROT_READ|PROT_WRITE,
			    MAP_PRIVATE|MAP_ANONYMOUS,
			    -1,  // no fd
			    0);  // no offset

	if (region->region == MAP_FAILED) {
		free(region);
		return NULL;
	}
	region->end = region->region + REGION_SIZE;
	region->allocated = region->region;
	region->read_only = false;
	return region;
}

static size_t alloc_size_for(size_t request_size)
{
	return request_size + sizeof(struct HeapObject);
}

static void destroy_region_objects(struct PupHeapRegion *region)
{
	void *ptr = region->region;
	while (ptr < region->allocated) {
		struct HeapObject *obj = (struct HeapObject *)ptr;
		switch (obj->kind) {
		    case PUP_KIND_OBJ:
			pup_object_destroy((struct PupObject *)obj->data);
		}
		ptr += alloc_size_for(obj->object_size);
	}
}

static void free_region(struct PupHeapRegion *region)
{
	destroy_region_objects(region);
	if (munmap(region->region, REGION_SIZE)) {
		fprintf(stderr, "munmap(%p, %d) unexpectedly failed: %s", region->region, REGION_SIZE, strerror(errno));
	}
	free(region);
}

static struct PupThreadInfo *get_thread_info(struct PupHeap *heap)
{
	void *res = pthread_getspecific(heap->this_thread_info);
	ABORT_ON(!res, "pthread_getspecific() produced null");
	return (struct PupThreadInfo *)res;
}

static int set_thread_info(struct PupHeap *heap, struct PupThreadInfo *info)
{
	ABORT_ON(!info, "set_thread_info() given null info");
	return pthread_setspecific(heap->this_thread_info, info);
}

static int set_local_region(struct PupHeap *heap, struct PupHeapRegion *region)
{
	ABORT_ON(!region, "set_local_region() given null region");
	get_thread_info(heap)->local_region = region;
	// TODO: error handling?
	return 0;
}

static struct PupThreadInfo *get_thread_list_head(
	struct PupHeap *heap
) {
	//ANNOTATE_HAPPENS_AFTER(&heap->thread_list);
	return (struct PupThreadInfo *)AO_load(&heap->thread_list);
}

static bool swap_thread_list_head(struct PupHeap *heap,
                                  struct PupThreadInfo *old_head,
                                  struct PupThreadInfo *new_head)
{
	return AO_compare_and_swap((AO_t *)&heap->thread_list,
	                           (AO_t)old_head,
	                           (AO_t)new_head);
}

static void attach_thread_to_heap(struct PupHeap *heap,
                             struct PupThreadInfo *new_head)
{
	while (true) {
		struct PupThreadInfo *old_head
			= get_thread_list_head(heap);
		if (swap_thread_list_head(heap, old_head, new_head)) {
			return;
		}
	}
}

static int attach_thread(struct PupHeap *heap, pthread_t thread)
{
	struct PupThreadInfo *tinfo = malloc(sizeof(struct PupThreadInfo));
	if (tinfo == NULL) {
		return -1;
	}
	AO_store(&tinfo->tid, thread);
	ANNOTATE_HAPPENS_BEFORE(&tinfo->tid);
	AO_store(&tinfo->next, 0);
	ANNOTATE_HAPPENS_BEFORE(&tinfo->next);
	tinfo->gc_waiting = false;
	if (set_thread_info(heap, tinfo)) {
		return -1;
	}
	attach_thread_to_heap(heap, tinfo);
	ANNOTATE_THREAD_NAME("mutator");
	return 0;
}

int pup_heap_thread_init(struct PupHeap *heap)
{
	struct PupHeapRegion *region = pup_heap_region_allocate();
	if (!region) {
		return -1;
	}
	int res = attach_thread(heap, pthread_self());
	if (res) {
		// TODO
		abort();
	}
	res = set_local_region(heap, region);
	if (res) {
		return res;
	}
	return 0;
}

static void safepoint_barrier_wait(struct PupHeap *heap)
{
	int res = pthread_barrier_wait(&heap->safepoint_barrier);
	ABORTF_ON(res && res!=PTHREAD_BARRIER_SERIAL_THREAD,
	          "pthread_barrier_wait() failed: %s",
	          strerror(errno));
}

static void announce_mutator_arrival(struct PupHeap *heap,
                                     struct PupThreadInfo *tinfo)
{
	safepoint_barrier_wait(heap);
	tinfo->gc_waiting = false;
}

static struct PupGCState *get_gc_state(struct PupHeap *heap)
{
	ANNOTATE_HAPPENS_AFTER(&heap->gc_state);
	return (struct PupGCState *)AO_load((AO_t *)&heap->gc_state);
}

void pup_heap_safepoint(struct PupHeap *heap)
{
	struct PupThreadInfo *tinfo = get_thread_info(heap);
	// the thread-local gc_waiting flag is set from a signal handler on
	// this thread, hence we don't use explicit atomic ops or locking
	// for this access
	if (tinfo->gc_waiting) {
		pup_gc_scan_stack(get_gc_state(heap));
		announce_mutator_arrival(heap, tinfo);
	}
}

static pthread_t get_thread(const struct PupThreadInfo *tinfo)
{
	ANNOTATE_HAPPENS_AFTER(&tinfo->tid);
	return AO_load(&tinfo->tid);
}

static void converge_on_safepoint(struct PupHeap *heap,
                                  struct PupThreadInfo *tinfo)
{
	union sigval sv;
	sv.sival_ptr = heap;
	pthread_t thread = get_thread(tinfo);
	fprintf(stderr, "converge_on_safepoint() signalling %p\n", (void *)thread);
	int ret = pthread_sigqueue(thread, SIGUSR1, sv);
	ABORTF_ON(ret, "sigqueue() failed: %s", strerror(errno));
	safepoint_barrier_wait(heap);
}

static struct PupHeapRegion *global_heap_head(struct PupHeap *heap)
{
	return (struct PupHeapRegion *)AO_load((AO_t *)&heap->region_list);
}

static bool swap_heap_region_head(struct PupHeap *heap,
                                  struct PupHeapRegion *old_region,
                                  struct PupHeapRegion *new_region)
{
	return AO_compare_and_swap((AO_t *)&heap->region_list,
	                           (AO_t)old_region,
	                           (AO_t)new_region);
}

static struct PupHeapRegion *region_next(struct PupHeapRegion *r)
{
	ANNOTATE_HAPPENS_AFTER(&r->next);
	return (struct PupHeapRegion *)AO_load(&r->next);
}

static struct PupHeapRegion *steal_heap_region(struct PupHeap *heap)
{
	while (true) {
		struct PupHeapRegion *r
			= global_heap_head(heap);
		if (!r) {
			return NULL;
		}
		struct PupHeapRegion *next = region_next(r);
		if (swap_heap_region_head(heap, r, next)) {
			return r;
		}
	}
}

static void collect_heap_object(struct HeapObject *obj,
                                struct PupGCState *state)
{
	switch (obj->kind) {
	    case PUP_KIND_OBJ:
		pup_object_gc_collect((struct PupObject *)obj->data, state);
		break;
	    case PUP_KIND_ATTR:
		pup_object_attr_gc_collect(obj->data, state);
		break;
	    default:
		ABORTF("Unknown object kind %d", obj->kind);
	}
}
static void prevent_mutator_access(struct PupHeap *heap,
                                   struct PupHeapRegion *region)
{
	region->read_only = true;
	// Prevent mutators being from writing to objects in this region while
	// we copy the live objects out.  Mutators attempting such writes
	// will be interrupted with SIGBUS, which we have to handle
	// TODO: write SIGBUS handling
	int res = mprotect(region->region,
	                   region->end - region->region,
	                   PROT_READ);
	ABORTF_ON(res, "mprotect failed: %s", strerror(errno));
}

static void *atomic_region_start(struct PupHeapRegion *region)
{
	return (void *)AO_load((AO_t *)&region->region);
}

static void collect_unmarked_objects_in_region(struct PupHeap *heap,
                                               struct PupHeapRegion *region)
{
	// make sure mutator threads can't alter objects in the process of
	// being copied
	// TODO: would it be a win to only if this if we find a live object
	//       in the region (i.e. there as actually a possibility of
	//       mutator access)?
	// FIXME: Where to unprotect the region again?
	prevent_mutator_access(heap, region);

	void *addr;
	for (addr=atomic_region_start(region); addr < region->allocated; ) {
		struct HeapObject *obj = addr;
		collect_heap_object(obj, get_gc_state(heap));
		addr += alloc_size_for(obj->object_size);
	}
}

static void collect_unmarked_objects(struct PupHeap *heap)
{
	struct PupHeapRegion *region;
	while ((region = steal_heap_region(heap))) {
		collect_unmarked_objects_in_region(heap, region);
		// reset the allocation for this region so that no object
		// freeing will occur,
		region->allocated = region->region;
		// release this region's memory,
		free_region(region);
	}
}

static struct PupThreadInfo *threadinfo_next(
	struct PupThreadInfo *tinfo
) {
	ANNOTATE_HAPPENS_AFTER(&tinfo->next);
	return (struct PupThreadInfo *)AO_load(&tinfo->next);
}

static void perform_gc(struct PupHeap *heap)
{
	struct PupGCState *gc_state = get_gc_state(heap);
	pup_gc_period_start(gc_state);
	for (struct PupThreadInfo *tinfo = get_thread_list_head(heap);
	     tinfo;
	     tinfo = threadinfo_next(tinfo))
	{
		converge_on_safepoint(heap, tinfo);
	}
	// all threads have arrived at a safepoint, scanned their stacks
	// for 'root' references, and added them to a reference queue, so
	// now scan the rest of the heap
	pup_gc_scan_heap(gc_state);

	collect_unmarked_objects(heap);
	pup_gc_period_end(gc_state);
}

static void *gc_thread(void *arg)
{
	struct PupHeap *heap = (struct PupHeap *)arg;
	struct timespec req = {
		.tv_sec = 0,
		.tv_nsec = 500000000
	};
	ANNOTATE_THREAD_NAME("garbage-collector");
	while (1) {
		int res = nanosleep(&req, NULL);
		ABORTF_ON(res==EINVAL, "nanosleep() reports invalid timespec");
		ABORTF_ON(res==EFAULT, "nanosleep() reports EFAULT");
		perform_gc(heap);
	}
	return NULL;
}

static int heap_thread_stop(struct PupHeap *heap)
{
	int res = pthread_cancel(heap->gc_thread);
	if (res) {
		return res;
	}
	
	res = pthread_join(heap->gc_thread, NULL);
	return res;
}

static int heap_thread_start(struct PupHeap *heap)
{
	pthread_attr_t attr;
	if (pthread_attr_init(&attr)) {
		return 1;
	}
	int res = pthread_create(&heap->gc_thread,
	                         &attr,
	                         gc_thread,
	                         (void *)heap);
	if (pthread_attr_destroy(&attr)) {
		heap_thread_stop(heap);
		return 1;
	}
	if (res) {
		return 1;
	}
	return 0;
}

static void pup_heap_thread_destroy(struct PupHeap *heap)
{
	struct PupHeapRegion *local_region = get_thread_info(heap)->local_region;
	if (local_region) {
		free_region(local_region);
	}
}

static void siguser1_handler(int sig, siginfo_t *si, void *unused)
{
	ABORTF_ON(si->si_code != SI_QUEUE, "expected SI_QUEUE(%d), got %d", SI_QUEUE, si->si_code);
	struct PupHeap *heap = (struct PupHeap *)si->si_value.sival_ptr;
	struct PupThreadInfo *tinfo = get_thread_info(heap);
	// set the thread-local variable indicating that when the (non-signal-
	// handler) code in this thread reaches a safepoint, it should notify
	// the gc thread that this has happened
	tinfo->gc_waiting = true;
	// sync the mark value used for thread local allocations with the
	// current global value
	tinfo->current_gc_mark = pup_gc_get_current_mark(get_gc_state(heap));
}

static int setup_signal_handling(struct PupHeap *heap)
{
	struct sigaction sa;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = siguser1_handler;
	int ret = sigaction(SIGUSR1, &sa, NULL);
	ABORTF_ON(ret, "sigaction() failed with errno %d", errno);
	// TODO: return status on error rather than abort()
	return 0;
}

static void set_gc_state(struct PupHeap *heap,
                         struct PupGCState *gc_state)
{
	AO_store((AO_t *)&heap->gc_state, (AO_t)gc_state);
	ANNOTATE_HAPPENS_BEFORE(&heap->gc_state);
}

int pup_heap_init(struct PupHeap *heap)
{
	fprintf(stderr, "pup_heap_init() pid=%d\n", getpid());
	int res;
	res = pthread_key_create(&heap->this_thread_info, NULL /* no dtor */);
	if (res) return res;

	heap->region_list = NULL;
	heap->thread_list = 0;

	// initialisation for the main thread,
	res = pup_heap_thread_init(heap);
	if (res) {
		// ignore return value, since we're cleaning up anyway,
		pthread_key_delete(heap->this_thread_info);
		return res;
	}
	res = setup_signal_handling(heap);
	if (res) {
		pthread_key_delete(heap->this_thread_info);
		return res;
	}
	res = heap_thread_start(heap);
	if (res) {
		pup_heap_thread_destroy(heap);
		pthread_key_delete(heap->this_thread_info);
		return res;
	}
	res = pthread_barrier_init(&heap->safepoint_barrier, NULL, 2);
	// FIXME: proper error handling,
	ABORTF_ON(res, "pthread_barrier_init() failed: %s",
	               strerror(errno));
	ANNOTATE_BARRIER_INIT(&heap->safepoint_barrier, 2, false);
	struct PupGCState *gc_state = pup_gc_state_create();
	// FIXME: proper error handling,
	ABORT_ON(!gc_state, "pup_gc_state_create() failed");
	set_gc_state(heap, gc_state);
	return 0;
}

static void destroy_global_heap(struct PupHeap *heap)
{
	struct PupHeapRegion *tail = global_heap_head(heap);
	while (tail) {
		struct PupHeapRegion *tmp = tail;
		tail = region_next(tail);
		free_region(tmp);
	}
}

void pup_heap_destroy(struct PupHeap *heap)
{
	heap_thread_stop(heap);
	destroy_global_heap(heap);
	pup_heap_thread_destroy(heap);
	if (pthread_key_delete(heap->this_thread_info)) {
		fprintf(stderr, "heap->this_thread_info was unexpectedly reported to be an invalid key\n");
	}
}

bool pup_heap_region_have_room_for(struct PupHeapRegion *region, size_t size)
{
	return region->allocated + alloc_size_for(size) <= region->end;
}

void *pup_heap_region_make_room_for(struct PupHeapRegion *region,
                                    const size_t size,
                                    const enum PupHeapKind kind)
{
	void *tmp = region->allocated;
	region->allocated += alloc_size_for(size);
	struct HeapObject *obj = (struct HeapObject *)tmp;
	obj->object_size = size;
	obj->kind = kind;
	return obj->data;
}

static void set_region_next(struct PupHeapRegion *region,
                            struct PupHeapRegion *next)
{
	AO_store(&region->next, (AO_t)next);
	ANNOTATE_HAPPENS_BEFORE(&region->next);
}

void pup_heap_add_to_global_heap(struct PupHeap *heap,
                                 struct PupHeapRegion *region)
{
	int limit = 1000;
	while (true) {
		struct PupHeapRegion *old_head = global_heap_head(heap);
		set_region_next(region, old_head);
		if (swap_heap_region_head(heap, old_head, region)) {
			return;
		}
		ABORTF_ON(!--limit, "failed to update heap->region_list after 1000 iterations");
	}
}


static void *thread_local_alloc(struct PupHeap *heap, size_t size, enum PupHeapKind kind)
{
	struct PupThreadInfo *tinfo = get_thread_info(heap);
	struct PupHeapRegion *region = tinfo->local_region;
	if (!pup_heap_region_have_room_for(region, size)) {
		// the old region doesn't have the space, so create a new
		// thread-local region and have the old one added to the global
		// region list
		struct PupHeapRegion *old_region = region;
		region = pup_heap_region_allocate();
		if (!region) {
			// TODO raise a pup exception or somesuch,
			ABORTF("pup_heap_region_allocate() failed");
		}
		int res = set_local_region(heap, region);
		ABORTF_ON(res, "set_local_region() failed with %d", res);
		pup_heap_add_to_global_heap(heap, old_region);
	}
	void *obj = pup_heap_region_make_room_for(region, size, kind);
	pup_object_gc_mark_unconditionally((struct PupObject *)obj,
	                                   tinfo->current_gc_mark);
	return obj;
}

static int is_large_object(size_t size)
{
	return size > MAX_REGION_ALLOCATION;
}

void *pup_heap_alloc(struct PupHeap *heap, size_t size, enum PupHeapKind kind)
{
	if (is_large_object(size)) {
		ABORTF("Large object allocator not implemented yet! (%ld bytes)", size);
	}
	return thread_local_alloc(heap, size, kind);
}

void *pup_heap_alloc_for_gc_copy(struct PupHeap *heap,
                                 size_t size,
                                 enum PupHeapKind kind)
{
	if (is_large_object(size)) {
		ABORTF("Large object allocator not implemented yet! (%ld bytes)", size);
	}
	return pup_gc_alloc_for_copy(get_gc_state(heap), heap, size, kind);
}


