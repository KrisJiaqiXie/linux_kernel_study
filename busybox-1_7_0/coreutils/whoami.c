/* vi: set sw=4 ts=4: */
/*
 * Mini whoami implementation for busybox
 *
 * Copyright (C) 2000  Edward Betts <edward@debian.org>.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this tarball for details.
 */

/* BB_AUDIT SUSv3 N/A -- Matches GNU behavior. */

#include "libbb.h"

/* This is a NOFORK applet. Be very careful! */

int whoami_main(int argc, char **argv);
int whoami_main(int argc, char **argv)
{
	if (argc > 1)
		bb_show_usage();

	/* Will complain and die if username not found */
	puts(bb_getpwuid(NULL, -1, geteuid()));

	return fflush(stdout);
}
