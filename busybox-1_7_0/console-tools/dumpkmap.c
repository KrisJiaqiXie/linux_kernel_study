/* vi: set sw=4 ts=4: */
/*
 * Mini dumpkmap implementation for busybox
 *
 * Copyright (C) Arne Bernin <arne@matrix.loopback.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this tarball for details.
 *
 */

#include "libbb.h"

/* From <linux/kd.h> */
struct kbentry {
	unsigned char kb_table;
	unsigned char kb_index;
	unsigned short kb_value;
};
#define KDGKBENT 0x4B46  /* gets one entry in translation table */

/* From <linux/keyboard.h> */
#define NR_KEYS 128
#define MAX_NR_KEYMAPS 256

int dumpkmap_main(int argc, char **argv);
int dumpkmap_main(int argc, char **argv)
{
	struct kbentry ke;
	int i, j, fd;
	char flags[MAX_NR_KEYMAPS];

	if (argc >= 2 && argv[1][0] == '-')
		bb_show_usage();

	fd = xopen(CURRENT_VC, O_RDWR);

	write(1, "bkeymap", 7);

	/* Here we want to set everything to 0 except for indexes:
	 * [0-2] [4-6] [8-10] [12] */
	memset(flags, 0x00, MAX_NR_KEYMAPS);
	memset(flags, 0x01, 13);
	flags[3] = flags[7] = flags[11] = 0;

	/* dump flags */
	write(1, flags, MAX_NR_KEYMAPS);

	for (i = 0; i < MAX_NR_KEYMAPS; i++) {
		if (flags[i] == 1) {
			for (j = 0; j < NR_KEYS; j++) {
				ke.kb_index = j;
				ke.kb_table = i;
				if (!ioctl_or_perror(fd, KDGKBENT, &ke,
						"ioctl failed with %s, %s, %p",
						(char *)&ke.kb_index,
						(char *)&ke.kb_table,
						&ke.kb_value)
				) {
					write(1, (void*)&ke.kb_value, 2);
				}
			}
		}
	}
	close(fd);
	return EXIT_SUCCESS;
}
