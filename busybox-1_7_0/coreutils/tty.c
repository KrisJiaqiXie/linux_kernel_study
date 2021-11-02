/* vi: set sw=4 ts=4: */
/*
 * tty implementation for busybox
 *
 * Copyright (C) 2003  Manuel Novoa III  <mjn3@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this tarball for details.
 */

/* BB_AUDIT SUSv3 compliant */
/* http://www.opengroup.org/onlinepubs/007904975/utilities/tty.html */

#include "libbb.h"

int tty_main(int argc, char **argv);
int tty_main(int argc, char **argv)
{
	const char *s;
	USE_INCLUDE_SUSv2(int silent;)	/* Note: No longer relevant in SUSv3. */
	int retval;

	xfunc_error_retval = 2;	/* SUSv3 requires > 1 for error. */

	USE_INCLUDE_SUSv2(silent = getopt32(argv, "s");)

	/* gnu tty outputs a warning that it is ignoring all args. */
	bb_warn_ignoring_args(argc - optind);

	retval = 0;

	s = ttyname(0);
	if (s == NULL) {
	/* According to SUSv3, ttyname can on fail with EBADF or ENOTTY.
	 * We know the file descriptor is good, so failure means not a tty. */
		s = "not a tty";
		retval = 1;
	}
	USE_INCLUDE_SUSv2(if (!silent) puts(s);)
	SKIP_INCLUDE_SUSv2(puts(s);)

	fflush_stdout_and_exit(retval);
}
