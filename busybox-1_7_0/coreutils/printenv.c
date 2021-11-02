/* vi: set sw=4 ts=4: */
/*
 * printenv implementation for busybox
 *
 * Copyright (C) 2005 by Erik Andersen <andersen@codepoet.org>
 * Copyright (C) 2005 by Mike Frysinger <vapier@gentoo.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this tarball for details.
 */

#include "libbb.h"
extern char **environ;

int printenv_main(int argc, char **argv);
int printenv_main(int argc, char **argv)
{
	/* no variables specified, show whole env */
	if (argc == 1) {
		int e = 0;
		while (environ[e])
			puts(environ[e++]);
	} else {
		/* search for specified variables and print them out if found */
		char *arg, *env;

		while ((arg = *++argv) != NULL) {
			env = getenv(arg);
			if (env)
				puts(env);
		}
	}

	fflush_stdout_and_exit(0);
}
