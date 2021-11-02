/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this tarball for details.
 */

#include "libbb.h"
#include "unarchive.h"

void data_skip(archive_handle_t *archive_handle)
{
	archive_handle->seek(archive_handle, archive_handle->file_header->size);
}
