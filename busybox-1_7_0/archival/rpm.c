/* vi: set sw=4 ts=4: */
/*
 * Mini rpm applet for busybox
 *
 * Copyright (C) 2001,2002 by Laurence Anderson
 *
 * Licensed under GPLv2 or later, see file LICENSE in this tarball for details.
 */

#include "libbb.h"
#include "unarchive.h"

#define RPM_HEADER_MAGIC        "\216\255\350"
#define RPM_CHAR_TYPE           1
#define RPM_INT8_TYPE           2
#define RPM_INT16_TYPE          3
#define RPM_INT32_TYPE          4
/* #define RPM_INT64_TYPE       5   ---- These aren't supported (yet) */
#define RPM_STRING_TYPE         6
#define RPM_BIN_TYPE            7
#define RPM_STRING_ARRAY_TYPE   8
#define RPM_I18NSTRING_TYPE     9

#define TAG_NAME                1000
#define TAG_VERSION             1001
#define TAG_RELEASE             1002
#define TAG_SUMMARY             1004
#define TAG_DESCRIPTION         1005
#define TAG_BUILDTIME           1006
#define TAG_BUILDHOST           1007
#define TAG_SIZE                1009
#define TAG_VENDOR              1011
#define TAG_LICENSE             1014
#define TAG_PACKAGER            1015
#define TAG_GROUP               1016
#define TAG_URL                 1020
#define TAG_PREIN               1023
#define TAG_POSTIN              1024
#define TAG_FILEFLAGS           1037
#define TAG_FILEUSERNAME        1039
#define TAG_FILEGROUPNAME       1040
#define TAG_SOURCERPM           1044
#define TAG_PREINPROG           1085
#define TAG_POSTINPROG          1086
#define TAG_PREFIXS             1098
#define TAG_DIRINDEXES          1116
#define TAG_BASENAMES           1117
#define TAG_DIRNAMES            1118
#define RPMFILE_CONFIG          (1 << 0)
#define RPMFILE_DOC             (1 << 1)

enum rpm_functions_e {
	rpm_query = 1,
	rpm_install = 2,
	rpm_query_info = 4,
	rpm_query_package = 8,
	rpm_query_list = 16,
	rpm_query_list_doc = 32,
	rpm_query_list_config = 64
};

typedef struct {
	uint32_t tag; /* 4 byte tag */
	uint32_t type; /* 4 byte type */
	uint32_t offset; /* 4 byte offset */
	uint32_t count; /* 4 byte count */
} rpm_index;

static void *map;
static rpm_index **mytags;
static int tagcount;

static void extract_cpio_gz(int fd);
static rpm_index **rpm_gettags(int fd, int *num_tags);
static int bsearch_rpmtag(const void *key, const void *item);
static char *rpm_getstr(int tag, int itemindex);
static int rpm_getint(int tag, int itemindex);
static int rpm_getcount(int tag);
static void fileaction_dobackup(char *filename, int fileref);
static void fileaction_setowngrp(char *filename, int fileref);
static void loop_through_files(int filetag, void (*fileaction)(char *filename, int fileref));

int rpm_main(int argc, char **argv);
int rpm_main(int argc, char **argv)
{
	int opt = 0, func = 0, rpm_fd, offset;
	const int pagesize = getpagesize();

	while ((opt = getopt(argc, argv, "iqpldc")) != -1) {
		switch (opt) {
		case 'i': /* First arg: Install mode, with q: Information */
			if (!func) func = rpm_install;
			else func |= rpm_query_info;
			break;
		case 'q': /* First arg: Query mode */
			if (func) bb_show_usage();
			func = rpm_query;
			break;
		case 'p': /* Query a package */
			func |= rpm_query_package;
			break;
		case 'l': /* List files in a package */
			func |= rpm_query_list;
			break;
		case 'd': /* List doc files in a package (implies list) */
			func |= rpm_query_list;
			func |= rpm_query_list_doc;
			break;
		case 'c': /* List config files in a package (implies list) */
			func |= rpm_query_list;
			func |= rpm_query_list_config;
			break;
		default:
			bb_show_usage();
		}
	}
	argv += optind;
	argc -= optind;
	if (!argc) bb_show_usage();

	while (*argv) {
		rpm_fd = xopen(*argv++, O_RDONLY);
		mytags = rpm_gettags(rpm_fd, &tagcount);
		if (!mytags)
			bb_error_msg_and_die("error reading rpm header");
		offset = xlseek(rpm_fd, 0, SEEK_CUR);
		/* Mimimum is one page */
		map = mmap(0, offset > pagesize ? (offset + offset % pagesize) : pagesize, PROT_READ, MAP_PRIVATE, rpm_fd, 0);

		if (func & rpm_install) {
			/* Backup any config files */
			loop_through_files(TAG_BASENAMES, fileaction_dobackup);
			/* Extact the archive */
			extract_cpio_gz(rpm_fd);
			/* Set the correct file uid/gid's */
			loop_through_files(TAG_BASENAMES, fileaction_setowngrp);
		}
		else if ((func & (rpm_query|rpm_query_package)) == (rpm_query|rpm_query_package)) {
			if (!(func & (rpm_query_info|rpm_query_list))) {
				/* If just a straight query, just give package name */
				printf("%s-%s-%s\n", rpm_getstr(TAG_NAME, 0), rpm_getstr(TAG_VERSION, 0), rpm_getstr(TAG_RELEASE, 0));
			}
			if (func & rpm_query_info) {
				/* Do the nice printout */
				time_t bdate_time;
				struct tm *bdate;
				char bdatestring[50];
				printf("Name        : %-29sRelocations: %s\n", rpm_getstr(TAG_NAME, 0), rpm_getstr(TAG_PREFIXS, 0) ? rpm_getstr(TAG_PREFIXS, 0) : "(not relocateable)");
				printf("Version     : %-34sVendor: %s\n", rpm_getstr(TAG_VERSION, 0), rpm_getstr(TAG_VENDOR, 0) ? rpm_getstr(TAG_VENDOR, 0) : "(none)");
				bdate_time = rpm_getint(TAG_BUILDTIME, 0);
				bdate = localtime((time_t *) &bdate_time);
				strftime(bdatestring, 50, "%a %d %b %Y %T %Z", bdate);
				printf("Release     : %-30sBuild Date: %s\n", rpm_getstr(TAG_RELEASE, 0), bdatestring);
				printf("Install date: %-30sBuild Host: %s\n", "(not installed)", rpm_getstr(TAG_BUILDHOST, 0));
				printf("Group       : %-30sSource RPM: %s\n", rpm_getstr(TAG_GROUP, 0), rpm_getstr(TAG_SOURCERPM, 0));
				printf("Size        : %-33dLicense: %s\n", rpm_getint(TAG_SIZE, 0), rpm_getstr(TAG_LICENSE, 0));
				printf("URL         : %s\n", rpm_getstr(TAG_URL, 0));
				printf("Summary     : %s\n", rpm_getstr(TAG_SUMMARY, 0));
				printf("Description :\n%s\n", rpm_getstr(TAG_DESCRIPTION, 0));
			}
			if (func & rpm_query_list) {
				int count, it, flags;
				count = rpm_getcount(TAG_BASENAMES);
				for (it = 0; it < count; it++) {
					flags = rpm_getint(TAG_FILEFLAGS, it);
					switch (func & (rpm_query_list_doc|rpm_query_list_config)) {
					case rpm_query_list_doc:
						if (!(flags & RPMFILE_DOC)) continue;
						break;
					case rpm_query_list_config:
						if (!(flags & RPMFILE_CONFIG)) continue;
						break;
					case rpm_query_list_doc|rpm_query_list_config:
						if (!(flags & (RPMFILE_CONFIG|RPMFILE_DOC))) continue;
						break;
					}
					printf("%s%s\n",
						rpm_getstr(TAG_DIRNAMES, rpm_getint(TAG_DIRINDEXES, it)),
						rpm_getstr(TAG_BASENAMES, it));
				}
			}
		}
		free(mytags);
	}
	return 0;
}

static void extract_cpio_gz(int fd)
{
	USE_DESKTOP(long long) int (*xformer)(int src_fd, int dst_fd);
	archive_handle_t *archive_handle;
	unsigned char magic[2];

	/* Initialize */
	archive_handle = init_handle();
	archive_handle->seek = seek_by_read;
	//archive_handle->action_header = header_list;
	archive_handle->action_data = data_extract_all;
	archive_handle->flags |= ARCHIVE_PRESERVE_DATE;
	archive_handle->flags |= ARCHIVE_CREATE_LEADING_DIRS;
	archive_handle->src_fd = fd;
	archive_handle->offset = 0;

	xread(archive_handle->src_fd, &magic, 2);
	xformer = unpack_gz_stream;
	if ((magic[0] != 0x1f) || (magic[1] != 0x8b)) {
		if (ENABLE_FEATURE_RPM_BZ2
		 && (magic[0] == 0x42) && (magic[1] == 0x5a)) {
			xformer = unpack_bz2_stream;
	/* We can do better, need modifying unpack_bz2_stream to not require
	 * first 2 bytes. Not very hard to do... I mean, TODO :) */
			xlseek(archive_handle->src_fd, -2, SEEK_CUR);
		} else
			bb_error_msg_and_die("no gzip"
				USE_FEATURE_RPM_BZ2("/bzip")
				" magic");
	} else
		check_header_gzip_or_die(archive_handle->src_fd);

	xchdir("/"); /* Install RPM's to root */
	archive_handle->src_fd = open_transformer(archive_handle->src_fd, xformer);
	archive_handle->offset = 0;
	while (get_header_cpio(archive_handle) == EXIT_SUCCESS)
		continue;
}


static rpm_index **rpm_gettags(int fd, int *num_tags)
{
	/* We should never need mode than 200, and realloc later */
	rpm_index **tags = xzalloc(200 * sizeof(struct rpmtag *));
	int pass, tagindex = 0;

	xlseek(fd, 96, SEEK_CUR); /* Seek past the unused lead */

	/* 1st pass is the signature headers, 2nd is the main stuff */
	for (pass = 0; pass < 2; pass++) {
		struct {
			char magic[3]; /* 3 byte magic: 0x8e 0xad 0xe8 */
			uint8_t version; /* 1 byte version number */
			uint32_t reserved; /* 4 bytes reserved */
			uint32_t entries; /* Number of entries in header (4 bytes) */
			uint32_t size; /* Size of store (4 bytes) */
		} header;
		rpm_index *tmpindex;
		int storepos;

		xread(fd, &header, sizeof(header));
		if (strncmp((char *) &header.magic, RPM_HEADER_MAGIC, 3) != 0)
			return NULL; /* Invalid magic */
		if (header.version != 1)
			return NULL; /* This program only supports v1 headers */
		header.size = ntohl(header.size);
		header.entries = ntohl(header.entries);
		storepos = xlseek(fd,0,SEEK_CUR) + header.entries * 16;

		while (header.entries--) {
			tmpindex = tags[tagindex++] = xmalloc(sizeof(rpm_index));
			xread(fd, tmpindex, sizeof(rpm_index));
			tmpindex->tag = ntohl(tmpindex->tag);
			tmpindex->type = ntohl(tmpindex->type);
			tmpindex->count = ntohl(tmpindex->count);
			tmpindex->offset = storepos + ntohl(tmpindex->offset);
			if (pass==0)
				tmpindex->tag -= 743;
		}
		xlseek(fd, header.size, SEEK_CUR); /* Seek past store */
		/* Skip padding to 8 byte boundary after reading signature headers */
		if (pass==0)
			xlseek(fd, (8 - (xlseek(fd,0,SEEK_CUR) % 8)) % 8, SEEK_CUR);
	}
	tags = xrealloc(tags, tagindex * sizeof(struct rpmtag *)); /* realloc tags to save space */
	*num_tags = tagindex;
	return tags; /* All done, leave the file at the start of the gzipped cpio archive */
}

static int bsearch_rpmtag(const void *key, const void *item)
{
	int *tag = (int *)key;
	rpm_index **tmp = (rpm_index **) item;
	return (*tag - tmp[0]->tag);
}

static int rpm_getcount(int tag)
{
	rpm_index **found;
	found = bsearch(&tag, mytags, tagcount, sizeof(struct rpmtag *), bsearch_rpmtag);
	if (!found)
		return 0;
	return found[0]->count;
}

static char *rpm_getstr(int tag, int itemindex)
{
	rpm_index **found;
	found = bsearch(&tag, mytags, tagcount, sizeof(struct rpmtag *), bsearch_rpmtag);
	if (!found || itemindex >= found[0]->count)
		return NULL;
	if (found[0]->type == RPM_STRING_TYPE || found[0]->type == RPM_I18NSTRING_TYPE || found[0]->type == RPM_STRING_ARRAY_TYPE) {
		int n;
		char *tmpstr = (char *) (map + found[0]->offset);
		for (n=0; n < itemindex; n++)
			tmpstr = tmpstr + strlen(tmpstr) + 1;
		return tmpstr;
	}
	return NULL;
}

static int rpm_getint(int tag, int itemindex)
{
	rpm_index **found;
	int *tmpint; /* NB: using int8_t* would be easier to code */

	/* gcc throws warnings here when sizeof(void*)!=sizeof(int) ...
	 * it's ok to ignore it because tag won't be used as a pointer */
	found = bsearch(&tag, mytags, tagcount, sizeof(struct rpmtag *), bsearch_rpmtag);
	if (!found || itemindex >= found[0]->count)
		return -1;

	tmpint = (int *) (map + found[0]->offset);

	if (found[0]->type == RPM_INT32_TYPE) {
		tmpint = (int *) ((char *) tmpint + itemindex*4);
		/*return ntohl(*tmpint);*/
		/* int can be != int32_t */
		return ntohl(*(int32_t*)tmpint);
	}
	if (found[0]->type == RPM_INT16_TYPE) {
		tmpint = (int *) ((char *) tmpint + itemindex*2);
		/* ??? read int, and THEN ntohs() it?? */
		/*return ntohs(*tmpint);*/
		return ntohs(*(int16_t*)tmpint);
	}
	if (found[0]->type == RPM_INT8_TYPE) {
		tmpint = (int *) ((char *) tmpint + itemindex);
		/* ??? why we don't read byte here??? */
		/*return ntohs(*tmpint);*/
		return *(int8_t*)tmpint;
	}
	return -1;
}

static void fileaction_dobackup(char *filename, int fileref)
{
	struct stat oldfile;
	int stat_res;
	char *newname;
	if (rpm_getint(TAG_FILEFLAGS, fileref) & RPMFILE_CONFIG) {
		/* Only need to backup config files */
		stat_res = lstat(filename, &oldfile);
		if (stat_res == 0 && S_ISREG(oldfile.st_mode)) {
			/* File already exists  - really should check MD5's etc to see if different */
			newname = xasprintf("%s.rpmorig", filename);
			copy_file(filename, newname, FILEUTILS_RECUR | FILEUTILS_PRESERVE_STATUS);
			remove_file(filename, FILEUTILS_RECUR | FILEUTILS_FORCE);
			free(newname);
		}
	}
}

static void fileaction_setowngrp(char *filename, int fileref)
{
	int uid, gid;
	uid = xuname2uid(rpm_getstr(TAG_FILEUSERNAME, fileref));
	gid = xgroup2gid(rpm_getstr(TAG_FILEGROUPNAME, fileref));
	chown(filename, uid, gid);
}

static void loop_through_files(int filetag, void (*fileaction)(char *filename, int fileref))
{
	int count = 0;
	while (rpm_getstr(filetag, count)) {
		char* filename = xasprintf("%s%s",
			rpm_getstr(TAG_DIRNAMES, rpm_getint(TAG_DIRINDEXES, count)),
			rpm_getstr(TAG_BASENAMES, count));
		fileaction(filename, count++);
		free(filename);
	}
}
