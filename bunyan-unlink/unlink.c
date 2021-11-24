/* */

/*
 * Copyright (c) 2017 The University of Queensland
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

/*
 * This code was written by David Gwynne <dlg@uq.edu.au> as part of the
 * IT Infrastructure Group in the Faculty of Engineering, Architecture and
 * Information Technology.
 */

#include <sys/stat.h>
#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <err.h>
#include <util.h>
#include <dirent.h>
#include <limits.h>

__dead void
usage(void)
{
	fprintf(stderr, "usage: %s [-acmn] [-f format] [-HMDW internal] dir",
	    getprogname());
	exit(1);
}

typedef int (*match_fn_t)(const char *, time_t, const struct stat *,
    const char *);

static int	match_format(const char *, time_t, const struct stat *,
		    const char *);
static int	match_atime(const char *, time_t, const struct stat *,
		    const char *);
static int	match_mtime(const char *, time_t, const struct stat *,
		    const char *);
static int	match_ctime(const char *, time_t, const struct stat *,
		    const char *);
static int	printfname(const char *);

int
main(int argc, char *argv[])
{
	int ecode = 0;
	int ch;
	const char *errstr = NULL;

	time_t age = 3LL * 60 * 60 * 24; /* 3 days */
	time_t now;

	const void *match_arg = &age;
	match_fn_t match_fn = match_mtime;
	int (*unlink_fn)(const char *) = unlink;

	const char *dirname = ".";
	DIR *dirp;
	struct dirent *dp;
	struct stat sb;
	int match;

	while ((ch = getopt(argc, argv, "acD:f:H:M:mnW:")) != -1) {
		switch (ch) {
		case 'a':
			match_fn = match_atime;
			break;
		case 'c':
			match_fn = match_ctime;
			break;
		case 'D':
			age = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "%s days: %s", optarg, errstr);
			age *= 60 * 60 * 24;
			break;
		case 'f':
			match_fn = match_format;
			match_arg = optarg;
			break;
		case 'H':
			age = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "%s hours: %s", optarg, errstr);
			age *= 60 * 60;
			break;
		case 'M':
			age = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "%s minutes: %s", optarg, errstr);
			age *= 60;
			break;
		case 'm':
			match_fn = match_mtime;
			break;
		case 'n':
			unlink_fn = printfname;
			break;
		case 'W':
			age = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "%s weeks: %s", optarg, errstr);
			age *= 60 * 60 * 24 * 7;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	argc -= optind;
	argv += optind;

	switch (argc) {
	case 0:
		break;
	case 1:
		dirname = argv[0];
		break;
	default:
		usage();
		/* NOTREACHED */
	}

	now = time(NULL);
	age = now - age;

	dirp = opendir(dirname);
	if (dirp == NULL)
		err(1, "directory %s", dirname);

	if (dirname == argv[0] && fchdir(dirfd(dirp)) == -1)
		err(1, "chdir %s", dirname);

	while ((dp = readdir(dirp)) != NULL) {
		if (dp->d_name[0] == '.')
			continue;

		if (stat(dp->d_name, &sb) == -1) {
			warn("stat %s", dp->d_name);
			ecode = 1;
		}

		if (!S_ISREG(sb.st_mode))
			continue;

		match = (*match_fn)(match_arg, age, &sb, dp->d_name);
		if (match == -1) {
			warnx("match %s", dp->d_name);
			ecode = 1;
			continue;
		}

		if (match && (*unlink_fn)(dp->d_name) == -1) {
			warn("%s", dp->d_name);
			ecode = 1;
			continue;
		}
	}

	return (ecode);
}

static int
match_format(const char *format, time_t age, const struct stat *sb,
    const char *fname)
{
	struct tm *tm = localtime(&age);
	time_t fage;

	if (strptime(fname, format, tm) == NULL)
		return (0);

	fage = mktime(tm);
	if (fage == -1)
		return (-1);

	return (fage <= age);
}

static inline int
match_time(time_t age, const struct timespec *ts)
{
	return (ts->tv_sec <= age);
}

static int
match_atime(const char *null, time_t age, const struct stat *sb,
    const char *fname)
{
	return (match_time(age, &sb->st_atim));
}

static int
match_mtime(const char *null, time_t age, const struct stat *sb,
    const char *fname)
{
	return (match_time(age, &sb->st_mtim));
}

static int
match_ctime(const char *null, time_t age, const struct stat *sb,
    const char *fname)
{
	return (match_time(age, &sb->st_ctim));
}

static int
printfname(const char *fname)
{
	printf("%s\n", fname);

	return (0);
}
