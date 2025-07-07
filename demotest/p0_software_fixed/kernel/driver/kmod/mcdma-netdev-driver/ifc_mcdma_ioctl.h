// SPDX-License-Identifier: GPL-2.0
/*-
 * Copyright(c) 2020-21 Intel Corporation. All rights reserved.
 */
#ifndef _IFC_MCDMA_IOCTL_H_
#define _IFC_MCDMA_IOCTL_H_

#include <net/sock.h>
#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/if.h>

#define IFC_MCDMA_SET_VALUE_AT		(SIOCDEVPRIVATE)
#define IFC_MCDMA_GET_VALUE_AT		(SIOCDEVPRIVATE + 1)
#define IFC_MCDMA_BAS_TX		(SIOCDEVPRIVATE + 2)
#define IFC_MCDMA_BAS_RX		(SIOCDEVPRIVATE + 3)
#define IFC_MCDMA_SET_VALUE128_AT	(SIOCDEVPRIVATE + 4)
#define IFC_MCDMA_GET_VALUE128_AT	(SIOCDEVPRIVATE + 5)
#define IFC_MCDMA_SET_VALUE256_AT	(SIOCDEVPRIVATE + 6)
#define IFC_MCDMA_GET_VALUE256_AT	(SIOCDEVPRIVATE + 7)

struct ifc_mcdma_netdev_priv_data {
	uint64_t data;
	__uint128_t data128;
	//__iomem data256;
	uint64_t offset;
	uint64_t burst_size;
};

#ifdef RHEL_RELEASE_CODE
int ifc_mcdma_netdev_ioctl(struct net_device *netdev,
			   struct ifreq *ifr, int cmd);
#else
int ifc_mcdma_netdev_ioctl(struct net_device *netdev,
             struct ifreq *ifr, void __user *data, int cmd);
#endif
uint64_t ifc_mcdma_pio_read(struct ifc_mcdma_dev_ctx *dev,
			    uint64_t offset);
void ifc_mcdma_pio_write(struct ifc_mcdma_dev_ctx *dev,
			 uint64_t offset, uint64_t data);
#endif	/* _IFC_MCDMA_IOCTL_H_ */
