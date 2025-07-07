/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020-21, Intel Corporation. */

#ifndef _IFC_MCDMA_UTILS_H_
#define _IFC_MCDMA_UTILS_H_

int ifc_mcdma_strncpy(char *d, int dlen,  const char *s, int slen);

int ifc_mcdma_get_mtime(const char *file_name, struct timespec *mtime);

int ifc_mcdma_open(const char *file_name, int mode);

FILE* ifc_mcdma_fopen(const char *file_name, const char *mode);

#endif /* _IFC_MCDMA_UTILS_H_ */
