
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <atomic_ops.h>
#include "abortf.h"
#include "heap.h"
#include "object.h"

#define REGION_SIZE 0x100000
#define MAX_REGION_ALLOCATION 0x1000

struct PupHeapRegion {
	void *region;
	void *end;
	void *allocated;
	struct PupHeapRegion *next;
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

static struct PupHeapRegion *get_local_region(struct PupHeap *heap)
{
	void *res = pthread_getspecific(heap->threadlocal_region);
	ABORT_ON(!res, "pthread_getspecific() produced null");
	return (struct PupHeapRegion *)res;
}

static int set_local_region(struct PupHeap *heap, struct PupHeapRegion *region)
{
	ABORT_ON(!region, "set_local_region() given null region");
	return pthread_setspecific(heap->threadlocal_region, region);
}

int pup_heap_thread_init(struct PupHeap *heap)
{
	struct PupHeapRegion *region = allocate_region();
	if (!region) {
		return -1;
	}
	int res = set_local_region(heap, region);
	if (res) {
		return res;
	}
	return 0;
}

int pup_heap_init(struct PupHeap *heap)
{
	int res;
	res = pthread_key_create(&heap->threadlocal_region, NULL /* no dtor */);
	if (res) return res;

	heap->region_list = NULL;

	// initialisation for the main thread,
	res = pup_heap_thread_init(heap);
	if (res) {
		// ignore return value, since we're cleaning up anyway,
		pthread_key_delete(heap->threadlocal_region);
		return res;
	}
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

void pup_heap_thread_destroy(struct PupHeap *heap)
{
	struct PupHeapRegion *local_region = get_local_region(heap);
	if (local_region) {
		free_region(local_region);
	}
}

void pup_heap_destroy(struct PupHeap *heap)
{
	destroy_global_heap(heap);
	pup_heap_thread_destroy(heap);
	if (pthread_key_delete(heap->threadlocal_region)) {
		fprintf(stderr, "heap->threadlocal_region was unexpectedly reported to be an invalid key\n");
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
	struct PupHeapRegion *region = get_local_region(heap);
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
