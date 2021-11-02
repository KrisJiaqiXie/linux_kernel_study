/* vi: set sw=4 ts=4: */
/*
 * Mini chroot implementation for busybox
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this tarball for details.
 */

/* BB_AUDIT SUSv3 N/A -- Matches GNU behavior. */

#include "libbb.h"

int chroot_main(int argc, char **argv);
int chroot_main(int argc, char **argv)
{
	if (argc < 2) {
		bb_show_usage();
	}

	++argv;
	if (chroot(*argv)) {
		bb_perror_msg_and_die("cannot change root directory to %s", *argv);
	}
	xchdir("/");

	++argv;
	if (argc == 2) {
		argv -= 2;
		argv[0] = getenv("SHELL");
		if (!argv[0]) {
			argv[0] = (char *) DEFAULT_SHELL;
		}
		argv[1] = (char *) "-i";
	}

	BB_EXECVP(*argv, argv);
	bb_perror_msg_and_die("cannot execute %s", *argv);
}
