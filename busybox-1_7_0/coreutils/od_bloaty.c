/* od -- dump files in octal and other formats
   Copyright (C) 92, 1995-2004 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/* Written by Jim Meyering.  */

/* Busyboxed by Denis Vlasenko

Based on od.c from coreutils-5.2.1
Top bloat sources:

00000073 t parse_old_offset
0000007b t get_lcm
00000090 r long_options
00000092 t print_named_ascii
000000bf t print_ascii
00000168 t write_block
00000366 t decode_format_string
00000a71 T od_main

Tested for compat with coreutils 6.3
using this script. Minor differences fixed.

#!/bin/sh
echo STD
time /path/to/coreutils/od \
...params... \
>std
echo Exit code $?
echo BBOX
time ./busybox od \
...params... \
>bbox
echo Exit code $?
diff -u -a std bbox >bbox.diff || { echo Different!; sleep 1; }

*/

#include "libbb.h"
#include <getopt.h>

#define assert(a) ((void)0)

/* Check for 0x7f is a coreutils 6.3 addition */
#define ISPRINT(c) (((c)>=' ') && (c) != 0x7f)

typedef long double longdouble_t;
typedef unsigned long long ulonglong_t;
typedef long long llong;

#if ENABLE_LFS
# define xstrtooff_sfx xstrtoull_sfx
#else
# define xstrtooff_sfx xstrtoul_sfx
#endif

/* The default number of input bytes per output line.  */
#define DEFAULT_BYTES_PER_BLOCK 16

/* The number of decimal digits of precision in a float.  */
#ifndef FLT_DIG
# define FLT_DIG 7
#endif

/* The number of decimal digits of precision in a double.  */
#ifndef DBL_DIG
# define DBL_DIG 15
#endif

/* The number of decimal digits of precision in a long double.  */
#ifndef LDBL_DIG
# define LDBL_DIG DBL_DIG
#endif

enum size_spec {
	NO_SIZE,
	CHAR,
	SHORT,
	INT,
	LONG,
	LONG_LONG,
	FLOAT_SINGLE,
	FLOAT_DOUBLE,
	FLOAT_LONG_DOUBLE,
	N_SIZE_SPECS
};

enum output_format {
	SIGNED_DECIMAL,
	UNSIGNED_DECIMAL,
	OCTAL,
	HEXADECIMAL,
	FLOATING_POINT,
	NAMED_CHARACTER,
	CHARACTER
};

/* Each output format specification (from '-t spec' or from
   old-style options) is represented by one of these structures.  */
struct tspec {
	enum output_format fmt;
	enum size_spec size;
	void (*print_function) (size_t, const char *, const char *);
	char *fmt_string;
	int hexl_mode_trailer;
	int field_width;
};

/* Convert the number of 8-bit bytes of a binary representation to
   the number of characters (digits + sign if the type is signed)
   required to represent the same quantity in the specified base/type.
   For example, a 32-bit (4-byte) quantity may require a field width
   as wide as the following for these types:
   11	unsigned octal
   11	signed decimal
   10	unsigned decimal
   8	unsigned hexadecimal  */

static const uint8_t bytes_to_oct_digits[] ALIGN1 =
{0, 3, 6, 8, 11, 14, 16, 19, 22, 25, 27, 30, 32, 35, 38, 41, 43};

static const uint8_t bytes_to_signed_dec_digits[] ALIGN1 =
{1, 4, 6, 8, 11, 13, 16, 18, 20, 23, 25, 28, 30, 33, 35, 37, 40};

static const uint8_t bytes_to_unsigned_dec_digits[] ALIGN1 =
{0, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25, 27, 29, 32, 34, 37, 39};

static const uint8_t bytes_to_hex_digits[] ALIGN1 =
{0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32};

/* Convert enum size_spec to the size of the named type.  */
static const signed char width_bytes[] ALIGN1 = {
	-1,
	sizeof(char),
	sizeof(short),
	sizeof(int),
	sizeof(long),
	sizeof(ulonglong_t),
	sizeof(float),
	sizeof(double),
	sizeof(longdouble_t)
};
/* Ensure that for each member of 'enum size_spec' there is an
   initializer in the width_bytes array.  */
struct ERR_width_bytes_has_bad_size {
	char ERR_width_bytes_has_bad_size[ARRAY_SIZE(width_bytes) == N_SIZE_SPECS ? 1 : -1];
};

static smallint flag_dump_strings;
/* Non-zero if an old-style 'pseudo-address' was specified.  */
static smallint flag_pseudo_start;
static smallint limit_bytes_to_format;
/* When zero and two or more consecutive blocks are equal, format
   only the first block and output an asterisk alone on the following
   line to indicate that identical blocks have been elided.  */
static smallint verbose;
static smallint ioerror;

static size_t string_min;

/* An array of specs describing how to format each input block.  */
static size_t n_specs;
static struct tspec *spec;

/* Function that accepts an address and an optional following char,
   and prints the address and char to stdout.  */
static void (*format_address)(off_t, char);
/* The difference between the old-style pseudo starting address and
   the number of bytes to skip.  */
static off_t pseudo_offset;
/* The number of input bytes to skip before formatting and writing.  */
static off_t n_bytes_to_skip;
/* When zero, MAX_BYTES_TO_FORMAT and END_OFFSET are ignored, and all
   input is formatted.  */
/* The maximum number of bytes that will be formatted.  */
static off_t max_bytes_to_format;
/* The offset of the first byte after the last byte to be formatted.  */
static off_t end_offset;

/* The number of input bytes formatted per output line.  It must be
   a multiple of the least common multiple of the sizes associated with
   the specified output types.  It should be as large as possible, but
   no larger than 16 -- unless specified with the -w option.  */
static size_t bytes_per_block;

/* Human-readable representation of *file_list (for error messages).
   It differs from *file_list only when *file_list is "-".  */
static char const *input_filename;

/* A NULL-terminated list of the file-arguments from the command line.  */
static char const *const *file_list;

/* Initializer for file_list if no file-arguments
   were specified on the command line.  */
static char const *const default_file_list[] = { "-", NULL };

/* The input stream associated with the current file.  */
static FILE *in_stream;

#define MAX_INTEGRAL_TYPE_SIZE sizeof(ulonglong_t)
static unsigned char integral_type_size[MAX_INTEGRAL_TYPE_SIZE + 1] ALIGN1 = {
	[sizeof(char)] = CHAR,
#if USHRT_MAX != UCHAR_MAX
	[sizeof(short)] = SHORT,
#endif
#if UINT_MAX != USHRT_MAX
	[sizeof(int)] = INT,
#endif
#if ULONG_MAX != UINT_MAX
	[sizeof(long)] = LONG,
#endif
#if ULLONG_MAX != ULONG_MAX
	[sizeof(ulonglong_t)] = LONG_LONG,
#endif
};

#define MAX_FP_TYPE_SIZE sizeof(longdouble_t)
static unsigned char fp_type_size[MAX_FP_TYPE_SIZE + 1] ALIGN1 = {
	/* gcc seems to allow repeated indexes. Last one stays */
	[sizeof(longdouble_t)] = FLOAT_LONG_DOUBLE,
	[sizeof(double)] = FLOAT_DOUBLE,
	[sizeof(float)] = FLOAT_SINGLE
};


static unsigned
gcd(unsigned u, unsigned v)
{
	unsigned t;
	while (v != 0) {
		t = u % v;
		u = v;
		v = t;
	}
	return u;
}

/* Compute the least common multiple of U and V.  */
static unsigned
lcm(unsigned u, unsigned v) {
	unsigned t = gcd(u, v);
	if (t == 0)
		return 0;
	return u * v / t;
}

static void
print_s_char(size_t n_bytes, const char *block, const char *fmt_string)
{
	while (n_bytes--) {
		int tmp = *(signed char *) block;
		printf(fmt_string, tmp);
		block += sizeof(unsigned char);
	}
}

static void
print_char(size_t n_bytes, const char *block, const char *fmt_string)
{
	while (n_bytes--) {
		unsigned tmp = *(unsigned char *) block;
		printf(fmt_string, tmp);
		block += sizeof(unsigned char);
	}
}

static void
print_s_short(size_t n_bytes, const char *block, const char *fmt_string)
{
	n_bytes /= sizeof(signed short);
	while (n_bytes--) {
		int tmp = *(signed short *) block;
		printf(fmt_string, tmp);
		block += sizeof(unsigned short);
	}
}

static void
print_short(size_t n_bytes, const char *block, const char *fmt_string)
{
	n_bytes /= sizeof(unsigned short);
	while (n_bytes--) {
		unsigned tmp = *(unsigned short *) block;
		printf(fmt_string, tmp);
		block += sizeof(unsigned short);
	}
}

static void
print_int(size_t n_bytes, const char *block, const char *fmt_string)
{
	n_bytes /= sizeof(unsigned);
	while (n_bytes--) {
		unsigned tmp = *(unsigned *) block;
		printf(fmt_string, tmp);
		block += sizeof(unsigned);
	}
}

#if UINT_MAX == ULONG_MAX
# define print_long print_int
#else
static void
print_long(size_t n_bytes, const char *block, const char *fmt_string)
{
	n_bytes /= sizeof(unsigned long);
	while (n_bytes--) {
		unsigned long tmp = *(unsigned long *) block;
		printf(fmt_string, tmp);
		block += sizeof(unsigned long);
	}
}
#endif

#if ULONG_MAX == ULLONG_MAX
# define print_long_long print_long
#else
static void
print_long_long(size_t n_bytes, const char *block, const char *fmt_string)
{
	n_bytes /= sizeof(ulonglong_t);
	while (n_bytes--) {
		ulonglong_t tmp = *(ulonglong_t *) block;
		printf(fmt_string, tmp);
		block += sizeof(ulonglong_t);
	}
}
#endif

static void
print_float(size_t n_bytes, const char *block, const char *fmt_string)
{
	n_bytes /= sizeof(float);
	while (n_bytes--) {
		float tmp = *(float *) block;
		printf(fmt_string, tmp);
		block += sizeof(float);
	}
}

static void
print_double(size_t n_bytes, const char *block, const char *fmt_string)
{
	n_bytes /= sizeof(double);
	while (n_bytes--) {
		double tmp = *(double *) block;
		printf(fmt_string, tmp);
		block += sizeof(double);
	}
}

static void
print_long_double(size_t n_bytes, const char *block, const char *fmt_string)
{
	n_bytes /= sizeof(longdouble_t);
	while (n_bytes--) {
		longdouble_t tmp = *(longdouble_t *) block;
		printf(fmt_string, tmp);
		block += sizeof(longdouble_t);
	}
}

/* print_[named]_ascii are optimized for speed.
 * Remember, someday you may want to pump gigabytes thru this thing.
 * Saving a dozen of .text bytes here is counter-productive */

static void
print_named_ascii(size_t n_bytes, const char *block,
		const char *unused_fmt_string ATTRIBUTE_UNUSED)
{
	/* Names for some non-printing characters.  */
	static const char charname[33][3] ALIGN1 = {
		"nul", "soh", "stx", "etx", "eot", "enq", "ack", "bel",
		" bs", " ht", " nl", " vt", " ff", " cr", " so", " si",
		"dle", "dc1", "dc2", "dc3", "dc4", "nak", "syn", "etb",
		"can", " em", "sub", "esc", " fs", " gs", " rs", " us",
		" sp"
	};
	// buf[N] pos:  01234 56789
	char buf[12] = "   x\0 0xx\0";
	// actually "   x\0 xxx\0", but I want to share the string with below.
	// [12] because we take three 32bit stack slots anyway, and
	// gcc is too dumb to initialize with constant stores,
	// it copies initializer from rodata. Oh well.

	while (n_bytes--) {
		unsigned masked_c = *(unsigned char *) block++;

		masked_c &= 0x7f;
		if (masked_c == 0x7f) {
			fputs(" del", stdout);
			continue;
		}
		if (masked_c > ' ') {
			buf[3] = masked_c;
			fputs(buf, stdout);
			continue;
		}
		/* Why? Because printf(" %3.3s") is much slower... */
		buf[6] = charname[masked_c][0];
		buf[7] = charname[masked_c][1];
		buf[8] = charname[masked_c][2];
		fputs(buf+5, stdout);
	}
}

static void
print_ascii(size_t n_bytes, const char *block,
		const char *unused_fmt_string ATTRIBUTE_UNUSED)
{
	// buf[N] pos:  01234 56789
	char buf[12] = "   x\0 0xx\0";

	while (n_bytes--) {
		const char *s;
		unsigned c = *(unsigned char *) block++;

		if (ISPRINT(c)) {
			buf[3] = c;
			fputs(buf, stdout);
			continue;
		}
		switch (c) {
		case '\0':
			s = "  \\0";
			break;
		case '\007':
			s = "  \\a";
			break;
		case '\b':
			s = "  \\b";
			break;
		case '\f':
			s = "  \\f";
			break;
		case '\n':
			s = "  \\n";
			break;
		case '\r':
			s = "  \\r";
			break;
		case '\t':
			s = "  \\t";
			break;
		case '\v':
			s = "  \\v";
			break;
		case '\x7f':
			s = " 177";
			break;
		default: /* c is never larger than 040 */
			buf[7] = (c >> 3) + '0';
			buf[8] = (c & 7) + '0';
			s = buf + 5;
		}
		fputs(s, stdout);
	}
}

/* Given a list of one or more input filenames FILE_LIST, set the global
   file pointer IN_STREAM and the global string INPUT_FILENAME to the
   first one that can be successfully opened. Modify FILE_LIST to
   reference the next filename in the list.  A file name of "-" is
   interpreted as standard input.  If any file open fails, give an error
   message and return nonzero.  */

static void
open_next_file(void)
{
	while (1) {
		input_filename = *file_list;
		if (!input_filename)
			return;
		file_list++;
		in_stream = fopen_or_warn_stdin(input_filename);
		if (in_stream) {
			if (in_stream == stdin)
				input_filename = bb_msg_standard_input;
			break;
		}
		ioerror = 1;
	}

	if (limit_bytes_to_format && !flag_dump_strings)
		setbuf(in_stream, NULL);
}

/* Test whether there have been errors on in_stream, and close it if
   it is not standard input.  Return nonzero if there has been an error
   on in_stream or stdout; return zero otherwise.  This function will
   report more than one error only if both a read and a write error
   have occurred.  IN_ERRNO, if nonzero, is the error number
   corresponding to the most recent action for IN_STREAM.  */

static void
check_and_close(void)
{
	if (in_stream) {
		if (ferror(in_stream))	{
			bb_error_msg("%s: read error", input_filename);
			ioerror = 1;
		}
		fclose_if_not_stdin(in_stream);
		in_stream = NULL;
	}

	if (ferror(stdout)) {
		bb_error_msg("write error");
		ioerror = 1;
	}
}

/* If S points to a single valid modern od format string, put
   a description of that format in *TSPEC, make *NEXT point at the
   character following the just-decoded format (if *NEXT is non-NULL),
   and return zero.  For example, if S were "d4afL"
   *NEXT would be set to "afL" and *TSPEC would be
	{
		fmt = SIGNED_DECIMAL;
		size = INT or LONG; (whichever integral_type_size[4] resolves to)
		print_function = print_int; (assuming size == INT)
		fmt_string = "%011d%c";
	}
   S_ORIG is solely for reporting errors.  It should be the full format
   string argument. */

static void
decode_one_format(const char *s_orig, const char *s, const char **next,
					   struct tspec *tspec)
{
	enum size_spec size_spec;
	unsigned size;
	enum output_format fmt;
	const char *p;
	char *end;
	char *fmt_string = NULL;
	void (*print_function) (size_t, const char *, const char *);
	unsigned c;
	unsigned field_width = 0;
	int pos;

	assert(tspec != NULL);

	switch (*s) {
	case 'd':
	case 'o':
	case 'u':
	case 'x': {
		static const char CSIL[] ALIGN1 = "CSIL";

		c = *s++;
		p = strchr(CSIL, *s);
		if (!p) {
			size = sizeof(int);
			if (isdigit(s[0])) {
				size = bb_strtou(s, &end, 0);
				if (errno == ERANGE
				 || MAX_INTEGRAL_TYPE_SIZE < size
				 || integral_type_size[size] == NO_SIZE
				) {
					bb_error_msg_and_die("invalid type string '%s'; "
						"%u-byte %s type is not supported",
						s_orig, size, "integral");
				}
				s = end;
			}
		} else {
			static const uint8_t CSIL_sizeof[] = {
				sizeof(char),
				sizeof(short),
				sizeof(int),
				sizeof(long),
			};
			size = CSIL_sizeof[p - CSIL];
		}

#define ISPEC_TO_FORMAT(Spec, Min_format, Long_format, Max_format) \
	((Spec) == LONG_LONG ? (Max_format) \
	: ((Spec) == LONG ? (Long_format) : (Min_format)))

#define FMT_BYTES_ALLOCATED 9
		size_spec = integral_type_size[size];

		{
			static const char doux[] ALIGN1 = "doux";
			static const char doux_fmt_letter[][4] = {
				"lld", "llo", "llu", "llx"
			};
			static const enum output_format doux_fmt[] = {
				SIGNED_DECIMAL,
				OCTAL,
				UNSIGNED_DECIMAL,
				HEXADECIMAL,
			};
			static const uint8_t *const doux_bytes_to_XXX[] = {
				bytes_to_signed_dec_digits,
				bytes_to_oct_digits,
				bytes_to_unsigned_dec_digits,
				bytes_to_hex_digits,
			};
			static const char doux_fmtstring[][sizeof(" %%0%u%s")] = {
				" %%%u%s",
				" %%0%u%s",
				" %%%u%s",
				" %%0%u%s",
			};

			pos = strchr(doux, c) - doux;
			fmt = doux_fmt[pos];
			field_width = doux_bytes_to_XXX[pos][size];
			p = doux_fmt_letter[pos] + 2;
			if (size_spec == LONG) p--;
			if (size_spec == LONG_LONG) p -= 2;
			fmt_string = xasprintf(doux_fmtstring[pos], field_width, p);
		}

		switch (size_spec) {
		case CHAR:
			print_function = (fmt == SIGNED_DECIMAL
				    ? print_s_char
				    : print_char);
			break;
		case SHORT:
			print_function = (fmt == SIGNED_DECIMAL
				    ? print_s_short
				    : print_short);
			break;
		case INT:
			print_function = print_int;
			break;
		case LONG:
			print_function = print_long;
			break;
		default: /* case LONG_LONG: */
			print_function = print_long_long;
			break;
		}
		break;
	}

	case 'f': {
		static const char FDL[] ALIGN1 = "FDL";

		fmt = FLOATING_POINT;
		++s;
		p = strchr(FDL, *s);
		if (!p) {
			size = sizeof(double);
			if (isdigit(s[0])) {
				size = bb_strtou(s, &end, 0);
				if (errno == ERANGE || size > MAX_FP_TYPE_SIZE
				 || fp_type_size[size] == NO_SIZE
				) {
					bb_error_msg_and_die("invalid type string '%s'; "
						"%u-byte %s type is not supported",
						s_orig, size, "floating point");
				}
				s = end;
			}
		} else {
			static const uint8_t FDL_sizeof[] = {
				sizeof(float),
				sizeof(double),
				sizeof(longdouble_t),
			};

			size = FDL_sizeof[p - FDL];
		}

		size_spec = fp_type_size[size];

		switch (size_spec) {
		case FLOAT_SINGLE:
			print_function = print_float;
			field_width = FLT_DIG + 8;
			/* Don't use %#e; not all systems support it.  */
			fmt_string = xasprintf(" %%%d.%de", field_width, FLT_DIG);
			break;
		case FLOAT_DOUBLE:
			print_function = print_double;
			field_width = DBL_DIG + 8;
			fmt_string = xasprintf(" %%%d.%de", field_width, DBL_DIG);
			break;
		default: /* case FLOAT_LONG_DOUBLE: */
			print_function = print_long_double;
			field_width = LDBL_DIG + 8;
			fmt_string = xasprintf(" %%%d.%dLe", field_width, LDBL_DIG);
			break;
		}
		break;
	}

	case 'a':
		++s;
		fmt = NAMED_CHARACTER;
		size_spec = CHAR;
		print_function = print_named_ascii;
		field_width = 3;
		break;
	case 'c':
		++s;
		fmt = CHARACTER;
		size_spec = CHAR;
		print_function = print_ascii;
		field_width = 3;
		break;
	default:
		bb_error_msg_and_die("invalid character '%c' "
				"in type string '%s'", *s, s_orig);
	}

	tspec->size = size_spec;
	tspec->fmt = fmt;
	tspec->print_function = print_function;
	tspec->fmt_string = fmt_string;

	tspec->field_width = field_width;
	tspec->hexl_mode_trailer = (*s == 'z');
	if (tspec->hexl_mode_trailer)
		s++;

	if (next != NULL)
		*next = s;
}

/* Decode the modern od format string S.  Append the decoded
   representation to the global array SPEC, reallocating SPEC if
   necessary.  Return zero if S is valid, nonzero otherwise.  */

static void
decode_format_string(const char *s)
{
	const char *s_orig = s;

	while (*s != '\0') {
		struct tspec tspec;
		const char *next;

		decode_one_format(s_orig, s, &next, &tspec);

		assert(s != next);
		s = next;
		n_specs++;
		spec = xrealloc(spec, n_specs * sizeof(*spec));
		memcpy(&spec[n_specs-1], &tspec, sizeof *spec);
	}
}

/* Given a list of one or more input filenames FILE_LIST, set the global
   file pointer IN_STREAM to position N_SKIP in the concatenation of
   those files.  If any file operation fails or if there are fewer than
   N_SKIP bytes in the combined input, give an error message and return
   nonzero.  When possible, use seek rather than read operations to
   advance IN_STREAM.  */

static void
skip(off_t n_skip)
{
	if (n_skip == 0)
		return;

	while (in_stream) { /* !EOF */
		struct stat file_stats;

		/* First try seeking.  For large offsets, this extra work is
		   worthwhile.  If the offset is below some threshold it may be
		   more efficient to move the pointer by reading.  There are two
		   issues when trying to seek:
			- the file must be seekable.
			- before seeking to the specified position, make sure
			  that the new position is in the current file.
			  Try to do that by getting file's size using fstat.
			  But that will work only for regular files.  */

			/* The st_size field is valid only for regular files
			   (and for symbolic links, which cannot occur here).
			   If the number of bytes left to skip is at least
			   as large as the size of the current file, we can
			   decrement n_skip and go on to the next file.  */
		if (fstat(fileno(in_stream), &file_stats) == 0
		 && S_ISREG(file_stats.st_mode) && file_stats.st_size >= 0
		) {
			if (file_stats.st_size < n_skip) {
				n_skip -= file_stats.st_size;
				/* take check&close / open_next route */
			} else {
				if (fseeko(in_stream, n_skip, SEEK_CUR) != 0)
					ioerror = 1;
				return;
			}
		} else {
			/* If it's not a regular file with nonnegative size,
			   position the file pointer by reading.  */
			char buf[BUFSIZ];
			size_t n_bytes_read, n_bytes_to_read = BUFSIZ;

			while (n_skip > 0) {
				if (n_skip < n_bytes_to_read)
					n_bytes_to_read = n_skip;
				n_bytes_read = fread(buf, 1, n_bytes_to_read, in_stream);
				n_skip -= n_bytes_read;
				if (n_bytes_read != n_bytes_to_read)
					break; /* EOF on this file or error */
			}
		}
		if (n_skip == 0)
			return;

		check_and_close();
		open_next_file();
	}

	if (n_skip)
		bb_error_msg_and_die("cannot skip past end of combined input");
}


typedef void FN_format_address(off_t address, char c);

static void
format_address_none(off_t address ATTRIBUTE_UNUSED, char c ATTRIBUTE_UNUSED)
{
}

static char address_fmt[] ALIGN1 = "%0n"OFF_FMT"xc";
/* Corresponds to 'x' above */
#define address_base_char address_fmt[sizeof(address_fmt)-3]
/* Corresponds to 'n' above */
#define address_pad_len_char address_fmt[2]

static void
format_address_std(off_t address, char c)
{
	/* Corresponds to 'c' */
	address_fmt[sizeof(address_fmt)-2] = c;
	printf(address_fmt, address);
}

#if ENABLE_GETOPT_LONG
/* only used with --traditional */
static void
format_address_paren(off_t address, char c)
{
	putchar('(');
	format_address_std(address, ')');
	if (c) putchar(c);
}

static void
format_address_label(off_t address, char c)
{
	format_address_std(address, ' ');
	format_address_paren(address + pseudo_offset, c);
}
#endif

static void
dump_hexl_mode_trailer(size_t n_bytes, const char *block)
{
	fputs("  >", stdout);
	while (n_bytes--) {
		unsigned c = *(unsigned char *) block++;
		c = (ISPRINT(c) ? c : '.');
		putchar(c);
	}
	putchar('<');
}

/* Write N_BYTES bytes from CURR_BLOCK to standard output once for each
   of the N_SPEC format specs.  CURRENT_OFFSET is the byte address of
   CURR_BLOCK in the concatenation of input files, and it is printed
   (optionally) only before the output line associated with the first
   format spec.  When duplicate blocks are being abbreviated, the output
   for a sequence of identical input blocks is the output for the first
   block followed by an asterisk alone on a line.  It is valid to compare
   the blocks PREV_BLOCK and CURR_BLOCK only when N_BYTES == BYTES_PER_BLOCK.
   That condition may be false only for the last input block -- and then
   only when it has not been padded to length BYTES_PER_BLOCK.  */

static void
write_block(off_t current_offset, size_t n_bytes,
		const char *prev_block, const char *curr_block)
{
	static char first = 1;
	static char prev_pair_equal = 0;
	size_t i;

	if (!verbose && !first
	 && n_bytes == bytes_per_block
	 && memcmp(prev_block, curr_block, bytes_per_block) == 0
	) {
		if (prev_pair_equal) {
			/* The two preceding blocks were equal, and the current
			   block is the same as the last one, so print nothing.  */
		} else {
			puts("*");
			prev_pair_equal = 1;
		}
	} else {
		first = 0;
		prev_pair_equal = 0;
		for (i = 0; i < n_specs; i++) {
			if (i == 0)
				format_address(current_offset, '\0');
			else
				printf("%*s", address_pad_len_char - '0', "");
			(*spec[i].print_function) (n_bytes, curr_block, spec[i].fmt_string);
			if (spec[i].hexl_mode_trailer) {
				/* space-pad out to full line width, then dump the trailer */
				int datum_width = width_bytes[spec[i].size];
				int blank_fields = (bytes_per_block - n_bytes) / datum_width;
				int field_width = spec[i].field_width + 1;
				printf("%*s", blank_fields * field_width, "");
				dump_hexl_mode_trailer(n_bytes, curr_block);
			}
			putchar('\n');
		}
	}
}

static void
read_block(size_t n, char *block, size_t *n_bytes_in_buffer)
{
	assert(0 < n && n <= bytes_per_block);

	*n_bytes_in_buffer = 0;

	if (n == 0)
		return;

	while (in_stream != NULL) { /* EOF.  */
		size_t n_needed;
		size_t n_read;

		n_needed = n - *n_bytes_in_buffer;
		n_read = fread(block + *n_bytes_in_buffer, 1, n_needed, in_stream);
		*n_bytes_in_buffer += n_read;
		if (n_read == n_needed)
			break;
		/* error check is done in check_and_close */
		check_and_close();
		open_next_file();
	}
}

/* Return the least common multiple of the sizes associated
   with the format specs.  */

static int
get_lcm(void)
{
	size_t i;
	int l_c_m = 1;

	for (i = 0; i < n_specs; i++)
		l_c_m = lcm(l_c_m, width_bytes[(int) spec[i].size]);
	return l_c_m;
}

#if ENABLE_GETOPT_LONG
/* If S is a valid traditional offset specification with an optional
   leading '+' return nonzero and set *OFFSET to the offset it denotes.  */

static int
parse_old_offset(const char *s, off_t *offset)
{
	static const struct suffix_mult Bb[] = {
		{ "B", 1024 },
		{ "b", 512 },
		{ }
	};
	char *p;
	int radix;

	/* Skip over any leading '+'. */
	if (s[0] == '+') ++s;

	/* Determine the radix we'll use to interpret S.  If there is a '.',
	 * it's decimal, otherwise, if the string begins with '0X'or '0x',
	 * it's hexadecimal, else octal.  */
	p = strchr(s, '.');
	radix = 8;
	if (p) {
		p[0] = '\0'; /* cheating */
		radix = 10;
	} else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		radix = 16;

	*offset = xstrtooff_sfx(s, radix, Bb);
	if (p) p[0] = '.';

	return (*offset >= 0);
}
#endif

/* Read a chunk of size BYTES_PER_BLOCK from the input files, write the
   formatted block to standard output, and repeat until the specified
   maximum number of bytes has been read or until all input has been
   processed.  If the last block read is smaller than BYTES_PER_BLOCK
   and its size is not a multiple of the size associated with a format
   spec, extend the input block with zero bytes until its length is a
   multiple of all format spec sizes.  Write the final block.  Finally,
   write on a line by itself the offset of the byte after the last byte
   read.  Accumulate return values from calls to read_block and
   check_and_close, and if any was nonzero, return nonzero.
   Otherwise, return zero.  */

static void
dump(void)
{
	char *block[2];
	off_t current_offset;
	int idx;
	size_t n_bytes_read;

	block[0] = xmalloc(2*bytes_per_block);
	block[1] = block[0] + bytes_per_block;

	current_offset = n_bytes_to_skip;

	idx = 0;
	if (limit_bytes_to_format) {
		while (1) {
			size_t n_needed;
			if (current_offset >= end_offset) {
				n_bytes_read = 0;
				break;
			}
			n_needed = MIN(end_offset - current_offset,
				(off_t) bytes_per_block);
			read_block(n_needed, block[idx], &n_bytes_read);
			if (n_bytes_read < bytes_per_block)
				break;
			assert(n_bytes_read == bytes_per_block);
			write_block(current_offset, n_bytes_read,
			       block[!idx], block[idx]);
			current_offset += n_bytes_read;
			idx = !idx;
		}
	} else {
		while (1) {
			read_block(bytes_per_block, block[idx], &n_bytes_read);
			if (n_bytes_read < bytes_per_block)
				break;
			assert(n_bytes_read == bytes_per_block);
			write_block(current_offset, n_bytes_read,
			       block[!idx], block[idx]);
			current_offset += n_bytes_read;
			idx = !idx;
		}
	}

	if (n_bytes_read > 0) {
		int l_c_m;
		size_t bytes_to_write;

		l_c_m = get_lcm();

		/* Make bytes_to_write the smallest multiple of l_c_m that
			 is at least as large as n_bytes_read.  */
		bytes_to_write = l_c_m * ((n_bytes_read + l_c_m - 1) / l_c_m);

		memset(block[idx] + n_bytes_read, 0, bytes_to_write - n_bytes_read);
		write_block(current_offset, bytes_to_write,
				   block[!idx], block[idx]);
		current_offset += n_bytes_read;
	}

	format_address(current_offset, '\n');

	if (limit_bytes_to_format && current_offset >= end_offset)
		check_and_close();

	free(block[0]);
}

/* Read a single byte into *C from the concatenation of the input files
   named in the global array FILE_LIST.  On the first call to this
   function, the global variable IN_STREAM is expected to be an open
   stream associated with the input file INPUT_FILENAME.  If IN_STREAM
   is at end-of-file, close it and update the global variables IN_STREAM
   and INPUT_FILENAME so they correspond to the next file in the list.
   Then try to read a byte from the newly opened file.  Repeat if
   necessary until EOF is reached for the last file in FILE_LIST, then
   set *C to EOF and return.  Subsequent calls do likewise.  The return
   value is nonzero if any errors occured, zero otherwise.  */

static void
read_char(int *c)
{
	while (in_stream) { /* !EOF */
		*c = fgetc(in_stream);
		if (*c != EOF)
			return;
		check_and_close();
		open_next_file();
	}
	*c = EOF;
}

/* Read N bytes into BLOCK from the concatenation of the input files
   named in the global array FILE_LIST.  On the first call to this
   function, the global variable IN_STREAM is expected to be an open
   stream associated with the input file INPUT_FILENAME.  If all N
   bytes cannot be read from IN_STREAM, close IN_STREAM and update
   the global variables IN_STREAM and INPUT_FILENAME.  Then try to
   read the remaining bytes from the newly opened file.  Repeat if
   necessary until EOF is reached for the last file in FILE_LIST.
   On subsequent calls, don't modify BLOCK and return zero.  Set
   *N_BYTES_IN_BUFFER to the number of bytes read.  If an error occurs,
   it will be detected through ferror when the stream is about to be
   closed.  If there is an error, give a message but continue reading
   as usual and return nonzero.  Otherwise return zero.  */

/* STRINGS mode.  Find each "string constant" in the input.
   A string constant is a run of at least 'string_min' ASCII
   graphic (or formatting) characters terminated by a null.
   Based on a function written by Richard Stallman for a
   traditional version of od.  Return nonzero if an error
   occurs.  Otherwise, return zero.  */

static void
dump_strings(void)
{
	size_t bufsize = MAX(100, string_min);
	char *buf = xmalloc(bufsize);
	off_t address = n_bytes_to_skip;

	while (1) {
		size_t i;
		int c;

		/* See if the next 'string_min' chars are all printing chars.  */
 tryline:
		if (limit_bytes_to_format && (end_offset - string_min <= address))
			break;
		i = 0;
		while (!limit_bytes_to_format || address < end_offset) {
			if (i == bufsize) {
				bufsize += bufsize/8;
				buf = xrealloc(buf, bufsize);
			}
			read_char(&c);
			if (c < 0) { /* EOF */
				free(buf);
				return;
			}
			address++;
			if (!c)
				break;
			if (!ISPRINT(c))
				goto tryline;	/* It isn't; give up on this string.  */
			buf[i++] = c;		/* String continues; store it all.  */
		}

		if (i < string_min)		/* Too short! */
			goto tryline;

		/* If we get here, the string is all printable and null-terminated,
		 * so print it.  It is all in 'buf' and 'i' is its length.  */
		buf[i] = 0;
		format_address(address - i - 1, ' ');

		for (i = 0; (c = buf[i]); i++) {
			switch (c) {
			case '\007': fputs("\\a", stdout); break;
			case '\b': fputs("\\b", stdout); break;
			case '\f': fputs("\\f", stdout); break;
			case '\n': fputs("\\n", stdout); break;
			case '\r': fputs("\\r", stdout); break;
			case '\t': fputs("\\t", stdout); break;
			case '\v': fputs("\\v", stdout); break;
			default: putc(c, stdout);
			}
		}
		putchar('\n');
	}

	/* We reach this point only if we search through
	   (max_bytes_to_format - string_min) bytes before reaching EOF.  */
	free(buf);

	check_and_close();
}

int od_main(int argc, char **argv);
int od_main(int argc, char **argv)
{
	static const struct suffix_mult bkm[] = {
		{ "b", 512 },
		{ "k", 1024 },
		{ "m", 1024*1024 },
		{ }
	};
	unsigned opt;
	int l_c_m;
	/* The old-style 'pseudo starting address' to be printed in parentheses
	   after any true address.  */
	off_t pseudo_start = 0; // only for gcc
	enum {
		OPT_A = 1 << 0,
		OPT_N = 1 << 1,
		OPT_a = 1 << 2,
		OPT_b = 1 << 3,
		OPT_c = 1 << 4,
		OPT_d = 1 << 5,
		OPT_f = 1 << 6,
		OPT_h = 1 << 7,
		OPT_i = 1 << 8,
		OPT_j = 1 << 9,
		OPT_l = 1 << 10,
		OPT_o = 1 << 11,
		OPT_t = 1 << 12,
		OPT_v = 1 << 13,
		OPT_x = 1 << 14,
		OPT_s = 1 << 15,
		OPT_S = 1 << 16,
		OPT_w = 1 << 17,
		OPT_traditional = (1 << 18) * ENABLE_GETOPT_LONG,
	};
#if ENABLE_GETOPT_LONG
	static const char od_longopts[] ALIGN1 =
		"skip-bytes\0"        Required_argument "j"
		"address-radix\0"     Required_argument "A"
		"read-bytes\0"        Required_argument "N"
		"format\0"            Required_argument "t"
		"output-duplicates\0" No_argument       "v"
		"strings\0"           Optional_argument "S"
		"width\0"             Optional_argument "w"
		"traditional\0"       No_argument       "\xff"
		;
#endif
	char *str_A, *str_N, *str_j, *str_S;
	char *str_w = NULL;
	llist_t *lst_t = NULL;

	spec = NULL;
	format_address = format_address_std;
	address_base_char = 'o';
	address_pad_len_char = '7';
	/* flag_dump_strings = 0; - already is */

	/* Parse command line */
	opt_complementary = "t::"; // list
#if ENABLE_GETOPT_LONG
	applet_long_options = od_longopts;
#endif
	opt = getopt32(argv, "A:N:abcdfhij:lot:vxsS:"
		"w::", // -w with optional param
		// -S was -s and also had optional parameter
		// but in coreutils 6.3 it was renamed and now has
		// _mandatory_ parameter
		&str_A, &str_N, &str_j, &lst_t, &str_S, &str_w);
	argc -= optind;
	argv += optind;
	if (opt & OPT_A) {
		static const char doxn[] ALIGN1 = "doxn";
		static const char doxn_address_base_char[] ALIGN1 = {
			'u', 'o', 'x', /* '?' fourth one is not important */
		};
		static const uint8_t doxn_address_pad_len_char[] ALIGN1 = {
			'7', '7', '6', /* '?' */
		};
		char *p;
		int pos;
		p = strchr(doxn, str_A[0]);
		if (!p)
			bb_error_msg_and_die("bad output address radix "
				"'%c' (must be [doxn])", str_A[0]);
		pos = p - doxn;
		if (pos == 3) format_address = format_address_none;
		address_base_char = doxn_address_base_char[pos];
		address_pad_len_char = doxn_address_pad_len_char[pos];
	}
	if (opt & OPT_N) {
		limit_bytes_to_format = 1;
		max_bytes_to_format = xstrtooff_sfx(str_N, 0, bkm);
	}
	if (opt & OPT_a) decode_format_string("a");
	if (opt & OPT_b) decode_format_string("oC");
	if (opt & OPT_c) decode_format_string("c");
	if (opt & OPT_d) decode_format_string("u2");
	if (opt & OPT_f) decode_format_string("fF");
	if (opt & OPT_h) decode_format_string("x2");
	if (opt & OPT_i) decode_format_string("d2");
	if (opt & OPT_j) n_bytes_to_skip = xstrtooff_sfx(str_j, 0, bkm);
	if (opt & OPT_l) decode_format_string("d4");
	if (opt & OPT_o) decode_format_string("o2");
	//if (opt & OPT_t)...
	while (lst_t) {
		decode_format_string(lst_t->data);
		lst_t = lst_t->link;
	}
	if (opt & OPT_v) verbose = 1;
	if (opt & OPT_x) decode_format_string("x2");
	if (opt & OPT_s) decode_format_string("d2");
	if (opt & OPT_S) {
		string_min = 3;
		string_min = xstrtou_sfx(str_S, 0, bkm);
		flag_dump_strings = 1;
	}
	//if (opt & OPT_w)...
	//if (opt & OPT_traditional)...

	if (flag_dump_strings && n_specs > 0)
		bb_error_msg_and_die("no type may be specified when dumping strings");

	/* If the --traditional option is used, there may be from
	 * 0 to 3 remaining command line arguments;  handle each case
	 * separately.
	 * od [file] [[+]offset[.][b] [[+]label[.][b]]]
	 * The offset and pseudo_start have the same syntax.
	 *
	 * FIXME: POSIX 1003.1-2001 with XSI requires support for the
	 * traditional syntax even if --traditional is not given.  */

#if ENABLE_GETOPT_LONG
	if (opt & OPT_traditional) {
		off_t o1, o2;

		if (argc == 1) {
			if (parse_old_offset(argv[0], &o1)) {
				n_bytes_to_skip = o1;
				--argc;
				++argv;
			}
		} else if (argc == 2) {
			if (parse_old_offset(argv[0], &o1)
			 && parse_old_offset(argv[1], &o2)
			) {
				n_bytes_to_skip = o1;
				flag_pseudo_start = 1;
				pseudo_start = o2;
				argv += 2;
				argc -= 2;
			} else if (parse_old_offset(argv[1], &o2)) {
				n_bytes_to_skip = o2;
				--argc;
				argv[1] = argv[0];
				++argv;
			} else {
				bb_error_msg_and_die("invalid second operand "
					"in compatibility mode '%s'", argv[1]);
			}
		} else if (argc == 3) {
			if (parse_old_offset(argv[1], &o1)
			 && parse_old_offset(argv[2], &o2)
			) {
				n_bytes_to_skip = o1;
				flag_pseudo_start = 1;
				pseudo_start = o2;
				argv[2] = argv[0];
				argv += 2;
				argc -= 2;
			} else {
				bb_error_msg_and_die("in compatibility mode "
					"the last two arguments must be offsets");
			}
		} else if (argc > 3)	{
			bb_error_msg_and_die("compatibility mode supports "
				"at most three arguments");
		}

		if (flag_pseudo_start) {
			if (format_address == format_address_none) {
				address_base_char = 'o';
				address_pad_len_char = '7';
				format_address = format_address_paren;
			} else
				format_address = format_address_label;
		}
	}
#endif

	if (limit_bytes_to_format) {
		end_offset = n_bytes_to_skip + max_bytes_to_format;
		if (end_offset < n_bytes_to_skip)
			bb_error_msg_and_die("skip-bytes + read-bytes is too large");
	}

	if (n_specs == 0) {
		decode_format_string("o2");
		n_specs = 1;
	}

	/* If no files were listed on the command line,
	   set the global pointer FILE_LIST so that it
	   references the null-terminated list of one name: "-".  */
	file_list = default_file_list;
	if (argc > 0) {
		/* Set the global pointer FILE_LIST so that it
		   references the first file-argument on the command-line.  */
		file_list = (char const *const *) argv;
	}

	/* open the first input file */
	open_next_file();
	/* skip over any unwanted header bytes */
	skip(n_bytes_to_skip);
	if (!in_stream)
		return 1;

	pseudo_offset = (flag_pseudo_start ? pseudo_start - n_bytes_to_skip : 0);

	/* Compute output block length.  */
	l_c_m = get_lcm();

	if (opt & OPT_w) { /* -w: width */
		bytes_per_block = 32;
		if (str_w)
			bytes_per_block = xatou(str_w);
		if (!bytes_per_block || bytes_per_block % l_c_m != 0) {
			bb_error_msg("warning: invalid width %zu; using %d instead",
					bytes_per_block, l_c_m);
			bytes_per_block = l_c_m;
		}
	} else {
		bytes_per_block = l_c_m;
		if (l_c_m < DEFAULT_BYTES_PER_BLOCK)
			bytes_per_block *= DEFAULT_BYTES_PER_BLOCK / l_c_m;
	}

#ifdef DEBUG
	for (i = 0; i < n_specs; i++) {
		printf("%d: fmt=\"%s\" width=%d\n",
			i, spec[i].fmt_string, width_bytes[spec[i].size]);
	}
#endif

	if (flag_dump_strings)
		dump_strings();
	else
		dump();

	if (fclose(stdin) == EOF)
		bb_perror_msg_and_die(bb_msg_standard_input);

	return ioerror;
}
