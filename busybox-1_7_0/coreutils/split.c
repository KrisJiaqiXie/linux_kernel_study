/* vi: set sw=4 ts=4: */
/*
 * split - split a file into pieces
 * Copyright (c) 2007 Bernhard Fischer
 *
 * Licensed under GPLv2 or later, see file LICENSE in this tarball for details.
 */
/* BB_AUDIT: SUSv3 compliant
 * SUSv3 requirements:
 * http://www.opengroup.org/onlinepubs/009695399/utilities/split.html
 */
#include "libbb.h"

static const struct suffix_mult split_suffices[] = {
#if ENABLE_FEATURE_SPLIT_FANCY
	{ "b", 512 },
#endif
	{ "k", 1024 },
	{ "m", 1024*1024 },
#if ENABLE_FEATURE_SPLIT_FANCY
	{ "g", 1024*1024*1024 },
#endif
	{ }
};

/* Increment the suffix part of the filename.
 * Returns NULL if we are out of filenames.
 */
static char *next_file(char *old, unsigned suffix_len)
{
	size_t end = strlen(old);
	unsigned i = 1;
	char *curr;

	do {
		curr = old + end - i;
		if (*curr < 'z') {
			*curr += 1;
			break;
		}
		i++;
		if (i > suffix_len) {
			return NULL;
		}
		*curr = 'a';
	} while (1);

	return old;
}

#define read_buffer bb_common_bufsiz1
enum { READ_BUFFER_SIZE = COMMON_BUFSIZE - 1 };

#define SPLIT_OPT_l (1<<0)
#define SPLIT_OPT_b (1<<1)
#define SPLIT_OPT_a (1<<2)

int split_main(int argc, char **argv);
int split_main(int argc, char **argv)
{
	unsigned suffix_len = 2;
	char *pfx;
	char *count_p;
	const char *sfx;
	off_t cnt = 1000;
	off_t remaining = 0;
	unsigned opt;
	ssize_t bytes_read, to_write;
	char *src;

	opt_complementary = "?2";
	opt = getopt32(argv, "l:b:a:", &count_p, &count_p, &sfx);

	if (opt & SPLIT_OPT_l)
		cnt = xatoul(count_p);
	if (opt & SPLIT_OPT_b)
		cnt = xatoul_sfx(count_p, split_suffices);
	if (opt & SPLIT_OPT_a)
		suffix_len = xatou(sfx);
	sfx = "x";

	argv += optind;
	if (argv[0]) {
		if (argv[1])
			sfx = argv[1];
		xmove_fd(xopen(argv[0], O_RDONLY), 0);
	} else {
		argv[0] = (char *) bb_msg_standard_input;
	}

	if (NAME_MAX < strlen(sfx) + suffix_len)
		bb_error_msg_and_die("suffix too long");

	{
		char *char_p = xzalloc(suffix_len + 1);
		memset(char_p, 'a', suffix_len);
		pfx = xasprintf("%s%s", sfx, char_p);
		if (ENABLE_FEATURE_CLEAN_UP)
			free(char_p);
	}

	while (1) {
		bytes_read = safe_read(0, read_buffer, READ_BUFFER_SIZE);
		if (!bytes_read)
			break;
		if (bytes_read < 0)
			bb_perror_msg_and_die("%s", argv[0]);
		src = read_buffer;
		do {
			if (!remaining) {
				if (!pfx)
					bb_error_msg_and_die("suffixes exhausted");
				xmove_fd(xopen(pfx, O_WRONLY | O_CREAT | O_TRUNC), 1);
				pfx = next_file(pfx, suffix_len);
				remaining = cnt;
			}

			if (opt & SPLIT_OPT_b) {
				/* split by bytes */
				to_write = (bytes_read < remaining) ? bytes_read : remaining;
				remaining -= to_write;
			} else {
				/* split by lines */
				/* can be sped up by using _memrchr_
				 * and writing many lines at once... */
				char *end = memchr(src, '\n', bytes_read);
				if (end) {
					--remaining;
					to_write = end - src + 1;
				} else {
					to_write = bytes_read;
				}
			}

			xwrite(1, src, to_write);
			bytes_read -= to_write;
			src += to_write;
		} while (bytes_read);
	}
	return 0;
}
