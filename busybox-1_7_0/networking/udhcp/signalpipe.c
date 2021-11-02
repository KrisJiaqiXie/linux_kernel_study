/* vi: set sw=4 ts=4: */
/* signalpipe.c
 *
 * Signal pipe infrastructure. A reliable way of delivering signals.
 *
 * Russ Dill <Russ.Dill@asu.edu> December 2003
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "common.h"


static int signal_pipe[2];

static void signal_handler(int sig)
{
	unsigned char ch = sig; /* use char, avoid dealing with partial writes */
	if (write(signal_pipe[1], &ch, 1) != 1)
		bb_perror_msg("cannot send signal");
}


/* Call this before doing anything else. Sets up the socket pair
 * and installs the signal handler */
void udhcp_sp_setup(void)
{
	/* was socketpair, but it needs AF_UNIX in kernel */
	xpipe(signal_pipe);
	fcntl(signal_pipe[0], F_SETFD, FD_CLOEXEC);
	fcntl(signal_pipe[1], F_SETFD, FD_CLOEXEC);
	fcntl(signal_pipe[1], F_SETFL, O_NONBLOCK);
	signal(SIGUSR1, signal_handler);
	signal(SIGUSR2, signal_handler);
	signal(SIGTERM, signal_handler);
}


/* Quick little function to setup the rfds. Will return the
 * max_fd for use with select. Limited in that you can only pass
 * one extra fd */
int udhcp_sp_fd_set(fd_set *rfds, int extra_fd)
{
	FD_ZERO(rfds);
	FD_SET(signal_pipe[0], rfds);
	if (extra_fd >= 0) {
		fcntl(extra_fd, F_SETFD, FD_CLOEXEC);
		FD_SET(extra_fd, rfds);
	}
	return signal_pipe[0] > extra_fd ? signal_pipe[0] : extra_fd;
}


/* Read a signal from the signal pipe. Returns 0 if there is
 * no signal, -1 on error (and sets errno appropriately), and
 * your signal on success */
int udhcp_sp_read(fd_set *rfds)
{
	unsigned char sig;

	if (!FD_ISSET(signal_pipe[0], rfds))
		return 0;

	if (read(signal_pipe[0], &sig, 1) != 1)
		return -1;

	return sig;
}
