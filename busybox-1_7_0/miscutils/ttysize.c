/* vi: set sw=4 ts=4: */
/*
 * Replacement for "stty size", which is awkward for shell script use.
 * - Allows to request width, height, or both, in any order.
 * - Does not complain on error, but returns width 80, height 24.
 * - Size: less than 200 bytes
 *
 * Copyright (C) 2007 by Denys Vlasenko <vda.linux@googlemail.com>
 *
 * Licensed under the GPL v2, see the file LICENSE in this tarball.
 */
#include "libbb.h"

int ttysize_main(int argc, char **argv);
int ttysize_main(int argc, char **argv)
{
	unsigned w,h;
	struct winsize wsz;

	w = 80;
	h = 24;
	if (!ioctl(0, TIOCGWINSZ, &wsz)) {
		w = wsz.ws_col;
		h = wsz.ws_row;
	}

	if (argc == 1) {
		printf("%u %u", w, h);
	} else {
		const char *fmt, *arg;

		fmt = "%u %u" + 3; /* "%u" */
		while ((arg = *++argv) != NULL) {
			char c = arg[0];
			if (c == 'w')
				printf(fmt, w);
			if (c == 'h')
				printf(fmt, h);
			fmt = "%u %u" + 2; /* " %u" */
		}
	}
	putchar('\n');
	return 0;
}
