/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020-21, Intel Corporation. */

#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ifc_mcdma_utils.h>


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
	int fd;

	if (!lstat(file_name, &before)) {
		if (S_ISLNK(before.st_mode))  {
			return -1;
		}
	}
        fd = open(file_name, mode);
        if (fd < 0)
                return -1;
	fstat(fd, &after);
	if (before.st_ino != after.st_ino) {
		close(fd);
		return -1;
	}
	return fd;
}
