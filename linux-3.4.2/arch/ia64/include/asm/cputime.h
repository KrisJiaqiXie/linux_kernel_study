/*
 * Definitions for measuring cputime on ia64 machines.
 *
 * Based on <asm-powerpc/cputime.h>.
 *
 * Copyright (C) 2007 FUJITSU LIMITED
 * Copyright (C) 2007 Hidetoshi Seto <seto.hidetoshi@jp.fujitsu.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * If we have CONFIG_VIRT_CPU_ACCOUNTING, we measure cpu time in nsec.
 * Otherwise we measure cpu time in jiffies using the generic definitions.
 */

#ifndef __IA64_CPUTIME_H
#define __IA64_CPUTIME_H

#ifndef CONFIG_VIRT_CPU_ACCOUNTING
#include <asm-generic/cputime.h>
#else

#include <linux/time.h>
#include <linux/jiffies.h>
#include <asm/processor.h>

typedef u64 __nocast cputime_t;
typedef u64 __nocast cputime64_t;

#define cputime_one_jiffy		jiffies_to_cputime(1)

/*
 * Convert cputime <-> jiffies (HZ)
 */
#define cputime_to_jiffies(__ct)	\
	((__force u64)(__ct) / (NSEC_PER_SEC / HZ))
#define jiffies_to_cputime(__jif)	\
	(__force cputime_t)((__jif) * (NSEC_PER_SEC / HZ))
#define cputime64_to_jiffies64(__ct)	\
	((__force u64)(__ct) / (NSEC_PER_SEC / HZ))
#define jiffies64_to_cputime64(__jif)	\
	(__force cputime64_t)((__jif) * (NSEC_PER_SEC / HZ))

/*
 * Convert cputime <-> microseconds
 */
#define cputime_to_usecs(__ct)		\
	((__force u64)(__ct) / NSEC_PER_USEC)
#define usecs_to_cputime(__usecs)	\
	(__force cputime_t)((__usecs) * NSEC_PER_USEC)
#define usecs_to_cputime64(__usecs)	\
	(__force cputime64_t)((__usecs) * NSEC_PER_USEC)

/*
 * Convert cputime <-> seconds
 */
#define cputime_to_secs(__ct)		\
	((__force u64)(__ct) / NSEC_PER_SEC)
#define secs_to_cputime(__secs)		\
	(__force cputime_t)((__secs) * NSEC_PER_SEC)

/*
 * Convert cputime <-> timespec (nsec)
 */
static inline cputime_t timespec_to_cputime(const struct timespec *val)
{
	u64 ret = val->tv_sec * NSEC_PER_SEC + val->tv_nsec;
	return (__force cputime_t) ret;
}
static inline void cputime_to_timespec(const cputime_t ct, struct timespec *val)
{
	val->tv_sec  = (__force u64) ct / NSEC_PER_SEC;
	val->tv_nsec = (__force u64) ct % NSEC_PER_SEC;
}

/*
 * Convert cputime <-> timeval (msec)
 */
static inline cputime_t timeval_to_cputime(struct timeval *val)
{
	u64 ret = val->tv_sec * NSEC_PER_SEC + val->tv_usec * NSEC_PER_USEC;
	return (__force cputime_t) ret;
}
static inline void cputime_to_timeval(const cputime_t ct, struct timeval *val)
{
	val->tv_sec = (__force u64) ct / NSEC_PER_SEC;
	val->tv_usec = ((__force u64) ct % NSEC_PER_SEC) / NSEC_PER_USEC;
}

/*
 * Convert cputime <-> clock (USER_HZ)
 */
#define cputime_to_clock_t(__ct)	\
	((__force u64)(__ct) / (NSEC_PER_SEC / USER_HZ))
#define clock_t_to_cputime(__x)		\
	(__force cputime_t)((__x) * (NSEC_PER_SEC / USER_HZ))

/*
 * Convert cputime64 to clock.
 */
#define cputime64_to_clock_t(__ct)	\
	cputime_to_clock_t((__force cputime_t)__ct)

#endif /* CONFIG_VIRT_CPU_ACCOUNTING */
#endif /* __IA64_CPUTIME_H */
