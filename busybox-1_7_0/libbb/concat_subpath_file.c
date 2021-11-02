/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) (C) 2003  Vladimir Oleynik  <dzo@simtreas.ru>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this tarball for details.
 */

/*
   This function make special for recursive actions with usage
   concat_path_file(path, filename)
   and skipping "." and ".." directory entries
*/

#include "libbb.h"

char *concat_subpath_file(const char *path, const char *f)
{
	if (f && DOT_OR_DOTDOT(f))
		return NULL;
	return concat_path_file(path, f);
}
