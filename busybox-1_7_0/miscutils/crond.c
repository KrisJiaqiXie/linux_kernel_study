/* vi: set sw=4 ts=4: */
/*
 * crond -d[#] -c <crondir> -f -b
 *
 * run as root, but NOT setuid root
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.west.oic.com)
 * (version 2.3.2)
 * Vladimir Oleynik <dzo@simtreas.ru> (C) 2002
 *
 * Licensed under the GPL v2 or later, see the file LICENSE in this tarball.
 */

#include <sys/syslog.h>
#include "libbb.h"

#ifndef CRONTABS
#define CRONTABS        "/var/spool/cron/crontabs"
#endif
#ifndef TMPDIR
#define TMPDIR          "/var/spool/cron"
#endif
#ifndef SENDMAIL
#define SENDMAIL        "sendmail"
#endif
#ifndef SENDMAIL_ARGS
#define SENDMAIL_ARGS   "-ti", "oem"
#endif
#ifndef CRONUPDATE
#define CRONUPDATE      "cron.update"
#endif
#ifndef MAXLINES
#define MAXLINES        256	/* max lines in non-root crontabs */
#endif

typedef struct CronFile {
	struct CronFile *cf_Next;
	struct CronLine *cf_LineBase;
	char *cf_User;		/* username                     */
	int cf_Ready;		/* bool: one or more jobs ready */
	int cf_Running;		/* bool: one or more jobs running */
	int cf_Deleted;		/* marked for deletion, ignore  */
} CronFile;

typedef struct CronLine {
	struct CronLine *cl_Next;
	char *cl_Shell;		/* shell command                        */
	pid_t cl_Pid;		/* running pid, 0, or armed (-1)        */
	int cl_MailFlag;	/* running pid is for mail              */
	int cl_MailPos;		/* 'empty file' size                    */
	char cl_Mins[60];	/* 0-59                                 */
	char cl_Hrs[24];	/* 0-23                                 */
	char cl_Days[32];	/* 1-31                                 */
	char cl_Mons[12];	/* 0-11                                 */
	char cl_Dow[7];		/* 0-6, beginning sunday                */
} CronLine;

#define RUN_RANOUT      1
#define RUN_RUNNING     2
#define RUN_FAILED      3

#define DaemonUid 0

#if ENABLE_DEBUG_CROND_OPTION
static unsigned DebugOpt;
#endif

static unsigned LogLevel = 8;
static const char *LogFile;
static const char *CDir = CRONTABS;

static void startlogger(void);

static void CheckUpdates(void);
static void SynchronizeDir(void);
static int TestJobs(time_t t1, time_t t2);
static void RunJobs(void);
static int CheckJobs(void);

static void RunJob(const char *user, CronLine * line);

#if ENABLE_FEATURE_CROND_CALL_SENDMAIL
static void EndJob(const char *user, CronLine * line);
#else
#define EndJob(user, line)  line->cl_Pid = 0
#endif

static void DeleteFile(const char *userName);

static CronFile *FileBase;


static void crondlog(const char *ctl, ...)
{
	va_list va;
	const char *fmt;
	int level = (int) (ctl[0] & 0xf);
	int type = level == 20 ?
		LOG_ERR : ((ctl[0] & 0100) ? LOG_WARNING : LOG_NOTICE);


	va_start(va, ctl);
	fmt = ctl + 1;
	if (level >= LogLevel) {

#if ENABLE_DEBUG_CROND_OPTION
		if (DebugOpt) {
			vfprintf(stderr, fmt, va);
		} else
#endif
		if (LogFile == 0) {
			vsyslog(type, fmt, va);
		} else {
#if !ENABLE_DEBUG_CROND_OPTION
			int logfd = open(LogFile, O_WRONLY | O_CREAT | O_APPEND, 0600);
#else
			int logfd = open3_or_warn(LogFile, O_WRONLY | O_CREAT | O_APPEND, 0600);
#endif
			if (logfd >= 0) {
				vdprintf(logfd, fmt, va);
				close(logfd);
			}
		}
	}
	va_end(va);
	if (ctl[0] & 0200) {
		exit(20);
	}
}

int crond_main(int ac, char **av);
int crond_main(int ac, char **av)
{
	unsigned opt;
	char *lopt, *Lopt, *copt;
	USE_DEBUG_CROND_OPTION(char *dopt;)

	opt_complementary = "f-b:b-f:S-L:L-S" USE_DEBUG_CROND_OPTION(":d-l");
	opterr = 0;			/* disable getopt 'errors' message. */
	opt = getopt32(av, "l:L:fbSc:" USE_DEBUG_CROND_OPTION("d:"),
			&lopt, &Lopt, &copt USE_DEBUG_CROND_OPTION(, &dopt));
	if (opt & 1) /* -l */
		LogLevel = xatou(lopt);
	if (opt & 2) /* -L */
		if (*Lopt)
			LogFile = Lopt;
	if (opt & 32) /* -c */
		if (*copt)
			CDir = copt;
#if ENABLE_DEBUG_CROND_OPTION
	if (opt & 64) { /* -d */
		DebugOpt = xatou(dopt);
		LogLevel = 0;
	}
#endif

	/* close stdin and stdout, stderr.
	 * close unused descriptors -  don't need.
	 * optional detach from controlling terminal
	 */
	if (!(opt & 4))
		bb_daemonize_or_rexec(DAEMON_CLOSE_EXTRA_FDS, av);

	xchdir(CDir);
	signal(SIGHUP, SIG_IGN); /* ? original crond dies on HUP... */

	startlogger();	/* need if syslog mode selected */

	/*
	 * main loop - synchronize to 1 second after the minute, minimum sleep
	 *             of 1 second.
	 */
	crondlog("\011%s " BB_VER " started, log level %d\n",
			 applet_name, LogLevel);

	SynchronizeDir();

	{
		time_t t1 = time(NULL);
		time_t t2;
		long dt;
		int rescan = 60;
		short sleep_time = 60;

		write_pidfile("/var/run/crond.pid");
		for (;;) {
			sleep((sleep_time + 1) - (short) (time(NULL) % sleep_time));

			t2 = time(NULL);
			dt = t2 - t1;

			/*
			 * The file 'cron.update' is checked to determine new cron
			 * jobs.  The directory is rescanned once an hour to deal
			 * with any screwups.
			 *
			 * check for disparity.  Disparities over an hour either way
			 * result in resynchronization.  A reverse-indexed disparity
			 * less then an hour causes us to effectively sleep until we
			 * match the original time (i.e. no re-execution of jobs that
			 * have just been run).  A forward-indexed disparity less then
			 * an hour causes intermediate jobs to be run, but only once
			 * in the worst case.
			 *
			 * when running jobs, the inequality used is greater but not
			 * equal to t1, and less then or equal to t2.
			 */

			if (--rescan == 0) {
				rescan = 60;
				SynchronizeDir();
			}
			CheckUpdates();
#if ENABLE_DEBUG_CROND_OPTION
			if (DebugOpt)
				crondlog("\005Wakeup dt=%d\n", dt);
#endif
			if (dt < -60 * 60 || dt > 60 * 60) {
				t1 = t2;
				crondlog("\111time disparity of %d minutes detected\n", dt / 60);
			} else if (dt > 0) {
				TestJobs(t1, t2);
				RunJobs();
				sleep(5);
				if (CheckJobs() > 0) {
					sleep_time = 10;
				} else {
					sleep_time = 60;
				}
				t1 = t2;
			}
		}
	}
	return 0; /* not reached */
}

static int ChangeUser(const char *user)
{
	struct passwd *pas;
	const char *err_msg;

	/*
	 * Obtain password entry and change privileges
	 */
	pas = getpwnam(user);
	if (pas == 0) {
		crondlog("\011failed to get uid for %s", user);
		return -1;
	}
	setenv("USER", pas->pw_name, 1);
	setenv("HOME", pas->pw_dir, 1);
	setenv("SHELL", DEFAULT_SHELL, 1);

	/*
	 * Change running state to the user in question
	 */
	err_msg = change_identity_e2str(pas);
	if (err_msg) {
		crondlog("\011%s for user %s", err_msg, user);
		return -1;
	}
	if (chdir(pas->pw_dir) < 0) {
		crondlog("\011chdir failed: %s: %m", pas->pw_dir);
		if (chdir(TMPDIR) < 0) {
			crondlog("\011chdir failed: %s: %m", TMPDIR);
			return -1;
		}
	}
	return pas->pw_uid;
}

static void startlogger(void)
{
	if (LogFile == 0) {
		openlog(applet_name, LOG_CONS | LOG_PID, LOG_CRON);
	}
#if ENABLE_DEBUG_CROND_OPTION
	else {				/* test logfile */
		int logfd;

		logfd = open3_or_warn(LogFile, O_WRONLY | O_CREAT | O_APPEND, 0600);
		if (logfd >= 0) {
			close(logfd);
		}
	}
#endif
}


static const char DowAry[] ALIGN1 =
	"sun""mon""tue""wed""thu""fri""sat"
	/* "Sun""Mon""Tue""Wed""Thu""Fri""Sat" */
;

static const char MonAry[] ALIGN1 =
	"jan""feb""mar""apr""may""jun""jul""aug""sep""oct""nov""dec"
	/* "Jan""Feb""Mar""Apr""May""Jun""Jul""Aug""Sep""Oct""Nov""Dec" */
;

static char *ParseField(char *user, char *ary, int modvalue, int off,
				const char *names, char *ptr)
/* 'names' is a pointer to a set of 3-char abbreviations */
{
	char *base = ptr;
	int n1 = -1;
	int n2 = -1;

	if (base == NULL) {
		return NULL;
	}

	while (*ptr != ' ' && *ptr != '\t' && *ptr != '\n') {
		int skip = 0;

		/* Handle numeric digit or symbol or '*' */

		if (*ptr == '*') {
			n1 = 0;		/* everything will be filled */
			n2 = modvalue - 1;
			skip = 1;
			++ptr;
		} else if (*ptr >= '0' && *ptr <= '9') {
			if (n1 < 0) {
				n1 = strtol(ptr, &ptr, 10) + off;
			} else {
				n2 = strtol(ptr, &ptr, 10) + off;
			}
			skip = 1;
		} else if (names) {
			int i;

			for (i = 0; names[i]; i += 3) {
				/* was using strncmp before... */
				if (strncasecmp(ptr, &names[i], 3) == 0) {
					ptr += 3;
					if (n1 < 0) {
						n1 = i / 3;
					} else {
						n2 = i / 3;
					}
					skip = 1;
					break;
				}
			}
		}

		/* handle optional range '-' */

		if (skip == 0) {
			crondlog("\111failed user %s parsing %s\n", user, base);
			return NULL;
		}
		if (*ptr == '-' && n2 < 0) {
			++ptr;
			continue;
		}

		/*
		 * collapse single-value ranges, handle skipmark, and fill
		 * in the character array appropriately.
		 */

		if (n2 < 0) {
			n2 = n1;
		}
		if (*ptr == '/') {
			skip = strtol(ptr + 1, &ptr, 10);
		}
		/*
		 * fill array, using a failsafe is the easiest way to prevent
		 * an endless loop
		 */

		{
			int s0 = 1;
			int failsafe = 1024;

			--n1;
			do {
				n1 = (n1 + 1) % modvalue;

				if (--s0 == 0) {
					ary[n1 % modvalue] = 1;
					s0 = skip;
				}
			}
			while (n1 != n2 && --failsafe);

			if (failsafe == 0) {
				crondlog("\111failed user %s parsing %s\n", user, base);
				return NULL;
			}
		}
		if (*ptr != ',') {
			break;
		}
		++ptr;
		n1 = -1;
		n2 = -1;
	}

	if (*ptr != ' ' && *ptr != '\t' && *ptr != '\n') {
		crondlog("\111failed user %s parsing %s\n", user, base);
		return NULL;
	}

	while (*ptr == ' ' || *ptr == '\t' || *ptr == '\n') {
		++ptr;
	}
#if ENABLE_DEBUG_CROND_OPTION
	if (DebugOpt) {
		int i;

		for (i = 0; i < modvalue; ++i) {
			crondlog("\005%d", ary[i]);
		}
		crondlog("\005\n");
	}
#endif

	return ptr;
}

static void FixDayDow(CronLine * line)
{
	int i;
	int weekUsed = 0;
	int daysUsed = 0;

	for (i = 0; i < (int)(ARRAY_SIZE(line->cl_Dow)); ++i) {
		if (line->cl_Dow[i] == 0) {
			weekUsed = 1;
			break;
		}
	}
	for (i = 0; i < (int)(ARRAY_SIZE(line->cl_Days)); ++i) {
		if (line->cl_Days[i] == 0) {
			daysUsed = 1;
			break;
		}
	}
	if (weekUsed && !daysUsed) {
		memset(line->cl_Days, 0, sizeof(line->cl_Days));
	}
	if (daysUsed && !weekUsed) {
		memset(line->cl_Dow, 0, sizeof(line->cl_Dow));
	}
}



static void SynchronizeFile(const char *fileName)
{
	int maxEntries = MAXLINES;
	int maxLines;
	char buf[1024];

	if (strcmp(fileName, "root") == 0) {
		maxEntries = 65535;
	}
	maxLines = maxEntries * 10;

	if (fileName) {
		FILE *fi;

		DeleteFile(fileName);

		fi = fopen(fileName, "r");
		if (fi != NULL) {
			struct stat sbuf;

			if (fstat(fileno(fi), &sbuf) == 0 && sbuf.st_uid == DaemonUid) {
				CronFile *file = xzalloc(sizeof(CronFile));
				CronLine **pline;

				file->cf_User = strdup(fileName);
				pline = &file->cf_LineBase;

				while (fgets(buf, sizeof(buf), fi) != NULL && --maxLines) {
					CronLine line;
					char *ptr;

					trim(buf);
					if (buf[0] == 0 || buf[0] == '#') {
						continue;
					}
					if (--maxEntries == 0) {
						break;
					}
					memset(&line, 0, sizeof(line));

#if ENABLE_DEBUG_CROND_OPTION
					if (DebugOpt) {
						crondlog("\111User %s Entry %s\n", fileName, buf);
					}
#endif

					/* parse date ranges */
					ptr = ParseField(file->cf_User, line.cl_Mins, 60, 0, NULL, buf);
					ptr = ParseField(file->cf_User, line.cl_Hrs, 24, 0, NULL, ptr);
					ptr = ParseField(file->cf_User, line.cl_Days, 32, 0, NULL, ptr);
					ptr = ParseField(file->cf_User, line.cl_Mons, 12, -1, MonAry, ptr);
					ptr = ParseField(file->cf_User, line.cl_Dow, 7, 0, DowAry, ptr);

					/* check failure */
					if (ptr == NULL) {
						continue;
					}

					/*
					 * fix days and dow - if one is not * and the other
					 * is *, the other is set to 0, and vise-versa
					 */

					FixDayDow(&line);

					*pline = xzalloc(sizeof(CronLine));
					**pline = line;

					/* copy command */
					(*pline)->cl_Shell = strdup(ptr);

#if ENABLE_DEBUG_CROND_OPTION
					if (DebugOpt) {
						crondlog("\111    Command %s\n", ptr);
					}
#endif

					pline = &((*pline)->cl_Next);
				}
				*pline = NULL;

				file->cf_Next = FileBase;
				FileBase = file;

				if (maxLines == 0 || maxEntries == 0) {
					crondlog("\111Maximum number of lines reached for user %s\n", fileName);
				}
			}
			fclose(fi);
		}
	}
}

static void CheckUpdates(void)
{
	FILE *fi;
	char buf[256];

	fi = fopen(CRONUPDATE, "r");
	if (fi != NULL) {
		remove(CRONUPDATE);
		while (fgets(buf, sizeof(buf), fi) != NULL) {
			SynchronizeFile(strtok(buf, " \t\r\n"));
		}
		fclose(fi);
	}
}

static void SynchronizeDir(void)
{
	/* Attempt to delete the database. */

	for (;;) {
		CronFile *file;

		for (file = FileBase; file && file->cf_Deleted; file = file->cf_Next);
		if (file == NULL) {
			break;
		}
		DeleteFile(file->cf_User);
	}

	/*
	 * Remove cron update file
	 *
	 * Re-chdir, in case directory was renamed & deleted, or otherwise
	 * screwed up.
	 *
	 * scan directory and add associated users
	 */

	remove(CRONUPDATE);
	if (chdir(CDir) < 0) {
		crondlog("\311cannot find %s\n", CDir);
	}
	{
		DIR *dir = opendir(".");
		struct dirent *den;

		if (dir) {
			while ((den = readdir(dir))) {
				if (strchr(den->d_name, '.') != NULL) {
					continue;
				}
				if (getpwnam(den->d_name)) {
					SynchronizeFile(den->d_name);
				} else {
					crondlog("\007ignoring %s\n", den->d_name);
				}
			}
			closedir(dir);
		} else {
			crondlog("\311cannot open current dir!\n");
		}
	}
}


/*
 *  DeleteFile() - delete user database
 *
 *  Note: multiple entries for same user may exist if we were unable to
 *  completely delete a database due to running processes.
 */

static void DeleteFile(const char *userName)
{
	CronFile **pfile = &FileBase;
	CronFile *file;

	while ((file = *pfile) != NULL) {
		if (strcmp(userName, file->cf_User) == 0) {
			CronLine **pline = &file->cf_LineBase;
			CronLine *line;

			file->cf_Running = 0;
			file->cf_Deleted = 1;

			while ((line = *pline) != NULL) {
				if (line->cl_Pid > 0) {
					file->cf_Running = 1;
					pline = &line->cl_Next;
				} else {
					*pline = line->cl_Next;
					free(line->cl_Shell);
					free(line);
				}
			}
			if (file->cf_Running == 0) {
				*pfile = file->cf_Next;
				free(file->cf_User);
				free(file);
			} else {
				pfile = &file->cf_Next;
			}
		} else {
			pfile = &file->cf_Next;
		}
	}
}

/*
 * TestJobs()
 *
 * determine which jobs need to be run.  Under normal conditions, the
 * period is about a minute (one scan).  Worst case it will be one
 * hour (60 scans).
 */

static int TestJobs(time_t t1, time_t t2)
{
	int nJobs = 0;
	time_t t;

	/* Find jobs > t1 and <= t2 */

	for (t = t1 - t1 % 60; t <= t2; t += 60) {
		if (t > t1) {
			struct tm *tp = localtime(&t);
			CronFile *file;
			CronLine *line;

			for (file = FileBase; file; file = file->cf_Next) {
#if ENABLE_DEBUG_CROND_OPTION
				if (DebugOpt)
					crondlog("\005FILE %s:\n", file->cf_User);
#endif
				if (file->cf_Deleted)
					continue;
				for (line = file->cf_LineBase; line; line = line->cl_Next) {
#if ENABLE_DEBUG_CROND_OPTION
					if (DebugOpt)
						crondlog("\005    LINE %s\n", line->cl_Shell);
#endif
					if (line->cl_Mins[tp->tm_min] && line->cl_Hrs[tp->tm_hour] &&
						(line->cl_Days[tp->tm_mday] || line->cl_Dow[tp->tm_wday])
						&& line->cl_Mons[tp->tm_mon]) {
#if ENABLE_DEBUG_CROND_OPTION
						if (DebugOpt) {
							crondlog("\005    JobToDo: %d %s\n",
								line->cl_Pid, line->cl_Shell);
						}
#endif
						if (line->cl_Pid > 0) {
							crondlog("\010    process already running: %s %s\n",
								file->cf_User, line->cl_Shell);
						} else if (line->cl_Pid == 0) {
							line->cl_Pid = -1;
							file->cf_Ready = 1;
							++nJobs;
						}
					}
				}
			}
		}
	}
	return nJobs;
}

static void RunJobs(void)
{
	CronFile *file;
	CronLine *line;

	for (file = FileBase; file; file = file->cf_Next) {
		if (file->cf_Ready) {
			file->cf_Ready = 0;

			for (line = file->cf_LineBase; line; line = line->cl_Next) {
				if (line->cl_Pid < 0) {

					RunJob(file->cf_User, line);

					crondlog("\010USER %s pid %3d cmd %s\n",
						file->cf_User, line->cl_Pid, line->cl_Shell);
					if (line->cl_Pid < 0) {
						file->cf_Ready = 1;
					}
					else if (line->cl_Pid > 0) {
						file->cf_Running = 1;
					}
				}
			}
		}
	}
}

/*
 * CheckJobs() - check for job completion
 *
 * Check for job completion, return number of jobs still running after
 * all done.
 */

static int CheckJobs(void)
{
	CronFile *file;
	CronLine *line;
	int nStillRunning = 0;

	for (file = FileBase; file; file = file->cf_Next) {
		if (file->cf_Running) {
			file->cf_Running = 0;

			for (line = file->cf_LineBase; line; line = line->cl_Next) {
				if (line->cl_Pid > 0) {
					int status;
					int r = wait4(line->cl_Pid, &status, WNOHANG, NULL);

					if (r < 0 || r == line->cl_Pid) {
						EndJob(file->cf_User, line);
						if (line->cl_Pid) {
							file->cf_Running = 1;
						}
					} else if (r == 0) {
						file->cf_Running = 1;
					}
				}
			}
		}
		nStillRunning += file->cf_Running;
	}
	return nStillRunning;
}


#if ENABLE_FEATURE_CROND_CALL_SENDMAIL
static void
ForkJob(const char *user, CronLine * line, int mailFd,
		const char *prog, const char *cmd, const char *arg, const char *mailf)
{
	/* Fork as the user in question and run program */
	pid_t pid = fork();

	line->cl_Pid = pid;
	if (pid == 0) {
		/* CHILD */

		/* Change running state to the user in question */

		if (ChangeUser(user) < 0) {
			exit(0);
		}
#if ENABLE_DEBUG_CROND_OPTION
		if (DebugOpt) {
			crondlog("\005Child Running %s\n", prog);
		}
#endif

		if (mailFd >= 0) {
			dup2(mailFd, mailf != NULL);
			dup2((mailf ? mailFd : 1), 2);
			close(mailFd);
		}
		execl(prog, prog, cmd, arg, NULL);
		crondlog("\024cannot exec, user %s cmd %s %s %s\n", user, prog, cmd, arg);
		if (mailf) {
			fdprintf(1, "Exec failed: %s -c %s\n", prog, arg);
		}
		exit(0);
	} else if (pid < 0) {
		/* FORK FAILED */
		crondlog("\024cannot fork, user %s\n", user);
		line->cl_Pid = 0;
		if (mailf) {
			remove(mailf);
		}
	} else if (mailf) {
		/* PARENT, FORK SUCCESS
		 * rename mail-file based on pid of process
		 */
		char mailFile2[128];

		snprintf(mailFile2, sizeof(mailFile2), TMPDIR "/cron.%s.%d", user, pid);
		rename(mailf, mailFile2);
	}
	/*
	 * Close the mail file descriptor.. we can't just leave it open in
	 * a structure, closing it later, because we might run out of descriptors
	 */

	if (mailFd >= 0) {
		close(mailFd);
	}
}

static void RunJob(const char *user, CronLine * line)
{
	char mailFile[128];
	int mailFd;

	line->cl_Pid = 0;
	line->cl_MailFlag = 0;

	/* open mail file - owner root so nobody can screw with it. */

	snprintf(mailFile, sizeof(mailFile), TMPDIR "/cron.%s.%d", user, getpid());
	mailFd = open(mailFile, O_CREAT | O_TRUNC | O_WRONLY | O_EXCL | O_APPEND, 0600);

	if (mailFd >= 0) {
		line->cl_MailFlag = 1;
		fdprintf(mailFd, "To: %s\nSubject: cron: %s\n\n", user,
			line->cl_Shell);
		line->cl_MailPos = lseek(mailFd, 0, SEEK_CUR);
	} else {
		crondlog("\024cannot create mail file user %s file %s, output to /dev/null\n", user, mailFile);
	}

	ForkJob(user, line, mailFd, DEFAULT_SHELL, "-c", line->cl_Shell, mailFile);
}

/*
 * EndJob - called when job terminates and when mail terminates
 */

static void EndJob(const char *user, CronLine * line)
{
	int mailFd;
	char mailFile[128];
	struct stat sbuf;

	/* No job */

	if (line->cl_Pid <= 0) {
		line->cl_Pid = 0;
		return;
	}

	/*
	 * End of job and no mail file
	 * End of sendmail job
	 */

	snprintf(mailFile, sizeof(mailFile), TMPDIR "/cron.%s.%d", user, line->cl_Pid);
	line->cl_Pid = 0;

	if (line->cl_MailFlag != 1) {
		return;
	}
	line->cl_MailFlag = 0;

	/*
	 * End of primary job - check for mail file.  If size has increased and
	 * the file is still valid, we sendmail it.
	 */

	mailFd = open(mailFile, O_RDONLY);
	remove(mailFile);
	if (mailFd < 0) {
		return;
	}

	if (fstat(mailFd, &sbuf) < 0 || sbuf.st_uid != DaemonUid
	 || sbuf.st_nlink != 0 || sbuf.st_size == line->cl_MailPos
	 || !S_ISREG(sbuf.st_mode)
	) {
		close(mailFd);
		return;
	}
	ForkJob(user, line, mailFd, SENDMAIL, SENDMAIL_ARGS, NULL);
}
#else
/* crond without sendmail */

static void RunJob(const char *user, CronLine * line)
{
	/* Fork as the user in question and run program */
	pid_t pid = fork();

	if (pid == 0) {
		/* CHILD */

		/* Change running state to the user in question */

		if (ChangeUser(user) < 0) {
			exit(0);
		}
#if ENABLE_DEBUG_CROND_OPTION
		if (DebugOpt) {
			crondlog("\005Child Running %s\n", DEFAULT_SHELL);
		}
#endif

		execl(DEFAULT_SHELL, DEFAULT_SHELL, "-c", line->cl_Shell, NULL);
		crondlog("\024cannot exec, user %s cmd %s -c %s\n", user,
				 DEFAULT_SHELL, line->cl_Shell);
		exit(0);
	} else if (pid < 0) {
		/* FORK FAILED */
		crondlog("\024cannot, user %s\n", user);
		pid = 0;
	}
	line->cl_Pid = pid;
}
#endif /* ENABLE_FEATURE_CROND_CALL_SENDMAIL */
