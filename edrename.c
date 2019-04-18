/*
 * Copyright (c) 2018 Michał Czarnecki <czarnecky@va.pl>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _DEFAULT_SOURCE
	#define _DEFAULT_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <limits.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <regex.h>

#define NO_ARG do { ++*argv; if (!(mid = **argv)) argv++; } while (0)

struct file_name {
	struct file_name *next;
	short nL;
	short rL;
	char *n;
	char *r;
};

void err(const char*, ...);
int xgetline(int, char*, size_t, char *[2]);
int gather_matching_files(char*, int, char*, struct file_name**);
int gather_fd(int, struct file_name**);
void file_name_free(struct file_name*);
int spawn(char *eargv[]);
char *basename(char*);
void usage(char*);
char *ARG(char***);
char* EARG(char***);

void err(const char *fmt, ...)
{
	va_list a;

	va_start(a, fmt);
	vdprintf(2, fmt, a);
	va_end(a);
	exit(EXIT_FAILURE);
}

#define XGETLINE_GETERRNO(R) (0x0000ffff & R)
/*
 * xgetline() returns:
 * -2   -> error
 * -1   -> no more lines
 * 0 <= -> line length (may be 0)
 */
int xgetline(int fd, char *buf, size_t bufs, char *b[2])
{
	char *src, *dst;
	size_t s;
	ssize_t r;
	int i, L = 0;

	src = b[0];
	dst = buf;
	i = s = b[1] - b[0];
	/* Move next lines to the very begining, having covered old line */
	while (i--) {
		*dst++ = *src++;
	}
	/* New values */
	b[0] = buf;
	b[1] = buf + s;

	for (;;) {
		/* Find newline */
		while (*b[0] != '\n' && *b[0] != '\r' && b[1] - b[0] > 0) {
			b[0]++;
		}
		/* If it was found */
		if (b[1] - b[0] > 0 && (*b[0] == '\n' || *b[0] == '\r')) {
			L = b[0] - buf;
			break;
		}
		/* If it was NOT found or ran out of buffer data */
		else {
			r = read(fd, b[1], bufs-(b[1]-buf));
			if (r == -1) {
				return 0xffff0000 | errno;
			}
			if (r == 0) {
				return -1;
			}
			b[1] += r;
		}
	}
	/* NULL terminator */
	*b[0] = 0;
	b[0]++;
	if (b[1] - b[0] > 0 && (*b[0] == '\n' || *b[0] == '\r')) {
		*b[0] = 0;
		b[0]++;
	}
	return L;
}

int gather_matching_files(char *str, int cflags, char *dir, struct file_name **H)
{
	regex_t R;
	const size_t nmatch = 1;
	regmatch_t pmatch[nmatch];
	int e;
	char errbuf[1024];
	size_t errbufn, L;
	struct dirent *ent;
	struct file_name *N;
	DIR *D;

	if ((e = regcomp(&R, str, cflags))) {
		errbufn = regerror(e, &R, errbuf, sizeof(errbuf));
		fprintf(stderr, "%.*s\n", (int)errbufn, errbuf);
		return -1;
	}
	*H = 0;
	D = opendir(dir);
	errno = 0;
	while ((ent = readdir(D))) {
		L = strnlen(ent->d_name, NAME_MAX);
		if (
		   (L == 1 && ent->d_name[0] == '.')
		|| (L == 2 && ent->d_name[0] == '.' && ent->d_name[1] == '.')
		|| (REG_NOMATCH == regexec(&R, ent->d_name, nmatch, pmatch, 0))
		) {
			continue;
		}
		N = malloc(sizeof(struct file_name));
		N->next = *H;
		N->nL = L;
		N->n = malloc(L+1);
		memcpy(N->n, ent->d_name, L+1);
		N->rL = 0;
		N->r = 0;
		*H = N;
	}
	e = errno;
	closedir(D);
	regfree(&R);
	return e;
}

int gather_fd(int fd, struct file_name **H)
{
	int L;
	char line[PATH_MAX];
	char *b[2] = { line, line };
	struct file_name *N;

	*H = 0;
	while (0 <= (L = xgetline(fd, line, sizeof(line), b))) {
		if (!L
		|| (L == 1 && line[0] == '.')
		|| (L == 2 && line[0] == '.' && line[1] == '.')
		) {
			continue;
		}
		N = malloc(sizeof(struct file_name));
		N->next = *H;
		N->nL = L;
		N->n = malloc(L+1);
		memcpy(N->n, line, L+1);
		N->rL = 0;
		N->r = 0;
		*H = N;
	}
	return L < -1 ? XGETLINE_GETERRNO(L) : 0;
}

void file_name_free(struct file_name *i)
{
	struct file_name *tmp;
	while (i) {
		tmp = i;
		i = i->next;
		if (tmp->n) free(tmp->n);
		if (tmp->r) free(tmp->r);
		free(tmp);
	}
}

int spawn(char *eargv[])
{
	pid_t p;
	int wstatus = 0;

	p = fork();
	if (p == -1) {
		return errno;
	}
	if (!p) {
		execvp(eargv[0], eargv);
		exit(EXIT_FAILURE);
	}
	else {
		if (-1 == waitpid(p, &wstatus, 0)) {
			return errno;
		}
		if (WIFEXITED(wstatus)) {
			return WEXITSTATUS(wstatus);
		}
		else {
			return -1; /* TODO */
		}
	}
}

char *basename(char *p)
{
	char *P = p;
	while (*P) P++;
	while (P != p && *P != '/') P--;
	if (*P == '/') P++;
	return P;
}

void usage(char *argv0)
{
	fprintf(stderr, "Usage: %s [-ieEh]\n", basename(argv0));
	fprintf(stderr,
	"Options:\n"
	"    -i         Read file list from stdin.\n"
	"    -e REGEXP  Filter files in current directory using POSIX regex.\n"
	"    -E REGEXP  Filter files in the current directory using extended regex.\n"
	"    -h         Display this message and exit.\n"
	);
}

char *ARG(char ***argv)
{
	char *r = 0;

	(**argv)++;
	if (***argv) { /* -oARG */
		r = **argv;
		(*argv)++;
	}
	else { /* -o ARG */
		(*argv)++;
		if (**argv && ***argv != '-') {
			r = **argv;
			(*argv)++;
		}
	}
	return r;
}

char* EARG(char ***argv)
{
	char *a;
	a = ARG(argv);
	if (!a) {
		err("ERROR: Expected argument.\n");
	}
	return a;
}

/* TODO
 * Arguments:
 * - enable extended REGEXP
 * - read filenames from stdin
 * - line endings
 * - $EDITOR vs $VISUAL
 */
int main(int argc, char *argv[])
{
	char *argv0,
	     *file_regex = 0,
	     tmpname[64],
	     *eargv[8];
	char buf[PATH_MAX] = { 0 };
	char *b[2] = { buf, buf };
	struct file_name *fn_list, *i;
	int L = 0, ret = 0;
	int tmpfd, e, cflags = 0;
	unsigned num_selected = 0, num_renamed = 0;
	struct iovec iov[2] = { { 0, 0 }, { "\n", 1 } };
	_Bool mid = 0, from_stdin = 0;

	(void)argc;

	argv0 = *argv;
	++argv;
	while ((mid && **argv) || (*argv && **argv == '-')) {
		if (!mid) ++*argv;
		mid = 0;
		if ((*argv)[0] == '-' && (*argv)[1] == 0) {
			argv++;
			break;
		}
		switch (**argv) {
		case 'i':
			from_stdin = 1;
			NO_ARG;
			break;
		case 'e':
			file_regex = EARG(&argv);
			break;
		case 'E':
			file_regex = EARG(&argv);
			cflags |= REG_EXTENDED;
			break;
		case 'h':
			usage(argv0);
			NO_ARG;
			return 0;
		default:
			usage(argv0);
			return 1;
		}
	}
	e = from_stdin
		? gather_fd(0, &fn_list)
		: gather_matching_files(file_regex, cflags, ".", &fn_list);
	if (e > 0) {
		fprintf(stderr, "error: %s\n", strerror(e));
		ret = 1;
		goto fail_cleanup;
	}
	if (!fn_list) {
		printf("no matching files\n");
		return 0;
	}
	/* TODO sort these files? */

	snprintf(tmpname, sizeof(tmpname), "/tmp/edrename.%d", getpid());

	tmpfd = creat(tmpname, 0600);
	for (i = fn_list; i; i = i->next) {
		iov[0].iov_base = i->n;
		iov[0].iov_len = i->nL;
		writev(tmpfd, iov, 2);
	}
	close(tmpfd);

	eargv[0] = getenv("EDITOR");
	eargv[1] = tmpname;
	eargv[2] = 0;
	if ((e = spawn(eargv))) {
		fprintf(stderr, "error: failed to spawn '%s': %s\n",
			eargv[0], strerror(e));
		ret = 1;
		goto fail_cleanup;
	}

	tmpfd = open(tmpname, 0600);
	if (tmpfd == -1) {
		e = errno;
		fprintf(stderr, "error: failed to open '%s': %s\n",
			tmpname, strerror(e));
		ret = 1;
		goto fail_cleanup;
	}
	i = fn_list;
	while (i) {
		/* TODO some instructions as comments? */
		if (0 < (L = xgetline(tmpfd, buf, sizeof(buf), b))) {
			i->rL = L;
			i->r = malloc(L+1);
			memcpy(i->r, buf, L+1);
		}
		else if (L == -1) {
			fprintf(stderr, "error: missing lines\n");
			close(tmpfd);
			unlink(tmpname);
			ret = 1;
			goto fail_cleanup;
		}
		else {
			fprintf(stderr, "error: %s\n",
				strerror(XGETLINE_GETERRNO(L)));
			close(tmpfd);
			unlink(tmpname);
			ret = 1;
			goto fail_cleanup;
		}
		i = i->next;
	}
	close(tmpfd);
	unlink(tmpname);

	eargv[0] = "/usr/bin/env";
	eargv[1] = "mv";
	eargv[2] = "-vi";
	eargv[3] = "--";
	eargv[6] = 0;
	for (i = fn_list; i; i = i->next) {
		num_selected++;
		if (i->nL == i->rL && !memcmp(i->n, i->r, i->nL+1)) {
			continue;
		}
		eargv[4] = i->n;
		eargv[5] = i->r;
		if ((e = spawn(eargv))) {
			fprintf(stderr, "error: failed to spawn '%s': %s\n",
				eargv[0], strerror(e));
			ret = 1;
			goto fail_cleanup;
		}
		num_renamed++;
	}

	printf("%d file%s renamed\n", num_renamed,
		num_renamed == 1 ? "" : "s");

	fail_cleanup:
	file_name_free(fn_list);
	return ret;
}
