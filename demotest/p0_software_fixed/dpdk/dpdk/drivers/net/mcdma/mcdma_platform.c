/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#include <rte_malloc.h>
#include <rte_spinlock.h>
#include "mcdma_access.h"
#include "mcdma_platform.h"
#include "mcdma.h"

uint32_t ifc_readl(void *ioaddr)
{
	return *(volatile uint32_t *)ioaddr;
}

void ifc_writel(void *ioaddr, uint32_t val)
{
	*(volatile uint32_t *)ioaddr = val;
}

uint64_t ifc_readll(void *ioaddr)
{
	return *(volatile uint64_t *)ioaddr;
}

void ifc_writell(void *ioaddr, uint64_t val)
{
	*(volatile uint64_t *)ioaddr = val;
}

void ifc_mcdma_reg_write(void *dev_hndl, uint32_t reg_offst, uint32_t val)
{
	struct ifc_mcdma_device *mcdma_dev;
	uint64_t bar_addr;

	mcdma_dev = ((struct rte_eth_dev *)dev_hndl)->data->dev_private;
	bar_addr = (uint64_t)mcdma_dev->bar_addr[IFC_MCDMA_CONFIG_BAR];
	*((volatile uint32_t *)(bar_addr + reg_offst)) = val;
}

uint32_t ifc_mcdma_reg_read(void *dev_hndl, uint32_t reg_offst)
{
	struct ifc_mcdma_device *mcdma_dev;
	uint64_t bar_addr;
	uint32_t val;

	mcdma_dev = ((struct rte_eth_dev *)dev_hndl)->data->dev_private;
	bar_addr = (uint64_t)mcdma_dev->bar_addr[IFC_MCDMA_CONFIG_BAR];
	val = *((volatile uint32_t *)(bar_addr + reg_offst));

	return val;
}

void ifc_mcdma_get_device_attr(void *dev_hndl, struct mcdma_dev_cap **dev_cap)
{
	struct ifc_mcdma_device *mcdma_dev;

	mcdma_dev = ((struct rte_eth_dev *)dev_hndl)->data->dev_private;
	*dev_cap = &mcdma_dev->ipcap;
}
