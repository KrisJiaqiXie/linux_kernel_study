/* vi: set sw=4 ts=4: */
/*
 * strings implementation for busybox
 *
 * Copyright Tito Ragusa <farmatito@tiscali.it>
 *
 * Licensed under the GPL v2 or later, see the file LICENSE in this tarball.
 */

#include <getopt.h>

#include "libbb.h"

#define WHOLE_FILE		1
#define PRINT_NAME		2
#define PRINT_OFFSET	4
#define SIZE			8

int strings_main(int argc, char **argv);
int strings_main(int argc, char **argv)
{
	int n, c, status = EXIT_SUCCESS;
	unsigned opt;
	unsigned count;
	off_t offset;
	FILE *file = stdin;
	char *string;
	const char *fmt = "%s: ";
	const char *n_arg = "4";

	opt = getopt32(argv, "afon:", &n_arg);
	/* -a is our default behaviour */
	/*argc -= optind;*/
	argv += optind;

	n = xatou_range(n_arg, 1, INT_MAX);
	string = xzalloc(n + 1);
	n--;

	if (!*argv) {
		fmt = "{%s}: ";
		*--argv = (char *)bb_msg_standard_input;
		goto PIPE;
	}

	do {
		file = fopen_or_warn(*argv, "r");
		if (!file) {
			status = EXIT_FAILURE;
			continue;
		}
 PIPE:
		offset = 0;
		count = 0;
		do {
			c = fgetc(file);
			if (isprint(c) || c == '\t') {
				if (count > n) {
					putchar(c);
				} else {
					string[count] = c;
					if (count == n) {
						if (opt & PRINT_NAME) {
							printf(fmt, *argv);
						}
						if (opt & PRINT_OFFSET) {
							printf("%7"OFF_FMT"o ", offset - n);
						}
						fputs(string, stdout);
					}
					count++;
				}
			} else {
				if (count > n) {
					putchar('\n');
				}
				count = 0;
			}
			offset++;
		} while (c != EOF);
		fclose_if_not_stdin(file);
	} while (*++argv);

	if (ENABLE_FEATURE_CLEAN_UP)
		free(string);

	fflush_stdout_and_exit(status);
}
