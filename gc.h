
struct PupGCState;

void pup_gc_scan_stack(struct PupGCState *state);
struct PupGCState *pup_gc_state_create(void);
void pup_gc_state_destroy(struct PupGCState *state);

