// SPDX-License-Identifier: GPL-2.0
/*-
 * Copyright(c) 2020-21 Intel Corporation. All rights reserved.
 */

#include "ifc_mcdma_net.h"
#include "ifc_mcdma_debugfs.h"

#ifdef ENABLE_DEBUGFS
static int __ifc_lbconfig_mapping_show(struct seq_file *s, void *data)
{
	struct ifc_mcdma_loopback_config *lbconfig =
		(struct ifc_mcdma_loopback_config *) (s->private);
	int i = 0;

	for (i = 0; i < 2048; i++) {
		if (lbconfig->mapping[i].valid)
			seq_printf(s, "%u => %u\n", i, lbconfig->mapping[i].chnl);
	}
	return 0;
}

ssize_t ifc_lbconfig_mapping_update(struct file *file,
			      const char __user * buffer,
			      size_t count, loff_t * ppos)
{
#ifdef IFC_MCDMA_LB_CFG
	struct ifc_mcdma_loopback_config *lbconfig = &ifc_mcdma_lb_config;
	uint32_t src = lbconfig->src_chnl;
	uint32_t dst = lbconfig->dst_chnl;
	lbconfig->mapping[src].valid  = 1;
	lbconfig->mapping[src].chnl  = dst;
	ifc_mcdma_update_lb_config(&ifc_mcdma_lb_config);
	pr_info("LB config update: src:%u --> dest:%u\n", src, dst);
#endif
	return count;
}

static int __ifc_mapping_update_open(struct seq_file *s, void *data)
{
	return 0;
}

static int __ifc_queue_stats_show(struct seq_file *s, void *data)
{
	struct net_device *netdev;
	struct ifc_mcdma_queue *q = (struct ifc_mcdma_queue *)(s->private);
	if (unlikely(q == NULL)) {
		pr_err("%s: Invalid queue context\n", __func__);
		return -1;
	}

	netdev = q->chnl->dev_ctx->netdev;

	seq_printf(s, "====================================================\n");
	seq_printf(s, "%s SW Stats: chnl:%u dir:%s\n", netdev->name,
		 q->chnl->channel_id, q->dir ? "H2D" : "D2H");
	seq_printf(s, "----------------------------------------------------\n");
	seq_printf(s, "TID updates : %llu\n", q->stats.tid_update);
	seq_printf(s, "processed : %llu\n", q->stats.processed);
	seq_printf(s, "ptail : %u\n", q->processed_tail);
	seq_printf(s, "tid skips : %u\n", q->stats.tid_skip);
	seq_printf(s, "lhead : %u\n", (volatile uint32_t)q->consumed_head);
	seq_printf(s, "shadow head : %u\n", q->head);
	seq_printf(s, "proc tail : %u\n", q->tail);
	seq_printf(s, "submitted didx : %u\n", q->stats.submitted_didx);
	seq_printf(s, "didx : %u\n", q->didx);
	seq_printf(s, "head : %u\n",
		 readl(q->qcsr + QDMA_REGS_2_Q_HEAD_POINTER));
	seq_printf(s, "compreg : %u\n",
		 readl(q->qcsr + QDMA_REGS_2_Q_COMPLETED_POINTER));
	seq_printf(s, "last intr head : %u\n", q->stats.last_intr_cons_head);
	seq_printf(s, "last intr reg : %u\n", q->stats.last_intr_reg);
	seq_printf(s, "----------------------------------------------------\n");
	return 0;
}

int ifc_queue_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, __ifc_queue_stats_show, inode->i_private);
}

int ifc_lbconfig_mapping_open(struct inode *inode, struct file *file)
{
	return single_open(file, __ifc_mapping_update_open, inode->i_private);
}

int ifc_lbconfig_mapping_show(struct inode *inode, struct file *file)
{
	return single_open(file, __ifc_lbconfig_mapping_show, inode->i_private);
}

static const struct file_operations ifc_mcdma_queue_stats_ops = {
	.open		= ifc_queue_stats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/* Loopback configuration */
static const struct file_operations ifc_mcdma_lbcfg_update_ops = {
	.open		= ifc_lbconfig_mapping_open,
	.write		= ifc_lbconfig_mapping_update,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations ifc_mcdma_lbcfg_show_ops = {
	.open		= ifc_lbconfig_mapping_show,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

#if defined(IFC_SELECT_QUEUE_ALGO)
static int __ifc_chnl_cfg_show(struct seq_file *s, void *data)
{
	struct net_device *netdev;
	struct ifc_mcdma_dev_ctx *dev_ctx;
	int i = 0;

	netdev = (struct net_device *)(s->private);
	if (unlikely(netdev == NULL)) {
		pr_err("%s: Invalid queue context\n", __func__);
		return -1;
	}

	dev_ctx = netdev_priv(netdev);
	if (dev_ctx == NULL) {
		pr_err("Invalid device context for debugfs setup");
		return  -EFAULT;
	}

	for (i = dev_ctx->start_chnl;i <= dev_ctx->end_chnl; i++) {
		seq_printf(s, "%u => %u\n", i, dev_ctx->channel_core_mapping[i]);
	}
	return 0;
}


/* Channel mapping configurations */
static ssize_t ifc_chnl_cfg_update(struct file *file,
			      const char __user * buffer,
			      size_t count, loff_t * ppos)
{
	struct net_device *netdev;
	struct ifc_mcdma_dev_ctx *dev_ctx;
	u32 i = 0;

	netdev = (struct net_device *)(file->f_inode->i_private);
	if (unlikely(netdev == NULL)) {
		pr_err("%s: Invalid queue context", netdev->name);
		return -1;
	}

	dev_ctx = netdev_priv(netdev);
	if (dev_ctx == NULL) {
		pr_err("%s: Invalid device context for debugfs setup",
			netdev->name);
		return  -EFAULT;
	}

	/* Check if input parameters are valid */
	if (dev_ctx->core >= dev_ctx->num_cores) {
		pr_err("%s: Invalid Core Number", netdev->name);
		return  -EFAULT;
	}

    if (((dev_ctx->end_chnl - dev_ctx->start_chnl) + 1) > dev_ctx->chnl_cnt)
    {
        pr_err("%s: Inv ch combination sch=%d,ech=%d,mxch=%d\n", netdev->name,dev_ctx->start_chnl,dev_ctx->end_chnl,dev_ctx->chnl_cnt);
        return  -EFAULT;
    }

	for (i = dev_ctx->start_chnl; i <= dev_ctx->end_chnl; i++) {
		ifc_refresh_core_info(dev_ctx, i, dev_ctx->core);
		dev_ctx->channel_core_mapping[i] = dev_ctx->core;
	}

	pr_info("%s: Core Channel mapping updated successfully\n", netdev->name);
	return count;
}

int ifc_chnl_cfg_update_open(struct inode *inode, struct file *file)
{
	return single_open(file, __ifc_mapping_update_open, inode->i_private);
}

int ifc_chnl_cfg_show(struct inode *inode, struct file *file)
{
	return single_open(file, __ifc_chnl_cfg_show, inode->i_private);
}

/* Channel to Core mapping configuration */
static const struct file_operations ifc_mcdma_chnl_cfg_update_ops = {
	.open		= ifc_chnl_cfg_update_open,
	.write		= ifc_chnl_cfg_update,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations ifc_mcdma_chnl_cfg_show_ops = {
	.open		= ifc_chnl_cfg_show,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int ifc_mcdma_chnl_map_debugfs_setup(struct net_device *netdev)
{
	struct ifc_mcdma_dev_ctx *dev_ctx;

	dev_ctx = netdev_priv(netdev);
	if (dev_ctx == NULL) {
		pr_err("Invalid device context for debugfs setup");
		return  -EFAULT;
	}
	debugfs_create_u32("start_chnl",
			S_IWUSR,
			dev_ctx->rootdir,
			(u32 *)&(dev_ctx->start_chnl));
	debugfs_create_u32("end_chnl",
			S_IWUSR,
			dev_ctx->rootdir,
			(u32 *)&(dev_ctx->end_chnl));
	debugfs_create_u32("core",
			S_IWUSR,
			dev_ctx->rootdir,
			(u32 *)&(dev_ctx->core));
	debugfs_create_file("map_update",
			S_IWUSR,
			dev_ctx->rootdir, netdev,
			&ifc_mcdma_chnl_cfg_update_ops);
	debugfs_create_file("map_show", S_IRUGO,
			dev_ctx->rootdir, netdev,
			&ifc_mcdma_chnl_cfg_show_ops);
	pr_debug("Added support for Loopback config DebugFS");
	return 0;
}
#endif

int ifc_mcdma_debugfs_setup(struct net_device *netdev)
{
	struct ifc_mcdma_dev_ctx *dev_ctx;
	struct ifc_mcdma_channel *chnl_ctx;
	struct ifc_mcdma_queue *tx, *rx;
	struct dentry *root;
	char *tx_fname_suffix = "H2D_queue_stats";
	char *rx_fname_suffix = "D2H_queue_stats";
	char file_name[30];
	int ret;
	int i;

	dev_ctx = netdev_priv(netdev);
	if (dev_ctx == NULL) {
		pr_err("Invalid device context for debugfs setup");
		return  -EFAULT;
	}
	
	/* DebugFS initialization */
	root = debugfs_create_dir(netdev->name, NULL);
	if (!root) {
		pr_err("Unable to create %s rootdir", netdev->name);
		return -ENOMEM;
	}
	dev_ctx->rootdir = root;

	for (i = 0; i < dev_ctx->chnl_cnt; i++) {
		chnl_ctx = dev_ctx->channel_context[i];
		if (unlikely(chnl_ctx == NULL)) {
			pr_err("Invalid channel context for debugfs setup");
			ret = -EFAULT;
			goto err_out;
		}
		tx = &chnl_ctx->tx;
		rx = &chnl_ctx->rx;
		if (unlikely(tx == NULL || rx == NULL)) {
			pr_err("Invalid channel queue context for debugfs setup");
			ret = -EFAULT;
			goto err_out;
		}

		memset(file_name, 0, 30 * sizeof(char));
		snprintf(file_name, 30, "Chnl%d_%s", i, tx_fname_suffix);
		if (!debugfs_create_file(file_name, S_IRUGO, root, tx,
				&ifc_mcdma_queue_stats_ops)) {
			ret = -ENOMEM;
			goto err_out;
		}

		memset(file_name, 0, 30 * sizeof(char));
		snprintf(file_name, 30, "Chnl%d_%s", i, rx_fname_suffix);
		if (!debugfs_create_file(file_name, S_IRUGO, root, rx,
				&ifc_mcdma_queue_stats_ops)) {
			ret = -ENOMEM;
			goto err_out;
		}
	}
#if defined(IFC_SELECT_QUEUE_ALGO)
	ifc_mcdma_chnl_map_debugfs_setup(netdev);
#endif
	pr_debug("Added support for DebugFS for %s", netdev->name);
	return 0;
err_out:
	debugfs_remove_recursive(dev_ctx->rootdir);
	return ret;
}

/* Loopback debugfs setup */
int ifc_mcdma_lb_debugfs_setup(void)
{
	/* DebugFS initialization */
	ifc_mcdma_lb_config.d = debugfs_create_dir("ifc_mcdma_config", NULL);
	if (ifc_mcdma_lb_config.d == NULL) {
		pr_err("Unable to create lbconfig dir");
		return -ENOMEM;
	}

	debugfs_create_u32("src_chnl",
			S_IWUSR,
			ifc_mcdma_lb_config.d,
			(u32 *)&(ifc_mcdma_lb_config.src_chnl));
	debugfs_create_u32("dst_chnl",
			S_IWUSR,
			ifc_mcdma_lb_config.d,
			(u32 *)&(ifc_mcdma_lb_config.dst_chnl));
	debugfs_create_file("update",
			S_IWUSR,
			ifc_mcdma_lb_config.d, &ifc_mcdma_lb_config,
			&ifc_mcdma_lbcfg_update_ops);
	debugfs_create_file("show", S_IRUGO,
			ifc_mcdma_lb_config.d, &ifc_mcdma_lb_config,
			&ifc_mcdma_lbcfg_show_ops);
	pr_debug("Added support for Loopback config DebugFS");
	return 0;
}

void ifc_mcdma_debugfs_remove(struct net_device *netdev)
{
	struct ifc_mcdma_dev_ctx *dev_ctx;
	dev_ctx = netdev_priv(netdev);
	debugfs_remove_recursive(dev_ctx->rootdir);
	pr_debug("Removed support for DebugFS for %s", netdev->name);
}
#endif /* DEBUG_STATS */
