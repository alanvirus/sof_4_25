// SPDX-License-Identifier: GPL-2.0
/*-
 * Copyright(c) 2020-21 Intel Corporation. All rights reserved.
 */

#include <linux/version.h>
#include "ifc_mcdma_net.h"
#include "ifc_mcdma_ethtool.h"

u32 ifc_mcdma_get_msglevel(struct net_device *netdev)
{
	struct ifc_mcdma_dev_ctx *dev_ctx = netdev_priv(netdev);
	return dev_ctx->msg_enable;
}

void ifc_mcdma_set_msglevel(struct net_device *netdev, u32 data)
{
	struct ifc_mcdma_dev_ctx *dev_ctx = netdev_priv(netdev);
	dev_ctx->msg_enable = data;
}

void ifc_mcdma_get_drvinfo(struct net_device *dev,
					      struct ethtool_drvinfo *info)
{
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,8,0)
		strscpy(info->driver, DRV_SUMMARY, sizeof(info->driver));
		strscpy(info->version, DRV_VERSION, sizeof(info->version));
	#else
		strlcpy(info->driver, DRV_SUMMARY, sizeof(info->driver));
		strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	#endif
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,17,0)
static void
ifc_mcdma_get_ringparam(struct net_device *netdev, struct ethtool_ringparam *ring,
		struct kernel_ethtool_ringparam *kernel_ring,
		struct netlink_ext_ack *extack)
{
#else
static void
ifc_mcdma_get_ringparam(struct net_device *netdev, struct ethtool_ringparam *ring)
{
#endif
        struct ifc_mcdma_dev_ctx *dev_ctx = netdev_priv(netdev);

	if (dev_ctx == NULL) {
		netdev_err(netdev, "Invalid device context\n");
		return;
	}

        ring->rx_max_pending = IFC_MCDMA_MAX_DESC;
        ring->tx_max_pending = IFC_MCDMA_MAX_DESC;
        ring->rx_pending = dev_ctx->num_of_pages * 128;
        ring->tx_pending = dev_ctx->num_of_pages * 128;;

        ring->rx_mini_max_pending = 0;
        ring->rx_jumbo_max_pending = 0;
        ring->rx_mini_pending = 0;
        ring->rx_jumbo_pending = 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,17,0)
static int
ifc_mcdma_set_ringparam(struct net_device *netdev, struct ethtool_ringparam *ring,
		struct kernel_ethtool_ringparam *kernel_ring,
			struct netlink_ext_ack *extack)
{
#else
static int
ifc_mcdma_set_ringparam(struct net_device *netdev, struct ethtool_ringparam *ring)
{
#endif
	struct ifc_mcdma_dev_ctx *dev_ctx;
	int ret;

        if (ring->tx_pending > IFC_MCDMA_MAX_DESC ||
            ring->tx_pending < IFC_MCDMA_MIN_DESC ||
            ring->rx_pending > IFC_MCDMA_MAX_DESC ||
            ring->rx_pending < IFC_MCDMA_MIN_DESC) {
                netdev_err(netdev, "Descriptors requested (Tx: %d / Rx: %d) out of range: Range [%d-%d]\n",
                           ring->tx_pending, ring->rx_pending,
                           IFC_MCDMA_MIN_DESC, IFC_MCDMA_MAX_DESC);
                return -EINVAL;
        }

	dev_ctx = netdev_priv(netdev);
	if (dev_ctx == NULL) {
                pr_err("%s: dev_ctx NULL\n", netdev->name);
                return  -EFAULT;
        }

	if (ring->tx_pending % 128)
                return  -EFAULT;

	dev_ctx->num_of_pages = ring->tx_pending / 128;

	if (netdev->state == __LINK_STATE_NOCARRIER) {
                pr_info("%s: Ring parameters updated pages:%u\n", netdev->name, dev_ctx->num_of_pages);
		return 0;
	}

        pr_info("%s: Stopping all queues\n", netdev->name);
        netif_tx_stop_all_queues(netdev);

	/* Disable NAPI */
	ifc_mcdma_napi_disable_all(netdev);
	ifc_mcdma_del_napi(netdev);

        /* Free Channel Context */
        pr_info("%s: Freeing all queue contexts\n", netdev->name);
        ifc_mcdma_chnl_free_all(dev_ctx);

        ret = ifc_mcdma_chnl_init_all(dev_ctx);
        if (ret < 0) {
                pr_err("%s: Channel Initialization failed\n", netdev->name);
                kfree(dev_ctx->cores);
                return  -EFAULT; 
        }

	/* Enable NAPI */
	ifc_mcdma_add_napi(netdev);
	ifc_mcdma_napi_enable_all(netdev);

        pr_info("%s: All Channel Initialization Successful\n", netdev->name);
	return 0;
}

static void 
ifc_mcdma_get_channels(struct net_device *dev, struct ethtool_channels *ch) 
{
        struct ifc_mcdma_dev_ctx *dev_ctx = netdev_priv(dev);
#if 0
	uint32_t reg, max_chcnt;

	reg = readl(dev_ctx->qcsr + QDMA_REGS_2_PF0_IP_PARAM_2);
	if (dev_ctx->is_pf)
		max_chcnt = ((reg & QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_MASK) >>
                      QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_SHIFT);
	else
		max_chcnt = ((reg & QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_MASK) >>
                      QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_SHIFT);
#endif

        /* report maximum channels */
        ch->max_rx = dev_ctx->tot_chnl_avl;
        ch->max_tx = dev_ctx->tot_chnl_avl;

	/* TX and RX Queue count same as channel count */
        ch->max_combined = dev_ctx->max_rx_cnt;

        /* report current channels */
        ch->combined_count = dev_ctx->rx_chnl_cnt;
        ch->rx_count = dev_ctx->rx_chnl_cnt;
        ch->tx_count = dev_ctx->tx_chnl_cnt;

        /* report other queues */
        ch->other_count = 0;
        ch->max_other = 0;
}

static int
ifc_mcdma_set_channels(struct net_device *dev, struct ethtool_channels *ch)
{
	struct ifc_mcdma_dev_ctx *dev_ctx = netdev_priv(dev);

    if((dev_ctx->tot_chnl_avl < ch->rx_count) || (dev_ctx->tot_chnl_avl < ch->tx_count))
    {
        pr_err("invalid ch cnt in ethtool: rxc:%u txc:%u avl:%u\n", ch->rx_count, ch->tx_count,dev_ctx->tot_chnl_avl);
        return -1;
    }
    pr_err("TEST: rxc:%u txc:%u\n", ch->rx_count, ch->tx_count);
        /* report maximum channels */
        dev_ctx->rx_chnl_cnt = ch->rx_count;
        dev_ctx->tx_chnl_cnt = ch->tx_count;
        dev_ctx->chnl_cnt = ch->rx_count;

	return 0;
}


const struct ethtool_ops ifc_mcdma_ethtool_ops = {
	.get_msglevel		= ifc_mcdma_get_msglevel,
	.set_msglevel		= ifc_mcdma_set_msglevel,
	.get_drvinfo            = ifc_mcdma_get_drvinfo,
	.get_ts_info		= ethtool_op_get_ts_info,
	.get_link		= ethtool_op_get_link,
	.get_channels           = ifc_mcdma_get_channels,
	.set_channels           = ifc_mcdma_set_channels,
        .get_ringparam          = ifc_mcdma_get_ringparam,
        .set_ringparam          = ifc_mcdma_set_ringparam,
};

void ifc_mcdma_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &ifc_mcdma_ethtool_ops;
}
