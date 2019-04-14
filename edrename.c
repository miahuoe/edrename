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

struct file_name {
	struct file_name *next;
	short nL;
	short rL;
	char *n;
	char *r;
};

int xgetline(int fd, size_t bufsize, char buf[bufsize], char **top, size_t *L);
int prepare_regex(regex_t*);
int gather_matching_files(char*, char*, struct file_name**);
int spawn(char *eargv[]);
void usage(char*);

/* TODO simplify */
int xgetline(int fd, size_t bufsize, char buf[bufsize], char **top, size_t *L)
{
	char *end;
	ssize_t r;
	size_t len;

	len = *L + (buf != *top ? 1 : 0);
	memmove(buf, buf + len, bufsize - len);
	memset(buf + (bufsize - len), 0, len);
	*top -= len;
	if ((r = read(fd, *top, bufsize - (*top - buf)))) {
		*top += r;
	}
	else if (*top == buf) {
		return 1;
	}
	if ((end = memchr(buf, '\r', bufsize))) {
		if ((size_t)(end-buf+1) <= bufsize && *(end+1) == '\n') {
 			/* TODO TEST */
			*end = 0;
			end++;
		}
	}
	else if ((end = memchr(buf, '\n', bufsize))
	|| (end = memchr(buf, '\0', bufsize))) {
	}
	else {
		end = buf+bufsize-1;
	}
	*end = 0;
	*L = end - buf;
	return 0;
}

int gather_matching_files(char *str, char *dir, struct file_name **H)
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

	if ((e = regcomp(&R, str, REG_EXTENDED))) {
		errbufn = regerror(e, &R, errbuf, sizeof(errbuf));
		fprintf(stderr, "%.*s\n", (int)errbufn, errbuf);
		regfree(&R);
		return e;
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
	if ((e = errno)) {
		fprintf(stderr, "readdir(): %s\n", strerror(e));
	}
	closedir(D);
	regfree(&R);
	return 0;
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
	printf("Usage: %s REGEXP\n", basename(argv0));
}

int main(int argc, char *argv[])
{
	char *argv0,
	     *file_regex = 0,
	     tmpname[64],
	     buf[PATH_MAX] = { 0 },
	     *top = buf,
	     *eargv[8];
	struct file_name *fn_list, *i, *tmp;
	size_t L = 0;
	int tmpfd, e;
	unsigned num_selected = 0, num_renamed = 0;
	struct iovec iov[2] = {
		{ 0, 0 },
		{ "\n", 1 },
	};

	argv0 = argv[0];
	if (argc == 1) {
		usage(argv0);
		return 0;
	}
	argv++;
	file_regex = *argv;
	if (!file_regex) {
		usage(argv0);
		return 0;
	}
	gather_matching_files(file_regex, ".", &fn_list);
	if (!fn_list) {
		printf("no matching files\n");
		return 0;
	}
	/* TODO sort these files */

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
		return 1;
	}

	tmpfd = open(tmpname, 0600);
	i = fn_list;
	while (i) {
		/* TODO some instructions as comments? */
		if (!xgetline(tmpfd, sizeof(buf), buf, &top, &L)) {
			i->rL = L;
			i->r = malloc(L+1);
			memcpy(i->r, buf, L+1);
		}
		else {
			fprintf(stderr, "error: missing lines\n");
			close(tmpfd);
			unlink(tmpname);
			return 1;
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
			return 1;
		}
		num_renamed++;
	}

	i = fn_list;
	while (i) {
		tmp = i;
		i = i->next;
		if (tmp->n) free(tmp->n);
		if (tmp->r) free(tmp->r);
		free(tmp);
	}
	printf("%d file%s renamed\n", num_renamed,
		num_renamed == 1 ? "" : "s");
	return 0;
}
