/* vi: set sw=4 ts=4: */
/*
 * Mini mv implementation for busybox
 *
 * Copyright (C) 2000 by Matt Kraai <kraai@alumni.carnegiemellon.edu>
 * SELinux support by Yuichi Nakamura <ynakam@hitachisoft.jp>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this tarball for details.
 */

/* Mar 16, 2003      Manuel Novoa III   (mjn3@codepoet.org)
 *
 * Size reduction and improved error checking.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <getopt.h> /* struct option */
#include "libbb.h"
#include "libcoreutils/coreutils.h"

#if ENABLE_FEATURE_MV_LONG_OPTIONS
static const char mv_longopts[] ALIGN1 =
	"interactive\0" No_argument "i"
	"force\0"       No_argument "f"
	;
#endif

#define OPT_FILEUTILS_FORCE       1
#define OPT_FILEUTILS_INTERACTIVE 2

static const char fmt[] ALIGN1 =
	"cannot overwrite %sdirectory with %sdirectory";

int mv_main(int argc, char **argv);
int mv_main(int argc, char **argv)
{
	struct stat dest_stat;
	const char *last;
	const char *dest;
	unsigned long flags;
	int dest_exists;
	int status = 0;
	int copy_flag = 0;

#if ENABLE_FEATURE_MV_LONG_OPTIONS
	applet_long_options = mv_longopts;
#endif
	opt_complementary = "f-i:i-f";
	flags = getopt32(argv, "fi");
	if (optind + 2 > argc) {
		bb_show_usage();
	}

	last = argv[argc - 1];
	argv += optind;

	if (optind + 2 == argc) {
		dest_exists = cp_mv_stat(last, &dest_stat);
		if (dest_exists < 0) {
			return 1;
		}

		if (!(dest_exists & 2)) {
			dest = last;
			goto DO_MOVE;
		}
	}

	do {
		dest = concat_path_file(last, bb_get_last_path_component(*argv));
		dest_exists = cp_mv_stat(dest, &dest_stat);
		if (dest_exists < 0) {
			goto RET_1;
		}

DO_MOVE:

		if (dest_exists && !(flags & OPT_FILEUTILS_FORCE) &&
			((access(dest, W_OK) < 0 && isatty(0)) ||
			(flags & OPT_FILEUTILS_INTERACTIVE))) {
			if (fprintf(stderr, "mv: overwrite '%s'? ", dest) < 0) {
				goto RET_1;	/* Ouch! fprintf failed! */
			}
			if (!bb_ask_confirmation()) {
				goto RET_0;
			}
		}
		if (rename(*argv, dest) < 0) {
			struct stat source_stat;
			int source_exists;

			if (errno != EXDEV ||
				(source_exists = cp_mv_stat(*argv, &source_stat)) < 1) {
				bb_perror_msg("cannot rename '%s'", *argv);
			} else {
				if (dest_exists) {
					if (dest_exists == 3) {
						if (source_exists != 3) {
							bb_error_msg(fmt, "", "non-");
							goto RET_1;
						}
					} else {
						if (source_exists == 3) {
							bb_error_msg(fmt, "non-", "");
							goto RET_1;
						}
					}
					if (unlink(dest) < 0) {
						bb_perror_msg("cannot remove '%s'", dest);
						goto RET_1;
					}
				}
				copy_flag = FILEUTILS_RECUR | FILEUTILS_PRESERVE_STATUS;
#if ENABLE_SELINUX
				copy_flag |= FILEUTILS_PRESERVE_SECURITY_CONTEXT;
#endif
				if ((copy_file(*argv, dest, copy_flag) >= 0) &&
					(remove_file(*argv, FILEUTILS_RECUR | FILEUTILS_FORCE) >= 0)) {
					goto RET_0;
				}
			}
RET_1:
			status = 1;
		}
RET_0:
		if (dest != last) {
			free((void *) dest);
		}
	} while (*++argv != last);

	return status;
}
