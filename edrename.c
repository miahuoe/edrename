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
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <limits.h>
#include <sys/wait.h>
#include <errno.h>
//#include <stdint.h>

#include <regex.h>

struct file_name {
	struct file_name *next;
	unsigned char nL;
	char *n;
	unsigned char rL;
	char *r;
};

int prepare_regex(regex_t*);
int gather_matching_files(char*, char*, struct file_name**);
void usage(char*);
int spawn(int, char*[]);

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
	if (
	   (end = memchr(buf, '\r', bufsize))
	|| (end = memchr(buf, '\n', bufsize))
	|| (end = memchr(buf, '\0', bufsize))
	) {
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
	int e;
	char errbuf[1024];
	size_t errbufn;
	struct dirent *ent;
	struct file_name *N;
	size_t L;
	const size_t nmatch = 1;
	regmatch_t pmatch[nmatch];
	DIR *D;

	if ((e = regcomp(&R, str, REG_EXTENDED))) {
		errbufn = regerror(e, &R, errbuf, sizeof(errbuf));
		printf("%.*s\n", (int)errbufn, errbuf);
		regfree(&R);
		return e;
	}
	*H = 0;
	D = opendir(dir);
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
	closedir(D);
	regfree(&R);
	return 0;
}

void usage(char *progname)
{
	printf("Usage: %s [OPTIONS] REGEXP\n"
	"Options:\n"
	"\t-h\tDisplay this message and exit.\n"
	"\n", progname);
}

int spawn(int eargc, char *eargv[eargc+1])
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
		waitpid(p, &wstatus, 0);
		if (WIFEXITED(wstatus)) {
			return WEXITSTATUS(wstatus);
		}
		else {
			return -1;
		}
	}
}

int main(int argc, char *argv[])
{
	char *progname,
	     *current_arg,
	     *file_regex = 0,
	     tmpname[32],
	     buf[80] = { 0 },
	     *top = buf,
	     *eargv[3];
	struct file_name *fn_list,
			 *i,
			 *tmp;
	size_t L = 0;
	int tmpfd;
	pid_t my_pid;

	progname = argv[0];
	if (argc == 1) {
		usage(progname);
		return 0;
	}
	argv++;
	while (*argv && **argv == '-') {
		current_arg = *argv;
		++*argv;
		switch (**argv) {
		case 0:
			break;
		case 'h':
			usage(progname);
			return 0;
		/*unrecognized_argument:*/
		default:
			fprintf(stderr, "error: Unrecognized option: %s\n", current_arg);
			usage(progname);
			return 1;
		}
		argv++;
	}
	file_regex = *argv;
	if (file_regex) {
		my_pid = getpid();
		gather_matching_files(file_regex, ".", &fn_list);
		snprintf(tmpname, sizeof(tmpname), "/tmp/edrename.%d", my_pid);
		tmpfd = creat(tmpname, 0600);
		for (i = fn_list; i; i = i->next) {
			write(tmpfd, i->n, i->nL);
			write(tmpfd, "\n", 1);
		}
		close(tmpfd);

		eargv[0] = getenv("EDITOR");
		eargv[1] = tmpname;
		eargv[2] = 0;
		spawn(2, eargv);

		tmpfd = open(tmpname, 0600);
		for (i = fn_list; i; i = i->next) {
			if (!xgetline(tmpfd, sizeof(buf), buf, &top, &L)) {
				i->rL = L;
				i->r = malloc(L+1);
				memcpy(i->r, buf, L+1);
			}
			else {
				fprintf(stderr, "error: missing lines\n");
				exit(EXIT_FAILURE);
			}
		}
		close(tmpfd);
		unlink(tmpname);

		for (i = fn_list; i; i = i->next) {
			printf("%d'%s' -> %d'%s'\n", i->nL, i->n, i->rL, i->r);
		}

		i = fn_list;
		while (i) {
			tmp = i;
			i = i->next;
			free(tmp->n);
			free(tmp->r);
			free(tmp);
		}
	}
	return 0;
}
