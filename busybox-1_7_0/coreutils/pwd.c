/* vi: set sw=4 ts=4: */
/*
 * Mini pwd implementation for busybox
 *
 * Copyright (C) 1995, 1996 by Bruce Perens <bruce@pixar.com>.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this tarball for details.
 */

#include "libbb.h"

/* This is a NOFORK applet. Be very careful! */

int pwd_main(int argc, char **argv);
int pwd_main(int argc, char **argv)
{
	char *buf;

	buf = xrealloc_getcwd_or_warn(NULL);
	if (buf != NULL) {
		puts(buf);
		free(buf);
		return fflush(stdout);
	}

	return EXIT_FAILURE;
}
