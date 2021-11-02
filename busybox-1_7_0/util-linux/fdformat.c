/* vi: set sw=4 ts=4: */
/* fdformat.c  -  Low-level formats a floppy disk - Werner Almesberger */

/* 1999-02-22 Arkadiusz Mi�kiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 * 1999-03-20 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 * - more i18n/nls translatable strings marked
 *
 * 5 July 2003 -- modified for Busybox by Erik Andersen
 */

#include "libbb.h"


/* Stuff extracted from linux/fd.h */
struct floppy_struct {
	unsigned int	size,		/* nr of sectors total */
			sect,		/* sectors per track */
			head,		/* nr of heads */
			track,		/* nr of tracks */
			stretch;	/* !=0 means double track steps */
#define FD_STRETCH 1
#define FD_SWAPSIDES 2

	unsigned char	gap,		/* gap1 size */

			rate,		/* data rate. |= 0x40 for perpendicular */
#define FD_2M 0x4
#define FD_SIZECODEMASK 0x38
#define FD_SIZECODE(floppy) (((((floppy)->rate&FD_SIZECODEMASK)>> 3)+ 2) %8)
#define FD_SECTSIZE(floppy) ( (floppy)->rate & FD_2M ? \
			     512 : 128 << FD_SIZECODE(floppy) )
#define FD_PERP 0x40

			spec1,		/* stepping rate, head unload time */
			fmt_gap;	/* gap2 size */
	const char	* name; /* used only for predefined formats */
};
struct format_descr {
	unsigned int device,head,track;
};
#define FDFMTBEG _IO(2,0x47)
#define	FDFMTTRK _IOW(2,0x48, struct format_descr)
#define FDFMTEND _IO(2,0x49)
#define FDGETPRM _IOR(2, 0x04, struct floppy_struct)
#define FD_FILL_BYTE 0xF6 /* format fill byte. */

int fdformat_main(int argc,char **argv);
int fdformat_main(int argc,char **argv)
{
	int fd, n, cyl, read_bytes, verify;
	unsigned char *data;
	struct stat st;
	struct floppy_struct param;
	struct format_descr descr;

	if (argc < 2) {
		bb_show_usage();
	}
	verify = !getopt32(argv, "n");
	argv += optind;

	xstat(*argv, &st);
	if (!S_ISBLK(st.st_mode)) {
		bb_error_msg_and_die("%s: not a block device", *argv);
		/* do not test major - perhaps this was an USB floppy */
	}

	/* O_RDWR for formatting and verifying */
	fd = xopen(*argv, O_RDWR);

	/* original message was: "Could not determine current format type" */
	xioctl(fd, FDGETPRM, &param);

	printf("%s-sided, %d tracks, %d sec/track. Total capacity %d kB\n",
		(param.head == 2) ? "Double" : "Single",
		param.track, param.sect, param.size >> 1);

	/* FORMAT */
	printf("Formatting... ");
	xioctl(fd, FDFMTBEG, NULL);

	/* n == track */
	for (n = 0; n < param.track; n++) {
		descr.head = 0;
		descr.track = n;
		xioctl(fd, FDFMTTRK, &descr);
		printf("%3d\b\b\b", n);
		if (param.head == 2) {
			descr.head = 1;
			xioctl(fd, FDFMTTRK, &descr);
		}
	}

	xioctl(fd, FDFMTEND, NULL);
	printf("done\n");

	/* VERIFY */
	if (verify) {
		/* n == cyl_size */
		n = param.sect*param.head*512;

		data = xmalloc(n);
		printf("Verifying... ");
		for (cyl = 0; cyl < param.track; cyl++) {
			printf("%3d\b\b\b", cyl);
			read_bytes = safe_read(fd, data, n);
			if (read_bytes != n) {
				if (read_bytes < 0) {
					bb_perror_msg(bb_msg_read_error);
				}
				bb_error_msg_and_die("problem reading cylinder %d, "
					"expected %d, read %d", cyl, n, read_bytes);
				// FIXME: maybe better seek & continue??
			}
			/* Check backwards so we don't need a counter */
			while (--read_bytes >= 0) {
				if (data[read_bytes] != FD_FILL_BYTE) {
					 printf("bad data in cyl %d\nContinuing... ",cyl);
				}
			}
		}
		/* There is no point in freeing blocks at the end of a program, because
		all of the program's space is given back to the system when the process
		terminates.*/

		if (ENABLE_FEATURE_CLEAN_UP) free(data);

		printf("done\n");
	}

	if (ENABLE_FEATURE_CLEAN_UP) close(fd);

	/* Don't bother closing.  Exit does
	 * that, so we can save a few bytes */
	return EXIT_SUCCESS;
}
