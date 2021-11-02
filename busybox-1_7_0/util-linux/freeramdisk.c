/* vi: set sw=4 ts=4: */
/*
 * freeramdisk and fdflush implementations for busybox
 *
 * Copyright (C) 2000 and written by Emanuele Caratti <wiz@iol.it>
 * Adjusted a bit by Erik Andersen <andersen@codepoet.org>
 * Unified with fdflush by Tito Ragusa <farmatito@tiscali.it>
 *
 * Licensed under GPLv2, see file LICENSE in this tarball for details.
 */

#include "libbb.h"

/* From <linux/fd.h> */
#define FDFLUSH  _IO(2,0x4b)

int freeramdisk_main(int argc, char **argv);
int freeramdisk_main(int argc, char **argv)
{
	int fd;

	if (argc != 2) bb_show_usage();

	fd = xopen(argv[1], O_RDWR);

	// Act like freeramdisk, fdflush, or both depending on configuration.
	ioctl_or_perror_and_die(fd, (ENABLE_FREERAMDISK && applet_name[1]=='r')
			|| !ENABLE_FDFLUSH ? BLKFLSBUF : FDFLUSH, NULL, "%s", argv[1]);

	if (ENABLE_FEATURE_CLEAN_UP) close(fd);

	return EXIT_SUCCESS;
}
