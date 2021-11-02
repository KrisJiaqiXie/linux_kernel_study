/* vi: set sw=4 ts=4: */
/*
 * Which implementation for busybox
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 * Copyright (C) 2006 Gabriel Somlo <somlo at cmu.edu>
 *
 * Licensed under the GPL v2 or later, see the file LICENSE in this tarball.
 *
 * Based on which from debianutils
 */

#include "libbb.h"

int which_main(int argc, char **argv);
int which_main(int argc, char **argv)
{
	int status = EXIT_SUCCESS;
	char *p;

	if (argc <= 1 || argv[1][0] == '-') {
		bb_show_usage();
	}

/* We shouldn't do this. Ever. Not our business.
	if (!getenv("PATH")) {
		putenv((char*)bb_PATH_root_path);
	}
*/

	while (--argc > 0) {
		argv++;
		if (strchr(*argv, '/')) {
			if (execable_file(*argv)) {
				puts(*argv);
				continue;
			}
		} else {
			p = find_execable(*argv);
			if (p) {
				puts(p);
				free(p);
				continue;
			}
		}
		status = EXIT_FAILURE;
	}

	fflush_stdout_and_exit(status);
}
