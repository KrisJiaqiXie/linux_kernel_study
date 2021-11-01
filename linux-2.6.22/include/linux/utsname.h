#ifndef _LINUX_UTSNAME_H
#define _LINUX_UTSNAME_H

#define __OLD_UTS_LEN 8

struct oldold_utsname {
	char sysname[9];
	char nodename[9];
	char release[9];
	char version[9];
	char machine[9];
};

#define __NEW_UTS_LEN 64

struct old_utsname {
	char sysname[65];
	char nodename[65];
	char release[65];
	char version[65];
	char machine[65];
};

struct new_utsname {
	char sysname[65];
	char nodename[65];
	char release[65];
	char version[65];
	char machine[65];
	char domainname[65];
};

#ifdef __KERNEL__

#include <linux/sched.h>
#include <linux/kref.h>
#include <linux/nsproxy.h>
#include <asm/atomic.h>

struct uts_namespace {
	struct kref kref;
	struct new_utsname name;
};
extern struct uts_namespace init_uts_ns;

static inline void get_uts_ns(struct uts_namespace *ns)
{
	kref_get(&ns->kref);
}

#ifdef CONFIG_UTS_NS
extern struct uts_namespace *copy_utsname(int flags, struct uts_namespace *ns);
extern void free_uts_ns(struct kref *kref);

static inline void put_uts_ns(struct uts_namespace *ns)
{
	kref_put(&ns->kref, free_uts_ns);
}
#else
static inline struct uts_namespace *copy_utsname(int flags,
						struct uts_namespace *ns)
{
	return ns;
}

static inline void put_uts_ns(struct uts_namespace *ns)
{
}
#endif

static inline struct new_utsname *utsname(void)
{
	return &current->nsproxy->uts_ns->name;
}

static inline struct new_utsname *init_utsname(void)
{
	return &init_uts_ns.name;
}

extern struct rw_semaphore uts_sem;

#endif /* __KERNEL__ */

#endif /* _LINUX_UTSNAME_H */
