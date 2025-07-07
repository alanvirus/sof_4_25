/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#ifndef _MCDMA_PLATFORM_H_
#define _MCDMA_PLATFORM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "mcdma_access.h"

uint32_t ifc_readl(void *ioaddr);

void ifc_writel(void *ioaddr, uint32_t val);

uint64_t ifc_readll(void *ioaddr);

void ifc_writell(void *ioaddr, uint64_t val);

void ifc_mcdma_reg_write(void *dev_hndl, uint32_t reg_offst, uint32_t val);

uint32_t ifc_mcdma_reg_read(void *dev_hndl, uint32_t reg_offst);

void ifc_mcdma_get_device_attr(void *dev_hndl, struct mcdma_dev_cap **dev_cap);

#ifdef __cplusplus
}
#endif

#endif
