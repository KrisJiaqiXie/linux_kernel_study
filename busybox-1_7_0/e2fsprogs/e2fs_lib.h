/* vi: set sw=4 ts=4: */
/*
 * See README for additional information
 *
 * This file can be redistributed under the terms of the GNU Library General
 * Public License
 */

/* Constants and structures */
#include "e2fs_defs.h"

/* Iterate a function on each entry of a directory */
int iterate_on_dir(const char *dir_name,
		int (*func)(const char *, struct dirent *, void *),
		void *private);

/* Get/set a file version on an ext2 file system */
int fgetsetversion(const char *name, unsigned long *get_version, unsigned long set_version);
#define fgetversion(name, version) fgetsetversion(name, version, 0)
#define fsetversion(name, version) fgetsetversion(name, NULL, version)

/* Get/set a file flags on an ext2 file system */
int fgetsetflags(const char *name, unsigned long *get_flags, unsigned long set_flags);
#define fgetflags(name, flags) fgetsetflags(name, flags, 0)
#define fsetflags(name, flags) fgetsetflags(name, NULL, flags)

/* Must be 1 for compatibility with `int long_format'. */
#define PFOPT_LONG  1
/* Print file attributes on an ext2 file system */
void print_flags(FILE *f, unsigned long flags, unsigned options);
