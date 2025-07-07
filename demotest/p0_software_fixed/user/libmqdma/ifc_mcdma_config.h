// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _IFC_MCDMA_CONFIG_H_
#define _IFC_MCDMA_CONFIG_H_

#define IFC_QDMA_RESET_WAIT_COUNT       2048
#define PIO_BAR                 2
#define IRQ_FD_BITS             20
#define MAX_IRQ_FD              1 << 20
#define IFC_QDMA_MB             0x100000

/* MSI Support to BAS */
#define MSI_VEC_BITS 8
#define MSI_VEC_MASK 0xFF000000
#define MSI_IRQFD_MASK 0xFFFFFF
#define MSI_IRQFD_BITS 24

#ifdef IFC_QDMA_NUM_CHANNELS
#define NUM_MAX_CHANNEL         IFC_QDMA_NUM_CHANNELS
#else
#ifdef IFC_QDMA_INTF_ST
#define NUM_MAX_CHANNEL         2048
#else
#define NUM_MAX_CHANNEL         512
#endif
#endif

#define IFC_NUM_DESC_INDEXES            65536
#define IFC_TID_CNT_TO_CHEK_FOR_HOL     1
#define IFC_DIDX_MASK(n)                ((1 << n) -1)
#define IFC_ITE_CNT__MASK(n)            ((n & (~IFC_DIDX_MASK)) >> q->num_desc_bits)
#define IFC_CNT_HEAD_MOVE               (100000)
#define IFC_QBUF_CTX_INDEX(c, d)        (c * 2  + d)

#define MSIX_IRQFD_MASK 0xFFFFF
#define MSIX_DISABLE_INTR 0xFFFFE

#endif
