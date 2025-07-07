// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ifc_qdma_utils.h>

int
ifc_qdma_strsplit(char *string, int stringlen,
             char **tokens, int maxtokens, char delim)
{
        int i, tok = 0;
        int tokstart = 1; /* first token is right at start of string */

        if (string == NULL || tokens == NULL)
                goto einval_error;

        for (i = 0; i < stringlen; i++) {
                if (string[i] == '\0' || tok >= maxtokens)
                        break;
                if (tokstart) {
                        tokstart = 0;
                        tokens[tok++] = &string[i];
                }
                if (string[i] == delim) {
                        string[i] = '\0';
                        tokstart = 1;
                }
        }
        return tok;

einval_error:
        errno = EINVAL;
        return -1;
}

int ifc_qdma_strncpy(char *d, int dlen,  const char *s, int slen)
{
	const char *overlapping;
	if ((d == NULL) || (dlen == 0))
		return -1;
	if ((s == NULL) || (slen == 0))
		return -1;
	overlapping = s;
	while (dlen > 0) {
		if (d == overlapping)
			return -1;
		if (slen == 0) {
			*d = '\0';
			return 0;
		}
		*d = *s;
		if (*d == '\0')
			return 0;
		dlen--;
		slen--;
		d++;
		s++;
	}
	return 0;
}

/* Get modification time of the file.
 * If successful, stores the mtime in '*mtime' and
 * returns 0.
 * On error, returns a positive errno value and updates mtime
 */
int
ifc_qdma_get_mtime(const char *file_name, struct timespec *mtime)
{
    struct stat s;

    if (!lstat(file_name, &s)) {
	if (S_ISLNK(s.st_mode))  {
        	mtime->tv_sec = mtime->tv_nsec = 0;
		return -1;
	}
        mtime->tv_sec = s.st_mtime;
        mtime->tv_nsec = s.st_mtim.tv_nsec;
        return 0;
    } else {
        mtime->tv_sec = mtime->tv_nsec = 0;
        return errno;
    }
}

DIR *ifc_qdma_opendir(const char *dir_name)
{
	struct stat before, after;
	DIR *dir;
	int fd;

	if (!lstat(dir_name, &before)) {
		if (S_ISLNK(before.st_mode))  {
			return NULL;
		}
	}
        dir = opendir(dir_name);
        if (!dir)
                return NULL;

	fd = dirfd(dir);
	if (fstat(fd, &after) == 0) {
		if (before.st_ino != after.st_ino) {
			close(fd);
			return NULL;
		}
	} else {
		close(fd);
		return NULL;
	}
	return dir;
}

/* opens the file
 */
int ifc_qdma_open(const char *file_name, int mode)
{
	struct stat before, after;
	int fd;
	int ret;

	ret = lstat(file_name, &before);
	if (ret == 0) {
		if (S_ISLNK(before.st_mode))
			return -1;
	}

        fd = open(file_name, mode, 0x666);
        if (fd < 0)
                return -1;

	if (ret == 0) {
		if(fstat(fd, &after) == 0) {
			if (before.st_ino != after.st_ino) {
				close(fd);
				return -1;
			}
		} else {
			close(fd);
			return -1;
		}
	}
	return fd;
}

/* fopens the file
 */
FILE* ifc_qdma_fopen(const char *file_name, const char *mode)
{
	struct stat s;
	FILE *f;

	if (!lstat(file_name, &s)) {
		if (S_ISLNK(s.st_mode))  {
			return NULL;
		}
	}
        f = fopen(file_name, mode);
	if(f == NULL) {
		printf("Unable to open file\n");
		return NULL;
	}
	return f;
}

