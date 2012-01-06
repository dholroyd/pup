
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define _GNU_SOURCE
#define __USE_GNU
#include <sys/mman.h>
#include <signal.h>
#include <libunwind.h>
#include "../abortf.h"

/*
static void dump_stack()
{
	unw_context_t context;
	unw_cursor_t cursor;
	if (unw_getcontext(&context)) {
		return;
	}
	if (unw_init_local(&cursor, &context)) {
		return;
	}
	do {
		char name[70];
		unw_word_t off;
		if (unw_get_proc_name(&cursor, name, 70, &off)) {
			fprintf(stderr, "<func name unavailable>()\n");
		} else {
			fprintf(stderr, "%s()\n", name);
		}
	} while (unw_step(&cursor) > 0);
}
*/

static int step_to_non_signal_frame(unw_cursor_t *cursor)
{
	// skip the signal handler frame itself,
	for (int i=0; i<=2; i++) {
		if (unw_step(cursor) <= 0) {
			return -1;
		}
	}
	while (unw_is_signal_frame(cursor)) {
		if (unw_step(cursor) <= 0) {
			// we failed to find the frame the caused the signal
			return -1;
		}
	}
	return 0;
}

static void dump_stack_frame_info(unw_cursor_t *cursor)
{
	char name[70];
	unw_word_t off;
	if (unw_get_proc_name(cursor, name, 70, &off)) {
		fprintf(stderr, "in <func name unavailable> offset=0x%lx\n", off);
	} else {
		fprintf(stderr, "in %s() offset=0x%lx\n", name, off);
	}
	unw_word_t reg_val;
	for (int i=0; i<=17; i++) {
		if (unw_get_reg(cursor, i, &reg_val)) {
			fprintf(stderr, "  reg%d N/A\n", i);
		} else {
			fprintf(stderr, "  reg%d: %p\n", i, (void *)reg_val);
		}
	}
}

void *region1;

static int handle_segv(siginfo_t *siginfo, void *ucontext)
{
	write(2, "segfault\n", 9);
	unw_context_t context;
	unw_cursor_t cursor;
	if (unw_getcontext(&context)) {
		return -1;
	}
	if (unw_init_local(&cursor, &context)) {
		return -1;
	}
	if (step_to_non_signal_frame(&cursor)) {
		return -1;
	}
	fprintf(stderr, "fault at address %p\n", siginfo->si_addr);
	unw_word_t reg_val;
	if (unw_get_reg(&cursor, 5, &reg_val)) {
		return -1;
	}
		dump_stack_frame_info(&cursor);
	if ((void *)reg_val != siginfo->si_addr) {
		fprintf(stderr, "reg5 didn't have faulting address!\n");
		return -1;
	}
	if (unw_set_reg(&cursor, 5, (unw_word_t)region1)) {
		fprintf(stderr, "failed to update reg5");
		return -1;
	}
	fprintf(stderr, "fixed up ref %p to be %p instead\n", siginfo->si_addr, region1);

	return 0;
}

static void sighandler(int signal, siginfo_t *siginfo, void *ucontext)
{
	ABORTF_ON(signal != SIGSEGV, "expected SIGSEGV, got %d", signal);

	if (handle_segv(siginfo, ucontext)) {
		abort();
	}
}

int main(int argc, char **argv)
{
	struct sigaction act = {
		.sa_sigaction = sighandler,
		.sa_flags = SA_SIGINFO
	};
	sigemptyset(&act.sa_mask);
	if (sigaction(SIGSEGV, &act, NULL)) {
		printf("sigaction() failed: %s\n", strerror(errno));
		return 1;
	}

	int pagesize = getpagesize() * 1024;
	region1 = mmap(NULL,
	                    pagesize,
	                    PROT_READ|PROT_WRITE,
			    MAP_PRIVATE|MAP_ANONYMOUS,
			    -1,  // no fd
			    0);  // no offset
	if (region1 == MAP_FAILED) {
		printf("mmap() failed: %s\n", strerror(errno));
		return 1;
	}
	printf("allocated region1 of size %d at %p\n", pagesize, region1);
	(((char *)region1)[0]) = 'A';
	printf("byte at region1[0] is %d\n", ((char *)region1)[0]);
	void *region2 = mmap(NULL,
	                    pagesize,
	                    PROT_READ|PROT_WRITE,
			    MAP_PRIVATE|MAP_ANONYMOUS,
			    -1,  // no fd
			    0);  // no offset

	if (region2 == MAP_FAILED) {
		printf("mmap() failed: %s\n", strerror(errno));
		return 1;
	}
	printf("allocated region2 of size %d at %p\n", pagesize, region2);
	(((char *)region2)[0]) = ';';
	printf("byte at region2[0] is %d\n", ((char *)region2)[0]);

	void *remapped = mremap(region2,
	                        pagesize,
				pagesize,
				MREMAP_FIXED|MREMAP_MAYMOVE,
				region1);

	if (MAP_FAILED == remapped) {
		printf("mremap() failed: %s\n", strerror(errno));
		return 1;
	}
	printf("mremap() suceeded\n");
	fflush(NULL);
	printf("byte at region2[0] is %d\n", ((char *)region2)[0]);
	printf("now, byte at region2[4] is %d\n", ((char *)region2)[4]);

	if (munmap(region1, pagesize)) {
		printf("munmap() failed: %s\n", strerror(errno));
		return 1;
	}

	printf("all done\n");
	return 0;
}
