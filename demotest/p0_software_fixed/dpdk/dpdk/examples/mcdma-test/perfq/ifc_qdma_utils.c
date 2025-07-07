/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "./ifc_qdma_utils.h"


int ifc_mcdma_strncpy(char *d, int dlen,  const char *s, int slen)
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

/* opens the file
 */
int ifc_mcdma_open(const char *file_name, int mode)
{
	struct stat before, after;
	int ret = 0;
	int fd;

	ret = lstat(file_name, &before);
	if (ret == 0) {
		if (S_ISLNK(before.st_mode))
			return -1;
	}

	fd = open(file_name, mode);
	if (fd < 0)
		return -1;

	if (ret == 0) {
		fstat(fd, &after);
		if (before.st_ino != after.st_ino) {
			close(fd);
			return -1;
		}
	}
	return fd;
}

/* fopens the file
 */
FILE *ifc_mcdma_fopen(const char *file_name, const char *mode)
{
	struct stat s;
	FILE *f;

	if (!lstat(file_name, &s)) {
		if (S_ISLNK(s.st_mode))
			return NULL;
	}
	f = fopen(file_name, mode);
	return f;
}

