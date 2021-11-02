/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) Manuel Novoa III <mjn3@codepoet.org>
 * and Vladimir Oleynik <dzo@simtreas.ru>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this tarball for details.
 */

#include "libbb.h"

#define WANT_HEX_ESCAPES 1

/* Usual "this only works for ascii compatible encodings" disclaimer. */
#undef _tolower
#define _tolower(X) ((X)|((char) 0x20))

char bb_process_escape_sequence(const char **ptr)
{
	static const char charmap[] ALIGN1 = {
		'a',  'b',  'f',  'n',  'r',  't',  'v',  '\\', 0,
		'\a', '\b', '\f', '\n', '\r', '\t', '\v', '\\', '\\' };

	const char *p;
	const char *q;
	unsigned int num_digits;
	unsigned int r;
	unsigned int n;
	unsigned int d;
	unsigned int base;

	num_digits = n = 0;
	base = 8;
	q = *ptr;

#ifdef WANT_HEX_ESCAPES
	if (*q == 'x') {
		++q;
		base = 16;
		++num_digits;
	}
#endif

	do {
		d = (unsigned char)(*q) - '0';
#ifdef WANT_HEX_ESCAPES
		if (d >= 10) {
			d = (unsigned char)(_tolower(*q)) - 'a' + 10;
		}
#endif

		if (d >= base) {
#ifdef WANT_HEX_ESCAPES
			if ((base == 16) && (!--num_digits)) {
/*				return '\\'; */
				--q;
			}
#endif
			break;
		}

		r = n * base + d;
		if (r > UCHAR_MAX) {
			break;
		}

		n = r;
		++q;
	} while (++num_digits < 3);

	if (num_digits == 0) {	/* mnemonic escape sequence? */
		p = charmap;
		do {
			if (*p == *q) {
				q++;
				break;
			}
		} while (*++p);
		n = *(p + (sizeof(charmap)/2));
	}

	*ptr = q;

	return (char) n;
}
