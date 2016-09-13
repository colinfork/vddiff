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

#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <avlbst.h>
#include <locale.h>
#include "compat.h"
#include "main.h"
#include "y.tab.h"
#include "ui.h"
#include "exec.h"
#include "db.h"
#include "diff.h"

int yyparse(void);

char *prog, *pwd, *rpwd, *arg[2];
size_t llen, rlen;
char lpath[PATHSIZ], rpath[PATHSIZ], lbuf[BUFSIZ], rbuf[BUFSIZ];
struct stat stat1, stat2;
short recursive, scan;

static void check_args(char **);
static int read_rc(void);
static void usage(void);
static char *usage_txt =
"Usage: %s [-ubcdfgklmnr] [-t <diff_tool>] [-v <view_tool>] <directory_1>\n"
"           <directory_2>\n";
static char *getopt_arg = "bcdfgklmnrt:uv:";

int
main(int argc, char **argv)
{
	int opt;

	prog = *argv;
	setlocale(LC_ALL, "");
	set_tool(&difftool, strdup("vim -dR"), 0);
	set_tool(&viewtool, strdup("less"), 0);

	if (argc < 2 || argv[1][0] != '-' || argv[1][1] != 'u')
		if (read_rc())
			return 1;

	while ((opt = getopt(argc, argv, getopt_arg)) != -1) {
		switch (opt) {
		case 'b':
			color = 0;
			break;
		case 'c':
			real_diff = 1;
			break;
		case 'd':
			set_tool(&difftool, strdup("diff '$1' '$2' | less"), 0);
			break;
		case 'f':
			sorting = FILESFIRST;
			break;
		case 'g':
			set_tool(&difftool, strdup("gvim -dR"), 0);
			set_tool(&viewtool, strdup("gvim -R"), 0);
			break;
		case 'k':
			set_tool(&difftool, strdup("tkdiff"), 1);
			break;
		case 'l':
			follow(1);
			break;
		case 'm':
			sorting = SORTMIXED;
			break;
		case 'n':
			noequal = 1;
			break;
		case 'r':
			recursive = 1;
			break;
		case 't':
			set_tool(&difftool, strdup(optarg), 0);
			break;
		case 'v':
			set_tool(&viewtool, strdup(optarg), 0);
			break;
		case 'u':
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 2) {
		printf("Two arguments expected\n");
		usage();
	}

	check_args(argv);
	pwd  = lpath + llen;
	rpwd = rpath + rlen;
	exec_sighdl();
	build_ui();
	return 0;
}

static int
read_rc(void)
{
	static char rc_name[] = "/.vddiffrc";
	char *s, *rc_path;
	size_t l;
	int rv = 0;
	extern FILE *yyin;

	if (!(s = getenv("HOME"))) {
		printf("HOME not set\n");
		return 1;
	} else {
		l = strlen(s);
		rc_path = malloc(l + sizeof rc_name);
		memcpy(rc_path, s, l);
		memcpy(rc_path + l, rc_name, sizeof rc_name);
	}

	if (stat(rc_path, &stat1) == -1) {
		if (errno == ENOENT)
			goto free;
		printf("stat(\"%s\") failed: %s\n", rc_path,
		    strerror(errno));
		rv = 1;
		goto free;
	}

	if (!(yyin = fopen(rc_path, "r"))) {
		printf("fopen(\"%s\") failed: %s\n", rc_path,
		    strerror(errno));
		rv = 1;
		goto free;
	}

	rv = yyparse();

	if (fclose(yyin) == EOF) {
		printf("fclose(\"%s\") failed: %s\n", rc_path,
		    strerror(errno));
	}
free:
	free(rc_path);
	return rv;
}

static void
check_args(char **argv)
{
	char *s;
	ino_t ino;

	arg[0] = *argv;

	if (stat(s = *argv++, &stat1) == -1) {
		printf("stat(\"%s\") failed: %s\n", s, strerror(errno));
		usage();
	}

	if (!S_ISDIR(stat1.st_mode)) {
		printf("\"%s\" is not a directory\n", s);
		usage();
	}

	llen = strlen(s);
	memcpy(lpath, s, llen + 1);
	ino = stat1.st_ino;
	arg[1] = *argv;

	if (stat(s = *argv++, &stat1) == -1) {
		printf("stat(\"%s\") failed: %s\n", s, strerror(errno));
		usage();
	}

	if (stat1.st_ino == ino) {
		printf("\"%s\" and \"%s\" are the same directory\n",
		    lpath, s);
		exit(0);
	}

	if (!S_ISDIR(stat1.st_mode)) {
		printf("\"%s\" is not a directory\n", s);
		usage();
	}

	rlen = strlen(s);
	memcpy(rpath, s, rlen + 1);
}

static void
usage(void)
{
	printf(usage_txt, prog);
	exit(1);
}
