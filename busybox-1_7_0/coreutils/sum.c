/* vi: set sw=4 ts=4: */
/*
 * sum -- checksum and count the blocks in a file
 *     Like BSD sum or SysV sum -r, except like SysV sum if -s option is given.
 *
 * Copyright (C) 86, 89, 91, 1995-2002, 2004 Free Software Foundation, Inc.
 * Copyright (C) 2005 by Erik Andersen <andersen@codepoet.org>
 * Copyright (C) 2005 by Mike Frysinger <vapier@gentoo.org>
 *
 * Written by Kayvan Aghaiepour and David MacKenzie
 * Taken from coreutils and turned into a busybox applet by Mike Frysinger
 *
 * Licensed under the GPL v2 or later, see the file LICENSE in this tarball.
 */

#include "libbb.h"

enum { SUM_BSD, PRINT_NAME, SUM_SYSV };

/* BSD: calculate and print the rotated checksum and the size in 1K blocks
   The checksum varies depending on sizeof (int). */
/* SYSV: calculate and print the checksum and the size in 512-byte blocks */
/* Return 1 if successful.  */
static unsigned sum_file(const char *file, const unsigned type)
{
#define buf bb_common_bufsiz1
	uintmax_t total_bytes = 0;
	int fd = 0, r;

	/* The sum of all the input bytes, modulo (UINT_MAX + 1).  */
	unsigned s = 0;

	if (NOT_LONE_DASH(file)) {
		fd = open(file, O_RDONLY);
		if (fd == -1)
			goto ret_bad;
	}

	while (1) {
		size_t bytes_read = safe_read(fd, buf, BUFSIZ);

		if ((ssize_t)bytes_read <= 0) {
			r = (fd && close(fd) != 0);
			if (!bytes_read && !r)
				/* no error */
				break;
 ret_bad:
			bb_perror_msg(file);
			return 0;
		}

		total_bytes += bytes_read;
		if (type >= SUM_SYSV) {
			do s += buf[--bytes_read]; while (bytes_read);
		} else {
			r = 0;
			do {
				s = (s >> 1) + ((s & 1) << 15);
				s += buf[r++];
				s &= 0xffff; /* Keep it within bounds. */
			} while (--bytes_read);
		}
	}

	if (type < PRINT_NAME)
		file = "";
	if (type >= SUM_SYSV) {
		r = (s & 0xffff) + ((s & 0xffffffff) >> 16);
		s = (r & 0xffff) + (r >> 16);
		printf("%d %ju %s\n", s, (total_bytes+511)/512, file);
	} else
		printf("%05d %5ju %s\n", s, (total_bytes+1023)/1024, file);
	return 1;
#undef buf
}

int sum_main(int argc, char **argv);
int sum_main(int argc, char **argv)
{
	unsigned n;
	unsigned type = SUM_BSD;

	n = getopt32(argv, "sr");
	if (n & 1) type = SUM_SYSV;
	/* give the bsd priority over sysv func */
	if (n & 2) type = SUM_BSD;

	if (argc == optind) {
		/* Do not print the name */
		n = sum_file("-", type);
	} else {
		/* Need to print the name if either
		   - more than one file given
		   - doing sysv */
		type += argc - 1 > optind || type == SUM_SYSV;
		for (n = 1; optind < argc; optind++)
			n &= sum_file(argv[optind], type);
	}
	return !n;
}
