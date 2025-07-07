/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#ifndef _IFC_QDMA_UTILS_H_
#define _IFC_QDMA_UTILS_H_

int ifc_mcdma_strncpy(char *d, int dlen,  const char *s, int slen);

int ifc_mcdma_open(const char *file_name, int mode);

FILE *ifc_mcdma_fopen(const char *file_name, const char *mode);

#endif /* _IFC_QDMA_UTILS_H_ */
