// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _IFC_QDMA_UTILS_H_
#define _IFC_QDMA_UTILS_H_
#include <dirent.h>

int ifc_qdma_strncpy(char *d, int dlen,  const char *s, int slen);

int ifc_qdma_get_mtime(const char *file_name, struct timespec *mtime);

int ifc_qdma_open(const char *file_name, int mode);

FILE* ifc_qdma_fopen(const char *file_name, const char *mode);

DIR *ifc_qdma_opendir(const char *dir_name);

int ifc_qdma_strsplit(char *string, int stringlen,
             char **tokens, int maxtokens, char delim);

#endif /* _IFC_QDMA_UTILS_H_ */
