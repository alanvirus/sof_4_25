// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _IFC_ENV_H_
#define _IFC_ENV_H_

#define BIT_ULL(x)      (1ULL << x)

struct ifc_hugepage {
	void *virt_addr;
	uint64_t phy_addr;
	size_t size;
	char filename[PATH_MAX];
};

struct ifc_env_ctx {
	int nr_hugepages;
	int chunks;
	size_t chunk_size;
	pthread_mutex_t env_lock;
	struct ifc_hugepage *hugepages;
	uint64_t *hugepage_bitmap;
	int chunks_per_page;
};

#endif /* _IFC_ENV_H_ */
