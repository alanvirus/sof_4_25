/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#ifndef MCDMA_ACCESS_H_
#define MCDMA_ACCESS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <mcdma_ip_params.h>

#define PCI_BAR_MAX	6

#ifdef IFC_QDMA_INTF_ST
#define NUM_MAX_CHANNEL         2048
#else
#define NUM_MAX_CHANNEL         512
#endif

#define QDMA_SUCCESS                           0
#define QDMA_INVALID_PARAM_ERR                 1
#undef  IFC_INTF_AVST_256_CHANNEL
struct mcdma_dev_cap {
	uint8_t num_pfs;
	uint16_t num_chan;
};

int ifc_mcdma_get_version(void *dev_hndl, uint32_t  *version_info);

int ifc_mcdma_get_device_capabilities(void *dev_hndl,
				 struct mcdma_dev_cap *dev_info);

#ifdef __cplusplus
}
#endif

#endif /* MCDMA_ACCESS_H_ */
