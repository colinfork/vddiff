/*
Copyright (c) 2016, Carsten Kunze <carsten.kunze@arcor.de>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.
*/

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/wait.h>
#include <signal.h>
#include "compat.h"
#include "main.h"
#include "ui.h"
#include "exec.h"
#include "uzp.h"
#include "db.h"
#include "diff.h"
#include "fs.h"

const char *const vimdiff  = "vim -dR --";
const char *const diffless = "diff -- $1 $2 | less";

static size_t add_path(char *, size_t, char *, char *);
static struct strlst *addarg(char *);
static void exec_tool(struct tool *, char *, char *, int);
static void sig_child(int);
static int shell_char(int);

struct tool difftool;
struct tool viewtool;
char *ishell;
char *nishell;
bool wait_after_exec;

void
tool(char *name, char *rnam, int tree, int ign_ext)
{
	size_t l;
	char *cmd;
	struct tool *tmptool = NULL;
	short skipped = 0;
	int c;

	l = strlen(name);
	cmd = lbuf + sizeof lbuf;
	*--cmd = 0;

	if (tree == 3 || ign_ext)
		goto settool;

	while (l) {
		*--cmd = tolower((int)name[--l]);

		if (!skipped && *cmd == '.' &&
		    !str_db_srch(&skipext_db, cmd + 1
#ifdef HAVE_LIBAVLBST
		    , NULL
#endif
		    )) {
			*cmd = 0;
			skipped = 1;
		}

		if (cmd == lbuf)
			break;
	}

	while ((c = *cmd++)) {
		if (c == '.' && *cmd && (tmptool = db_srch_ext(cmd)))
			break;
	}

	if (!tmptool)
settool:
		tmptool = tree == 3 && !ign_ext ? &difftool : &viewtool;

	if (tmptool->flags & TOOL_SHELL) {
		cmd = exec_mk_cmd(tmptool, name, rnam, tree);
		exec_cmd(&cmd, tmptool->flags | TOOL_TTY, NULL, NULL);
		free(cmd);
	} else
		exec_tool(tmptool, name, rnam, tree);
}

#define GRWCMD \
	do { \
		if (csiz - clen < PATHSIZ) { \
			csiz *= 2; \
			cmd = realloc(cmd, csiz); \
		} \
	} while (0)

char *
exec_mk_cmd(struct tool *tmptool, char *name, char *rnam, int tree)
{
	size_t csiz, clen, l;
	char *cmd, *s;
	struct strlst *args;

	if (!rnam)
		rnam = name;

	csiz = 2 * PATHSIZ;
	cmd = malloc(csiz);
	clen = strlen(s = tmptool->tool);
	memcpy(cmd, s, clen);
	lpath[llen] = 0; /* in add_path() used without len */

	if (!bmode)
		rpath[rlen] = 0;

	if (tmptool->flags & TOOL_NOARG) {
	} else if ((args = tmptool->args)) {
		do {
			s = args->str;
			GRWCMD;

			switch (*s) {
			case '1':
				clen = add_path(cmd, clen, lpath, name);
				break;
			case '2':
				clen = add_path(cmd, clen, rpath, rnam);
				break;
			}

			if ((l = strlen(s) - 1)) {
				GRWCMD;
				memcpy(cmd + clen, s + 1, l);
				clen += l;
			}
		} while ((args = args->next));
	} else {
		if (tree & 1) {
			GRWCMD;
			clen = add_path(cmd, clen, lpath, name);
		}

		if (tree & 2) {
			GRWCMD;
			clen = add_path(cmd, clen, rpath, rnam);
		}
	}

	cmd[clen] = 0;
	return cmd;
}

static size_t
add_path(char *cmd, size_t l0, char *path, char *name)
{
	size_t l;

	if (!bmode && *name != '/') {
		l = shell_quote(lbuf, path, sizeof lbuf);
		memcpy(cmd + l0, lbuf, l);
		l0 += l;
		cmd[l0++] = '/';
	}

	l = shell_quote(lbuf, name, sizeof lbuf);
	memcpy(cmd + l0, lbuf, l);
	l0 += l;
	return l0;
}

void
free_tool(struct tool *t)
{
	struct strlst *p1, *p2;

	free(t->tool);
	p1 = t->args;

	while (p1) {
		p2 = p1->next;
		free(p1);
		p1 = p2;
	}
}

void
set_tool(struct tool *_tool, char *s, tool_flags_t flags)
{
	char *b;
	int c;
	struct strlst **next, *args;
	bool sh = FALSE;

	free_tool(_tool);
	_tool->tool = b = s;
	_tool->flags = flags;
	next = &_tool->args;

	while ((c = *s)) {
		if (c == '$') {
			sh = TRUE;

			if (s[1] == '1' || s[1] == '2') {
				*s++ = 0;
				args = addarg(s);
				*next = args;
				next = &args->next;
			}
		} else if (!sh && shell_char(c))
			sh = TRUE;

		s++;
	}

	while (--s >= b && isblank((int)*s))
		*s = 0;

	if (*s == '#' && (s == b || isblank((int)s[-1]))) {
		_tool->flags |= TOOL_NOARG;

		while (--s >= b && isblank((int)*s))
			*s = 0;
	}

	if (sh)
		_tool->flags |= TOOL_SHELL;
}

static struct strlst *
addarg(char *s)
{
	struct strlst *p;

	p = malloc(sizeof(struct strlst));
	p->str = s;
	p->next = NULL;
	return p;
}

static int
shell_char(int c)
{
	static const char s[] = "|&;<>()`\\\"'[#~";
	int sc;
	const char *p;

	for (p = s; (sc = *p++); )
		if (c == sc)
			return 1;

	return 0;
}

static void
exec_tool(struct tool *t, char *name, char *rnam, int tree)
{
	int o, c;
	char *s, **a, **av;
	int status;
	int zipped = 0;

	if (!rnam)
		rnam = name;

	s = t->tool;
	o = 0;
	while (1) {
		while ((c = *s++) && !isblank(c));
		if (!c) break;
		while ((c = *s++) &&  isblank(c));
		if (!c) break;
		o++;
	}

	a = av = malloc((1 + o + (tree == 3 ? 2 : 1) + 1) * sizeof(*av));

	if (!o) {
		*a++ = t->tool;
	} else {
		*a++ = s = strdup(t->tool);

		while (1) {
			while ((c = *s++) && !isblank(c));
			if (!c) break;
			s[-1] = 0;
			while ((c = *s++) &&  isblank(c));
			if (!c) break;
			*a++ = s - 1;
		}
	}

	if (tree & 1) {
		if ((zipped = *name == '/') || bmode) {
			*a++ = name;
		} else {
			pthcat(lpath, llen, name);
			*a++ = lpath;
		}
	}

	if (tree & 2) {
		if ((zipped |= *rnam == '/') || bmode) {
			*a++ = rnam;
		} else {
			pthcat(rpath, rlen, rnam);
			*a++ = rpath;
		}
	}


	if (zipped)
		t->flags &= ~TOOL_BG;

	*a = NULL;
	status = exec_cmd(av, t->flags | TOOL_TTY, NULL, NULL);

	if (o)
		free(*av);

	free(av);

	if (WIFEXITED(status) && WEXITSTATUS(status) == 77 &&
	    !strcmp(t->tool, vimdiff)) {
		set_tool(&difftool, strdup(diffless), 0);
		tool(name, rnam, tree, 0);
	}
}

int
exec_cmd(char **av, tool_flags_t flags, char *path, char *msg)
{
	pid_t pid;
	int status = 0;
	char prompt[] = "Type <ENTER> to continue ";

	erase();
	refresh();
	def_prog_mode();

	if (flags & TOOL_TTY)
		endwin();

	switch ((pid = fork())) {
	case -1:
		break;
	case 0:
		if (path && chdir(path) == -1) {
			printf("chdir \"%s\" failed: %s\n", path,
			    strerror(errno));
			exit(1);
		}

		if (msg)
			puts(msg);

		if (flags & TOOL_SHELL) {
			char *shell = nishell ? nishell : "sh";

			if (execlp(shell, shell, "-c", *av, NULL) == -1) {
				/* only seen when vddiff exits later */
				printf("exec %s -c \"%s\" failed: %s\n",
				    shell, *av, strerror(errno));
			}
		} else if (execvp(*av, av) == -1) {
			/* only seen when vddiff exits later */
			printf("exec \"%s\" failed: %s\n", *av,
			    strerror(errno));
		}

		write(STDOUT_FILENO, prompt, sizeof prompt);
		fgetc(stdin);
		exit(77);
	default:
		if (!wait_after_exec && (flags & TOOL_BG))
			break;

		/* did always return "interrupted sys call" on OI */
		waitpid(pid, &status, 0);

		if (wait_after_exec || (flags & TOOL_WAIT)) {
			write(STDOUT_FILENO, prompt, sizeof prompt);
			fgetc(stdin);
		}
	}

	reset_prog_mode();
	refresh();

	if (pid == -1)
		printerr(strerror(errno), "fork failed");

	if (!(flags & TOOL_NOLIST))
		disp_list();

	return status;
}

size_t
shell_quote(char *to, char *from, size_t siz)
{
	int c;
	size_t len = 0;

	siz--; /* for last \0 byte */

	while (len < siz && (c = *from++)) {
		switch (c) {
		case '|':
		case '&':
		case ';':
		case '<': case '>':
		case '(': case ')':
		case '$':
		case '`':
		case '\\':
		case '"':
		case '\'':
		case ' ':
		case '\t':
		case '\n':
			*to++ = '\\';
			len++;

			if (len >= siz)
				break;

			/* fall through */
		default:
			*to++ = c;
			len++;
		}
	}

	*to = 0;
	return len;
}

void
open_sh(int tree)
{
	struct filediff *f = NULL;
	char *s;
	struct passwd *pw;
	char *av[2];

	if (db_num) {
		f = db_list[top_idx + curs];

		if ((tree == 3 && f->ltype && f->rtype) ||
		    (tree == 1 && !f->ltype) ||
		    (tree == 2 && !f->rtype))
			return;
	}

	if (bmode)
		s = rpath;
	else if (tree == 2 || ((tree & 2) && db_num && f->rtype)) {
		rpath[rlen] = 0;
		s = rpath;
	} else {
		lpath[llen] = 0;
		s = lpath;
	}

	if (ishell)
		*av = ishell;
	else {
		if (!(pw = getpwuid(getuid()))) {
			printerr(strerror(errno), "getpwuid failed");
			return;
		}

		*av = pw->pw_shell;
	}

	av[1] = NULL;
	exec_cmd(av, TOOL_NOLIST | TOOL_TTY, s,
	    "\nType \"exit\" or '^D' to return to vddiff.\n");

	/* exec_cmd() did likely create or delete files */
	rebuild_db(0);
}

void
exec_sighdl(void)
{
	struct sigaction act;

	act.sa_handler = sig_child;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;

	if (sigaction(SIGCHLD, &act, NULL) == -1) {
		printf("sigaction SIGCHLD failed: %s\n", strerror(errno));
		exit(1);
	}
}

static void
sig_child(int signo)
{
	(void)signo;
	wait(NULL);
}
