// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _IFC_ENV_CONFIG_H_
#define _IFC_ENV_CONFIG_H_


#define PCI_BAR_MAX             6
#define MAX_REGIONS             8

#define UIO_MAX_DEVICE          40
#define UIO_PCI_VENDOR_ID       0x1172
#define UIO_PCI_DEVICE_ID       0x0000

/* request cache/allocator from hugemem */
#ifdef IFC_32BIT_SUPPORT
#define IFC_DESC_MEM                    (1<<20)
#define IFC_CTX_MEM                     (1<<19)
#else
#define IFC_DESC_MEM                    (1<<25)
#define IFC_CTX_MEM                     (1<<24)
#endif

#define IFC_HUGEPAGE_REQ_OFFSET         ((IFC_DESC_MEM) + (IFC_CTX_MEM))

#endif
