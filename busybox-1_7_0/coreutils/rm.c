/* vi: set sw=4 ts=4: */
/*
 * Mini rm implementation for busybox
 *
 * Copyright (C) 2001 Matt Kraai <kraai@alumni.carnegiemellon.edu>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this tarball for details.
 */

/* BB_AUDIT SUSv3 compliant */
/* http://www.opengroup.org/onlinepubs/007904975/utilities/rm.html */

/* Mar 16, 2003      Manuel Novoa III   (mjn3@codepoet.org)
 *
 * Size reduction.
 */

#include "libbb.h"

/* This is a NOFORK applet. Be very careful! */

int rm_main(int argc, char **argv);
int rm_main(int argc, char **argv)
{
	int status = 0;
	int flags = 0;
	unsigned opt;

	opt_complementary = "f-i:i-f";
	opt = getopt32(argv, "fiRr");
	argv += optind;
	if (opt & 1)
		flags |= FILEUTILS_FORCE;
	if (opt & 2)
		flags |= FILEUTILS_INTERACTIVE;
	if (opt & 12)
		flags |= FILEUTILS_RECUR;

	if (*argv != NULL) {
		do {
			const char *base = bb_get_last_path_component(*argv);

			if (DOT_OR_DOTDOT(base)) {
				bb_error_msg("cannot remove '.' or '..'");
			} else if (remove_file(*argv, flags) >= 0) {
				continue;
			}
			status = 1;
		} while (*++argv);
	} else if (!(flags & FILEUTILS_FORCE)) {
		bb_show_usage();
	}

	return status;
}
