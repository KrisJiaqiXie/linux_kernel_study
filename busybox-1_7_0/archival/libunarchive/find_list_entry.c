/* vi: set sw=4 ts=4: */
/*
 * Copyright (C) 2002 by Glenn McGrath
 *
 * Licensed under GPLv2 or later, see file LICENSE in this tarball for details.
 */

#include <fnmatch.h>
#include "libbb.h"
#include "unarchive.h"

/* Find a string in a shell pattern list */
const llist_t *find_list_entry(const llist_t *list, const char *filename)
{
	while (list) {
		if (fnmatch(list->data, filename, 0) == 0) {
			return list;
		}
		list = list->link;
	}
	return NULL;
}

/* Same, but compares only path components present in pattern
 * (extra trailing path components in filename are assumed to match)
 */
const llist_t *find_list_entry2(const llist_t *list, const char *filename)
{
	char buf[PATH_MAX];
	int pattern_slash_cnt;
	const char *c;
	char *d;

	while (list) {
		c = list->data;
		pattern_slash_cnt = 0;
		while (*c)
			if (*c++ == '/') pattern_slash_cnt++;
		c = filename;
		d = buf;
		/* paranoia is better that buffer overflows */
		while (*c && d != buf + sizeof(buf)-1) {
			if (*c == '/' && --pattern_slash_cnt < 0)
				break;
			*d++ = *c++;
		}
		*d = '\0';
		if (fnmatch(list->data, buf, 0) == 0) {
			return list;
		}
		list = list->link;
	}
	return NULL;
}
