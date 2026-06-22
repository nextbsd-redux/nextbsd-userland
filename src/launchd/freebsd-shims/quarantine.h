/*
 * quarantine.h — FreeBSD shim for Apple's <quarantine.h>.
 *
 * Apple's quarantine library tracks the "downloaded from the
 * internet" Gatekeeper attribute. launchd-842's libvproc.c calls
 * qtn_proc_to_data() when serializing a posix_spawn quarantine
 * attribute. FreeBSD has no Gatekeeper / quarantine subsystem, so
 * the type is opaque and the one call we hit is stubbed to fail
 * (returns nonzero), which libvproc.c already handles as "no
 * quarantine data to attach". Same posture as libxpc's stubs.c.
 */
#ifndef _QUARANTINE_H_SHIM_
#define _QUARANTINE_H_SHIM_

#include <sys/types.h>
#include <stdio.h>

/* Upper bound on a serialized quarantine blob. libvproc.c stack-
 * allocates a char[QTN_SERIALIZED_DATA_MAX] before calling
 * qtn_proc_to_data(). Apple's value; our qtn_proc_to_data stub
 * returns "no data" so the buffer is never actually filled, but the
 * array still needs a valid compile-time size. */
#ifndef QTN_SERIALIZED_DATA_MAX
#define QTN_SERIALIZED_DATA_MAX	4096
#endif

typedef struct _qtn_proc *qtn_proc_t;
typedef struct _qtn_file *qtn_file_t;

/*
 * qtn_proc_alloc() — Apple's allocator for a per-process quarantine
 * handle (later filled via qtn_proc_init_with_*() and serialized via
 * qtn_proc_to_data()). FreeBSD has no Gatekeeper; the stub returns
 * NULL so launchd's `if (job_assumes(j, qp = qtn_proc_alloc()))`
 * check takes the "no quarantine" branch.
 */
static __inline qtn_proc_t
qtn_proc_alloc(void)
{
	return (qtn_proc_t)0;
}

static __inline void
qtn_proc_free(qtn_proc_t proc)
{
	(void)proc;
}

static __inline int
qtn_proc_to_data(qtn_proc_t proc, void *buf, size_t *buflen)
{
	(void)proc; (void)buf;
	if (buflen != NULL)
		*buflen = 0;
	/* nonzero == "no quarantine data" — libvproc.c skips the attr */
	return 1;
}

/*
 * qtn_proc_init_with_data() / qtn_proc_apply_to_self() — core.c's job-spawn
 * path calls these, but only inside `if (job_assumes(j, qp = qtn_proc_alloc()))`,
 * and our qtn_proc_alloc() returns NULL, so the branch is dead at runtime. They
 * must still compile (and be declared, for -Werror=implicit-function-declaration).
 * Stub to nonzero (failure); never reached on FreeBSD (no Gatekeeper).
 */
static __inline int
qtn_proc_init_with_data(qtn_proc_t proc, const void *data, size_t len)
{
	(void)proc; (void)data; (void)len;
	return 1;
}

static __inline int
qtn_proc_apply_to_self(qtn_proc_t proc)
{
	(void)proc;
	return 1;
}

/* qtn_proc_set_identifier / qtn_proc_set_flags — syslogd labels its own
 * quarantine handle before applying it. No Gatekeeper on FreeBSD, so these are
 * no-ops returning 0 (success); qtn_proc_alloc() returns NULL anyway, so the
 * caller's path is inert. */
static __inline int
qtn_proc_set_identifier(qtn_proc_t proc, const char *identifier)
{
	(void)proc; (void)identifier;
	return 0;
}

static __inline int
qtn_proc_set_flags(qtn_proc_t proc, uint32_t flags)
{
	(void)proc; (void)flags;
	return 0;
}

#endif /* !_QUARANTINE_H_SHIM_ */
