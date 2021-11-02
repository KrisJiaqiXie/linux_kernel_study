/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright 1998 by Albert Cahalan; all rights reserved.
 * Copyright (C) 2002 by Vladimir Oleynik <dzo@simtreas.ru>
 * SELinux support: (c) 2007 by Yuichi Nakamura <ynakam@hitachisoft.jp>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this tarball for details.
 */

#include "libbb.h"


typedef struct unsigned_to_name_map_t {
	unsigned id;
	char name[USERNAME_MAX_SIZE];
} unsigned_to_name_map_t;

typedef struct cache_t {
	unsigned_to_name_map_t *cache;
	int size;
} cache_t;

static cache_t username, groupname;

static void clear_cache(cache_t *cp)
{
	free(cp->cache);
	cp->cache = NULL;
	cp->size = 0;
}
void clear_username_cache(void)
{
	clear_cache(&username);
	clear_cache(&groupname);
}

#if 0 /* more generic, but we don't need that yet */
/* Returns -N-1 if not found. */
/* cp->cache[N] is allocated and must be filled in this case */
static int get_cached(cache_t *cp, unsigned id)
{
	int i;
	for (i = 0; i < cp->size; i++)
		if (cp->cache[i].id == id)
			return i;
	i = cp->size++;
	cp->cache = xrealloc(cp->cache, cp->size * sizeof(*cp->cache));
	cp->cache[i++].id = id;
	return -i;
}
#endif

typedef char* ug_func(char *name, int bufsize, long uid);
static char* get_cached(cache_t *cp, unsigned id, ug_func* fp)
{
	int i;
	for (i = 0; i < cp->size; i++)
		if (cp->cache[i].id == id)
			return cp->cache[i].name;
	i = cp->size++;
	cp->cache = xrealloc(cp->cache, cp->size * sizeof(*cp->cache));
	cp->cache[i].id = id;
	/* Never fails. Generates numeric string if name isn't found */
	fp(cp->cache[i].name, sizeof(cp->cache[i].name), id);
	return cp->cache[i].name;
}
const char* get_cached_username(uid_t uid)
{
	return get_cached(&username, uid, bb_getpwuid);
}
const char* get_cached_groupname(gid_t gid)
{
	return get_cached(&groupname, gid, bb_getgrgid);
}


#define PROCPS_BUFSIZE 1024

static int read_to_buf(const char *filename, void *buf)
{
	int fd;
	/* open_read_close() would do two reads, checking for EOF.
	 * When you have 10000 /proc/$NUM/stat to read, it isn't desirable */
	ssize_t ret = -1;
	fd = open(filename, O_RDONLY);
	if (fd >= 0) {
		ret = read(fd, buf, PROCPS_BUFSIZE-1);
		close(fd);
	}
	((char *)buf)[ret > 0 ? ret : 0] = '\0';
	return ret;
}

procps_status_t *alloc_procps_scan(int flags)
{
	procps_status_t* sp = xzalloc(sizeof(procps_status_t));
	sp->dir = xopendir("/proc");
	return sp;
}

void free_procps_scan(procps_status_t* sp)
{
	closedir(sp->dir);
	free(sp->argv0);
	USE_SELINUX(free(sp->context);)
	free(sp);
}

#if ENABLE_FEATURE_FAST_TOP
/* We cut a lot of corners here for speed */
static unsigned long fast_strtoul_10(char **endptr)
{
	char c;
	char *str = *endptr;
	unsigned long n = *str - '0';

	while ((c = *++str) != ' ')
		n = n*10 + (c - '0');

	*endptr = str + 1; /* We skip trailing space! */
	return n;
}
static char *skip_fields(char *str, int count)
{
	do {
		while (*str++ != ' ')
			continue;
		/* we found a space char, str points after it */
	} while (--count);
	return str;
}
#endif

void BUG_comm_size(void);
procps_status_t *procps_scan(procps_status_t* sp, int flags)
{
	struct dirent *entry;
	char buf[PROCPS_BUFSIZE];
	char filename[sizeof("/proc//cmdline") + sizeof(int)*3];
	char *filename_tail;
	long tasknice;
	unsigned pid;
	int n;
	struct stat sb;

	if (!sp)
		sp = alloc_procps_scan(flags);

	for (;;) {
		entry = readdir(sp->dir);
		if (entry == NULL) {
			free_procps_scan(sp);
			return NULL;
		}
		pid = bb_strtou(entry->d_name, NULL, 10);
		if (errno)
			continue;

		/* After this point we have to break, not continue
		 * ("continue" would mean that current /proc/NNN
		 * is not a valid process info) */

		memset(&sp->vsz, 0, sizeof(*sp) - offsetof(procps_status_t, vsz));

		sp->pid = pid;
		if (!(flags & ~PSSCAN_PID)) break;

#if ENABLE_SELINUX
		if (flags & PSSCAN_CONTEXT) {
			if (getpidcon(sp->pid, &sp->context) < 0)
				sp->context = NULL;
		}
#endif

		filename_tail = filename + sprintf(filename, "/proc/%d", pid);

		if (flags & PSSCAN_UIDGID) {
			if (stat(filename, &sb))
				break;
			/* Need comment - is this effective or real UID/GID? */
			sp->uid = sb.st_uid;
			sp->gid = sb.st_gid;
		}

		if (flags & PSSCAN_STAT) {
			char *cp, *comm1;
			int tty;
#if !ENABLE_FEATURE_FAST_TOP
			unsigned long vsz, rss;
#endif

			/* see proc(5) for some details on this */
			strcpy(filename_tail, "/stat");
			n = read_to_buf(filename, buf);
			if (n < 0)
				break;
			cp = strrchr(buf, ')'); /* split into "PID (cmd" and "<rest>" */
			/*if (!cp || cp[1] != ' ')
				break;*/
			cp[0] = '\0';
			if (sizeof(sp->comm) < 16)
				BUG_comm_size();
			comm1 = strchr(buf, '(');
			/*if (comm1)*/
				safe_strncpy(sp->comm, comm1 + 1, sizeof(sp->comm));

#if !ENABLE_FEATURE_FAST_TOP
			n = sscanf(cp+2,
				"%c %u "               /* state, ppid */
				"%u %u %d %*s "        /* pgid, sid, tty, tpgid */
				"%*s %*s %*s %*s %*s " /* flags, min_flt, cmin_flt, maj_flt, cmaj_flt */
				"%lu %lu "             /* utime, stime */
				"%*s %*s %*s "         /* cutime, cstime, priority */
				"%ld "                 /* nice */
				"%*s %*s %*s "         /* timeout, it_real_value, start_time */
				"%lu "                 /* vsize */
				"%lu "                 /* rss */
			/*	"%lu %lu %lu %lu %lu %lu " rss_rlim, start_code, end_code, start_stack, kstk_esp, kstk_eip */
			/*	"%u %u %u %u "         signal, blocked, sigignore, sigcatch */
			/*	"%lu %lu %lu"          wchan, nswap, cnswap */
				,
				sp->state, &sp->ppid,
				&sp->pgid, &sp->sid, &tty,
				&sp->utime, &sp->stime,
				&tasknice,
				&vsz,
				&rss);
			if (n != 10)
				break;
			sp->vsz = vsz >> 10; /* vsize is in bytes and we want kb */
			sp->rss = rss >> 10;
			sp->tty_major = (tty >> 8) & 0xfff;
			sp->tty_minor = (tty & 0xff) | ((tty >> 12) & 0xfff00);
#else
/* This costs ~100 bytes more but makes top faster by 20%
 * If you run 10000 processes, this may be important for you */
			sp->state[0] = cp[2];
			cp += 4;
			sp->ppid = fast_strtoul_10(&cp);
			sp->pgid = fast_strtoul_10(&cp);
			sp->sid = fast_strtoul_10(&cp);
			tty = fast_strtoul_10(&cp);
			sp->tty_major = (tty >> 8) & 0xfff;
			sp->tty_minor = (tty & 0xff) | ((tty >> 12) & 0xfff00);
			cp = skip_fields(cp, 6); /* tpgid, flags, min_flt, cmin_flt, maj_flt, cmaj_flt */
			sp->utime = fast_strtoul_10(&cp);
			sp->stime = fast_strtoul_10(&cp);
			cp = skip_fields(cp, 3); /* cutime, cstime, priority */
			tasknice = fast_strtoul_10(&cp);
			cp = skip_fields(cp, 3); /* timeout, it_real_value, start_time */
			sp->vsz = fast_strtoul_10(&cp) >> 10; /* vsize is in bytes and we want kb */
			sp->rss = fast_strtoul_10(&cp) >> 10;
#endif

			if (sp->vsz == 0 && sp->state[0] != 'Z')
				sp->state[1] = 'W';
			else
				sp->state[1] = ' ';
			if (tasknice < 0)
				sp->state[2] = '<';
			else if (tasknice) /* > 0 */
				sp->state[2] = 'N';
			else
				sp->state[2] = ' ';

		}

#if 0 /* PSSCAN_CMD is not used */
		if (flags & (PSSCAN_CMD|PSSCAN_ARGV0)) {
			if (sp->argv0) {
				free(sp->argv0);
				sp->argv0 = NULL;
			}
			if (sp->cmd) {
				free(sp->cmd);
				sp->cmd = NULL;
			}
			strcpy(filename_tail, "/cmdline");
			/* TODO: to get rid of size limits, read into malloc buf,
			 * then realloc it down to real size. */
			n = read_to_buf(filename, buf);
			if (n <= 0)
				break;
			if (flags & PSSCAN_ARGV0)
				sp->argv0 = xstrdup(buf);
			if (flags & PSSCAN_CMD) {
				do {
					n--;
					if ((unsigned char)(buf[n]) < ' ')
						buf[n] = ' ';
				} while (n);
				sp->cmd = xstrdup(buf);
			}
		}
#else
		if (flags & PSSCAN_ARGV0) {
			if (sp->argv0) {
				free(sp->argv0);
				sp->argv0 = NULL;
			}
			strcpy(filename_tail, "/cmdline");
			n = read_to_buf(filename, buf);
			if (n <= 0)
				break;
			if (flags & PSSCAN_ARGV0)
				sp->argv0 = xstrdup(buf);
		}
#endif
		break;
	}
	return sp;
}

void read_cmdline(char *buf, int col, unsigned pid, const char *comm)
{
	ssize_t sz;
	char filename[sizeof("/proc//cmdline") + sizeof(int)*3];

	sprintf(filename, "/proc/%u/cmdline", pid);
	sz = open_read_close(filename, buf, col);
	if (sz > 0) {
		buf[sz] = '\0';
		while (--sz >= 0)
			if ((unsigned char)(buf[sz]) < ' ')
				buf[sz] = ' ';
	} else {
		snprintf(buf, col, "[%s]", comm);
	}
}

/* from kernel:
	//             pid comm S ppid pgid sid tty_nr tty_pgrp flg
	sprintf(buffer,"%d (%s) %c %d  %d   %d  %d     %d       %lu %lu \
%lu %lu %lu %lu %lu %ld %ld %ld %ld %d 0 %llu %lu %ld %lu %lu %lu %lu %lu \
%lu %lu %lu %lu %lu %lu %lu %lu %d %d %lu %lu %llu\n",
		task->pid,
		tcomm,
		state,
		ppid,
		pgid,
		sid,
		tty_nr,
		tty_pgrp,
		task->flags,
		min_flt,
		cmin_flt,
		maj_flt,
		cmaj_flt,
		cputime_to_clock_t(utime),
		cputime_to_clock_t(stime),
		cputime_to_clock_t(cutime),
		cputime_to_clock_t(cstime),
		priority,
		nice,
		num_threads,
		// 0,
		start_time,
		vsize,
		mm ? get_mm_rss(mm) : 0,
		rsslim,
		mm ? mm->start_code : 0,
		mm ? mm->end_code : 0,
		mm ? mm->start_stack : 0,
		esp,
		eip,
the rest is some obsolete cruft
*/
