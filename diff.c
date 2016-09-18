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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <avlbst.h>
#include "compat.h"
#include "main.h"
#include "ui.h"
#include "diff.h"
#include "db.h"

static int cmp_file(void);
static struct filediff *alloc_diff(char *);
static void add_diff_dir(void);
static char *read_link(char *, off_t);

static int (*xstat)(const char *, struct stat *) = lstat;
static struct filediff *diff;
static struct bst scan_db = { NULL, name_cmp };

/* 1: Proc left dir
 * 2: Proc right dir
 * 3: Proc both dirs */
int
build_diff_db(int tree)
{
	DIR *d;
	struct dirent *ent;
	char *name;
	void *dirs;
	short dir_diff = 0;
	int retval = 0;

	if (scan)
		dirs = db_new(name_cmp);

	if (!(tree & 1))
		goto right_tree;

	if (bmode && !getcwd(rpath, sizeof rpath))
		printerr(strerror(errno), "getcwd failed");

	if (!(d = opendir(lpath))) {
		printerr(strerror(errno), "opendir %s failed", lpath);
		retval = -1;
		goto dir_scan_end;
	}

	while (1) {
		errno = 0;
		if (!(ent = readdir(d))) {
			if (!errno)
				break;

			lpath[llen] = 0;
			printerr(strerror(errno), "readdir %s failed", lpath);
			closedir(d);
			retval = -1;
			goto dir_scan_end;
		}

		name = ent->d_name;
		if (*name == '.' && (!name[1] || (name[1] == '.' &&
		    !name[2])))
			continue;

		add_name(name);
		pthcat(lpath, llen, name);

		if (xstat(lpath, &stat1) == -1) {
			if (errno != ENOENT) {
				printerr(strerror(errno), "stat %s failed",
				   lpath);
				continue;
			}

			stat1.st_mode = 0;
		}

		if (tree & 2) {
			pthcat(rpath, rlen, name);
		} else
			goto no_tree2;

		if (xstat(rpath, &stat2) == -1) {
			if (errno != ENOENT) {
				printerr(strerror(errno), "stat %s failed",
				    rpath);
				continue;
			}

no_tree2:
			if (!stat1.st_mode)
				continue; /* ignore two dead links */
			stat2.st_mode = 0;
		}

		if (scan) {
			if (S_ISDIR(stat1.st_mode) &&
			    S_ISDIR(stat2.st_mode)) {
				db_scan_add(dirs, name);
				continue;
			}

			if (!*pwd || dir_diff)
				continue;

			if (S_ISREG(stat1.st_mode) &&
			    S_ISREG(stat2.st_mode)) {
				if (cmp_file() == 1)
					dir_diff = 1;
				continue;
			}

			if (S_ISLNK(stat1.st_mode) &&
			    S_ISLNK(stat2.st_mode)) {
				char *a, *b;

				if (!(a = read_link(lpath, stat1.st_size)))
					continue;

				if (!(b = read_link(rpath, stat2.st_size)))
					goto free_a;

				if (strcmp(a, b))
					dir_diff = 1;

				free(b);

free_a:
				free(a);
				continue;
			}

			if (real_diff)
				continue;

			if (!stat1.st_mode || !stat2.st_mode ||
			     stat1.st_mode !=  stat2.st_mode) {
				dir_diff = 1;
				continue;
			}

			continue;
		}

		diff = alloc_diff(name);

		if ((diff->ltype = stat1.st_mode)) {
			diff->luid  = stat1.st_uid;
			diff->lgid  = stat1.st_gid;
			diff->lsiz  = stat1.st_size;
			diff->lmtim = stat1.st_mtim.tv_sec;

			if (S_ISLNK(stat1.st_mode))
				diff->llink = read_link(lpath, stat1.st_size);
		}

		if ((diff->rtype = stat2.st_mode)) {
			diff->ruid  = stat2.st_uid;
			diff->rgid  = stat2.st_gid;
			diff->rsiz  = stat2.st_size;
			diff->rmtim = stat2.st_mtim.tv_sec;

			if (S_ISLNK(stat2.st_mode))
				diff->rlink = read_link(rpath, stat2.st_size);
		}

		if ((diff->ltype & S_IFMT) != (diff->rtype & S_IFMT)) {

			db_add(diff);
			continue;

		} else if (stat1.st_ino == stat2.st_ino) {

			diff->diff = '=';
			db_add(diff);
			continue;

		} else if (S_ISREG(stat1.st_mode)) {

			switch (cmp_file()) {
			case 1:
				diff->diff = '!';
				/* fall through */
			case 0:
				db_add(diff);
				continue;
			}

		} else if (S_ISDIR(stat1.st_mode)) {

			db_add(diff);
			continue;

		} else if (S_ISLNK(stat1.st_mode)) {

			if (diff->llink && diff->rlink) {
				if (strcmp(diff->llink, diff->rlink))
					diff->diff = '!';
				db_add(diff);
				continue;
			}

		/* any other file type */
		} else {
			db_add(diff);
			continue;
		}

		free(diff);
	}

	closedir(d);
	lpath[llen] = 0;

	if (tree & 2)
		rpath[rlen] = 0;

right_tree:
	if (!(tree & 2))
		goto build_list;

	if (scan && (real_diff || dir_diff))
		goto dir_scan_end;

	if (!(d = opendir(rpath))) {
		printerr(strerror(errno), "opendir %s failed", rpath);
		retval = -1;
		goto dir_scan_end;
	}

	while (1) {
		errno = 0;
		if (!(ent = readdir(d))) {
			if (!errno)
				break;
			printerr(strerror(errno), "readdir %s failed", rpath);
			closedir(d);
			retval = -1;
			goto dir_scan_end;
		}

		name = ent->d_name;
		if (*name == '.' && (!name[1] || (name[1] == '.' &&
		    !name[2])))
			continue;

		if (!srch_name(name))
			continue;

		if (scan) {
			dir_diff = 1;
			break;
		}

		pthcat(rpath, rlen, name);

		if (xstat(rpath, &stat2) == -1) {
			if (errno != ENOENT) {
				printerr(strerror(errno), "lstat %s failed",
				    rpath);
			}
			continue;
		}

		diff = alloc_diff(name);
		diff->ltype = 0;
		diff->rtype = stat2.st_mode;
		diff->ruid  = stat2.st_uid;
		diff->rgid  = stat2.st_gid;
		diff->rsiz  = stat2.st_size;
		diff->rmtim = stat2.st_mtim.tv_sec;

		if (S_ISLNK(stat2.st_mode))
			diff->rlink = read_link(rpath, stat2.st_size);

		db_add(diff);
	}

	closedir(d);

build_list:
	if (!scan)
		db_sort();

dir_scan_end:
	free_names();

	if (!scan)
		return retval;

	if (dir_diff)
		add_diff_dir();

	db_scan_walk(dirs);
	db_destroy(dirs);
	return retval;
}

void
scan_subdir(char *name, char *rnam, int tree)
{
	if (tree & 1) {
		if (name)
			llen = pthcat(lpath, llen, name);
		else
			lpath[llen] = 0;
	}

	if (tree & 2)
		rlen = pthcat(rpath, rlen, rnam ? rnam : name);

	build_diff_db(tree);
}

static void
add_diff_dir(void)
{
	char *path, *end;

	if (!*pwd)
		return;

	path = strdup(PWD);
	end = path + strlen(path);

	while (1) {
		if (!bst_srch(&scan_db, (union bst_val)(void *)path, NULL))
			goto ret;

		avl_add(&scan_db, (union bst_val)(void *)strdup(path),
			    (union bst_val)(int)0);

		do {
			if (--end < path)
				goto ret;
		} while (*end != '/');

		*end = 0;
	}

ret:
	free(path);
}

int
is_diff_dir(char *name)
{
	char *s;
	size_t l1;
	short v;

	if (!recursive)
		return 0;
	if (!*pwd)
		return bst_srch(&scan_db, (union bst_val)(void *)name, NULL) ?
		    0 : 1;
	l1 = strlen(PWD);
	s = malloc(l1 + strlen(name) + 2);
	memcpy(s, PWD, l1);
	pthcat(s, l1, name);
	v = bst_srch(&scan_db, (union bst_val)(void *)s, NULL);
	free(s);
	return v ? 0 : 1;
}

static char *
read_link(char *path, off_t size)
{
	char *link = malloc(size + 1);

	if ((size = readlink(path, link, size)) == -1) {
		printerr(strerror(errno), "readlink %s failed", lpath);
		free(link);
		return NULL;
	}

	link[size] = 0;
	return link;
}

/* -1  Error, don't make DB entry
 *  0  No diff
 *  1  Diff */

static int
cmp_file(void)
{
	int rv = 0, f1, f2;
	ssize_t l1, l2;

	if (stat1.st_size != stat2.st_size)
		return 1;

	if (!stat1.st_size)
		return 0;

	if ((f1 = open(lpath, O_RDONLY)) == -1) {
		printerr(strerror(errno), "open %s failed", lpath);
		return -1;
	}

	if ((f2 = open(rpath, O_RDONLY)) == -1) {
		printerr(strerror(errno), "open %s failed", rpath);
		rv = -1;
		goto close_f1;
	}

	while (1) {
		if ((l1 = read(f1, lbuf, sizeof lbuf)) == -1) {
			printerr(strerror(errno), "read %s failed", lpath);
			rv = -1;
			break;
		}

		if ((l2 = read(f2, rbuf, sizeof rbuf)) == -1) {
			printerr(strerror(errno), "read %s failed", rpath);
			rv = -1;
			break;
		}

		if (l1 != l2) {
			rv = 1;
			break;
		}

		if (!l1)
			break;

		if (memcmp(lbuf, rbuf, l1)) {
			rv = 1;
			break;
		}

		if (l1 < (ssize_t)(sizeof lbuf))
			break;
	}

	close(f2);
close_f1:
	close(f1);
	return rv;
}

static struct filediff *
alloc_diff(char *name)
{
	struct filediff *p = malloc(sizeof(struct filediff));
	p->name  = strdup(name);
	p->llink = NULL; /* to simply use free() later */
	p->rlink = NULL;
	p->diff  = ' ';
	return p;
}

void
follow(int f)
{
	xstat = f ? stat : lstat;
}

size_t
pthcat(char *p, size_t l, char *n)
{
	size_t ln = strlen(n);

	if (l + ln + 2 > PATHSIZ) {
		printerr(NULL, "Path buffer overflow");
		return l;
	}

	if (p[l-1] != '/')
		p[l++] = '/';

	memcpy(p + l, n, ln + 1);
	return l + ln;
}
