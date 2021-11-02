/* vi: set sw=4 ts=4: */
/*
 * Mini expr implementation for busybox
 *
 * based on GNU expr Mike Parker.
 * Copyright (C) 86, 1991-1997, 1999 Free Software Foundation, Inc.
 *
 * Busybox modifications
 * Copyright (c) 2000  Edward Betts <edward@debian.org>.
 * Copyright (C) 2003-2005  Vladimir Oleynik <dzo@simtreas.ru>
 *  - reduced 464 bytes.
 *  - 64 math support
 *
 * Licensed under GPLv2 or later, see file LICENSE in this tarball for details.
 */

/* This program evaluates expressions.  Each token (operator, operand,
 * parenthesis) of the expression must be a separate argument.  The
 * parser used is a reasonably general one, though any incarnation of
 * it is language-specific.  It is especially nice for expressions.
 *
 * No parse tree is needed; a new node is evaluated immediately.
 * One function can handle multiple operators all of equal precedence,
 * provided they all associate ((x op x) op x). */

/* no getopt needed */

#include "libbb.h"
#include "xregex.h"

/* The kinds of value we can have.  */
enum valtype {
	integer,
	string
};
typedef enum valtype TYPE;

#if ENABLE_EXPR_MATH_SUPPORT_64
typedef int64_t arith_t;

#define PF_REZ      "ll"
#define PF_REZ_TYPE (long long)
#define STRTOL(s, e, b) strtoll(s, e, b)
#else
typedef long arith_t;

#define PF_REZ      "l"
#define PF_REZ_TYPE (long)
#define STRTOL(s, e, b) strtol(s, e, b)
#endif

/* TODO: use bb_strtol[l]? It's easier to check for errors... */

/* A value is.... */
struct valinfo {
	TYPE type;			/* Which kind. */
	union {				/* The value itself. */
		arith_t i;
		char *s;
	} u;
};
typedef struct valinfo VALUE;

/* The arguments given to the program, minus the program name.  */
struct globals {
	char **args;
};
#define G (*(struct globals*)&bb_common_bufsiz1)

/* forward declarations */
static VALUE *eval(void);


/* Return a VALUE for I.  */

static VALUE *int_value(arith_t i)
{
	VALUE *v;

	v = xmalloc(sizeof(VALUE));
	v->type = integer;
	v->u.i = i;
	return v;
}

/* Return a VALUE for S.  */

static VALUE *str_value(const char *s)
{
	VALUE *v;

	v = xmalloc(sizeof(VALUE));
	v->type = string;
	v->u.s = xstrdup(s);
	return v;
}

/* Free VALUE V, including structure components.  */

static void freev(VALUE * v)
{
	if (v->type == string)
		free(v->u.s);
	free(v);
}

/* Return nonzero if V is a null-string or zero-number.  */

static int null(VALUE * v)
{
	if (v->type == integer)
		return v->u.i == 0;
	/* string: */
	return v->u.s[0] == '\0' || LONE_CHAR(v->u.s, '0');
}

/* Coerce V to a string value (can't fail).  */

static void tostring(VALUE * v)
{
	if (v->type == integer) {
		v->u.s = xasprintf("%" PF_REZ "d", PF_REZ_TYPE v->u.i);
		v->type = string;
	}
}

/* Coerce V to an integer value.  Return 1 on success, 0 on failure.  */

static bool toarith(VALUE * v)
{
	if (v->type == string) {
		arith_t i;
		char *e;

		/* Don't interpret the empty string as an integer.  */
		/* Currently does not worry about overflow or int/long differences. */
		i = STRTOL(v->u.s, &e, 10);
		if ((v->u.s == e) || *e)
			return 0;
		free(v->u.s);
		v->u.i = i;
		v->type = integer;
	}
	return 1;
}

/* Return nonzero if the next token matches STR exactly.
   STR must not be NULL.  */

static bool nextarg(const char *str)
{
	if (*G.args == NULL)
		return 0;
	return strcmp(*G.args, str) == 0;
}

/* The comparison operator handling functions.  */

static int cmp_common(VALUE * l, VALUE * r, int op)
{
	int cmpval;

	if (l->type == string || r->type == string) {
		tostring(l);
		tostring(r);
		cmpval = strcmp(l->u.s, r->u.s);
	} else
		cmpval = l->u.i - r->u.i;
	if (op == '<')
		return cmpval < 0;
	if (op == ('L' + 'E'))
		return cmpval <= 0;
	if (op == '=')
		return cmpval == 0;
	if (op == '!')
		return cmpval != 0;
	if (op == '>')
		return cmpval > 0;
	/* >= */
	return cmpval >= 0;
}

/* The arithmetic operator handling functions.  */

static arith_t arithmetic_common(VALUE * l, VALUE * r, int op)
{
	arith_t li, ri;

	if (!toarith(l) || !toarith(r))
		bb_error_msg_and_die("non-numeric argument");
	li = l->u.i;
	ri = r->u.i;
	if ((op == '/' || op == '%') && ri == 0)
		bb_error_msg_and_die("division by zero");
	if (op == '+')
		return li + ri;
	else if (op == '-')
		return li - ri;
	else if (op == '*')
		return li * ri;
	else if (op == '/')
		return li / ri;
	else
		return li % ri;
}

/* Do the : operator.
   SV is the VALUE for the lhs (the string),
   PV is the VALUE for the rhs (the pattern).  */

static VALUE *docolon(VALUE * sv, VALUE * pv)
{
	VALUE *v;
	regex_t re_buffer;
	const int NMATCH = 2;
	regmatch_t re_regs[NMATCH];

	tostring(sv);
	tostring(pv);

	if (pv->u.s[0] == '^') {
		bb_error_msg("\
warning: unportable BRE: `%s': using `^' as the first character\n\
of a basic regular expression is not portable; it is being ignored", pv->u.s);
	}

	memset(&re_buffer, 0, sizeof(re_buffer));
	memset(re_regs, 0, sizeof(*re_regs));
	xregcomp(&re_buffer, pv->u.s, 0);

	/* expr uses an anchored pattern match, so check that there was a
	 * match and that the match starts at offset 0. */
	if (regexec(&re_buffer, sv->u.s, NMATCH, re_regs, 0) != REG_NOMATCH &&
		re_regs[0].rm_so == 0) {
		/* Were \(...\) used? */
		if (re_buffer.re_nsub > 0) {
			sv->u.s[re_regs[1].rm_eo] = '\0';
			v = str_value(sv->u.s + re_regs[1].rm_so);
		} else
			v = int_value(re_regs[0].rm_eo);
	} else {
		/* Match failed -- return the right kind of null.  */
		if (re_buffer.re_nsub > 0)
			v = str_value("");
		else
			v = int_value(0);
	}
//FIXME: sounds like here is a bit missing: regfree(&re_buffer);
	return v;
}

/* Handle bare operands and ( expr ) syntax.  */

static VALUE *eval7(void)
{
	VALUE *v;

	if (!*G.args)
		bb_error_msg_and_die("syntax error");

	if (nextarg("(")) {
		G.args++;
		v = eval();
		if (!nextarg(")"))
			bb_error_msg_and_die("syntax error");
		G.args++;
		return v;
	}

	if (nextarg(")"))
		bb_error_msg_and_die("syntax error");

	return str_value(*G.args++);
}

/* Handle match, substr, index, length, and quote keywords.  */

static VALUE *eval6(void)
{
	static const char keywords[] ALIGN1 =
		"quote\0""length\0""match\0""index\0""substr\0";

	VALUE *r, *i1, *i2;
	VALUE *l = l; /* silence gcc */
	VALUE *v = v; /* silence gcc */
	int key = *G.args ? index_in_strings(keywords, *G.args) + 1 : 0;

	if (key == 0) /* not a keyword */
		return eval7();
	G.args++; /* We have a valid token, so get the next argument.  */
	if (key == 1) { /* quote */
		if (!*G.args)
			bb_error_msg_and_die("syntax error");
		return str_value(*G.args++);
	}
	if (key == 2) { /* length */
		r = eval6();
		tostring(r);
		v = int_value(strlen(r->u.s));
		freev(r);
	} else
		l = eval6();

	if (key == 3) { /* match */
		r = eval6();
		v = docolon(l, r);
		freev(l);
		freev(r);
	}
	if (key == 4) { /* index */
		r = eval6();
		tostring(l);
		tostring(r);
		v = int_value(strcspn(l->u.s, r->u.s) + 1);
		if (v->u.i == (arith_t) strlen(l->u.s) + 1)
			v->u.i = 0;
		freev(l);
		freev(r);
	}
	if (key == 5) { /* substr */
		i1 = eval6();
		i2 = eval6();
		tostring(l);
		if (!toarith(i1) || !toarith(i2)
		 || i1->u.i > (arith_t) strlen(l->u.s)
		 || i1->u.i <= 0 || i2->u.i <= 0)
			v = str_value("");
		else {
			v = xmalloc(sizeof(VALUE));
			v->type = string;
			v->u.s = xstrndup(l->u.s + i1->u.i - 1, i2->u.i);
		}
		freev(l);
		freev(i1);
		freev(i2);
	}
	return v;

}

/* Handle : operator (pattern matching).
   Calls docolon to do the real work.  */

static VALUE *eval5(void)
{
	VALUE *l, *r, *v;

	l = eval6();
	while (nextarg(":")) {
		G.args++;
		r = eval6();
		v = docolon(l, r);
		freev(l);
		freev(r);
		l = v;
	}
	return l;
}

/* Handle *, /, % operators.  */

static VALUE *eval4(void)
{
	VALUE *l, *r;
	int op;
	arith_t val;

	l = eval5();
	while (1) {
		if (nextarg("*"))
			op = '*';
		else if (nextarg("/"))
			op = '/';
		else if (nextarg("%"))
			op = '%';
		else
			return l;
		G.args++;
		r = eval5();
		val = arithmetic_common(l, r, op);
		freev(l);
		freev(r);
		l = int_value(val);
	}
}

/* Handle +, - operators.  */

static VALUE *eval3(void)
{
	VALUE *l, *r;
	int op;
	arith_t val;

	l = eval4();
	while (1) {
		if (nextarg("+"))
			op = '+';
		else if (nextarg("-"))
			op = '-';
		else
			return l;
		G.args++;
		r = eval4();
		val = arithmetic_common(l, r, op);
		freev(l);
		freev(r);
		l = int_value(val);
	}
}

/* Handle comparisons.  */

static VALUE *eval2(void)
{
	VALUE *l, *r;
	int op;
	arith_t val;

	l = eval3();
	while (1) {
		if (nextarg("<"))
			op = '<';
		else if (nextarg("<="))
			op = 'L' + 'E';
		else if (nextarg("=") || nextarg("=="))
			op = '=';
		else if (nextarg("!="))
			op = '!';
		else if (nextarg(">="))
			op = 'G' + 'E';
		else if (nextarg(">"))
			op = '>';
		else
			return l;
		G.args++;
		r = eval3();
		toarith(l);
		toarith(r);
		val = cmp_common(l, r, op);
		freev(l);
		freev(r);
		l = int_value(val);
	}
}

/* Handle &.  */

static VALUE *eval1(void)
{
	VALUE *l, *r;

	l = eval2();
	while (nextarg("&")) {
		G.args++;
		r = eval2();
		if (null(l) || null(r)) {
			freev(l);
			freev(r);
			l = int_value(0);
		} else
			freev(r);
	}
	return l;
}

/* Handle |.  */

static VALUE *eval(void)
{
	VALUE *l, *r;

	l = eval1();
	while (nextarg("|")) {
		G.args++;
		r = eval1();
		if (null(l)) {
			freev(l);
			l = r;
		} else
			freev(r);
	}
	return l;
}

int expr_main(int argc, char **argv);
int expr_main(int argc, char **argv)
{
	VALUE *v;

	if (argc == 1) {
		bb_error_msg_and_die("too few arguments");
	}

	G.args = argv + 1;

	v = eval();
	if (*G.args)
		bb_error_msg_and_die("syntax error");

	if (v->type == integer)
		printf("%" PF_REZ "d\n", PF_REZ_TYPE v->u.i);
	else
		puts(v->u.s);

	fflush_stdout_and_exit(null(v));
}
