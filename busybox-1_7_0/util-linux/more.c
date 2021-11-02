/* vi: set sw=4 ts=4: */
/*
 * Mini more implementation for busybox
 *
 * Copyright (C) 1995, 1996 by Bruce Perens <bruce@pixar.com>.
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Latest version blended together by Erik Andersen <andersen@codepoet.org>,
 * based on the original more implementation by Bruce, and code from the
 * Debian boot-floppies team.
 *
 * Termios corrects by Vladimir Oleynik <dzo@simtreas.ru>
 *
 * Licensed under GPLv2 or later, see file License in this tarball for details.
 */

#include "libbb.h"
#if ENABLE_FEATURE_USE_TERMIOS
#include <termios.h>
#endif /* FEATURE_USE_TERMIOS */


#if ENABLE_FEATURE_USE_TERMIOS

struct globals {
	int cin_fileno;
	struct termios initial_settings;
	struct termios new_settings;
};
#define G (*(struct globals*)bb_common_bufsiz1)
//#define G (*ptr_to_globals)
#define INIT_G() ((void)0)
//#define INIT_G() PTR_TO_GLOBALS = xzalloc(sizeof(G))
#define initial_settings (G.initial_settings)
#define new_settings     (G.new_settings    )
#define cin_fileno       (G.cin_fileno      )

#define setTermSettings(fd, argp) tcsetattr(fd, TCSANOW, argp)
#define getTermSettings(fd, argp) tcgetattr(fd, argp)

static void gotsig(int sig)
{
	putchar('\n');
	setTermSettings(cin_fileno, &initial_settings);
	exit(EXIT_FAILURE);
}

#else /* !FEATURE_USE_TERMIOS */
#define INIT_G() ((void)0)
#define setTermSettings(fd, argp) ((void)0)
#endif /* FEATURE_USE_TERMIOS */

#define CONVERTED_TAB_SIZE 8

int more_main(int argc, char **argv);
int more_main(int argc, char **argv)
{
	int c = c; /* for gcc */
	int lines;
	int input = 0;
	int spaces = 0;
	int please_display_more_prompt;
	struct stat st;
	FILE *file;
	FILE *cin;
	int len;
	int terminal_width;
	int terminal_height;

	INIT_G();

	argv++;
	/* Another popular pager, most, detects when stdout
	 * is not a tty and turns into cat. This makes sense. */
	if (!isatty(STDOUT_FILENO))
		return bb_cat(argv);
	cin = fopen(CURRENT_TTY, "r");
	if (!cin)
		return bb_cat(argv);

#if ENABLE_FEATURE_USE_TERMIOS
	cin_fileno = fileno(cin);
	getTermSettings(cin_fileno, &initial_settings);
	new_settings = initial_settings;
	new_settings.c_lflag &= ~ICANON;
	new_settings.c_lflag &= ~ECHO;
	new_settings.c_cc[VMIN] = 1;
	new_settings.c_cc[VTIME] = 0;
	setTermSettings(cin_fileno, &new_settings);
	signal(SIGINT, gotsig);
	signal(SIGQUIT, gotsig);
	signal(SIGTERM, gotsig);
#endif

	do {
		file = stdin;
		if (*argv) {
			file = fopen_or_warn(*argv, "r");
			if (!file)
				continue;
		}
		st.st_size = 0;
		fstat(fileno(file), &st);

		please_display_more_prompt = 0;
		/* never returns w, h <= 1 */
		get_terminal_width_height(fileno(cin), &terminal_width, &terminal_height);
		terminal_height -= 1;

		len = 0;
		lines = 0;
		while (spaces || (c = getc(file)) != EOF) {
			int wrap;
			if (spaces)
				spaces--;
 loop_top:
			if (input != 'r' && please_display_more_prompt) {
				len = printf("--More-- ");
				if (st.st_size > 0) {
					len += printf("(%d%% of %"OFF_FMT"d bytes)",
						(int) (ftello(file)*100 / st.st_size),
						st.st_size);
				}
				fflush(stdout);

				/*
				 * We've just displayed the "--More--" prompt, so now we need
				 * to get input from the user.
				 */
				for (;;) {
					input = getc(cin);
					input = tolower(input);
#if !ENABLE_FEATURE_USE_TERMIOS
					printf("\033[A"); /* up cursor */
#endif
					/* Erase the last message */
					printf("\r%*s\r", len, "");

					/* Due to various multibyte escape
					 * sequences, it's not ok to accept
					 * any input as a command to scroll
					 * the screen. We only allow known
					 * commands, else we show help msg. */
					if (input == ' ' || input == '\n' || input == 'q' || input == 'r')
						break;
					len = printf("(Enter:next line Space:next page Q:quit R:show the rest)");
				}
				len = 0;
				lines = 0;
				please_display_more_prompt = 0;

				if (input == 'q')
					goto end;

				/* The user may have resized the terminal.
				 * Re-read the dimensions. */
#if ENABLE_FEATURE_USE_TERMIOS
				get_terminal_width_height(cin_fileno, &terminal_width, &terminal_height);
				terminal_height -= 1;
#endif
			}

			/* Crudely convert tabs into spaces, which are
			 * a bajillion times easier to deal with. */
			if (c == '\t') {
				spaces = CONVERTED_TAB_SIZE - 1;
				c = ' ';
 			}

			/*
			 * There are two input streams to worry about here:
			 *
			 * c    : the character we are reading from the file being "mored"
			 * input: a character received from the keyboard
			 *
			 * If we hit a newline in the _file_ stream, we want to test and
			 * see if any characters have been hit in the _input_ stream. This
			 * allows the user to quit while in the middle of a file.
			 */
			wrap = (++len > terminal_width);
			if (c == '\n' || wrap) {
				/* Then outputting this character
				 * will move us to a new line. */
				if (++lines >= terminal_height || input == '\n')
					please_display_more_prompt = 1;
				len = 0;
			}
			if (c != '\n' && wrap) {
				/* Then outputting this will also put a character on
				 * the beginning of that new line. Thus we first want to
				 * display the prompt (if any), so we skip the putchar()
				 * and go back to the top of the loop, without reading
				 * a new character. */
				goto loop_top;
			}
			/* My small mind cannot fathom backspaces and UTF-8 */
			putchar(c);
		}
		fclose(file);
		fflush(stdout);
	} while (*argv && *++argv);
 end:
	setTermSettings(cin_fileno, &initial_settings);
	return 0;
}
