
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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <err.h>
#include <util.h>

#include <sys/stat.h>

#include <zlib.h>
#include <openssl/md5.h>
#include <openssl/sha.h>

#define BUFSIZE	65536

struct ofile {
	void	*(*of_fdopen)(int, int);
	ssize_t	 (*of_write)(void *, const void *, size_t);
	int	 (*of_close)(void *);
};

void	*fd_dopen(int, int);
ssize_t	 fd_write(void *, const void *, size_t);
int	 fd_close(void *);

static const struct ofile fd_ofile = {
	fd_dopen,
	fd_write,
	fd_close,
};

void	*gz_dopen(int, int);
ssize_t	 gz_write(void *, const void *, size_t);
int	 gz_close(void *);

static const struct ofile gz_ofile = {
	gz_dopen,
	gz_write,
	gz_close,
};

void __dead
usage(void)
{
	fprintf(stderr, "usage: %s [-FTz] [-s size] [-l level] "
	    "bunyan.log outfile\n", getprogname());
	exit(1);
}

int
main(int argc, char *argv[])
{
	int ch;
	const char *errstr = NULL;

	long long size = 0;
	int format = 1;
	int trunc = O_RDWR;
	int level = Z_NO_COMPRESSION;
	const struct ofile *ofop = &fd_ofile;
	int meta = 0;

	MD5_CTX md5c;
	SHA256_CTX sha256c;
	off_t iflen;

	const char *ifile, *ofile;
	char ofmt[FILENAME_MAX];
	int ifd, ofd;
	void *of;
	unsigned char buf[BUFSIZE];
	ssize_t len;

	while ((ch = getopt(argc, argv, "Fl:MNs:Tz")) != -1) {
		switch (ch) {
		case 'F':
			format = 0;
			break;
		case 'l':
			level = strtonum(optarg, Z_BEST_SPEED,
			    Z_BEST_COMPRESSION, &errstr);
			if (errstr != NULL) {
				errx(1, "compression level %s is %s", optarg,
				    errstr);
			}

			ofop = &gz_ofile;
			break;
		case 'M':
			meta = 1;
			break;
		case 's':
			if (scan_scaled(optarg, &size) == -1)
				err(1, "file size %s", optarg);
			if (size <= 0)
				errx(1, "file size %s: too small", optarg);
			break;
		case 'T':
			trunc = O_RDONLY;
			break;
		case 'z':
			level = Z_DEFAULT_COMPRESSION;
			ofop = &gz_ofile;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 2)
		usage();

	ifile = argv[0];

	if (format) {
		time_t t;
		struct tm *tm;
		size_t flen;

		t = time(NULL);
		if (t == (time_t)-1)
			err(1, "time");

		tm = localtime(&t);
		if (tm == NULL)
			errx(1, "localtime");

		flen = strftime(ofmt, sizeof(ofmt), argv[1], tm);
		if (flen == 0)
			errx(1, "output file name format failed");
		if (flen > sizeof(ofmt))
			errx(1, "formatted output file name is too long");

		ofile = ofmt;
	} else
		ofile = argv[1];

	ifd = open(ifile, trunc);
	if (ifd == -1)
		err(1, "\"%s\" open", ifile);

	if (size > 0) {
		struct stat st;

		if (fstat(ifd, &st) == -1)
			err(1, "\"%s\" stat", ifile);

		if (st.st_size < size) {
			/* not big enough yet */
			exit(0);
		}
	}

	if (meta) {
		iflen = 0;
		if (MD5_Init(&md5c) == 0)
			errx(1, "MD5 Init failed");
		if (SHA256_Init(&sha256c) == 0)
			errx(1, "SHA256 Init failed");
	}

	ofd = open(ofile, O_WRONLY|O_CREAT|O_TRUNC, 0666);
	if (ofd == -1)
		err(1, "\"%s\" open", ofile);

	of = ofop->of_fdopen(ofd, level);
	if (of == NULL)
		err(1, "\"%s\" open stream", ofile);

	for (;;) {
		len = read(ifd, buf, sizeof(buf));
		if (len == -1)
			err(1, "%s read", ifile);
		if (len == 0) {
			/* lock and make sure we have really have finished */
			if (flock(ifd, LOCK_EX) == -1)
				err(1, "%s lock", ifile);

			len = read(ifd, buf, sizeof(buf));
			if (len == -1)
				err(1, "%s read", ifile);
			if (len == 0) {
				if (ofop->of_close(of) == -1)
					err(1, "\"%s\" close", ofile);

				if (trunc == O_RDWR && ftruncate(ifd, 0) == -1)
					err(1, "%s truncate", ifile);
			}

			if (flock(ifd, LOCK_UN) == -1)
				err(1, "%s unlock", ifile);

			if (len == 0)
				break;
		}

		if (meta) {
			iflen += len;
			if (MD5_Update(&md5c, buf, len) == 0)
				errx(1, "MD5 Update failed");
			if (SHA256_Update(&sha256c, buf, len) == 0)
				errx(1, "SHA256 Update failed");
		}

		len = ofop->of_write(of, buf, len);
		if (len == -1)
			errx(1, "\"%s\" write", ofile);
	}

	if (meta) {
		unsigned char md5sum[MD5_DIGEST_LENGTH];
		unsigned char sha256sum[SHA256_DIGEST_LENGTH];
		char *mfile;
		FILE *mf;
		size_t i;
		int rv;

		if (MD5_Final(md5sum, &md5c) == 0)
			errx(1, "MD5 Final failed");
		if (SHA256_Final(sha256sum, &sha256c) == 0)
			errx(1, "SHA256 Final failed");

		rv = asprintf(&mfile, "%s.meta", ofile);
		if (rv == -1)
			errx(1, "%s meta file name", ofile);

		mf = fopen(mfile, "w");
		if (mf == NULL)
			err(1, "%s", mfile);

		fprintf(mf, "ifile=%s\n" "len=%llu\n", ifile, iflen);

		fprintf(mf, "md5=");
		for (i = 0; i < sizeof(md5sum); i++)
			fprintf(mf, "%02x", md5sum[i]);
		fprintf(mf, "\n");

		fprintf(mf, "sha256=");
		for (i = 0; i < sizeof(sha256sum); i++)
			fprintf(mf, "%02x", sha256sum[i]);
		fprintf(mf, "\n");

		fclose(mf);
		free(mfile);
	}

	return (0);
}

void *
fd_dopen(int fd, int level)
{
	return ((void *)(long)fd);
}

ssize_t
fd_write(void *ptr, const void *buf, size_t len)
{
	return (write((int)(long)ptr, buf, len));
}

int
fd_close(void *ptr)
{
	return (close((int)(long)ptr));
}

#define GZ_MAGIC_0	0x1f
#define GZ_MAGIC_1	0x8b
#define GZ_OS_CODE	0x03 /* unix */
#define GZ_MEM_LEVEL	8

struct gz_stream {
	FILE		*gz_f;
	z_stream	 gz_stream;
	u_char		 gz_buf[65536];
	uint32_t	 gz_crc;
};

void *
gz_dopen(int fd, int level)
{
	static const uint8_t hdr[10] = {
	    GZ_MAGIC_0, GZ_MAGIC_1,
	    Z_DEFLATED, 0 /* flags */,
	    0, 0, 0, 0, /* mtime */
	    0 /* xflags */, GZ_OS_CODE,
	};
	struct gz_stream *gz;

	gz = malloc(sizeof(*gz));
	if (gz == NULL)
		return (NULL);

	gz->gz_f = fdopen(fd, "w");
	if (gz->gz_f == NULL)
		goto fail;

	gz->gz_stream.zalloc = Z_NULL;
	gz->gz_stream.zfree = Z_NULL;
	gz->gz_stream.opaque = Z_NULL;

	switch (deflateInit2(&gz->gz_stream, level, Z_DEFLATED,
	    -MAX_WBITS, GZ_MEM_LEVEL, 0)) {
	case Z_OK:
		break;
	case Z_MEM_ERROR:
		errno = ENOMEM;
		goto fclose;
	case Z_STREAM_ERROR:
		errno = EINVAL;
		goto fclose;
	case Z_VERSION_ERROR:
		errno = EPROGMISMATCH; /* ? */
		goto fclose;
	default:
		abort();
	}

	gz->gz_stream.next_out = gz->gz_buf;
	gz->gz_stream.avail_out = sizeof(gz->gz_buf);

	gz->gz_crc = crc32(0L, Z_NULL, 0);

	if (fwrite(hdr, 1, sizeof(hdr), gz->gz_f) < sizeof(hdr)) {
		errno = EIO;
		goto fail;
	}

	return (gz);

fclose:
	fclose(gz->gz_f);
fail:
	free(gz);
	return (NULL);
}

ssize_t
gz_write(void *ptr, const void *buf, size_t len)
{
	struct gz_stream *gz = ptr;

	gz->gz_stream.next_in = (void *)buf;
	gz->gz_stream.avail_in = len;

	while (gz->gz_stream.avail_in > 0) {
		if (gz->gz_stream.avail_out == 0) {
			if (fwrite(gz->gz_buf, 1, sizeof(gz->gz_buf),
			    gz->gz_f) != sizeof(gz->gz_buf)) {
				/* must be an error */
				return (-1);
			}
			gz->gz_stream.next_out = gz->gz_buf;
			gz->gz_stream.avail_out = sizeof(gz->gz_buf);
		}
	
		if (deflate(&gz->gz_stream, Z_NO_FLUSH) != Z_OK) {
			errno = EIO;
			return (-1);
		}
	}
	gz->gz_crc = crc32(gz->gz_crc, buf, len);

	return (len);
}

int
gz_close(void *ptr)
{
	struct gz_stream *gz = ptr;
	size_t len;
	uint32_t word;
	int done = 0;

	for (;;) {
		len = sizeof(gz->gz_buf) - gz->gz_stream.avail_out;
		if (len > 0) {
			if (fwrite(gz->gz_buf, 1, len, gz->gz_f) != len) {
				/* must be an error */
				return (-1);
			}
			gz->gz_stream.next_out = gz->gz_buf;
			gz->gz_stream.avail_out = sizeof(gz->gz_buf);
		}
		if (done)
			break;

		switch (deflate(&gz->gz_stream, Z_FINISH)) {
		case Z_OK:
			break;
		case Z_STREAM_END:
			done = 1;
			break;
		default:
			errno = EIO;
			return (-1);
		}
	}

	word = htole32(gz->gz_crc);
	if (fwrite(&word, 1, sizeof(word), gz->gz_f) != sizeof(word))
		return (-1);
	word = htole32(gz->gz_stream.total_in);
	if (fwrite(&word, 1, sizeof(word), gz->gz_f) != sizeof(word))
		return (-1);

	if (deflateEnd(&gz->gz_stream) != Z_OK) {
		errno = EIO;
		return (-1);
	}

	if (fclose(gz->gz_f) != 0)
		return (-1);

	free(gz);

	return (0);
}
