#include <stdlib.h>
#include <stdio.h>
#include <libunwind.h>
#include <dlfcn.h>
#include <atomic_ops.h>
#include <valgrind/drd.h>
#include "abortf.h"
#include "heap.h"
#include "gc/refqueue.h"
#include "object.h"

struct PupGCState {
	// the return value of dlopen(NULL, RTLD_LAZY),
	void *dlhandle;
	// actually a 'struct PupRefQueueSegment *' (TODO: atomic access actually needed?)
	volatile AO_t reference_queue_head;
	// current bit-value used to mark live objects,
	volatile AO_t live_mark_value;
	// counters marking the progress of a collection,
	int garbage_count;
	int live_count;
	// region we are currently copying live objects into,
	struct PupHeapRegion *copy_target;
	// the segment we are 
	struct PupRefQueueSegment *current_segment;
};

struct PupGCSafepoint {
	void *safepoint_addr;
	int frame_size;
	int live_count;
	int live_offsets[0];
};

struct PupGCMap {
	int32_t point_count;
	struct PupGCSafepoint points[0];
};

static const struct PupGCMap *get_stack_frame_roots(struct PupGCState *state,
                                  unw_cursor_t *cursor)
{
	char proc_name[1024];
	unw_word_t off;
	if (unw_get_proc_name(cursor, proc_name, 1024, &off)) {
		return NULL;
	}
	char sym_name[1024];
	int count = snprintf(sym_name, 1024, "__gcmap_%s", proc_name);
	if (count >= 1024) {
		// name longer than buffer,
		return NULL;
	}
	dlerror();  // clear any existing error
	void const *sym = dlsym(state->dlhandle, sym_name);
	char *err = dlerror();
	if (err != NULL) {
		return NULL;
	}
	return sym;
}

static const struct PupGCSafepoint *find_safepoint(
	unw_cursor_t *cursor,
	const struct PupGCMap *gc_map)
{
	char proc_name[1024];
	unw_word_t off;
	if (unw_get_proc_name(cursor, proc_name, 1024, &off)) {
		return NULL;
	}
	unw_proc_info_t pip;
	if (unw_get_proc_info(cursor, &pip)) {
		return NULL;
	}
	if (!pip.start_ip) {
		// presumably no unwind info available for this stack frame
		return NULL;
	}
	void *ip = (void *)(pip.start_ip + off);
	void const *addr = &gc_map->points[0];
	for (int i=0; i<gc_map->point_count; i++) {
		const struct PupGCSafepoint *point = addr;
		void *safepoint_addr = point->safepoint_addr;
		if (safepoint_addr == ip) {
			return point;
		}
		// skip the live-roots list for this non-matching safepoint,
		int live_count = point->live_count;
		addr += sizeof(struct PupGCSafepoint);
		addr += sizeof(int32_t) * live_count;
		addr += 4;  // FIXME: alignment hack
	}
	ABORTF("no safepoint in %s() for ip=%p", proc_name, ip);
}

static unw_word_t get_frame_pointer(unw_cursor_t *cursor)
{
	unw_word_t sp;
	int res = unw_get_reg(cursor, UNW_X86_64_RSP, &sp);
	if (res) {
		return 0;
	}
	return sp;
}

static struct PupRefQueueSegment *get_ref_queue_head(struct PupGCState *state)
{
	return (struct PupRefQueueSegment *)AO_load(&state->reference_queue_head);
}

static void set_ref_queue_head(struct PupGCState *state,
                               struct PupRefQueueSegment *seg)
{
	AO_store(&state->reference_queue_head, (AO_t)seg);
}

static int compare_and_swap_ref_queue_head(struct PupGCState *state,
                                           struct PupRefQueueSegment *old_head,
                                           struct PupRefQueueSegment *new_head)
{
	return AO_compare_and_swap(&state->reference_queue_head,
	                           (AO_t)old_head, (AO_t)new_head);
}

static void add_segment_to_global_queue(
	struct PupGCState *state,
	struct PupRefQueueSegment *segment)
{
	while (true) {
		struct PupRefQueueSegment *head = get_ref_queue_head(state);
		pup_refqueuesegment_set_next(segment, head);
		if (compare_and_swap_ref_queue_head(state, head, segment)) {
			break;
		}
	}
}

static struct PupRefQueueSegment *steal_segment_from_global_queue(
	struct PupGCState *state
) {
	while (true) {
		struct PupRefQueueSegment *head = get_ref_queue_head(state);
		if (!head) {
			return NULL;
		}
		if (compare_and_swap_ref_queue_head(state, head, pup_refqueuesegment_get_next(head))) {
			return head;
		}
	}
}

// TODO: not so pretty,
static void queue_for_marking(struct PupGCState *state, void **ref,
                           struct PupRefQueueSegment **ref_queue_segment)
{
	if (!*ref_queue_segment) {
		*ref_queue_segment = pup_refqueuesegment_create();
	} else if (!pup_refqueueseqment_has_free_space(*ref_queue_segment)) {
		add_segment_to_global_queue(state, *ref_queue_segment);
		*ref_queue_segment = pup_refqueuesegment_create();
	}
	ABORTF_ON(!*ref_queue_segment, "pup_refqueuesegment_create() failed");
	pup_refqueuesegment_add(*ref_queue_segment, *ref);
}

void pup_gc_queue_for_marking(struct PupGCState *state, void **ref)
{
	// For use in refqueue.c, within functions called recursively from
	// collect_stack_root_pointers() (therefore state->current_segment will
	// have a valid value).
	queue_for_marking(state, ref, &state->current_segment);
}

static void collect_stack_root_pointers(struct PupGCState *state,
                                        unw_cursor_t *cursor,
                                        const struct PupGCSafepoint *safepoint)
{
	struct PupRefQueueSegment *current_segment = NULL;

	unw_word_t fp = get_frame_pointer(cursor);
	if (!fp) {
		return;
	}
	for (int i=0; i<safepoint->live_count; i++) {
		int fp_offset = safepoint->live_offsets[i];
		void **root = ((void *)fp) + fp_offset;
		queue_for_marking(state, root, &current_segment);
	}
	if (current_segment) {
		add_segment_to_global_queue(state, current_segment);
	}
}

static void scan_stack_frame(struct PupGCState *state, unw_cursor_t *cursor)
{
	const struct PupGCMap *gc_map = get_stack_frame_roots(state, cursor);
	if (!gc_map) {
		return;
	}
	fprintf(stderr, "    %d safe points\n", gc_map->point_count);
	const struct PupGCSafepoint *safepoint = find_safepoint(cursor, gc_map);
	if (!safepoint) {
		return;
	}
	collect_stack_root_pointers(state, cursor, safepoint);
	return;
}

void pup_gc_scan_stack(struct PupGCState *state)
{
	unw_context_t context;
	unw_cursor_t cursor;
	if (unw_getcontext(&context)) {
		return;
	}
	if (unw_init_local(&cursor, &context)) {
		return;
	}
	fprintf(stderr, "doing pup_gc_scan_stack()\n");
	do {
		scan_stack_frame(state, &cursor);
	} while (unw_step(&cursor) > 0);
}

static void set_live_mark_value(struct PupGCState *state, int mark)
{
	AO_store(&state->live_mark_value, mark);
	ANNOTATE_HAPPENS_BEFORE(&state->live_mark_value);
}

struct PupGCState *pup_gc_state_create(void)
{
	struct PupGCState *state = malloc(sizeof(struct PupGCState));
	if (!state) return NULL;
	state->dlhandle = dlopen(NULL, RTLD_LAZY);
	if (!state->dlhandle) {
		free(state);
		return NULL;
	}
	set_ref_queue_head(state, NULL);
	set_live_mark_value(state, 0);
	// these are initialised in pup_gc_period_start()
	//state->garbage_count = 0;
	//state->live_count = 0;
	state->copy_target = NULL;
	return state;
}

void pup_gc_state_destroy(struct PupGCState *state)
{
	dlclose(state->dlhandle);
	free(state);
}


void pup_gc_scan_heap(struct PupGCState *state)
{
	int count = 0;
	struct PupRefQueueSegment *seg;
	while ((seg = steal_segment_from_global_queue(state)) != NULL) {
		count++;
		pup_refqueuesegment_scan(seg, state);
	}
	fprintf(stderr, "pup_gc_scan_heap() processed %d segments\n", count);
}

bool pup_gc_mark_reachable(struct PupGCState *state, struct PupObject *ref)
{
	return pup_object_gc_mark(ref, state->live_mark_value);
}

int pup_gc_get_current_mark(const struct PupGCState *state)
{
	ANNOTATE_HAPPENS_AFTER(&state->live_mark_value);
	// called from mutator thread, therefore atomic
	return AO_load(&state->live_mark_value);
}

bool pup_gc_is_live_mark(const struct PupGCState *state, const int mark_value)
{
	return pup_gc_get_current_mark(state) == mark_value;
}

void pup_gc_inc_garbage_count(struct PupGCState *state)
{
	state->garbage_count++;
}

void pup_gc_inc_live_count(struct PupGCState *state)
{
	state->live_count++;
}

void pup_gc_period_start(struct PupGCState *state)
{
	state->garbage_count = 0;
	state->live_count = 0;
	set_live_mark_value(state, !state->live_mark_value);
}

void pup_gc_period_end(struct PupGCState *state)
{
	fprintf(stderr, "live:%d garbage:%d\n",
	                state->live_count,
	                state->garbage_count);
}

// TODO: deduplicate vs. heap.c thread_local_alloc()

void *pup_gc_alloc_for_copy(struct PupGCState *state,
                            struct PupHeap *heap,
                            const size_t size,
                            const enum PupHeapKind kind)
{
	struct PupHeapRegion *region = state->copy_target;
	ABORTF_ON(!region, "region is NULL");
	if (!pup_heap_region_have_room_for(region, size)) {
		// the old region doesn't have the space, so create a new
		// thread-local region and have the old one added to the global
		// region list
		struct PupHeapRegion *old_region = region;
		region = pup_heap_region_allocate();
		ABORTF_ON(!region, "pup_heap_region_allocate() failed");
		state->copy_target = region;
		pup_heap_add_to_global_heap(heap, old_region);
	}
	void *obj = pup_heap_region_make_room_for(region, size, kind);
	// obj should gain the correct gc_mark when it gets copied
	// from its current region to *obj, so no
	// pup_object_gc_mark_unconditionally() call needed here
	
	return obj;
}
