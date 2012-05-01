#include <stdbool.h>
#include "../gc.h"

struct PupRefQueueSegment *pup_refqueuesegment_create(void);
void pup_refqueuesegment_destroy(struct PupRefQueueSegment *segment);
bool pup_refqueueseqment_has_free_space(struct PupRefQueueSegment *segment);
void pup_refqueuesegment_add(struct PupRefQueueSegment *segment, void *ref);
void pup_refqueuesegment_set_next(struct PupRefQueueSegment *segment,
                                  struct PupRefQueueSegment *next);
struct PupRefQueueSegment *pup_refqueuesegment_get_next(
	struct PupRefQueueSegment *segment
);
void pup_refqueuesegment_scan(struct PupRefQueueSegment *seg,
                              struct PupGCState *state);
