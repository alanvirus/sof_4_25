/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2020-21 Intel Corporation. All rights reserved.
 */
#ifndef _IFC_MCDMA_ETHTOOL_H_
#define _IFC_MCDMA_ETHTOOL_H_

#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

u32 ifc_mcdma_get_msglevel(struct net_device *netdev);
void ifc_mcdma_set_msglevel(struct net_device *netdev, u32 data);
void ifc_mcdma_get_drvinfo(struct net_device *dev,
					      struct ethtool_drvinfo *info);
void ifc_mcdma_set_ethtool_ops(struct net_device *netdev);

#endif	/* _IFC_MCDMA_NET_H_ */
