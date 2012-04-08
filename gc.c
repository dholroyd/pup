#include <stdlib.h>
#include <stdio.h>
#include <libunwind.h>
#include <dlfcn.h>
#include "abortf.h"

struct PupGCState {
	void *dlhandle;
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

static void collect_stack_root_pointers(unw_cursor_t *cursor,
                                        const struct PupGCSafepoint *safepoint)
{
	unw_word_t fp = get_frame_pointer(cursor);
	if (!fp) {
		return;
	}
	fprintf(stderr, "frame pointer %p\n", (void *)fp);
	for (int i=0; i<safepoint->live_count; i++) {
		int fp_offset = safepoint->live_offsets[i];
		void **root = ((void *)fp) + fp_offset;
		fprintf(stderr, "  root[%d]=%p ref is %p\n", i, root, *root);
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
	collect_stack_root_pointers(cursor, safepoint);
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

struct PupGCState *pup_gc_state_create(void)
{
	struct PupGCState *state = malloc(sizeof(struct PupGCState));
	if (!state) return NULL;
	void *dlhandle = dlopen(NULL, RTLD_LAZY);
	if (!dlhandle) {
		free(state);
		return NULL;
	}
	return state;
}

void pup_gc_state_destroy(struct PupGCState *state)
{
	dlclose(state->dlhandle);
	free(state);
}
