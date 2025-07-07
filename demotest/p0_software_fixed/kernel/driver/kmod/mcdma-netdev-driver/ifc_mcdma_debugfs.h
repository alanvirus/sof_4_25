/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2020-21 Intel Corporation. All rights reserved.
 */
#ifndef _IFC_MCDMA_DEBUGFS_H_
#define _IFC_MCDMA_DEBUGFS_H_

#ifdef ENABLE_DEBUGFS
#include <linux/debugfs.h>

int ifc_queue_stats_open(struct inode *inode, struct file *file);
int ifc_mcdma_debugfs_setup(struct net_device *netdev);
void ifc_mcdma_debugfs_remove(struct net_device *netdev);
int ifc_lbconfig_mapping_open(struct inode *inode, struct file *file);
int ifc_lbconfig_mapping_show(struct inode *inode, struct file *file);
ssize_t ifc_lbconfig_mapping_update(struct file *file,
			      const char __user * buffer,
			      size_t count, loff_t * ppos);
int ifc_mcdma_lb_debugfs_setup(void);
int ifc_chnl_cfg_update_open(struct inode *inode, struct file *file);
int ifc_chnl_cfg_show(struct inode *inode, struct file *file);
#endif

#endif	/* _IFC_MCDMA_NET_H_ */
