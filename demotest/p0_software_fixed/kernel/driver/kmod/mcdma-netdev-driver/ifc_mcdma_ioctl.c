// SPDX-License-Identifier: GPL-2.0
/*-
 * Copyright(c) 2020-21 Intel Corporation. All rights reserved.
 */

#include "ifc_mcdma_net.h"
#include "ifc_mcdma_ioctl.h"
#include "ifc_mcdma_bas.h"

static void ifc_mcdma_pio_write128(struct ifc_mcdma_dev_ctx *dev,
			 uint64_t offset, __uint128_t value)
{
	volatile __uint128_t *data;
	if (dev == NULL) {
		pr_err("Dev context is NULL\n");
		return;
	}

	/* PIO BAR base Addr*/
	data = (__uint128_t *)((char *)dev->pctx.info[2].iaddr + offset);

	*data = value;
	return;
}

static __uint128_t ifc_mcdma_pio_read128(struct ifc_mcdma_dev_ctx *qdev, uint64_t addr)
{
	volatile __uint128_t *data;
        __uint128_t temp;

        if (qdev == NULL) {
		pr_err("Dev context is NULL\n");
                return -1;
	}

        data = (__uint128_t *)((char *)(qdev->pctx.info[2].iaddr) + addr);
	temp = *data;

        return temp;
}

#if 0
static void ifc_mcdma_pio_write256(struct ifc_mcdma_dev_ctx *dev,
			 uint64_t offset, __iomem value)
{
#ifdef __AVX__
    asm volatile("vmovdqa %0,%%ymm2": :"m"(*data));

    asm volatile("vmovdqa %%ymm2,%0"
                 :"=m"(*(volatile uint8_t * __force) addr)
                 : :"memory");
#else /* !__AVX__ */
    ifc_mcdma_pio_write128(dev, offset, value);
    ifc_mcdma_pio_write128(dev, offset + 16, value + 16);
#endif /* !__AVX__ */
}
#endif


void ifc_mcdma_pio_write(struct ifc_mcdma_dev_ctx *dev,
			 uint64_t offset, uint64_t data)
{
	char *base = NULL;

	if (dev == NULL) {
		pr_err("Dev context is NULL\n");
		return;
	}

	/* PIO BAR base Addr*/
	base = (char *)dev->pctx.info[2].iaddr;
	writeq(data, base + offset);
}

uint64_t ifc_mcdma_pio_read(struct ifc_mcdma_dev_ctx *dev,
			    uint64_t offset)
{
	char *base = NULL;

	if (dev == NULL) {
		pr_err("Dev context is NULL\n");
		return 0;
	}

	/* PIO BAR base Addr*/
	base = (char *)dev->pctx.info[2].iaddr;
	return readq(base + offset);
}
#ifdef RHEL_RELEASE_CODE
int ifc_mcdma_netdev_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
#else
int ifc_mcdma_netdev_ioctl(struct net_device *netdev, struct ifreq *ifr, void __user *data, int cmd)
#endif
{
	struct ifc_mcdma_dev_ctx *dev = NULL;
	struct ifc_mcdma_netdev_priv_data __user *u_priv = ifr->ifr_data;
	struct ifc_mcdma_netdev_priv_data k_priv;
	
	dev = netdev_priv(netdev);
	if (dev == NULL) {
		pr_err("%s: Dev context is NULL\n", netdev->name);
		return	-EFAULT;
	}

	/* Copy User space data to Kernel */
	if (copy_from_user(&k_priv, u_priv, sizeof(k_priv)))
		return -EFAULT;
	switch (cmd) {
		case IFC_MCDMA_SET_VALUE_AT:
			if (k_priv.offset > dev->pctx.info[2].size) {
				pr_err("%s: Offset out of range\n", netdev->name);
				return -EFAULT;
			}
			ifc_mcdma_pio_write(dev, k_priv.offset, k_priv.data);
		break;
		case IFC_MCDMA_SET_VALUE128_AT:
			if (k_priv.offset > dev->pctx.info[2].size) {
				pr_err("%s: Offset out of range\n", netdev->name);
				return -EFAULT;
			}
			ifc_mcdma_pio_write128(dev, k_priv.offset, k_priv.data128);
		break;
#if 0
		case IFC_MCDMA_SET_VALUE256_AT:
			ifc_mcdma_pio_write256(dev, k_priv.offset, k_priv.data256);
		break;
#endif
		case IFC_MCDMA_GET_VALUE_AT:
			if (k_priv.offset > dev->pctx.info[2].size) {
				pr_err("%s: Offset out of range\n", netdev->name);
				return -EFAULT;
			}
		 	k_priv.data = ifc_mcdma_pio_read(dev, k_priv.offset);

			/* Copy Kernel space data to User space */
			if (copy_to_user(u_priv, &k_priv, sizeof(u_priv))) {
				pr_err("%s: Failed to copy data to user space\n",
					netdev->name);
				return -EFAULT;
			}
		break;
		case IFC_MCDMA_GET_VALUE128_AT:
			if (k_priv.offset > dev->pctx.info[2].size) {
				pr_err("%s: Offset out of range\n", netdev->name);
				return -EFAULT;
			}
			k_priv.data128 = ifc_mcdma_pio_read128(dev, k_priv.offset);

			/* Copy Kernel space data to User space */
			if (copy_to_user(u_priv, &k_priv, sizeof(struct ifc_mcdma_netdev_priv_data))) {
				pr_err("%s: Failed to copy data to user space\n",
					netdev->name);
				return -EFAULT;
			}
		break;
		case IFC_MCDMA_BAS_TX:
			ifc_bas_transfer_handler(IFC_MCDMA_DIR_TX, dev, &k_priv);
		break;
		case IFC_MCDMA_BAS_RX:
			ifc_bas_transfer_handler(IFC_MCDMA_DIR_RX, dev, &k_priv);
		break;
		default:
			pr_err("%s: Invalid IOCTL command\n", netdev->name);
			return -EFAULT;
		break;
	}

	return 0;
}

