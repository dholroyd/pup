
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <atomic_ops.h>
#include "abortf.h"
#include "heap.h"
#include "object.h"
#include <signal.h>
#include <semaphore.h>
#include <time.h>
#include "gc.h"

#define REGION_SIZE 0x100000
#define MAX_REGION_ALLOCATION 0x1000

struct PupHeapRegion {
	void *region;
	void *end;
	void *allocated;
	struct PupHeapRegion *next;
};

struct PupThreadInfo {
	int tid;
	struct PupHeapRegion *local_region;
	struct PupThreadInfo *next;
	int gc_waiting;
};

// TODO: optimise HeapObject layout
struct HeapObject {
	size_t object_size;  // the requested size (NB not HeapObject's size)
	unsigned int kind : 1;  // is it an object or an AttrListEntry
	char data[0];  // actual object data starts from here
};

static struct PupHeapRegion *allocate_region()
{
	struct PupHeapRegion *region = malloc(sizeof(struct PupHeapRegion));
	if (!region) {
		return NULL;
	}
	region->next = NULL;
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

static struct PupThreadInfo **find_last_threadinfo(struct PupHeap *heap)
{
	struct PupThreadInfo **last = &heap->thread_list;
	while(*last) {
		last = &(*last)->next;
	}
	return last;
}

static int attach_thread(struct PupHeap *heap, pthread_t thread)
{
	struct PupThreadInfo *tinfo = malloc(sizeof(struct PupThreadInfo));
	if (tinfo == NULL) {
		return -1;
	}
	tinfo->tid = syscall(SYS_gettid);
	tinfo->next = NULL;
	tinfo->gc_waiting = 0;
	struct PupThreadInfo **last = find_last_threadinfo(heap);
	*last = tinfo;
	set_thread_info(heap, tinfo);
	return 0;
}

int pup_heap_thread_init(struct PupHeap *heap)
{
	struct PupHeapRegion *region = allocate_region();
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

static void announce_mutator_arrival(struct PupHeap *heap,
                                     struct PupThreadInfo *tinfo)
{
	sem_post(&heap->safepoint_sem);
	tinfo->gc_waiting = false;
}

void pup_heap_safepoint(struct PupHeap *heap)
{
	struct PupThreadInfo *tinfo = get_thread_info(heap);
	// the thread-local gc_waiting flag is set from a signal handler on
	// this thread, hence we don't use explicit atomic ops or locking
	// for this access
	if (tinfo->gc_waiting) {
		announce_mutator_arrival(heap, tinfo);
		pup_gc_scan_stack(heap->gc_state);
	}
}

static void await_mutator_arrival(struct PupHeap *heap, struct PupThreadInfo *tinfo)
{
	const int seconds = 1;
	struct timespec abs_timeout;
	int ret;
	while (true) {
		clock_gettime(CLOCK_REALTIME, &abs_timeout);
		abs_timeout.tv_sec += seconds;
		ret = sem_timedwait(&heap->safepoint_sem, &abs_timeout);
		if (!ret) break;
		if (ETIMEDOUT == errno) {
			fprintf(stderr, "thread %d failed to converge at safepoint after %d seconds\n", tinfo->tid, seconds);
		}
	}
}

static void converge_on_safepoint(struct PupHeap *heap, struct PupThreadInfo *tinfo)
{
	union sigval sv;
	sv.sival_ptr = heap;
	fprintf(stderr, "converge_on_safepoint() signalling %d\n", tinfo->tid);
	int ret = sigqueue(tinfo->tid, SIGUSR1, sv);
	ABORTF_ON(ret, "sigqueue() failed: %s", strerror(errno));
	await_mutator_arrival(heap, tinfo);
}

#define PUP_EACH_GCTHREAD(_heap) \
	for (struct PupThreadInfo *tinfo=(_heap)->thread_list; \
	     tinfo; \
	     tinfo = tinfo->next)

static void perform_gc(struct PupHeap *heap)
{
	PUP_EACH_GCTHREAD(heap) {
		// FIXME: hack to avoid main thread, which doesn't have
		// safepoints
		if (tinfo->tid != getpid())
			converge_on_safepoint(heap, tinfo);
	}
}

static void *gc_thread(void *arg)
{
	struct PupHeap *heap = (struct PupHeap *)arg;
	struct timespec req = {
		.tv_sec = 0,
		.tv_nsec = 500000000
	};
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

int pup_heap_init(struct PupHeap *heap)
{
	fprintf(stderr, "pup_heap_init() pid=%d\n", getpid());
	int res;
	res = pthread_key_create(&heap->this_thread_info, NULL /* no dtor */);
	if (res) return res;

	heap->region_list = NULL;
	heap->thread_list = NULL;
	heap->gc_state = NULL;

	// initialisation for the main thread,
	res = pup_heap_thread_init(heap);
	if (res) {
		// ignore return value, since we're cleaning up anyway,
		pthread_key_delete(heap->this_thread_info);
		return res;
	}
	res = setup_signal_handling(heap);
	res = heap_thread_start(heap);
	if (res) {
		pup_heap_thread_destroy(heap);
		pthread_key_delete(heap->this_thread_info);
		return res;
	}
	res = sem_init(&heap->safepoint_sem, 0, 0);
	// FIXME: proper error handling,
	ABORTF_ON(res, "pup_heap_init(): sem_init() failed: %s",
	               strerror(errno));
	heap->gc_state = pup_gc_state_create();
	// FIXME: proper error handling,
	ABORT_ON(!heap->gc_state, "pup_gc_state_create() failed");
	return 0;
}

static void destroy_global_heap(struct PupHeap *heap)
{
	struct PupHeapRegion *tail = heap->region_list;
	while (tail) {
		struct PupHeapRegion *tmp = tail;
		tail = tail->next;
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

static int have_room_for(struct PupHeapRegion *region, size_t size)
{
	return region->allocated + alloc_size_for(size) <= region->end;
}

static void *make_room_for(struct PupHeapRegion *region,
                           size_t size,
                           enum PupHeapKind kind)
{
	void *tmp = region->allocated;
	region->allocated += alloc_size_for(size);
	struct HeapObject *obj = (struct HeapObject *)tmp;
	obj->object_size = size;
	obj->kind = kind;
	return obj->data;
}

static void add_to_global_heap(struct PupHeap *heap,
                               struct PupHeapRegion *region)
{
	int limit = 1000;
	while (1) {
		struct PupHeapRegion *old_head =
			(struct PupHeapRegion *)AO_load((AO_t *)&heap->region_list);
		region->next = old_head;
		if (AO_compare_and_swap((AO_t *)&heap->region_list, (AO_t)old_head, (AO_t)region)) {
			return;
		}
		ABORTF_ON(!--limit, "failed to update heap->region_list after 1000 iterations");
	}
}

static void *thread_local_alloc(struct PupHeap *heap, size_t size, enum PupHeapKind kind)
{
	struct PupHeapRegion *region = get_thread_info(heap)->local_region;
	if (!have_room_for(region, size)) {
		// the old region doesn't have the space, so create a new
		// thread-local region and have the old one added to the global
		// region list
		struct PupHeapRegion *old_region = region;
		region = allocate_region();
		if (!region) {
			// TODO raise a pup exception or somesuch,
			ABORTF("allocate_region() failed");
		}
		int res = set_local_region(heap, region);
		ABORTF_ON(res, "set_local_region() failed with %d", res);
		add_to_global_heap(heap, old_region);
	}
	return make_room_for(region, size, kind);
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
