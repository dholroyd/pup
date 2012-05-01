#include "refqueue.h"
#include <stdio.h>
#include <atomic_ops.h>
#include <valgrind/drd.h>
#include "../abortf.h"
#include "../object.h"

#define REF_QUEUE_SEGMENT_SIZE 200

struct PupRefQueueSegment {
	// actually a 'struct PupRefQueueSegment *',
	volatile AO_t next_segment;
	int in_use;
	struct PupObject* refs[REF_QUEUE_SEGMENT_SIZE];
};

struct PupRefQueueSegment *pup_refqueuesegment_create(void)
{
	struct PupRefQueueSegment *seg
		= malloc(sizeof(struct PupRefQueueSegment));
	if (!seg) {
		return NULL;
	}
	pup_refqueuesegment_set_next(seg, NULL);
	seg->in_use = 0;
	return seg;
}

void pup_refqueuesegment_destroy(struct PupRefQueueSegment *segment)
{
	free(segment);
}

bool pup_refqueueseqment_has_free_space(struct PupRefQueueSegment *segment)
{
	return segment->in_use < REF_QUEUE_SEGMENT_SIZE;
}

static void add_unchecked(struct PupRefQueueSegment *segment, void *ref)
{
	segment->refs[segment->in_use++] = ref;
}

void pup_refqueuesegment_add(struct PupRefQueueSegment *segment, void *ref)
{
	ABORTF_ON(!pup_refqueueseqment_has_free_space(segment),
	         "ref queue segment %p has no free space", segment);
	add_unchecked(segment, ref);
}

void pup_refqueuesegment_set_next(struct PupRefQueueSegment *segment,
                                  struct PupRefQueueSegment *next)
{
	AO_store(&segment->next_segment, (AO_t)next);
	ANNOTATE_HAPPENS_BEFORE(&segment->next_segment);
}

// TODO: move code over here and out of gc.c so that this can go away
struct PupRefQueueSegment *pup_refqueuesegment_get_next(
	struct PupRefQueueSegment *segment
) {
	ANNOTATE_HAPPENS_AFTER(&segment->next_segment);
	return (struct PupRefQueueSegment *)AO_load(&segment->next_segment);
}

static void ref_visitor(struct PupObject **ref, void *data)
{
	struct PupGCState *state = data;
	pup_gc_queue_for_marking(state, (void **)ref);
	fprintf(stderr, "	ref_visitor() at %p found %p\n", ref, *ref);
}

static void scan_queue_ref(struct PupObject *ref, struct PupGCState *state)
{
	if (pup_gc_mark_reachable(state, ref)) {
		pup_object_each_ref(ref, ref_visitor, state);
	}
}

void pup_refqueuesegment_scan(struct PupRefQueueSegment *seg,
                              struct PupGCState *state)
{
	for (int i=0; i<seg->in_use; i++) {
		scan_queue_ref(seg->refs[i], state);
	}
}
