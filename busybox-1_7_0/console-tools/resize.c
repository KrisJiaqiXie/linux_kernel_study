/* vi: set sw=4 ts=4: */
/*
 * resize - set terminal width and height.
 *
 * Copyright 2006 Bernhard Fischer
 *
 * Licensed under GPLv2 or later, see file LICENSE in this tarball for details.
 */
/* no options, no getopt */
#include "libbb.h"

#define ESC "\033"

#define old_termios (*(struct termios*)&bb_common_bufsiz1)

static void
onintr(int sig ATTRIBUTE_UNUSED)
{
	tcsetattr(STDERR_FILENO, TCSANOW, &old_termios);
	exit(1);
}

int resize_main(int argc, char **argv);
int resize_main(int argc, char **argv)
{
	struct termios new;
	struct winsize w = { 0,0,0,0 };
	int ret;

	/* We use _stderr_ in order to make resize usable
	 * in shell backticks (those redirect stdout away from tty).
	 * NB: other versions of resize open "/dev/tty"
	 * and operate on it - should we do the same?
	 */

	tcgetattr(STDERR_FILENO, &old_termios); /* fiddle echo */
	new = old_termios;
	new.c_cflag |= (CLOCAL | CREAD);
	new.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
	signal(SIGINT, onintr);
	signal(SIGQUIT, onintr);
	signal(SIGTERM, onintr);
	signal(SIGALRM, onintr);
	tcsetattr(STDERR_FILENO, TCSANOW, &new);

	/* save_cursor_pos 7
	 * scroll_whole_screen [r
	 * put_cursor_waaaay_off [$x;$yH
	 * get_cursor_pos [6n
	 * restore_cursor_pos 8
	 */
	fprintf(stderr, ESC"7" ESC"[r" ESC"[999;999H" ESC"[6n");
	alarm(3); /* Just in case terminal won't answer */
	scanf(ESC"[%hu;%huR", &w.ws_row, &w.ws_col);
	fprintf(stderr, ESC"8");

	/* BTW, other versions of resize recalculate w.ws_xpixel, ws.ws_ypixel
	 * by calculating character cell HxW from old values
	 * (gotten via TIOCGWINSZ) and recomputing *pixel values */
	ret = ioctl(STDERR_FILENO, TIOCSWINSZ, &w);

	tcsetattr(STDERR_FILENO, TCSANOW, &old_termios);

	if (ENABLE_FEATURE_RESIZE_PRINT)
		printf("COLUMNS=%d;LINES=%d;export COLUMNS LINES;\n",
			w.ws_col, w.ws_row);

	return ret;
}
