// SPDX-License-Identifier: GPL-2.0
/*-
 * Copyright(c) 2020-21 Intel Corporation. All rights reserved.
 */

#include "ifc_mcdma_bas.h"

static int ifc_bas_tx_configure(struct ifc_mcdma_dev_ctx *dev,
				struct ifc_mcdma_netdev_priv_data *pd)
{
	uint64_t offset = 0x0;
	uint64_t val = 0;
	uint64_t ed_addr;
	int shift_off;

	if (dev == NULL) {
		pr_err("Null Dev Context\n");
		return -1 ;
	}

	pr_info("Configuring BAS Write region\n");

#ifdef IFC_MCDMA_X16
	pr_info("PCIe width: x16\n");
	shift_off = IFC_MCDMA_BAS_X16_SHIFT_WIDTH;
#else
	pr_info("PCIe width: x8\n");
	shift_off = IFC_MCDMA_BAS_X8_SHIFT_WIDTH;
#endif

	/* BAS Write configure */
	/* Configure physical address */
	pr_info("Configuring Physical address\n");
	ed_addr = IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off;
	ifc_mcdma_pio_write(dev, ed_addr, pd->data);

	/* Configure start address */
	pr_info("Configuring Start address\n");
	val = offset << shift_off;
	ed_addr = IFC_MCDMA_BAS_WRITE_ADDR << shift_off;
	ifc_mcdma_pio_write(dev, ed_addr, val);

	/* Configure burst count */
	pr_info("Configuring Burst count\n");
	val = (pd->burst_size / IFC_MCDMA_BAS_BURST_BYTES);
	ed_addr = IFC_MCDMA_BAS_WRITE_COUNT << shift_off;
	ifc_mcdma_pio_write(dev, ed_addr, val);

	/* Configure write control register */
	pr_info("Configuring Write control register\n");
#ifdef IFC_MCDMA_X16
	val = IFC_MCDMA_BAS_X16_BURST_LENGTH;
#else
	val = IFC_MCDMA_BAS_X8_BURST_LENGTH;
#endif
	val = ((val & ~IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_MASK) |
		IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_MASK);
	ed_addr = IFC_MCDMA_BAS_WRITE_CTRL << shift_off;
	ifc_mcdma_pio_write(dev, ed_addr, val);

	return val;
}

static int ifc_bas_rx_configure(struct ifc_mcdma_dev_ctx *dev,
				struct ifc_mcdma_netdev_priv_data *pd)
{
	uint64_t offset = 0x0;
	uint64_t val = 0;
	uint64_t ed_addr;
	int shift_off;

	if (dev == NULL) {
		pr_err("Null Dev Context\n");
		return -1 ;
	}

	pr_info("Configuring BAS Read region\n");

#ifdef IFC_MCDMA_X16
	pr_info("PCIe width: x16\n");
	shift_off = IFC_MCDMA_BAS_X16_SHIFT_WIDTH;
#else
	pr_info("PCIe width: x8\n");
	shift_off = IFC_MCDMA_BAS_X8_SHIFT_WIDTH;
#endif

	/* BAS Read configure */
	/* Configure physical address */
	pr_info("Configuring Physical address\n");
	ed_addr = IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off;
	ifc_mcdma_pio_write(dev, ed_addr, pd->data);

	/* Configure start address */
	pr_info("Configuring Start address\n");
	val = offset << shift_off;
	ed_addr = IFC_MCDMA_BAS_READ_ADDR << shift_off;
	ifc_mcdma_pio_write(dev, ed_addr, val);

	/* Configure burst count */
	pr_info("Configuring Burst count\n");
	val = (pd->burst_size / IFC_MCDMA_BAS_BURST_BYTES);
	ed_addr = IFC_MCDMA_BAS_READ_COUNT << shift_off;
	ifc_mcdma_pio_write(dev, ed_addr, val);

	/* Configure read control register */
	pr_info("Configuring Read control register\n");
#ifdef IFC_MCDMA_X16
	val = IFC_MCDMA_BAS_X16_BURST_LENGTH;
#else
	val = IFC_MCDMA_BAS_X8_BURST_LENGTH;
#endif
	val = ((val & ~IFC_MCDMA_BAS_READ_CTRL_ENABLE_MASK) |
		IFC_MCDMA_BAS_READ_CTRL_ENABLE_MASK);
	ed_addr = IFC_MCDMA_BAS_READ_CTRL << shift_off;
	ifc_mcdma_pio_write(dev, ed_addr, val);

	return val;
}
int ifc_bas_transfer_handler(int dir, struct ifc_mcdma_dev_ctx *dev,
			     struct ifc_mcdma_netdev_priv_data *pd)
{
	int ret = 0;

	if (dir == IFC_MCDMA_DIR_TX)
		ret = ifc_bas_tx_configure(dev, pd);
	else
		ret = ifc_bas_rx_configure(dev, pd);

	return ret;
}
