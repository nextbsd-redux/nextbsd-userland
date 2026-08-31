/*
 * test_mach_name_recycle — does a freed Mach port name come back
 * bit-identical, and does the stale name still resolve?
 *
 * This is the regression guard for the port-name generation work
 * (nextbsd-kernel#151, umbrella #145).
 *
 * In classic Mach a port name carries a generation counter precisely so
 * that a REUSED name is detectably stale: free a name, allocate another
 * port, and the new name differs from the old one even though the index
 * half is the same. A caller still holding the old name gets
 * KERN_INVALID_NAME rather than silently operating on a different port.
 *
 * In this port names are file descriptors allocated lowest-free-first,
 * and the generation machinery is compiled out, so a name released by
 * one call is handed straight back to the next -- bit for bit. That is
 * the root cause behind the non-deterministic Mach message loss tracked
 * in #145.
 *
 * The test is deliberately phrased as a property, not a guess about the
 * fix: allocate, free, reallocate, and report how many names came back
 * identical. It is EXPECTED TO FAIL until generations land. A failing
 * run here is the bug reproducing, not the test being broken.
 *
 * Exit codes:
 *   0  — no reissued name was bit-identical; staleness is detectable
 *   1  — no Mach state (mach_task_self() returned MACH_PORT_NULL)
 *   2  — a mach_port_allocate call failed; test inconclusive
 *  10  — names are reissued bit-identical (the bug)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mach/mach_traps.h>
#include <mach/mach_port.h>
#include <mach/port.h>
#include <mach/message.h>

#define	NPORTS	64

int
main(void)
{
	mach_port_name_t self = mach_task_self();
	mach_port_name_t pass1[NPORTS], pass2[NPORTS];
	mach_port_type_t type = 0;
	int i, j, kr, identical = 0, stale_kr;

	if (self == MACH_PORT_NULL) {
		fprintf(stderr, "mach_task_self() == MACH_PORT_NULL "
		    "(no Mach state for this process)\n");
		return (1);
	}

	for (i = 0; i < NPORTS; i++) {
		kr = mach_port_allocate(self, MACH_PORT_RIGHT_RECEIVE, &pass1[i]);
		if (kr != 0) {
			fprintf(stderr, "pass1 alloc %d failed kr=%d\n", i, kr);
			return (2);
		}
	}

	for (i = 0; i < NPORTS; i++)
		(void)mach_port_mod_refs(self, pass1[i],
		    MACH_PORT_RIGHT_RECEIVE, -1);

	/*
	 * Probe the first freed name BEFORE reallocating. On its own this
	 * says little -- a freed fd fails lookup today too -- but combined
	 * with the identical-name count below it distinguishes "the name is
	 * gone" from "the name now points at somebody else's port".
	 */
	stale_kr = mach_port_type(self, pass1[0], &type);

	for (i = 0; i < NPORTS; i++) {
		kr = mach_port_allocate(self, MACH_PORT_RIGHT_RECEIVE, &pass2[i]);
		if (kr != 0) {
			fprintf(stderr, "pass2 alloc %d failed kr=%d\n", i, kr);
			return (2);
		}
	}

	for (i = 0; i < NPORTS; i++) {
		for (j = 0; j < NPORTS; j++) {
			if (pass2[i] == pass1[j]) {
				identical++;
				break;
			}
		}
	}

	printf("first name  pass1=0x%08x  pass2=0x%08x\n", pass1[0], pass2[0]);
	printf("index half  pass1=0x%06x  pass2=0x%06x\n",
	    MACH_PORT_INDEX(pass1[0]), MACH_PORT_INDEX(pass2[0]));
	printf("gen   half  pass1=0x%08x  pass2=0x%08x\n",
	    MACH_PORT_GEN(pass1[0]), MACH_PORT_GEN(pass2[0]));
	printf("reissued bit-identical: %d of %d\n", identical, NPORTS);
	printf("freed name resolves:    %s (kr=%d)\n",
	    stale_kr == 0 ? "YES" : "no", stale_kr);

	for (i = 0; i < NPORTS; i++)
		(void)mach_port_mod_refs(self, pass2[i],
		    MACH_PORT_RIGHT_RECEIVE, -1);

	if (identical > 0) {
		printf("FAIL: %d name(s) reissued bit-identical -- a caller "
		    "holding a freed name would silently address a different "
		    "port\n", identical);
		return (10);
	}

	printf("PASS: reissued names differ from freed names\n");
	return (0);
}
