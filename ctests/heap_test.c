
#include "../heap.h"
#include <stdlib.h>
#include <stdio.h>
#include "../abortf.h"

int main (int argc, char **argv)
{
	struct PupHeap heap;
	int res = pup_heap_init(&heap);
	ABORTF_ON(res, "pup_heap_init() failed with %d", res);
	// enough allocations to require more than one region,
	for (int i=0; i<100000; i++) {
		void *mem = pup_heap_alloc(&heap, 13);
		ABORTF_ON(mem == NULL, "pup_heap_alloc() gave NULL");
	}
	pup_heap_destroy(&heap);
}
