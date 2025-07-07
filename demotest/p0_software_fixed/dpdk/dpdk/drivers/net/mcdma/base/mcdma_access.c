/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#include "mcdma_access.h"
#include "qdma_regs_2_registers.h"
#include "pio_reg_registers.h"
#include "mcdma_platform.h"

int ifc_mcdma_get_version(void *dev_hndl, uint32_t  *version_info)
{
	uint32_t reg_addr = 0x200070;
	*version_info = ifc_mcdma_reg_read(dev_hndl, reg_addr);

	return QDMA_SUCCESS;
}

int ifc_mcdma_get_device_capabilities(void *dev_hndl,
		struct mcdma_dev_cap *dev_info)
{
	if (!dev_hndl)
		return -QDMA_INVALID_PARAM_ERR;

	ifc_mcdma_reg_read(dev_hndl, QDMA_REGS_2_PF0_IP_PARAM_1);
	dev_info->num_pfs = 1;
	dev_info->num_chan = NUM_MAX_CHANNEL;

	return QDMA_SUCCESS;
}
