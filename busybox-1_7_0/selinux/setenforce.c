/*
 * setenforce
 *
 * Based on libselinux 1.33.1
 * Port to BusyBox  Hiroshi Shinji <shiroshi@my.email.ne.jp>
 *
 */

#include "libbb.h"

/* These strings are arranged so that odd ones
 * result in security_setenforce(1) being done,
 * the rest will do security_setenforce(0) */
static const char *const setenforce_cmd[] = {
	"0",
	"1",
	"permissive",
	"enforcing",
	NULL,
};

int setenforce_main(int argc, char **argv);
int setenforce_main(int argc, char **argv)
{
	int i, rc;

	if (argc != 2)
		bb_show_usage();

	selinux_or_die();

	for (i = 0; setenforce_cmd[i]; i++) {
		if (strcasecmp(argv[1], setenforce_cmd[i]) != 0)
			continue;
		rc = security_setenforce(i & 1);
		if (rc < 0)
			bb_perror_msg_and_die("setenforce() failed");
		return 0;
	}

	bb_show_usage();
}
