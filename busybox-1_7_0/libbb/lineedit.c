/* vi: set sw=4 ts=4: */
/*
 * Termios command line History and Editing.
 *
 * Copyright (c) 1986-2003 may safely be consumed by a BSD or GPL license.
 * Written by:   Vladimir Oleynik <dzo@simtreas.ru>
 *
 * Used ideas:
 *      Adam Rogoyski    <rogoyski@cs.utexas.edu>
 *      Dave Cinege      <dcinege@psychosis.com>
 *      Jakub Jelinek (c) 1995
 *      Erik Andersen    <andersen@codepoet.org> (Majorly adjusted for busybox)
 *
 * This code is 'as is' with no warranty.
 */

/*
   Usage and known bugs:
   Terminal key codes are not extensive, and more will probably
   need to be added. This version was created on Debian GNU/Linux 2.x.
   Delete, Backspace, Home, End, and the arrow keys were tested
   to work in an Xterm and console. Ctrl-A also works as Home.
   Ctrl-E also works as End.

   Small bugs (simple effect):
   - not true viewing if terminal size (x*y symbols) less
     size (prompt + editor's line + 2 symbols)
   - not true viewing if length prompt less terminal width
 */

#include "libbb.h"


/* FIXME: obsolete CONFIG item? */
#define ENABLE_FEATURE_NONPRINTABLE_INVERSE_PUT 0


#ifdef TEST

#define ENABLE_FEATURE_EDITING 0
#define ENABLE_FEATURE_TAB_COMPLETION 0
#define ENABLE_FEATURE_USERNAME_COMPLETION 0
#define ENABLE_FEATURE_NONPRINTABLE_INVERSE_PUT 0
#define ENABLE_FEATURE_CLEAN_UP 0

#endif  /* TEST */


/* Entire file (except TESTing part) sits inside this #if */
#if ENABLE_FEATURE_EDITING

#if ENABLE_LOCALE_SUPPORT
#define Isprint(c) isprint(c)
#else
#define Isprint(c) ((c) >= ' ' && (c) != ((unsigned char)'\233'))
#endif

#define ENABLE_FEATURE_GETUSERNAME_AND_HOMEDIR \
(ENABLE_FEATURE_USERNAME_COMPLETION || ENABLE_FEATURE_EDITING_FANCY_PROMPT)

enum { MAX_LINELEN = CONFIG_FEATURE_EDITING_MAX_LEN };

static line_input_t *state;

static struct termios initial_settings, new_settings;

static volatile unsigned cmdedit_termw = 80;        /* actual terminal width */

static int cmdedit_x;           /* real x terminal position */
static int cmdedit_y;           /* pseudoreal y terminal position */
static int cmdedit_prmt_len;    /* length of prompt (without colors etc) */

static unsigned cursor;
static unsigned command_len;
static char *command_ps;
static const char *cmdedit_prompt;

#if ENABLE_FEATURE_EDITING_FANCY_PROMPT
static char *hostname_buf;
static int num_ok_lines = 1;
#endif

#if ENABLE_FEATURE_GETUSERNAME_AND_HOMEDIR
static char *user_buf = (char*)"";
static char *home_pwd_buf = (char*)"";
#endif

/* Put 'command_ps[cursor]', cursor++.
 * Advance cursor on screen. If we reached right margin, scroll text up
 * and remove terminal margin effect by printing 'next_char' */
static void cmdedit_set_out_char(int next_char)
{
	int c = (unsigned char)command_ps[cursor];

	if (c == '\0') {
		/* erase character after end of input string */
		c = ' ';
	}
#if ENABLE_FEATURE_NONPRINTABLE_INVERSE_PUT
	/* Display non-printable characters in reverse */
	if (!Isprint(c)) {
		if (c >= 128)
			c -= 128;
		if (c < ' ')
			c += '@';
		if (c == 127)
			c = '?';
		printf("\033[7m%c\033[0m", c);
	} else
#endif
	{
		if (initial_settings.c_lflag & ECHO)
			putchar(c);
	}
	if (++cmdedit_x >= cmdedit_termw) {
		/* terminal is scrolled down */
		cmdedit_y++;
		cmdedit_x = 0;
		/* destroy "(auto)margin" */
		putchar(next_char);
		putchar('\b');
	}
// Huh? What if command_ps[cursor] == '\0' (we are at the end already?)
	cursor++;
}

/* Move to end of line (by printing all chars till the end) */
static void input_end(void)
{
	while (cursor < command_len)
		cmdedit_set_out_char(' ');
}

/* Go to the next line */
static void goto_new_line(void)
{
	input_end();
	if (cmdedit_x)
		putchar('\n');
}


static void out1str(const char *s)
{
	if (s)
		fputs(s, stdout);
}

static void beep(void)
{
	putchar('\007');
}

/* Move back one character */
/* (optimized for slow terminals) */
static void input_backward(unsigned num)
{
	int count_y;

	if (num > cursor)
		num = cursor;
	if (!num)
		return;
	cursor -= num;

	if (cmdedit_x >= num) {
		cmdedit_x -= num;
		if (num <= 4) {
			printf("\b\b\b\b" + (4-num));
			return;
		}
		printf("\033[%uD", num);
		return;
	}

	/* Need to go one or more lines up */
	num -= cmdedit_x;
	count_y = 1 + (num / cmdedit_termw);
	cmdedit_y -= count_y;
	cmdedit_x = cmdedit_termw * count_y - num;
	/* go to 1st column; go up; go to correct column */
	printf("\r" "\033[%dA" "\033[%dC", count_y, cmdedit_x);
}

static void put_prompt(void)
{
	out1str(cmdedit_prompt);
	cmdedit_x = cmdedit_prmt_len;
	cursor = 0;
// Huh? what if cmdedit_prmt_len >= width?
	cmdedit_y = 0;                  /* new quasireal y */
}

/* draw prompt, editor line, and clear tail */
static void redraw(int y, int back_cursor)
{
	if (y > 0)                              /* up to start y */
		printf("\033[%dA", y);
	putchar('\r');
	put_prompt();
	input_end();                            /* rewrite */
	printf("\033[J");                       /* erase after cursor */
	input_backward(back_cursor);
}

#if ENABLE_FEATURE_EDITING_VI
#define DELBUFSIZ 128
static char *delbuf;  /* a (malloced) place to store deleted characters */
static char *delp;
static char newdelflag;      /* whether delbuf should be reused yet */
#endif

/* Delete the char in front of the cursor, optionally saving it
 * for later putback */
static void input_delete(int save)
{
	int j = cursor;

	if (j == command_len)
		return;

#if ENABLE_FEATURE_EDITING_VI
	if (save) {
		if (newdelflag) {
			if (!delbuf)
				delbuf = malloc(DELBUFSIZ);
			/* safe if malloc fails */
			delp = delbuf;
			newdelflag = 0;
		}
		if (delbuf && (delp - delbuf < DELBUFSIZ))
			*delp++ = command_ps[j];
	}
#endif

	strcpy(command_ps + j, command_ps + j + 1);
	command_len--;
	input_end();                    /* rewrite new line */
	cmdedit_set_out_char(' ');      /* erase char */
	input_backward(cursor - j);     /* back to old pos cursor */
}

#if ENABLE_FEATURE_EDITING_VI
static void put(void)
{
	int ocursor;
	int j = delp - delbuf;

	if (j == 0)
		return;
	ocursor = cursor;
	/* open hole and then fill it */
	memmove(command_ps + cursor + j, command_ps + cursor, command_len - cursor + 1);
	strncpy(command_ps + cursor, delbuf, j);
	command_len += j;
	input_end();                    /* rewrite new line */
	input_backward(cursor - ocursor - j + 1); /* at end of new text */
}
#endif

/* Delete the char in back of the cursor */
static void input_backspace(void)
{
	if (cursor > 0) {
		input_backward(1);
		input_delete(0);
	}
}

/* Move forward one character */
static void input_forward(void)
{
	if (cursor < command_len)
		cmdedit_set_out_char(command_ps[cursor + 1]);
}


#if ENABLE_FEATURE_TAB_COMPLETION

static char **matches;
static unsigned num_matches;

static void free_tab_completion_data(void)
{
	if (matches) {
		while (num_matches)
			free(matches[--num_matches]);
		free(matches);
		matches = NULL;
	}
}

static void add_match(char *matched)
{
	int nm = num_matches;
	int nm1 = nm + 1;

	matches = xrealloc(matches, nm1 * sizeof(char *));
	matches[nm] = matched;
	num_matches++;
}

#if ENABLE_FEATURE_USERNAME_COMPLETION
static void username_tab_completion(char *ud, char *with_shash_flg)
{
	struct passwd *entry;
	int userlen;

	ud++;                           /* ~user/... to user/... */
	userlen = strlen(ud);

	if (with_shash_flg) {           /* "~/..." or "~user/..." */
		char *sav_ud = ud - 1;
		char *home = 0;
		char *temp;

		if (*ud == '/') {       /* "~/..."     */
			home = home_pwd_buf;
		} else {
			/* "~user/..." */
			temp = strchr(ud, '/');
			*temp = 0;              /* ~user\0 */
			entry = getpwnam(ud);
			*temp = '/';            /* restore ~user/... */
			ud = temp;
			if (entry)
				home = entry->pw_dir;
		}
		if (home) {
			if ((userlen + strlen(home) + 1) < MAX_LINELEN) {
				char temp2[MAX_LINELEN];     /* argument size */

				/* /home/user/... */
				sprintf(temp2, "%s%s", home, ud);
				strcpy(sav_ud, temp2);
			}
		}
	} else {
		/* "~[^/]*" */
		/* Using _r function to avoid pulling in static buffers */
		char line_buff[256];
		struct passwd pwd;
		struct passwd *result;

		setpwent();
		while (!getpwent_r(&pwd, line_buff, sizeof(line_buff), &result)) {
			/* Null usernames should result in all users as possible completions. */
			if (/*!userlen || */ strncmp(ud, pwd.pw_name, userlen) == 0) {
				add_match(xasprintf("~%s/", pwd.pw_name));
			}
		}
		endpwent();
	}
}
#endif  /* FEATURE_COMMAND_USERNAME_COMPLETION */

enum {
	FIND_EXE_ONLY = 0,
	FIND_DIR_ONLY = 1,
	FIND_FILE_ONLY = 2,
};

static int path_parse(char ***p, int flags)
{
	int npth;
	const char *pth;
	char *tmp;
	char **res;

	/* if not setenv PATH variable, to search cur dir "." */
	if (flags != FIND_EXE_ONLY)
		return 1;

	if (state->flags & WITH_PATH_LOOKUP)
		pth = state->path_lookup;
	else
		pth = getenv("PATH");
	/* PATH=<empty> or PATH=:<empty> */
	if (!pth || !pth[0] || LONE_CHAR(pth, ':'))
		return 1;

	tmp = (char*)pth;
	npth = 1; /* path component count */
	while (1) {
		tmp = strchr(tmp, ':');
		if (!tmp)
			break;
		if (*++tmp == '\0')
			break;  /* :<empty> */
		npth++;
	}

	res = xmalloc(npth * sizeof(char*));
	res[0] = tmp = xstrdup(pth);
	npth = 1;
	while (1) {
		tmp = strchr(tmp, ':');
		if (!tmp)
			break;
		*tmp++ = '\0'; /* ':' -> '\0' */
		if (*tmp == '\0')
			break; /* :<empty> */
		res[npth++] = tmp;
	}
	*p = res;
	return npth;
}

static void exe_n_cwd_tab_completion(char *command, int type)
{
	DIR *dir;
	struct dirent *next;
	char dirbuf[MAX_LINELEN];
	struct stat st;
	char *path1[1];
	char **paths = path1;
	int npaths;
	int i;
	char *found;
	char *pfind = strrchr(command, '/');

	npaths = 1;
	path1[0] = (char*)".";

	if (pfind == NULL) {
		/* no dir, if flags==EXE_ONLY - get paths, else "." */
		npaths = path_parse(&paths, type);
		pfind = command;
	} else {
		/* dirbuf = ".../.../.../" */
		safe_strncpy(dirbuf, command, (pfind - command) + 2);
#if ENABLE_FEATURE_USERNAME_COMPLETION
		if (dirbuf[0] == '~')   /* ~/... or ~user/... */
			username_tab_completion(dirbuf, dirbuf);
#endif
		paths[0] = dirbuf;
		/* point to 'l' in "..../last_component" */
		pfind++;
	}

	for (i = 0; i < npaths; i++) {
		dir = opendir(paths[i]);
		if (!dir)                       /* Don't print an error */
			continue;

		while ((next = readdir(dir)) != NULL) {
			int len1;
			const char *str_found = next->d_name;

			/* matched? */
			if (strncmp(str_found, pfind, strlen(pfind)))
				continue;
			/* not see .name without .match */
			if (*str_found == '.' && *pfind == 0) {
				if (NOT_LONE_CHAR(paths[i], '/') || str_found[1])
					continue;
				str_found = ""; /* only "/" */
			}
			found = concat_path_file(paths[i], str_found);
			/* hmm, remover in progress? */
			if (stat(found, &st) < 0)
				goto cont;
			/* find with dirs? */
			if (paths[i] != dirbuf)
				strcpy(found, next->d_name);    /* only name */

			len1 = strlen(found);
			found = xrealloc(found, len1 + 2);
			found[len1] = '\0';
			found[len1+1] = '\0';

			if (S_ISDIR(st.st_mode)) {
				/* name is directory      */
				if (found[len1-1] != '/') {
					found[len1] = '/';
				}
			} else {
				/* not put found file if search only dirs for cd */
				if (type == FIND_DIR_ONLY)
					goto cont;
			}
			/* Add it to the list */
			add_match(found);
			continue;
 cont:
			free(found);
		}
		closedir(dir);
	}
	if (paths != path1) {
		free(paths[0]);                 /* allocated memory only in first member */
		free(paths);
	}
}

#define QUOT (UCHAR_MAX+1)

#define collapse_pos(is, in) { \
	memmove(int_buf+(is), int_buf+(in), (MAX_LINELEN+1-(is)-(in))*sizeof(int)); \
	memmove(pos_buf+(is), pos_buf+(in), (MAX_LINELEN+1-(is)-(in))*sizeof(int)); }

static int find_match(char *matchBuf, int *len_with_quotes)
{
	int i, j;
	int command_mode;
	int c, c2;
	int int_buf[MAX_LINELEN + 1];
	int pos_buf[MAX_LINELEN + 1];

	/* set to integer dimension characters and own positions */
	for (i = 0;; i++) {
		int_buf[i] = (unsigned char)matchBuf[i];
		if (int_buf[i] == 0) {
			pos_buf[i] = -1;        /* indicator end line */
			break;
		}
		pos_buf[i] = i;
	}

	/* mask \+symbol and convert '\t' to ' ' */
	for (i = j = 0; matchBuf[i]; i++, j++)
		if (matchBuf[i] == '\\') {
			collapse_pos(j, j + 1);
			int_buf[j] |= QUOT;
			i++;
#if ENABLE_FEATURE_NONPRINTABLE_INVERSE_PUT
			if (matchBuf[i] == '\t')        /* algorithm equivalent */
				int_buf[j] = ' ' | QUOT;
#endif
		}
#if ENABLE_FEATURE_NONPRINTABLE_INVERSE_PUT
		else if (matchBuf[i] == '\t')
			int_buf[j] = ' ';
#endif

	/* mask "symbols" or 'symbols' */
	c2 = 0;
	for (i = 0; int_buf[i]; i++) {
		c = int_buf[i];
		if (c == '\'' || c == '"') {
			if (c2 == 0)
				c2 = c;
			else {
				if (c == c2)
					c2 = 0;
				else
					int_buf[i] |= QUOT;
			}
		} else if (c2 != 0 && c != '$')
			int_buf[i] |= QUOT;
	}

	/* skip commands with arguments if line has commands delimiters */
	/* ';' ';;' '&' '|' '&&' '||' but `>&' `<&' `>|' */
	for (i = 0; int_buf[i]; i++) {
		c = int_buf[i];
		c2 = int_buf[i + 1];
		j = i ? int_buf[i - 1] : -1;
		command_mode = 0;
		if (c == ';' || c == '&' || c == '|') {
			command_mode = 1 + (c == c2);
			if (c == '&') {
				if (j == '>' || j == '<')
					command_mode = 0;
			} else if (c == '|' && j == '>')
				command_mode = 0;
		}
		if (command_mode) {
			collapse_pos(0, i + command_mode);
			i = -1;                         /* hack incremet */
		}
	}
	/* collapse `command...` */
	for (i = 0; int_buf[i]; i++)
		if (int_buf[i] == '`') {
			for (j = i + 1; int_buf[j]; j++)
				if (int_buf[j] == '`') {
					collapse_pos(i, j + 1);
					j = 0;
					break;
				}
			if (j) {
				/* not found close ` - command mode, collapse all previous */
				collapse_pos(0, i + 1);
				break;
			} else
				i--;                    /* hack incremet */
		}

	/* collapse (command...(command...)...) or {command...{command...}...} */
	c = 0;                                          /* "recursive" level */
	c2 = 0;
	for (i = 0; int_buf[i]; i++)
		if (int_buf[i] == '(' || int_buf[i] == '{') {
			if (int_buf[i] == '(')
				c++;
			else
				c2++;
			collapse_pos(0, i + 1);
			i = -1;                         /* hack incremet */
		}
	for (i = 0; pos_buf[i] >= 0 && (c > 0 || c2 > 0); i++)
		if ((int_buf[i] == ')' && c > 0) || (int_buf[i] == '}' && c2 > 0)) {
			if (int_buf[i] == ')')
				c--;
			else
				c2--;
			collapse_pos(0, i + 1);
			i = -1;                         /* hack incremet */
		}

	/* skip first not quote space */
	for (i = 0; int_buf[i]; i++)
		if (int_buf[i] != ' ')
			break;
	if (i)
		collapse_pos(0, i);

	/* set find mode for completion */
	command_mode = FIND_EXE_ONLY;
	for (i = 0; int_buf[i]; i++)
		if (int_buf[i] == ' ' || int_buf[i] == '<' || int_buf[i] == '>') {
			if (int_buf[i] == ' ' && command_mode == FIND_EXE_ONLY
			 && matchBuf[pos_buf[0]]=='c'
			 && matchBuf[pos_buf[1]]=='d'
			) {
				command_mode = FIND_DIR_ONLY;
			} else {
				command_mode = FIND_FILE_ONLY;
				break;
			}
		}
	for (i = 0; int_buf[i]; i++)
		/* "strlen" */;
	/* find last word */
	for (--i; i >= 0; i--) {
		c = int_buf[i];
		if (c == ' ' || c == '<' || c == '>' || c == '|' || c == '&') {
			collapse_pos(0, i + 1);
			break;
		}
	}
	/* skip first not quoted '\'' or '"' */
	for (i = 0; int_buf[i] == '\'' || int_buf[i] == '"'; i++)
		/*skip*/;
	/* collapse quote or unquote // or /~ */
	while ((int_buf[i] & ~QUOT) == '/'
	 && ((int_buf[i+1] & ~QUOT) == '/' || (int_buf[i+1] & ~QUOT) == '~')
	) {
		i++;
	}

	/* set only match and destroy quotes */
	j = 0;
	for (c = 0; pos_buf[i] >= 0; i++) {
		matchBuf[c++] = matchBuf[pos_buf[i]];
		j = pos_buf[i] + 1;
	}
	matchBuf[c] = 0;
	/* old lenght matchBuf with quotes symbols */
	*len_with_quotes = j ? j - pos_buf[0] : 0;

	return command_mode;
}

/*
 * display by column (original idea from ls applet,
 * very optimized by me :)
 */
static void showfiles(void)
{
	int ncols, row;
	int column_width = 0;
	int nfiles = num_matches;
	int nrows = nfiles;
	int l;

	/* find the longest file name-  use that as the column width */
	for (row = 0; row < nrows; row++) {
		l = strlen(matches[row]);
		if (column_width < l)
			column_width = l;
	}
	column_width += 2;              /* min space for columns */
	ncols = cmdedit_termw / column_width;

	if (ncols > 1) {
		nrows /= ncols;
		if (nfiles % ncols)
			nrows++;        /* round up fractionals */
	} else {
		ncols = 1;
	}
	for (row = 0; row < nrows; row++) {
		int n = row;
		int nc;

		for (nc = 1; nc < ncols && n+nrows < nfiles; n += nrows, nc++) {
			printf("%s%-*s", matches[n],
				(int)(column_width - strlen(matches[n])), "");
		}
		printf("%s\n", matches[n]);
	}
}

static char *add_quote_for_spec_chars(char *found)
{
	int l = 0;
	char *s = xmalloc((strlen(found) + 1) * 2);

	while (*found) {
		if (strchr(" `\"#$%^&*()=+{}[]:;\'|\\<>", *found))
			s[l++] = '\\';
		s[l++] = *found++;
	}
	s[l] = 0;
	return s;
}

static int match_compare(const void *a, const void *b)
{
	return strcmp(*(char**)a, *(char**)b);
}

/* Do TAB completion */
static void input_tab(int *lastWasTab)
{
	if (!(state->flags & TAB_COMPLETION))
		return;

	if (!*lastWasTab) {
		char *tmp, *tmp1;
		int len_found;
		char matchBuf[MAX_LINELEN];
		int find_type;
		int recalc_pos;

		*lastWasTab = TRUE;             /* flop trigger */

		/* Make a local copy of the string -- up
		 * to the position of the cursor */
		tmp = strncpy(matchBuf, command_ps, cursor);
		tmp[cursor] = '\0';

		find_type = find_match(matchBuf, &recalc_pos);

		/* Free up any memory already allocated */
		free_tab_completion_data();

#if ENABLE_FEATURE_USERNAME_COMPLETION
		/* If the word starts with `~' and there is no slash in the word,
		 * then try completing this word as a username. */
		if (state->flags & USERNAME_COMPLETION)
			if (matchBuf[0] == '~' && strchr(matchBuf, '/') == 0)
				username_tab_completion(matchBuf, NULL);
#endif
		/* Try to match any executable in our path and everything
		 * in the current working directory */
		if (!matches)
			exe_n_cwd_tab_completion(matchBuf, find_type);
		/* Sort, then remove any duplicates found */
		if (matches) {
			int i, n = 0;
			qsort(matches, num_matches, sizeof(char*), match_compare);
			for (i = 0; i < num_matches - 1; ++i) {
				if (matches[i] && matches[i+1]) { /* paranoia */
					if (strcmp(matches[i], matches[i+1]) == 0) {
						free(matches[i]);
						matches[i] = NULL; /* paranoia */
					} else {
						matches[n++] = matches[i];
					}
				}
			}
			matches[n] = matches[i];
			num_matches = n + 1;
		}
		/* Did we find exactly one match? */
		if (!matches || num_matches > 1) {
			beep();
			if (!matches)
				return;         /* not found */
			/* find minimal match */
		// ash: yet another failure in trying to achieve "we don't die on OOM"
			tmp1 = xstrdup(matches[0]);
			for (tmp = tmp1; *tmp; tmp++)
				for (len_found = 1; len_found < num_matches; len_found++)
					if (matches[len_found][(tmp - tmp1)] != *tmp) {
						*tmp = '\0';
						break;
					}
			if (*tmp1 == '\0') {        /* have unique */
				free(tmp1);
				return;
			}
			tmp = add_quote_for_spec_chars(tmp1);
			free(tmp1);
		} else {                        /* one match */
			tmp = add_quote_for_spec_chars(matches[0]);
			/* for next completion current found */
			*lastWasTab = FALSE;

			len_found = strlen(tmp);
			if (tmp[len_found-1] != '/') {
				tmp[len_found] = ' ';
				tmp[len_found+1] = '\0';
			}
		}
		len_found = strlen(tmp);
		/* have space to placed match? */
		if ((len_found - strlen(matchBuf) + command_len) < MAX_LINELEN) {
			/* before word for match   */
			command_ps[cursor - recalc_pos] = 0;
			/* save   tail line        */
			strcpy(matchBuf, command_ps + cursor);
			/* add    match            */
			strcat(command_ps, tmp);
			/* add    tail             */
			strcat(command_ps, matchBuf);
			/* back to begin word for match    */
			input_backward(recalc_pos);
			/* new pos                         */
			recalc_pos = cursor + len_found;
			/* new len                         */
			command_len = strlen(command_ps);
			/* write out the matched command   */
			redraw(cmdedit_y, command_len - recalc_pos);
		}
		free(tmp);
	} else {
		/* Ok -- the last char was a TAB.  Since they
		 * just hit TAB again, print a list of all the
		 * available choices... */
		if (matches && num_matches > 0) {
			int sav_cursor = cursor;        /* change goto_new_line() */

			/* Go to the next line */
			goto_new_line();
			showfiles();
			redraw(0, command_len - sav_cursor);
		}
	}
}

#else
#define input_tab(a) ((void)0)
#endif  /* FEATURE_COMMAND_TAB_COMPLETION */


#if MAX_HISTORY > 0

/* state->flags is already checked to be nonzero */
static void get_previous_history(void)
{
	if (command_ps[0] != '\0' || state->history[state->cur_history] == NULL) {
		free(state->history[state->cur_history]);
		state->history[state->cur_history] = xstrdup(command_ps);
	}
	state->cur_history--;
}

static int get_next_history(void)
{
	if (state->flags & DO_HISTORY) {
		int ch = state->cur_history;
		if (ch < state->cnt_history) {
			get_previous_history(); /* save the current history line */
			state->cur_history = ch + 1;
			return state->cur_history;
		}
	}
	beep();
	return 0;
}

#if ENABLE_FEATURE_EDITING_SAVEHISTORY
/* state->flags is already checked to be nonzero */
static void load_history(const char *fromfile)
{
	FILE *fp;
	int hi;

	/* cleanup old */
	for (hi = state->cnt_history; hi > 0;) {
		hi--;
		free(state->history[hi]);
	}

	fp = fopen(fromfile, "r");
	if (fp) {
		for (hi = 0; hi < MAX_HISTORY;) {
			char *hl = xmalloc_getline(fp);
			int l;

			if (!hl)
				break;
			l = strlen(hl);
			if (l >= MAX_LINELEN)
				hl[MAX_LINELEN-1] = '\0';
			if (l == 0 || hl[0] == ' ') {
				free(hl);
				continue;
			}
			state->history[hi++] = hl;
		}
		fclose(fp);
	}
	state->cur_history = state->cnt_history = hi;
}

/* state->flags is already checked to be nonzero */
static void save_history(const char *tofile)
{
	FILE *fp;

	fp = fopen(tofile, "w");
	if (fp) {
		int i;

		for (i = 0; i < state->cnt_history; i++) {
			fprintf(fp, "%s\n", state->history[i]);
		}
		fclose(fp);
	}
}
#else
#define load_history(a) ((void)0)
#define save_history(a) ((void)0)
#endif /* FEATURE_COMMAND_SAVEHISTORY */

static void remember_in_history(const char *str)
{
	int i;

	if (!(state->flags & DO_HISTORY))
		return;

	i = state->cnt_history;
	free(state->history[MAX_HISTORY]);
	state->history[MAX_HISTORY] = NULL;
	/* After max history, remove the oldest command */
	if (i >= MAX_HISTORY) {
		free(state->history[0]);
		for (i = 0; i < MAX_HISTORY-1; i++)
			state->history[i] = state->history[i+1];
	}
// Maybe "if (!i || strcmp(history[i-1], command) != 0) ..."
// (i.e. do not save dups?)
	state->history[i++] = xstrdup(str);
	state->cur_history = i;
	state->cnt_history = i;
#if ENABLE_FEATURE_EDITING_SAVEHISTORY
	if ((state->flags & SAVE_HISTORY) && state->hist_file)
		save_history(state->hist_file);
#endif
	USE_FEATURE_EDITING_FANCY_PROMPT(num_ok_lines++;)
}

#else /* MAX_HISTORY == 0 */
#define remember_in_history(a) ((void)0)
#endif /* MAX_HISTORY */


/*
 * This function is used to grab a character buffer
 * from the input file descriptor and allows you to
 * a string with full command editing (sort of like
 * a mini readline).
 *
 * The following standard commands are not implemented:
 * ESC-b -- Move back one word
 * ESC-f -- Move forward one word
 * ESC-d -- Delete back one word
 * ESC-h -- Delete forward one word
 * CTL-t -- Transpose two characters
 *
 * Minimalist vi-style command line editing available if configured.
 * vi mode implemented 2005 by Paul Fox <pgf@foxharp.boston.ma.us>
 */

#if ENABLE_FEATURE_EDITING_VI
static void
vi_Word_motion(char *command, int eat)
{
	while (cursor < command_len && !isspace(command[cursor]))
		input_forward();
	if (eat) while (cursor < command_len && isspace(command[cursor]))
		input_forward();
}

static void
vi_word_motion(char *command, int eat)
{
	if (isalnum(command[cursor]) || command[cursor] == '_') {
		while (cursor < command_len
		 && (isalnum(command[cursor+1]) || command[cursor+1] == '_'))
			input_forward();
	} else if (ispunct(command[cursor])) {
		while (cursor < command_len && ispunct(command[cursor+1]))
			input_forward();
	}

	if (cursor < command_len)
		input_forward();

	if (eat && cursor < command_len && isspace(command[cursor]))
		while (cursor < command_len && isspace(command[cursor]))
			input_forward();
}

static void
vi_End_motion(char *command)
{
	input_forward();
	while (cursor < command_len && isspace(command[cursor]))
		input_forward();
	while (cursor < command_len-1 && !isspace(command[cursor+1]))
		input_forward();
}

static void
vi_end_motion(char *command)
{
	if (cursor >= command_len-1)
		return;
	input_forward();
	while (cursor < command_len-1 && isspace(command[cursor]))
		input_forward();
	if (cursor >= command_len-1)
		return;
	if (isalnum(command[cursor]) || command[cursor] == '_') {
		while (cursor < command_len-1
		 && (isalnum(command[cursor+1]) || command[cursor+1] == '_')
		) {
			input_forward();
		}
	} else if (ispunct(command[cursor])) {
		while (cursor < command_len-1 && ispunct(command[cursor+1]))
			input_forward();
	}
}

static void
vi_Back_motion(char *command)
{
	while (cursor > 0 && isspace(command[cursor-1]))
		input_backward(1);
	while (cursor > 0 && !isspace(command[cursor-1]))
		input_backward(1);
}

static void
vi_back_motion(char *command)
{
	if (cursor <= 0)
		return;
	input_backward(1);
	while (cursor > 0 && isspace(command[cursor]))
		input_backward(1);
	if (cursor <= 0)
		return;
	if (isalnum(command[cursor]) || command[cursor] == '_') {
		while (cursor > 0
		 && (isalnum(command[cursor-1]) || command[cursor-1] == '_')
		) {
			input_backward(1);
		}
	} else if (ispunct(command[cursor])) {
		while (cursor > 0 && ispunct(command[cursor-1]))
			input_backward(1);
	}
}
#endif


/*
 * read_line_input and its helpers
 */

#if !ENABLE_FEATURE_EDITING_FANCY_PROMPT
static void parse_prompt(const char *prmt_ptr)
{
	cmdedit_prompt = prmt_ptr;
	cmdedit_prmt_len = strlen(prmt_ptr);
	put_prompt();
}
#else
static void parse_prompt(const char *prmt_ptr)
{
	int prmt_len = 0;
	size_t cur_prmt_len = 0;
	char flg_not_length = '[';
	char *prmt_mem_ptr = xzalloc(1);
	char *pwd_buf = xrealloc_getcwd_or_warn(NULL);
	char buf2[PATH_MAX + 1];
	char buf[2];
	char c;
	char *pbuf;

	cmdedit_prmt_len = 0;

	if (!pwd_buf) {
		pwd_buf = (char *)bb_msg_unknown;
	}

	while (*prmt_ptr) {
		pbuf = buf;
		pbuf[1] = 0;
		c = *prmt_ptr++;
		if (c == '\\') {
			const char *cp = prmt_ptr;
			int l;

			c = bb_process_escape_sequence(&prmt_ptr);
			if (prmt_ptr == cp) {
				if (*cp == 0)
					break;
				c = *prmt_ptr++;
				switch (c) {
#if ENABLE_FEATURE_GETUSERNAME_AND_HOMEDIR
				case 'u':
					pbuf = user_buf;
					break;
#endif
				case 'h':
					pbuf = hostname_buf;
					if (!pbuf) {
						pbuf = xzalloc(256);
						if (gethostname(pbuf, 255) < 0) {
							strcpy(pbuf, "?");
						} else {
							char *s = strchr(pbuf, '.');
							if (s)
								*s = '\0';
						}
						hostname_buf = pbuf;
					}
					break;
				case '$':
					c = (geteuid() == 0 ? '#' : '$');
					break;
#if ENABLE_FEATURE_GETUSERNAME_AND_HOMEDIR
				case 'w':
					pbuf = pwd_buf;
					l = strlen(home_pwd_buf);
					if (home_pwd_buf[0] != 0
					 && strncmp(home_pwd_buf, pbuf, l) == 0
					 && (pbuf[l]=='/' || pbuf[l]=='\0')
					 && strlen(pwd_buf+l)<PATH_MAX
					) {
						pbuf = buf2;
						*pbuf = '~';
						strcpy(pbuf+1, pwd_buf+l);
					}
					break;
#endif
				case 'W':
					pbuf = pwd_buf;
					cp = strrchr(pbuf, '/');
					if (cp != NULL && cp != pbuf)
						pbuf += (cp-pbuf) + 1;
					break;
				case '!':
					pbuf = buf2;
					snprintf(buf2, sizeof(buf2), "%d", num_ok_lines);
					break;
				case 'e': case 'E':     /* \e \E = \033 */
					c = '\033';
					break;
				case 'x': case 'X':
					for (l = 0; l < 3;) {
						int h;
						buf2[l++] = *prmt_ptr;
						buf2[l] = 0;
						h = strtol(buf2, &pbuf, 16);
						if (h > UCHAR_MAX || (pbuf - buf2) < l) {
							l--;
							break;
						}
						prmt_ptr++;
					}
					buf2[l] = 0;
					c = (char)strtol(buf2, NULL, 16);
					if (c == 0)
						c = '?';
					pbuf = buf;
					break;
				case '[': case ']':
					if (c == flg_not_length) {
						flg_not_length = flg_not_length == '[' ? ']' : '[';
						continue;
					}
					break;
				}
			}
		}
		if (pbuf == buf)
			*pbuf = c;
		cur_prmt_len = strlen(pbuf);
		prmt_len += cur_prmt_len;
		if (flg_not_length != ']')
			cmdedit_prmt_len += cur_prmt_len;
		prmt_mem_ptr = strcat(xrealloc(prmt_mem_ptr, prmt_len+1), pbuf);
	}
	if (pwd_buf != (char *)bb_msg_unknown)
		free(pwd_buf);
	cmdedit_prompt = prmt_mem_ptr;
	put_prompt();
}
#endif

#define setTermSettings(fd, argp) tcsetattr(fd, TCSANOW, argp)
#define getTermSettings(fd, argp) tcgetattr(fd, argp);

static sighandler_t previous_SIGWINCH_handler;

static void cmdedit_setwidth(unsigned w, int redraw_flg)
{
	cmdedit_termw = w;
	if (redraw_flg) {
		/* new y for current cursor */
		int new_y = (cursor + cmdedit_prmt_len) / w;
		/* redraw */
		redraw((new_y >= cmdedit_y ? new_y : cmdedit_y), command_len - cursor);
		fflush(stdout);
	}
}

static void win_changed(int nsig)
{
	int width;
	get_terminal_width_height(0, &width, NULL);
	cmdedit_setwidth(width, nsig /* - just a yes/no flag */);
	if (nsig == SIGWINCH)
		signal(SIGWINCH, win_changed); /* rearm ourself */
}

/*
 * The emacs and vi modes share much of the code in the big
 * command loop.  Commands entered when in vi's command mode (aka
 * "escape mode") get an extra bit added to distinguish them --
 * this keeps them from being self-inserted.  This clutters the
 * big switch a bit, but keeps all the code in one place.
 */

#define vbit 0x100

/* leave out the "vi-mode"-only case labels if vi editing isn't
 * configured. */
#define vi_case(caselabel) USE_FEATURE_EDITING(case caselabel)

/* convert uppercase ascii to equivalent control char, for readability */
#undef CTRL
#define CTRL(a) ((a) & ~0x40)

int read_line_input(const char* prompt, char* command, int maxsize, line_input_t *st)
{
	int lastWasTab = FALSE;
	unsigned int ic;
	unsigned char c;
	smallint break_out = 0;
#if ENABLE_FEATURE_EDITING_VI
	smallint vi_cmdmode = 0;
	smalluint prevc;
#endif

// FIXME: audit & improve this
	if (maxsize > MAX_LINELEN)
		maxsize = MAX_LINELEN;

	/* With null flags, no other fields are ever used */
	state = st ? st : (line_input_t*) &const_int_0;
#if ENABLE_FEATURE_EDITING_SAVEHISTORY
	if ((state->flags & SAVE_HISTORY) && state->hist_file)
		load_history(state->hist_file);
#endif

	/* prepare before init handlers */
	cmdedit_y = 0;  /* quasireal y, not true if line > xt*yt */
	command_len = 0;
	command_ps = command;
	command[0] = '\0';

	getTermSettings(0, (void *) &initial_settings);
	memcpy(&new_settings, &initial_settings, sizeof(new_settings));
	new_settings.c_lflag &= ~ICANON;        /* unbuffered input */
	/* Turn off echoing and CTRL-C, so we can trap it */
	new_settings.c_lflag &= ~(ECHO | ECHONL | ISIG);
	/* Hmm, in linux c_cc[] is not parsed if ICANON is off */
	new_settings.c_cc[VMIN] = 1;
	new_settings.c_cc[VTIME] = 0;
	/* Turn off CTRL-C, so we can trap it */
#ifndef _POSIX_VDISABLE
#define _POSIX_VDISABLE '\0'
#endif
	new_settings.c_cc[VINTR] = _POSIX_VDISABLE;
	setTermSettings(0, (void *) &new_settings);

	/* Now initialize things */
	previous_SIGWINCH_handler = signal(SIGWINCH, win_changed);
	win_changed(0); /* do initial resizing */
#if ENABLE_FEATURE_GETUSERNAME_AND_HOMEDIR
	{
		struct passwd *entry;

		entry = getpwuid(geteuid());
		if (entry) {
			user_buf = xstrdup(entry->pw_name);
			home_pwd_buf = xstrdup(entry->pw_dir);
		}
	}
#endif
	/* Print out the command prompt */
	parse_prompt(prompt);

	while (1) {
		fflush(stdout);

		if (safe_read(0, &c, 1) < 1) {
			/* if we can't read input then exit */
			goto prepare_to_die;
		}

		ic = c;

#if ENABLE_FEATURE_EDITING_VI
		newdelflag = 1;
		if (vi_cmdmode)
			ic |= vbit;
#endif
		switch (ic) {
		case '\n':
		case '\r':
		vi_case('\n'|vbit:)
		vi_case('\r'|vbit:)
			/* Enter */
			goto_new_line();
			break_out = 1;
			break;
#if ENABLE_FEATURE_EDITING_FANCY_KEYS
		case CTRL('A'):
		vi_case('0'|vbit:)
			/* Control-a -- Beginning of line */
			input_backward(cursor);
			break;
		case CTRL('B'):
		vi_case('h'|vbit:)
		vi_case('\b'|vbit:)
		vi_case('\x7f'|vbit:) /* DEL */
			/* Control-b -- Move back one character */
			input_backward(1);
			break;
#endif
		case CTRL('C'):
		vi_case(CTRL('C')|vbit:)
			/* Control-c -- stop gathering input */
			goto_new_line();
			command_len = 0;
			break_out = -1; /* "do not append '\n'" */
			break;
		case CTRL('D'):
			/* Control-d -- Delete one character, or exit
			 * if the len=0 and no chars to delete */
			if (command_len == 0) {
				errno = 0;
 prepare_to_die:
				/* to control stopped jobs */
				break_out = command_len = -1;
				break;
			}
			input_delete(0);
			break;

#if ENABLE_FEATURE_EDITING_FANCY_KEYS
		case CTRL('E'):
		vi_case('$'|vbit:)
			/* Control-e -- End of line */
			input_end();
			break;
		case CTRL('F'):
		vi_case('l'|vbit:)
		vi_case(' '|vbit:)
			/* Control-f -- Move forward one character */
			input_forward();
			break;
#endif

		case '\b':
		case '\x7f': /* DEL */
			/* Control-h and DEL */
			input_backspace();
			break;

		case '\t':
			input_tab(&lastWasTab);
			break;

#if ENABLE_FEATURE_EDITING_FANCY_KEYS
		case CTRL('K'):
			/* Control-k -- clear to end of line */
			command[cursor] = 0;
			command_len = cursor;
			printf("\033[J");
			break;
		case CTRL('L'):
		vi_case(CTRL('L')|vbit:)
			/* Control-l -- clear screen */
			printf("\033[H");
			redraw(0, command_len - cursor);
			break;
#endif

#if MAX_HISTORY > 0
		case CTRL('N'):
		vi_case(CTRL('N')|vbit:)
		vi_case('j'|vbit:)
			/* Control-n -- Get next command in history */
			if (get_next_history())
				goto rewrite_line;
			break;
		case CTRL('P'):
		vi_case(CTRL('P')|vbit:)
		vi_case('k'|vbit:)
			/* Control-p -- Get previous command from history */
			if ((state->flags & DO_HISTORY) && state->cur_history > 0) {
				get_previous_history();
				goto rewrite_line;
			}
			beep();
			break;
#endif

#if ENABLE_FEATURE_EDITING_FANCY_KEYS
		case CTRL('U'):
		vi_case(CTRL('U')|vbit:)
			/* Control-U -- Clear line before cursor */
			if (cursor) {
				strcpy(command, command + cursor);
				command_len -= cursor;
				redraw(cmdedit_y, command_len);
			}
			break;
#endif
		case CTRL('W'):
		vi_case(CTRL('W')|vbit:)
			/* Control-W -- Remove the last word */
			while (cursor > 0 && isspace(command[cursor-1]))
				input_backspace();
			while (cursor > 0 && !isspace(command[cursor-1]))
				input_backspace();
			break;

#if ENABLE_FEATURE_EDITING_VI
		case 'i'|vbit:
			vi_cmdmode = 0;
			break;
		case 'I'|vbit:
			input_backward(cursor);
			vi_cmdmode = 0;
			break;
		case 'a'|vbit:
			input_forward();
			vi_cmdmode = 0;
			break;
		case 'A'|vbit:
			input_end();
			vi_cmdmode = 0;
			break;
		case 'x'|vbit:
			input_delete(1);
			break;
		case 'X'|vbit:
			if (cursor > 0) {
				input_backward(1);
				input_delete(1);
			}
			break;
		case 'W'|vbit:
			vi_Word_motion(command, 1);
			break;
		case 'w'|vbit:
			vi_word_motion(command, 1);
			break;
		case 'E'|vbit:
			vi_End_motion(command);
			break;
		case 'e'|vbit:
			vi_end_motion(command);
			break;
		case 'B'|vbit:
			vi_Back_motion(command);
			break;
		case 'b'|vbit:
			vi_back_motion(command);
			break;
		case 'C'|vbit:
			vi_cmdmode = 0;
			/* fall through */
		case 'D'|vbit:
			goto clear_to_eol;

		case 'c'|vbit:
			vi_cmdmode = 0;
			/* fall through */
		case 'd'|vbit: {
			int nc, sc;
			sc = cursor;
			prevc = ic;
			if (safe_read(0, &c, 1) < 1)
				goto prepare_to_die;
			if (c == (prevc & 0xff)) {
				/* "cc", "dd" */
				input_backward(cursor);
				goto clear_to_eol;
				break;
			}
			switch (c) {
			case 'w':
			case 'W':
			case 'e':
			case 'E':
				switch (c) {
				case 'w':   /* "dw", "cw" */
					vi_word_motion(command, vi_cmdmode);
					break;
				case 'W':   /* 'dW', 'cW' */
					vi_Word_motion(command, vi_cmdmode);
					break;
				case 'e':   /* 'de', 'ce' */
					vi_end_motion(command);
					input_forward();
					break;
				case 'E':   /* 'dE', 'cE' */
					vi_End_motion(command);
					input_forward();
					break;
				}
				nc = cursor;
				input_backward(cursor - sc);
				while (nc-- > cursor)
					input_delete(1);
				break;
			case 'b':  /* "db", "cb" */
			case 'B':  /* implemented as B */
				if (c == 'b')
					vi_back_motion(command);
				else
					vi_Back_motion(command);
				while (sc-- > cursor)
					input_delete(1);
				break;
			case ' ':  /* "d ", "c " */
				input_delete(1);
				break;
			case '$':  /* "d$", "c$" */
			clear_to_eol:
				while (cursor < command_len)
					input_delete(1);
				break;
			}
			break;
		}
		case 'p'|vbit:
			input_forward();
			/* fallthrough */
		case 'P'|vbit:
			put();
			break;
		case 'r'|vbit:
			if (safe_read(0, &c, 1) < 1)
				goto prepare_to_die;
			if (c == 0)
				beep();
			else {
				*(command + cursor) = c;
				putchar(c);
				putchar('\b');
			}
			break;
#endif /* FEATURE_COMMAND_EDITING_VI */

		case '\x1b': /* ESC */

#if ENABLE_FEATURE_EDITING_VI
			if (state->flags & VI_MODE) {
				/* ESC: insert mode --> command mode */
				vi_cmdmode = 1;
				input_backward(1);
				break;
			}
#endif
			/* escape sequence follows */
			if (safe_read(0, &c, 1) < 1)
				goto prepare_to_die;
			/* different vt100 emulations */
			if (c == '[' || c == 'O') {
		vi_case('['|vbit:)
		vi_case('O'|vbit:)
				if (safe_read(0, &c, 1) < 1)
					goto prepare_to_die;
			}
			if (c >= '1' && c <= '9') {
				unsigned char dummy;

				if (safe_read(0, &dummy, 1) < 1)
					goto prepare_to_die;
				if (dummy != '~')
					c = '\0';
			}

			switch (c) {
#if ENABLE_FEATURE_TAB_COMPLETION
			case '\t':                      /* Alt-Tab */
				input_tab(&lastWasTab);
				break;
#endif
#if MAX_HISTORY > 0
			case 'A':
				/* Up Arrow -- Get previous command from history */
				if ((state->flags & DO_HISTORY) && state->cur_history > 0) {
					get_previous_history();
					goto rewrite_line;
				}
				beep();
				break;
			case 'B':
				/* Down Arrow -- Get next command in history */
				if (!get_next_history())
					break;
 rewrite_line:
				/* Rewrite the line with the selected history item */
				/* change command */
				command_len = strlen(strcpy(command, state->history[state->cur_history]));
				/* redraw and go to eol (bol, in vi */
				redraw(cmdedit_y, (state->flags & VI_MODE) ? 9999 : 0);
				break;
#endif
			case 'C':
				/* Right Arrow -- Move forward one character */
				input_forward();
				break;
			case 'D':
				/* Left Arrow -- Move back one character */
				input_backward(1);
				break;
			case '3':
				/* Delete */
				input_delete(0);
				break;
			case '1': // vt100? linux vt? or what?
			case '7': // vt100? linux vt? or what?
			case 'H': /* xterm's <Home> */
				input_backward(cursor);
				break;
			case '4': // vt100? linux vt? or what?
			case '8': // vt100? linux vt? or what?
			case 'F': /* xterm's <End> */
				input_end();
				break;
			default:
				c = '\0';
				beep();
			}
			break;

		default:        /* If it's regular input, do the normal thing */
#if ENABLE_FEATURE_NONPRINTABLE_INVERSE_PUT
			/* Control-V -- Add non-printable symbol */
			if (c == CTRL('V')) {
				if (safe_read(0, &c, 1) < 1)
					goto prepare_to_die;
				if (c == 0) {
					beep();
					break;
				}
			} else
#endif

#if ENABLE_FEATURE_EDITING_VI
			if (vi_cmdmode)  /* Don't self-insert */
				break;
#endif
			if (!Isprint(c)) /* Skip non-printable characters */
				break;

			if (command_len >= (maxsize - 2))        /* Need to leave space for enter */
				break;

			command_len++;
			if (cursor == (command_len - 1)) {      /* Append if at the end of the line */
				command[cursor] = c;
				command[cursor+1] = '\0';
				cmdedit_set_out_char(' ');
			} else {                        /* Insert otherwise */
				int sc = cursor;

				memmove(command + sc + 1, command + sc, command_len - sc);
				command[sc] = c;
				sc++;
				/* rewrite from cursor */
				input_end();
				/* to prev x pos + 1 */
				input_backward(cursor - sc);
			}
			break;
		}
		if (break_out)                  /* Enter is the command terminator, no more input. */
			break;

		if (c != '\t')
			lastWasTab = FALSE;
	}

	if (command_len > 0)
		remember_in_history(command);

	if (break_out > 0) {
		command[command_len++] = '\n';
		command[command_len] = '\0';
	}

#if ENABLE_FEATURE_CLEAN_UP && ENABLE_FEATURE_TAB_COMPLETION
	free_tab_completion_data();
#endif

#if ENABLE_FEATURE_EDITING_FANCY_PROMPT
	free((char*)cmdedit_prompt);
#endif
	/* restore initial_settings */
	setTermSettings(STDIN_FILENO, (void *) &initial_settings);
	/* restore SIGWINCH handler */
	signal(SIGWINCH, previous_SIGWINCH_handler);
	fflush(stdout);
	return command_len;
}

line_input_t *new_line_input_t(int flags)
{
	line_input_t *n = xzalloc(sizeof(*n));
	n->flags = flags;
	return n;
}

#else

#undef read_line_input
int read_line_input(const char* prompt, char* command, int maxsize)
{
	fputs(prompt, stdout);
	fflush(stdout);
	fgets(command, maxsize, stdin);
	return strlen(command);
}

#endif  /* FEATURE_COMMAND_EDITING */


/*
 * Testing
 */

#ifdef TEST

#include <locale.h>

const char *applet_name = "debug stuff usage";

int main(int argc, char **argv)
{
	char buff[MAX_LINELEN];
	char *prompt =
#if ENABLE_FEATURE_EDITING_FANCY_PROMPT
		"\\[\\033[32;1m\\]\\u@\\[\\x1b[33;1m\\]\\h:"
		"\\[\\033[34;1m\\]\\w\\[\\033[35;1m\\] "
		"\\!\\[\\e[36;1m\\]\\$ \\[\\E[0m\\]";
#else
		"% ";
#endif

#if ENABLE_FEATURE_NONPRINTABLE_INVERSE_PUT
	setlocale(LC_ALL, "");
#endif
	while (1) {
		int l;
		l = read_line_input(prompt, buff);
		if (l <= 0 || buff[l-1] != '\n')
			break;
		buff[l-1] = 0;
		printf("*** read_line_input() returned line =%s=\n", buff);
	}
	printf("*** read_line_input() detect ^D\n");
	return 0;
}

#endif  /* TEST */
