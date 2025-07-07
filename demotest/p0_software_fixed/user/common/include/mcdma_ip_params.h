/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020-21, Intel Corporation. */

#ifndef _MCDMA_IP_PARAMS_H_
#define _MCDMA_IP_PARAMS_H_

/**
 * AXI-ST enablement
 */
#define IFC_QDMA_INTF_ST

/**
 * MSIX enable
 */
#define IFC_QDMA_MSIX_ENABLE


/**
 * Data width valid values are 128, 256, 512, 1024
 */
#define IFC_DATA_WIDTH   1024


/**
 * AVST Loopback Example design
 */
#define IFC_QDMA_ST_LOOPBACK




#undef IFC_QDMA_DYN_CHAN
#undef IFC_QDMA_TELEMETRY
#endif /* _MCDMA_IP_PARAMS_H_ */

