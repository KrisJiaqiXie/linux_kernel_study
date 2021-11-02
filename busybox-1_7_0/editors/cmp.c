/* vi: set sw=4 ts=4: */
/*
 * Mini cmp implementation for busybox
 *
 * Copyright (C) 2000,2001 by Matt Kraai <kraai@alumni.carnegiemellon.edu>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this tarball for details.
 */

/* BB_AUDIT SUSv3 (virtually) compliant -- uses nicer GNU format for -l. */
/* http://www.opengroup.org/onlinepubs/007904975/utilities/cmp.html */

/* Mar 16, 2003      Manuel Novoa III   (mjn3@codepoet.org)
 *
 * Original version majorly reworked for SUSv3 compliance, bug fixes, and
 * size optimizations.  Changes include:
 * 1) Now correctly distinguishes between errors and actual file differences.
 * 2) Proper handling of '-' args.
 * 3) Actual error checking of i/o.
 * 4) Accept SUSv3 -l option.  Note that we use the slightly nicer gnu format
 *    in the '-l' case.
 */

#include "libbb.h"

static FILE *cmp_xfopen_input(const char *filename)
{
	FILE *fp;

	fp = fopen_or_warn_stdin(filename);
	if (fp)
		return fp;
	xfunc_die();	/* We already output an error message. */
}

static const char fmt_eof[] ALIGN1 = "cmp: EOF on %s\n";
static const char fmt_differ[] ALIGN1 = "%s %s differ: char %"OFF_FMT"d, line %d\n";
// This fmt_l_opt uses gnu-isms.  SUSv3 would be "%.0s%.0s%"OFF_FMT"d %o %o\n"
static const char fmt_l_opt[] ALIGN1 = "%.0s%.0s%"OFF_FMT"d %3o %3o\n";

static const char opt_chars[] ALIGN1 = "sl";
#define CMP_OPT_s (1<<0)
#define CMP_OPT_l (1<<1)

int cmp_main(int argc, char **argv);
int cmp_main(int argc, char **argv)
{
	FILE *fp1, *fp2, *outfile = stdout;
	const char *filename1, *filename2 = "-";
	USE_DESKTOP(off_t skip1 = 0, skip2 = 0;)
	off_t char_pos = 0;
	int line_pos = 1; /* Hopefully won't overflow... */
	const char *fmt;
	int c1, c2;
	unsigned opt;
	int retval = 0;

	xfunc_error_retval = 2;	/* 1 is returned if files are different. */

	opt_complementary = "-1"
			USE_DESKTOP(":?4")
			SKIP_DESKTOP(":?2")
			":l--s:s--l";
	opt = getopt32(argv, opt_chars);
	argv += optind;

	filename1 = *argv;
	fp1 = cmp_xfopen_input(filename1);

	if (*++argv) {
		filename2 = *argv;
#if ENABLE_DESKTOP
		if (*++argv) {
			skip1 = XATOOFF(*argv);
			if (*++argv) {
				skip2 = XATOOFF(*argv);
			}
		}
#endif
	}

	fp2 = cmp_xfopen_input(filename2);
	if (fp1 == fp2) {		/* Paranoia check... stdin == stdin? */
		/* Note that we don't bother reading stdin.  Neither does gnu wc.
		 * But perhaps we should, so that other apps down the chain don't
		 * get the input.  Consider 'echo hello | (cmp - - && cat -)'.
		 */
		return 0;
	}

	if (opt & CMP_OPT_l)
		fmt = fmt_l_opt;
	else
		fmt = fmt_differ;

#if ENABLE_DESKTOP
	while (skip1) { getc(fp1); skip1--; }
	while (skip2) { getc(fp2); skip2--; }
#endif
	do {
		c1 = getc(fp1);
		c2 = getc(fp2);
		++char_pos;
		if (c1 != c2) {			/* Remember: a read error may have occurred. */
			retval = 1;		/* But assume the files are different for now. */
			if (c2 == EOF) {
				/* We know that fp1 isn't at EOF or in an error state.  But to
				 * save space below, things are setup to expect an EOF in fp1
				 * if an EOF occurred.  So, swap things around.
				 */
				fp1 = fp2;
				filename1 = filename2;
				c1 = c2;
			}
			if (c1 == EOF) {
				die_if_ferror(fp1, filename1);
				fmt = fmt_eof;	/* Well, no error, so it must really be EOF. */
				outfile = stderr;
				/* There may have been output to stdout (option -l), so
				 * make sure we fflush before writing to stderr. */
				xfflush_stdout();
			}
			if (!(opt & CMP_OPT_s)) {
				if (opt & CMP_OPT_l) {
					line_pos = c1;	/* line_pos is unused in the -l case. */
				}
				fprintf(outfile, fmt, filename1, filename2, char_pos, line_pos, c2);
				if (opt) {	/* This must be -l since not -s. */
					/* If we encountered an EOF,
					 * the while check will catch it. */
					continue;
				}
			}
			break;
		}
		if (c1 == '\n') {
			++line_pos;
		}
	} while (c1 != EOF);

	die_if_ferror(fp1, filename1);
	die_if_ferror(fp2, filename2);

	fflush_stdout_and_exit(retval);
}
