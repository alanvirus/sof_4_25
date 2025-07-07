// SPDX-License-Identifier: GPL-2.0
/*-
 * Copyright(c) 2020-21 Intel Corporation. All rights reserved.
 */

#include "ifc_mcdma_net.h"
#include "ifc_mcdma_ethtool.h"
#include <net/sock.h>
#include <linux/rbtree.h>
#include <linux/cpumask.h>
#include <linux/netdevice.h>
#include <linux/version.h>
#include <linux/if_ether.h>
#include <linux/list.h>
#include "ifc_mcdma_ioctl.h"
#include <linux/byteorder/little_endian.h>

#define DEFAULT_MSG_ENABLE (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_IFUP | \
		NETIF_MSG_TX_DONE | NETIF_MSG_TX_ERR | NETIF_MSG_RX_ERR)

#define IFC_CNT_HEAD_MOVE	100000

static int debug = 3;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all)");

struct ifc_mcdma_loopback_config ifc_mcdma_lb_config;

#ifdef IFC_MCDMA_HEX_DUMP
static void ifc_mcdma_hex_dump(unsigned char *base, int len)
{
	int i;
	for (i = 0; i < len; i++) {
		if ((i % 16) == 0)
			pr_debug("\n%8lx ", (unsigned long) base + i);
		pr_debug("%02x ", base[i]);
	}
	pr_debug("\n");
}
#endif

#ifdef IFC_QDMA_INTF_ST
static int ifc_check_payload(int payload)
{
	int new_payload = 0;

/*This macro refers to data width of AVST interface between HIP and QDMA IP.
 * For 1024 data width required 128 bytes payload alignment.
 */
#if     (IFC_DATA_WIDTH == 1024)
        int remainder = payload % 128;
#else
        int remainder = payload % 64;
#endif

	if (remainder == 0)
		return payload;
#if     (IFC_DATA_WIDTH == 1024)
        /* Rounding up to the nearest multiple of a 128 */
        new_payload = payload + 128 - remainder;
#else
	/* Rounding up to the nearest multiple of a 64 */
	new_payload = payload + 64 - remainder;
#endif
	return new_payload;
}
#endif

#ifdef IFC_MCDMA_LB_CFG
void ifc_mcdma_update_lb_config(struct ifc_mcdma_loopback_config *lbconfig)
{
	/* set loopback config */
	struct ifc_mcdma_dev_ctx *mcdma_ctx = lbconfig->dev_ctx;
	char *base = (char *)mcdma_ctx->pctx.info[2].iaddr;

	writeq(lbconfig->dst_chnl,
		base + 0x10000 + (lbconfig->src_chnl * 8));
}

#ifdef IFC_MCDMA_LB_DFLT_CFG
static void ifc_mcdma_set_lb_config(struct ifc_mcdma_dev_ctx *dev_ctx, int pf_num)
{
	uint32_t src, dst;
	struct ifc_mcdma_loopback_config *lbconfig = &ifc_mcdma_lb_config;
	struct ifc_mcdma_dev_ctx *mcdma_ctx = lbconfig->dev_ctx;
	char *base = (char *)mcdma_ctx->pctx.info[2].iaddr;
	int i;

	if (pf_num % 2 == 0)
		return;

	/* set loopback config */
	pr_debug("Setting default config");
	for (i = 0; i < dev_ctx->chnl_cnt; i++) {
		src = (pf_num * dev_ctx->chnl_cnt) + i;
		dst = src - dev_ctx->chnl_cnt;

		lbconfig->mapping[src].valid  = 1;
		lbconfig->mapping[src].chnl  = dst;
		writeq(dst, base + 0x10000 + (src * 8));

		lbconfig->mapping[dst].valid  = 1;
		lbconfig->mapping[dst].chnl  = src;
		writeq(src, base + 0x10000 + (dst * 8));
	}
}
#endif
#endif

#ifdef IFC_MCDMA_DEBUG
/**
 *  * ifc_mcdma_print_qstats - display queue statistics
 *   */
static void ifc_mcdma_print_qstats(struct ifc_mcdma_queue *q)
{
	struct pci_dev *pdev;

	if (unlikely(q == NULL)) {
		pr_err("Invalid channel context\n");
		return;
	}

	if (unlikely(q->chnl->channel_id >= q->chnl->dev_ctx->chnl_cnt)) {
		pr_err("Invalid channel context\n");
		return;
	}
	pdev = q->chnl->dev_ctx->pctx.pdev;

	dev_info(&pdev->dev, "SW Stats: chnl:%u dir:%s, ", q->chnl->channel_id,
		 q->dir == IFC_MCDMA_DIR_RX ? "Rx" : "Tx");
	dev_info(&pdev->dev, "processed head:%u, ", q->cur_con_head);
	dev_info(&pdev->dev, "WB head :%u\n", (volatile uint32_t)
		 q->consumed_head);
	dev_info(&pdev->dev, "SW head :%u, ", q->head);
	dev_info(&pdev->dev, "SW tail :%u, ", q->tail);
	dev_info(&pdev->dev, "DIDX:%u, ", q->didx);
	dev_info(&pdev->dev, "HW head:%u, ",
		 readl(q->qcsr + QDMA_REGS_2_Q_HEAD_POINTER));
	dev_info(&pdev->dev, "WB reg:%u\n",
		 readl(q->qcsr + QDMA_REGS_2_Q_COMPLETED_POINTER));
}
#endif

static void ifc_mcdma_set_avoid_hol(void *dev_qcsr)
{
	/* avoid hol */
#ifdef IFC_MCDMA_HOL
	writel(0xff, dev_qcsr + QDMA_REGS_2_RSVD_9);
#else
	writel(0x0, dev_qcsr + QDMA_REGS_2_RSVD_9);
#endif
}

static inline int ifc_mcdma_get_mtu(struct ifc_mcdma_queue *q)
{
	unsigned int mtu;

	mtu = READ_ONCE(q->chnl->dev_ctx->netdev->mtu);
	mtu += ETH_HLEN;
	return mtu;
}

static int ifc_mcdma_construct_skb(struct ifc_mcdma_queue *q, int ring_idx)
{
	struct ifc_mcdma_dev_ctx *mcdma_ctx = q->chnl->dev_ctx;
	uint32_t pkt_len = ifc_mcdma_get_mtu(q);
	struct pci_dev *pdev;
	struct sk_buff *skb;

	pdev = q->chnl->dev_ctx->pctx.pdev;
	skb = netdev_alloc_skb(mcdma_ctx->netdev, pkt_len + NET_IP_ALIGN);
	if (skb == NULL) {
		pr_err("skb allocation failed\n");
		return -ENOMEM;
	} else {
		skb_reserve(skb, NET_IP_ALIGN);
		skb_put(skb, pkt_len);	    /* Make room */
	}
	if (skb->data == NULL) {
		pr_err("va is zero\n");
		return -ENOMEM;
	}

	skb_set_queue_mapping(skb, q->chnl->channel_id);
	q->ctx[ring_idx].skb = skb;
	q->ctx[ring_idx].phy_addr = virt_to_phys(skb->data);
	q->ctx[ring_idx].hw_addr = virt_to_phys(skb->data);
#ifdef IFC_MCDMA_IOMMU_SUPPORT
	q->ctx[ring_idx].dma_addr = dma_map_single(&(pdev->dev),
					skb->data, pkt_len + NET_IP_ALIGN,
					DMA_FROM_DEVICE);
        if (dma_mapping_error(&(pdev->dev),  q->ctx[ring_idx].dma_addr)) {
		pr_err("IOMMU mapping failed\n");
		return -ENOMEM;
	}
	q->ctx[ring_idx].hw_addr = (uint64_t)(q->ctx[ring_idx].dma_addr);
#endif
	return 0;
}

int ifc_mcdma_ip_reset(void *dev_qcsr)
{
	void *base;

	if (unlikely(dev_qcsr == NULL))
		return -EFAULT;

	base = dev_qcsr;
	mdelay(500UL);

	/* IP reset */
	writel(0x01, base + QDMA_REGS_2_SOFT_RESET);

	/* giving time for the FPGA reset to complete */
	mdelay(500UL);

	pr_debug("Successfully reset IP\n");
	return 0;
}

static int ifc_descq_empty_slots(struct ifc_mcdma_queue *q)
{
	int head = q->head;
	int tail = q->tail;
	int desc_per_page;
	int free_slots;
	int links;

	if (unlikely(q == NULL)) {
		pr_err("Chnl:%d:%d: Invalid Queue Context",
			q->chnl->channel_id, q->dir);
		return -EFAULT;
	}
	/* Completely empty? */
	if (q->head_didx == q->tail_didx)
		return q->num_desc_pages * 127;

	/* Full? */
	if ((q->head_didx != q->tail_didx) && (q->head == q->tail))
		return 0;

	desc_per_page = PAGE_SIZE / sizeof(struct ifc_mcdma_desc);

	/* #empty slots */
	if (head <= tail)
		free_slots = q->qlen - tail + head;
	else
		free_slots = head - tail;

	/* Remove links */
	links = (tail / desc_per_page - head / desc_per_page);

	if (head >= tail)
		free_slots += links;
	else
		free_slots = free_slots - (q->num_desc_pages - links);

	return free_slots;
}

static int
ifc_mcdma_desc_prep(struct ifc_mcdma_queue *q, uint64_t mem_hw_addr,
		    size_t pyld_cnt, struct sk_buff *skb)
{
	struct ifc_mcdma_desc *d;
	struct pci_dev *pdev;
	uint32_t tail;
	uint64_t addr = 0ULL;
	int ret;

	if (unlikely(q == NULL) ||
	    (q->dir == IFC_MCDMA_DIR_TX && mem_hw_addr == 0UL)) {
		pr_err("Chnl:%d:%d: Invalid Queue Context",
			q->chnl->channel_id, q->dir);
		return -EFAULT;
	}
	pdev = q->chnl->dev_ctx->pctx.pdev;

	if (pyld_cnt > IFC_MCDMA_BUF_LIMIT) {
		dev_err(&pdev->dev,
			"chnl:%u dir:%u buffer limit reached max %lu\n",
			q->chnl->channel_id, q->dir, pyld_cnt);
		return -EINVAL;
	}
	
	tail = q->tail;
	d = (struct ifc_mcdma_desc *)
	    ((uint64_t)q->ring_virt_addr + sizeof(*d) * tail);
	if (d->link) {	/* skip the link */
		tail = (tail + 1) % q->qlen;
		q->didx = ((q->didx + 1) % IFC_NUM_DESC_INDEXES);
		d = (struct ifc_mcdma_desc *)
		    ((uint64_t)q->ring_virt_addr + sizeof(*d) * tail);
	}

	memset(d, 0, sizeof(struct ifc_mcdma_desc));

	if (q->dir == IFC_MCDMA_DIR_RX) {
		/* ifc_mcdma_contruct_skb would return 0 on success */
		ret = ifc_mcdma_construct_skb(q, tail);
		if (ret != 0)
			return -ENOMEM;
		mem_hw_addr = q->ctx[tail].hw_addr;
	}

	/* book-keeping of xmit SKB to free on completion interrupt. */
	if (q->dir == IFC_MCDMA_DIR_TX) {
		if (unlikely(skb == NULL)) {
			dev_err(&pdev->dev,
				"chnl:%u dir:%u Invalid skb to SW context\n",
				q->chnl->channel_id, q->dir);
			return -EINVAL;
		}
		q->ctx[tail].skb = skb;
		q->ctx[tail].hw_addr = mem_hw_addr;
	}

#ifdef IFC_MCDMA_IOMMU_SUPPORT
	/* record length, and DMA address */
        dma_unmap_len_set(&(q->ctx[tail]), len, pyld_cnt);
        dma_unmap_addr_set(&(q->ctx[tail]), dma, mem_hw_addr);
#endif

	switch (q->dir) {
	case IFC_MCDMA_DIR_RX:
		d->src = addr;
		d->dest = mem_hw_addr;
		break;
	case IFC_MCDMA_DIR_TX:
		d->src = mem_hw_addr;
		d->dest = addr;
		break;
	default:
		break;
	}

	d->len = pyld_cnt;
	q->tail = (tail + 1) % q->qlen;
	q->didx = ((q->didx + 1) % IFC_NUM_DESC_INDEXES);
	d->didx = q->didx;
	d->wb_en = 0;
	d->msix_en = 0;
	d->pad_len = 0;
	q->tail_didx = q->didx;

	/* Setting to address small packets
	*  for batch size transfer, need to disable
	*/
	d->wb_en = 1;
	d->msix_en = 1;
	d->sof = 1;
	d->eof = 1;
	return 0;
}

static int ifc_mcdma_desc_submit(struct ifc_mcdma_queue *q)
{
	struct ifc_mcdma_desc *d;
	int index;
	int count = 0;
	uint32_t qhead;
#ifdef ENABLE_DEBUGFS
#ifdef IFC_MCDMA_DEBUG
	uint32_t cnt;
#endif
#endif

	if (unlikely(q == NULL)) {
		pr_err("Chnl:%d:Invalid Queue Context\n", q->chnl->channel_id);
		return -EFAULT;
	}

	index = q->tail - 1;
	d = (struct ifc_mcdma_desc *)
	    ((uint64_t)q->ring_virt_addr + sizeof(*d) * index);

	/* update completion mechanism */
	d->msix_en = q->ire;
	if (q->wbe || q->ire)
		d->wb_en = 1;

	/* this code handles the scenario,when same tid update may happen
         due to the device taking more time to process data with higher payloads */
	if (q->tail == q->processed_tail) {
		writel(q->qlen, q->qcsr + QDMA_REGS_2_Q_TAIL_POINTER);
		while (count < IFC_CNT_HEAD_MOVE) {
			qhead = readl(q->qcsr + QDMA_REGS_2_Q_HEAD_POINTER);
			if ((qhead % 128) == 0)
				break;
			count++;
		}
	}

#ifdef ENABLE_DEBUGFS
#ifdef IFC_MCDMA_DEBUG
	cnt = (q->tail <= q->processed_tail) ?
		(q->qlen - 1 - q->processed_tail + q->tail) :
		(q->tail - q->processed_tail);
	q->stats.tid_update += cnt;
	q->stats.submitted_didx = q->didx;
#endif
#endif

	/* update tail pointer */
	writel(q->tail, q->qcsr + QDMA_REGS_2_Q_TAIL_POINTER);

	/* update last update tail */
	q->processed_tail = q->tail;

	return 0;
}

static int ifc_descq_processed_count(struct ifc_mcdma_queue *q, int *cur_head)
{
	int head;
	int prev_head;

	if (q->wbe || q->ire)
		head = q->consumed_head;
	else
		head = readl(q->qcsr + QDMA_REGS_2_Q_COMPLETED_POINTER);

	prev_head = q->cur_con_head;
	q->cur_con_head = head;

	if (cur_head)
		*cur_head = head % q->qlen;

	return (head >= prev_head) ?
		(head - prev_head) : (IFC_NUM_DESC_INDEXES - prev_head + head);
}

static int ifc_mcdma_queue_poll(struct ifc_mcdma_queue *q)
{
	int desc_per_page;
	int links;
	int head;
	int nr;

	if (unlikely(q == NULL)) {
		pr_err("Chnl:%d:%d: Invalid Queue Context",
			q->chnl->channel_id, q->dir);
		return -EFAULT;
	}

	desc_per_page = PAGE_SIZE / sizeof(struct ifc_mcdma_desc);
	nr = ifc_descq_processed_count(q, &head);
	q->head_didx = q->consumed_head;
	links = (head / desc_per_page - q->head / desc_per_page);
	return nr;
}

static int ifc_mcdma_queue_ring_fill(struct ifc_mcdma_queue *q)
{
	uint64_t mem_hw_addr = 0UL;
	size_t pyld_cnt = ifc_check_payload(ifc_mcdma_get_mtu(q));
	int count = 0;
	int nr_segs;
	int i, ret;

	if (unlikely(q == NULL)) {
		pr_err("Chnl:%d:%d: Invalid Queue Context",
			q->chnl->channel_id, q->dir);
		return -EINVAL;
	}

	/* check if descq have enough empty slots */
	nr_segs = ifc_descq_empty_slots(q);
	if (nr_segs <= 0) {
		pr_debug("cid:%u dir:%u No empty slots for submission\n"
			 , q->chnl->channel_id, q->dir);
		return -ENOMEM;
	}

	for (i = 0; i < nr_segs; i++) {
		ret = ifc_mcdma_desc_prep(q, mem_hw_addr, pyld_cnt, NULL);
		if (ret < 0) {
			pr_err("Failed to prepare after %d new descriptors: nr_segs:%d", i, nr_segs);
			break;
		}
		count++;
	}
	/* Submit the newly created descriptors */
	if (i > 0)
		ifc_mcdma_desc_submit(q);

	return count;
}

static int ifc_mcdma_process_comp_pckts(struct ifc_mcdma_queue *q, int nr)
{
	struct ifc_mcdma_desc_sw *sw;
	struct ifc_mcdma_desc *desc;
	struct net_device *netdev;
	struct pci_dev *pdev;
	int i = 0;
	int t;

	if (unlikely(q == NULL)) {
		pr_err("Chnl:%d:%d: Invalid Queue Context",
			q->chnl->channel_id, q->dir);
		return -EFAULT;
	}

	netdev = q->chnl->dev_ctx->netdev;
	if (unlikely(q == NULL)) {
		pr_err("Invalid netdev context\n");
		return -EFAULT;
	}
	pdev = q->chnl->dev_ctx->pctx.pdev;
	if (unlikely(pdev == NULL)) {
		pr_err("Invalid PCIe Context\n");
		return -EFAULT;
	}

	desc = (struct ifc_mcdma_desc *)
		(q->ring_virt_addr + sizeof(*desc) * q->head);

	if (q->dir != IFC_MCDMA_DIR_TX && q->dir != IFC_MCDMA_DIR_RX) {
		pr_err("Invalid queue direction.");
		return -EFAULT;
	}

	for (t = q->head; i < nr; t = (t + 1) % q->qlen) {
		if (desc->link) {
			if (desc->src == q->ring_hw_addr)
				desc =
				(struct ifc_mcdma_desc *)(q->ring_virt_addr);
			else
				desc++;
			--nr;
			continue;
		}
		sw = (struct ifc_mcdma_desc_sw *)&q->ctx[t];
		i++;
		if (sw->skb == NULL) {
			pr_err("ERR: NULL skb: bad DS\n");
			if (q->dir == IFC_MCDMA_DIR_RX)
				netdev->stats.rx_dropped++;
			else if (q->dir == IFC_MCDMA_DIR_TX)
				netdev->stats.tx_dropped++;
			break;
		}
#ifdef IFC_MCDMA_HEX_DUMP
		ifc_mcdma_hex_dump((unsigned char *)sw->skb->data,
				   sw->skb->len);
#endif
		if (q->dir == IFC_MCDMA_DIR_RX) {
			sw->skb->len = desc->rx_pyld_cnt;
#ifdef SKB_HEX_DUMP
			pr_debug("%s:RX--------------------------", netdev->name);
			pr_debug("SKB virtual address: %p physical address: %llx",
				sw->skb->data, q->ctx[t].hw_addr);
			print_hex_dump(KERN_ERR, "", DUMP_PREFIX_ADDRESS, 16, 1,
					sw->skb->data, sw->skb->len, true);
			pr_debug("---------------------------------------------");
#endif
			sw->skb->protocol = eth_type_trans(sw->skb,
						q->chnl->dev_ctx->netdev);
#ifdef IFC_MCDMA_IOMMU_SUPPORT
			dma_unmap_single(&(pdev->dev),
					dma_unmap_addr(sw, dma),
					dma_unmap_len(sw, len),
					DMA_FROM_DEVICE);
#endif
			if (netif_rx(sw->skb) != NET_RX_SUCCESS) {
				netdev->stats.rx_dropped++;
			} else {
				netdev->stats.rx_packets++;
				netdev->stats.rx_bytes += sw->skb->len;
			}
		}
		if (q->dir == IFC_MCDMA_DIR_TX) {
			netdev->stats.tx_packets++;
			netdev->stats.tx_bytes += sw->skb->len;
#ifdef IFC_MCDMA_IOMMU_SUPPORT
			dma_unmap_single(&(pdev->dev),
					dma_unmap_addr(sw, dma),
					dma_unmap_len(sw, len),
					DMA_TO_DEVICE);
#endif
			dev_consume_skb_any(sw->skb);
		}
		sw->skb = NULL;
		desc++;
	}
	/* Update queue head to descriptor which is processed by SW */
	q->head = t;
	return 0;
}

static irqreturn_t ifc_irq_handler(int irq, void *arg)
{
	struct ifc_mcdma_queue *q;

	if (unlikely(arg == NULL)) {
		pr_err("Invalid Interrupt context\n");
		return IRQ_NONE;
	}
	q = (struct ifc_mcdma_queue *)arg;

	/* Schedule NAPI */
        napi_schedule(&q->napi);

	return IRQ_HANDLED;
}

static int ifc_mcdma_process_completions(struct ifc_mcdma_queue *q)
{
	int ret, nr = 0;

	if (unlikely(q == NULL)) {
		pr_err("Chnl:%d:%d: Invalid Queue Context",
			q->chnl->channel_id, q->dir);
		return -EFAULT;
	}

	nr = ifc_mcdma_queue_poll(q);
	ifc_mcdma_process_comp_pckts(q, nr);

	if (q->dir == IFC_MCDMA_DIR_RX) {
		ret = ifc_mcdma_queue_ring_fill(q);
		if (ret < 0)
			return 0;
	}

	return nr;
}

static int ifc_irq_handler_alloc(struct ifc_msix_info *msix_info,
				 struct ifc_mcdma_queue *q)
{
	unsigned long irq_flags = (IRQF_NO_THREAD | IRQF_NOBALANCING |
				   IRQF_ONESHOT | IRQF_IRQPOLL);
	int irq_index;
	int ret;

	if (unlikely(q == NULL || msix_info == NULL)) {
		pr_err("Invalid Queue Context or msix info\n");
		return -EFAULT;
	}

	irq_index = (q->chnl->channel_id * 4) + ((1 - q->dir) * 2);

	/* Register irq handler */
	ret = request_irq(msix_info->table[irq_index].vector, ifc_irq_handler,
			  irq_flags, IFC_MCDMA_DRIVER_NAME, q);
	if (ret < 0) {
		pr_err("Failed to register irq handler: %d\n", ret);
		return -1;
	}

	q->irq_index = irq_index;
	pr_debug("Registered IRQ handler: nvectors: %d, irq_index: %d\n",
		 msix_info->nvectors, irq_index);
	return 0;
}

static void ifc_irq_handler_free(struct ifc_msix_info *msix_info,
				 struct ifc_mcdma_queue *q)
{
	int irq_index;

	if (unlikely(q == NULL || msix_info == NULL)) {
		pr_err("Invalid Queue Context or msix info\n");
		return;
	}

	irq_index = q->irq_index;
	free_irq(msix_info->table[irq_index].vector, q);
	pr_debug("Unregistered IRQ handler: nvectors: %d, irq_index: %d\n",
		 msix_info->nvectors, q->irq_index);
}

static int q_size_bits(uint32_t val)
{
	int i;

	for (i = 8 * sizeof(uint32_t) - 1; i >= 0; i--) {
		if (val >> i)
			return i;
	}
	return -1;
}

static int ifc_mcdma_napi_poll(struct napi_struct *napi, int budget)
{
	struct ifc_mcdma_queue *que;
	uint32_t nr;

	que = container_of(napi, struct ifc_mcdma_queue, napi);
	if (que == NULL) {
		pr_err("Invalid Queue Context \n");
		return 0;
	}

	nr = ifc_mcdma_process_completions(que);
	if (nr < budget)
		napi_complete_done(napi, nr);
	if ( nr >= budget) {
		#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,7,0)
			napi_schedule(napi);
		#else
			napi_reschedule(napi);
		#endif
	}
	return min_t(int, nr, budget);
}

void ifc_mcdma_napi_disable_all(struct net_device *netdev)
{
	u16 i;
	struct ifc_mcdma_channel **chnl_ctx_list;
	struct ifc_mcdma_dev_ctx *dev_ctx;

	dev_ctx = netdev_priv(netdev);
	if (dev_ctx == NULL) {
		pr_err("%s: dev_ctx NULL\n", netdev->name);
		return;
	}

	chnl_ctx_list = dev_ctx->channel_context;

	for (i = 0; i < dev_ctx->chnl_cnt; i++) {
		pr_info("%s: disabling NAPI: chno:%u \n", netdev->name, i);
		napi_disable(&chnl_ctx_list[i]->rx.napi);
		napi_disable(&chnl_ctx_list[i]->tx.napi);
	}

}

void ifc_mcdma_napi_enable_all(struct net_device *netdev)
{
	u16 i;
	struct ifc_mcdma_channel **chnl_ctx_list;
	struct ifc_mcdma_dev_ctx *dev_ctx;

	dev_ctx = netdev_priv(netdev);
	if (dev_ctx == NULL) {
		pr_err("%s: dev_ctx NULL\n", netdev->name);
		return;
	}

	chnl_ctx_list = dev_ctx->channel_context;

	for (i = 0; i < dev_ctx->chnl_cnt; i++) {
		pr_debug("%s: enabling NAPI chno:%u \n", netdev->name, i);
		napi_enable(&chnl_ctx_list[i]->tx.napi);
		napi_enable(&chnl_ctx_list[i]->rx.napi);
	}

}

/* Register NAPI handler for device */
void ifc_mcdma_del_napi(struct net_device *netdev)
{
	struct ifc_mcdma_channel **chnl_ctx_list;
	struct ifc_mcdma_dev_ctx *dev_ctx;
	int i = 0;

	dev_ctx = netdev_priv(netdev);
	if (dev_ctx == NULL) {
		pr_err("%s: dev_ctx NULL\n", netdev->name);
		return;
	}

	chnl_ctx_list = dev_ctx->channel_context;

	/* Set NAPI for TX Queue */
	for (i = 0; i < dev_ctx->chnl_cnt; i++) {
		pr_debug("%s: Removing NAPI cid:%u\n", netdev->name, chnl_ctx_list[i]->tx.chnl->channel_id);
		netif_napi_del(&(chnl_ctx_list[i]->tx.napi));
		netif_napi_del(&(chnl_ctx_list[i]->rx.napi));
	}
}

/* Register NAPI handler for device */
void ifc_mcdma_add_napi(struct net_device *netdev)
{
	struct ifc_mcdma_channel **chnl_ctx_list;
	struct ifc_mcdma_dev_ctx *dev_ctx;
	int i = 0;

	dev_ctx = netdev_priv(netdev);
	if (dev_ctx == NULL) {
		pr_err("%s: dev_ctx NULL\n", netdev->name);
		return;
	}

	chnl_ctx_list = dev_ctx->channel_context;

	/* Set NAPI for TX Queue */
	for (i = 0; i < dev_ctx->chnl_cnt; i++) {
		pr_debug("%s: Adding NAPI cid:%u\n", netdev->name, chnl_ctx_list[i]->tx.chnl->channel_id);
		#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,1,0)
			netif_napi_add(netdev, &(chnl_ctx_list[i]->tx.napi),
				ifc_mcdma_napi_poll);
			netif_napi_add(netdev, &(chnl_ctx_list[i]->rx.napi),
				ifc_mcdma_napi_poll);
		#else
			netif_napi_add(netdev, &(chnl_ctx_list[i]->tx.napi),
				ifc_mcdma_napi_poll, NAPI_POLL_WEIGHT);
			netif_napi_add(netdev, &(chnl_ctx_list[i]->rx.napi),
				ifc_mcdma_napi_poll, NAPI_POLL_WEIGHT);
		#endif

	}
}

static int ifc_descq_init(struct ifc_mcdma_queue *q)
{
	QDMA_REGS_2_Q_CTRL_t q_ctrl = { { 0 } };
	struct ifc_pci_dev *pci_ctx;
	struct ifc_mcdma_desc *desc;
	struct ifc_mcdma_desc *temp_desc;
	struct ifc_mcdma_desc_sw *sw;
	int desc_per_page;
	uint64_t cons_head;
	uint32_t log_len;
	int ret;
	int i;
#ifdef IFC_QDMA_INTF_ST
	int payload;
#endif

	if (unlikely(q == NULL)) {
		pr_err("Chnl:%d:%d: Invalid Queue Context",
			q->chnl->channel_id, q->dir);
		return -EFAULT;
	}

	desc_per_page = PAGE_SIZE / sizeof(struct ifc_mcdma_desc);
	q->qlen = q->num_desc_pages * desc_per_page;

	/* Allocate the ring buffer */
	pci_ctx = &q->chnl->dev_ctx->pctx;
	q->ring_virt_addr = dma_alloc_coherent(&pci_ctx->pdev->dev,
			q->num_desc_pages * PAGE_SIZE,
			&q->ring_dma_addr, GFP_KERNEL);
	if (q->ring_virt_addr == NULL)
		return -ENOMEM;

	log_len = q_size_bits(q->qlen);
	q->num_desc_bits = log_len;
#ifdef IFC_MCDMA_IOMMU_SUPPORT
	q->ring_hw_addr = (uint64_t)q->ring_dma_addr;
#else
	q->ring_hw_addr = virt_to_phys(q->ring_virt_addr);
#endif
	temp_desc = (struct ifc_mcdma_desc*)q->ring_hw_addr;

	pr_debug("Chnl:%d:%d:Desc per page:%d, Qlen:%d, Log len:%d\n",
		 q->chnl->channel_id, q->dir, desc_per_page, q->qlen, log_len);

	/* Allocate memory to hold onto the
	 * requests passed from the application
	 */
	q->ctx = (struct ifc_mcdma_desc_sw *)
		kcalloc(q->qlen, sizeof(*sw), GFP_KERNEL);
	if (q->ctx == NULL)
		goto fail_cleanup;

	/* Link the descriptor pages */
	desc = (struct ifc_mcdma_desc *)q->ring_virt_addr;
	for (i = 0; i < q->qlen; i++)
		desc[i].link = 0;

	desc[i - 1].src = q->ring_hw_addr;
	desc[i - 1].link = 1;

	for (i = desc_per_page; i < q->qlen; i += desc_per_page) {
		desc[i - 1].link = 1;
		desc[i - 1].src = (uint64_t)&temp_desc[i];
	}

	/* Program regs */
	writel((uint32_t)q->ring_hw_addr,
	       q->qcsr + QDMA_REGS_2_Q_START_ADDR_L);
	writel(q->ring_hw_addr >> 32, q->qcsr + QDMA_REGS_2_Q_START_ADDR_H);
	writel(log_len, q->qcsr + QDMA_REGS_2_Q_SIZE);
	writel(0U, q->qcsr + QDMA_REGS_2_Q_TAIL_POINTER);

	/* Configure address for write backs from HW */
	cons_head = (uint64_t)dma_map_single(&(pci_ctx->pdev->dev),
				&q->consumed_head, sizeof(int),
				DMA_FROM_DEVICE);
        if (dma_mapping_error(&(pci_ctx->pdev->dev), cons_head)) {
		pr_err("IOMMU mapping failed\n");
		goto fail_cleanup;
	}

	writel((uint32_t)cons_head,
	       q->qcsr + QDMA_REGS_2_Q_CONSUMED_HEAD_ADDR_L);
	writel(cons_head >> 32, q->qcsr + QDMA_REGS_2_Q_CONSUMED_HEAD_ADDR_H);

	/* Configure batch delay */
	writel(IFC_MCDMA_Q_BATCH_DELAY, q->qcsr + QDMA_REGS_2_Q_BATCH_DELAY);

#ifdef IFC_QDMA_INTF_ST
	/* Configure paylaod */
	payload = ifc_check_payload(ifc_mcdma_get_mtu(q));
	writel(payload, q->qcsr + QDMA_REGS_2_Q_DEBUG_STATUS_4);
#endif
	/* Configure the control register */
	q_ctrl.field.q_en = 1;
	q_ctrl.field.q_intr_en = 1;
	q_ctrl.field.q_wb_en = 1;
	q->ire = 1;
	q->wbe = 0;

	/* Register irq handler */
	ret = ifc_irq_handler_alloc(&q->chnl->dev_ctx->msix_info, q);
	if (ret < 0)
		goto fail_cleanup;

	/* Write in QCSR register */
	writel(q_ctrl.val, q->qcsr + QDMA_REGS_2_Q_CTRL);
	return 0;
fail_cleanup:
	dma_free_coherent(&pci_ctx->pdev->dev, q->num_desc_pages * PAGE_SIZE,
			  q->ring_virt_addr, q->ring_dma_addr);
	return -1;
}

static int ifc_queue_reset(struct ifc_mcdma_queue *q)
{
	ktime_t start, now;
	int val;

	if (unlikely(q == NULL)) {
		pr_err("Chnl:%d:%d: Invalid Queue Context",
			q->chnl->channel_id, q->dir);
		return -EFAULT;
	}

	/* Assert reset */
	writel(1, q->qcsr + QDMA_REGS_2_Q_RESET);
	start = ktime_get();

	/* Wait for reset to deassert */
	while (1) {
		val = readl(q->qcsr + QDMA_REGS_2_Q_RESET);
		if (!val)
			return 0;
		now = ktime_get();
		if (ktime_to_ms(ktime_sub(now, start)) > 5)
			break;
	}
	return -1;
}

static int
ifc_queue_init(struct ifc_mcdma_queue *q, int dir, void *qcsr)
{
	int ret;

	if (unlikely(q == NULL)) {
		pr_err("Chnl:%d:%d: Invalid Queue Context",
			q->chnl->channel_id, q->dir);
		return -EFAULT;
	}

	q->qcsr = qcsr;
	q->num_desc_pages = q->chnl->dev_ctx->num_of_pages;
	q->dir = dir;

	/* Reset the queue */
	ret = ifc_queue_reset(q);
	if (ret < 0) {
		pr_err("cid:%u dir:%u Queue Reset failed\n",
		       q->chnl->channel_id, q->dir);
		return ret;
	}

	/* Init descriptor ring buffer */
	ret = ifc_descq_init(q);
	if (ret < 0) {
		pr_err("cid:%u dir:%u Descriptor init failed\n",
		       q->chnl->channel_id, q->dir);
		return ret;
	}

	/* Prepare Descriptors to receive data on Rx*/
	if (q->dir == IFC_MCDMA_DIR_RX) {
		ret = ifc_mcdma_queue_ring_fill(q);
		if (ret < 0) {
			pr_err("cid:%u dir:%u Ring prefilling failed\n",
			       q->chnl->channel_id, q->dir);
			return ret;
		}
	}
	return 0;
}

static void ifc_queue_free(struct ifc_mcdma_queue *q)
{
	struct ifc_pci_dev *pci_ctx;
        int ret = 0;

	if (unlikely(q == NULL)) {
		pr_err("Chnl:%d:%d: Invalid Queue Context",
			q->chnl->channel_id, q->dir);
		return;
	}

        /* Reset the queue */
        ret = ifc_queue_reset(q);
        if (ret < 0) {
              pr_err("%s cid:%u dir:%u Queue Reset failed\n",
              q->chnl->dev_ctx->netdev->name,q->chnl->channel_id, q->dir);
      }

	/* Disable descriptor fetching */
	writel(0U, q->qcsr + QDMA_REGS_2_Q_CTRL);

	/* Free IRQ handler */
	if (q->ire)
		ifc_irq_handler_free(&q->chnl->dev_ctx->msix_info, q);

	/* Deallocate the ring buffer */
	pci_ctx = &q->chnl->dev_ctx->pctx;
	dma_free_coherent(&pci_ctx->pdev->dev, q->num_desc_pages * PAGE_SIZE,
			  q->ring_virt_addr, q->ring_dma_addr);
	kfree(q->ctx);

	pr_debug("Chnl%d: %s Queue successfully freed\n", q->chnl->channel_id,
		 q->dir == IFC_MCDMA_DIR_TX ? "Tx" : "Rx");
}

/* TODO: Remove channel_bitmap */
static void ifc_mcdma_chnl_put(struct ifc_mcdma_dev_ctx *dctx, int chnl_id)
{
	dctx->channel_bitmap[chnl_id] = false;
	dctx->channel_context[chnl_id] = NULL;
}

static int ifc_chnl_init(struct ifc_mcdma_channel *chnl_ctx, void *dev_qcsr)
{
	int ret;

	if (unlikely(chnl_ctx == NULL || dev_qcsr == NULL)) {
		pr_err("Invalid Channel Context\n");
		return -EFAULT;
	}

	chnl_ctx->tx.chnl = chnl_ctx;
	chnl_ctx->rx.chnl = chnl_ctx;

	/* Init the queue contexts */
	ret = ifc_queue_init(&chnl_ctx->rx, IFC_MCDMA_DIR_RX,
				   dev_qcsr + chnl_ctx->channel_id * 256);
	if (ret < 0) {
		pr_err("Chnl%d: Failed to initialize Rx context\n",
		       chnl_ctx->channel_id);
		return ret;
	}
	ret = ifc_queue_init(&chnl_ctx->tx, IFC_MCDMA_DIR_TX,
			     dev_qcsr + (1UL << 19) +
			     chnl_ctx->channel_id * 256);
	if (ret < 0) {
		pr_err("Chnl%d: Failed to initialize Tx context\n",
		       chnl_ctx->channel_id);
		return ret;
	}
	/* Setting HOL for test purpose*/
	ifc_mcdma_set_avoid_hol(dev_qcsr);

	pr_debug("Chnl%d: Queue Initialization successful\n",
		chnl_ctx->channel_id);

	return 0;
}

static void ifc_chnl_free(struct ifc_mcdma_channel *chnl_ctx)
{
	struct ifc_pci_dev *pci_ctx;

	pci_ctx = &chnl_ctx->dev_ctx->pctx;
	if (unlikely(pci_ctx == NULL)) {
		pr_err("Invalid PCI Context\n");
		return;
	}

#ifdef IFC_MCDMA_DEBUG
	/* print qstats */
	ifc_mcdma_print_qstats(&chnl_ctx->rx);
	ifc_mcdma_print_qstats(&chnl_ctx->tx);
#endif

	/* Free the queue contexts */
	ifc_queue_free(&chnl_ctx->rx);
	ifc_queue_free(&chnl_ctx->tx);

	ifc_mcdma_chnl_put(chnl_ctx->dev_ctx, chnl_ctx->channel_id);
	kfree(chnl_ctx);
}

int ifc_mcdma_chnl_init_all(struct ifc_mcdma_dev_ctx *dev_ctx)
{
	struct ifc_mcdma_channel **chnl_ctx_list;
	struct ifc_mcdma_channel *chnl_ctx;
	int chnl_id;
	int ret;
	int i;

	if (unlikely(dev_ctx == NULL)) {
		pr_err("Invalid Dev Context\n");
		return -1;
	}

	chnl_ctx_list = dev_ctx->channel_context;
	mutex_init(&dev_ctx->chnl_lock);

	pr_debug("%s: Starting Initialization of all channnels\n",
		  dev_ctx->netdev->name);

	for (i = 0; i < dev_ctx->chnl_cnt; i++) {
		chnl_id = i;
		/* Allocate memory for the channel context */
		chnl_ctx = kzalloc(sizeof(struct ifc_mcdma_channel), GFP_KERNEL);
		if (chnl_ctx == NULL) {
			pr_err("%s: Mem alloc failed\n", dev_ctx->netdev->name);
			goto err_chnl_malloc_fail;
		}

		chnl_ctx->channel_id = chnl_id;
		chnl_ctx->dev_ctx = dev_ctx;
		chnl_ctx_list[i] = chnl_ctx;

		/* Init Channel Context */
		ret = ifc_chnl_init(chnl_ctx, dev_ctx->qcsr);
		if (ret < 0) {
			pr_err("%s: Chnl:%d: Failed to initialize channel\n",
				dev_ctx->netdev->name, chnl_id);
			goto err_out_chnl_free;
		}
		pr_info("%s: Chnl:%d: Initialization Successful\n",
				dev_ctx->netdev->name, chnl_id);
	}
	pr_debug("%s: All Channnels Initialized\n", dev_ctx->netdev->name);
#ifdef IFC_MCDMA_LB_CFG
#ifdef IFC_MCDMA_LB_DFLT_CFG
	ifc_mcdma_set_lb_config(dev_ctx, PCI_FUNC(dev_ctx->pctx.pdev->devfn));
#endif
#endif
	pr_debug("%s: Starting all queues\n", dev_ctx->netdev->name);
	netif_tx_start_all_queues(dev_ctx->netdev);
	return 0;

err_chnl_malloc_fail:
	i--;
err_out_chnl_free:
	while (i) {
		ifc_chnl_free(chnl_ctx_list[i]);
		chnl_ctx_list[i--] = NULL;
	}
	return -1;
}

static void ifc_core_free(struct ifc_core_info *core)
{
	struct ifc_queue_alloc *q, *n;

	list_for_each_entry_safe(q, n, &core->lru_list, list) {
		list_del(&q->list);
		if (q->hash)
			rb_erase(&q->node, &core->root);
		kfree(q);
	}

	/* Clear out the mapping & allocated queues */
	memset(core->queues_mapped, 0, sizeof(bool) * IFC_MCDMA_CHANNEL_MAX);
	memset(core->queues_allocated, 0, sizeof(bool) * IFC_MCDMA_CHANNEL_MAX);
}

void ifc_core_free_all(struct ifc_mcdma_dev_ctx *dev_ctx)
{
	struct ifc_core_info *core;
	int i;

	for (i = 0; i < dev_ctx->num_cores; i++) {
		core = &dev_ctx->cores[i];
		ifc_core_free(core);
	}

	/* Clear out queue_core_mapping */
	memset(dev_ctx->queue_core_mapping, 0, sizeof(int) * IFC_MCDMA_CHANNEL_MAX);
}

void ifc_mcdma_chnl_free_all(struct ifc_mcdma_dev_ctx *dev_ctx)
{
	struct ifc_mcdma_channel **chnl_ctx_list;
	int i;

	if (dev_ctx == NULL) {
		pr_err("Invalid dev_ctx\n");
		return;
	}

	chnl_ctx_list = dev_ctx->channel_context;
	for (i = 0; i < dev_ctx->chnl_cnt; i++) {
		if (chnl_ctx_list[i])
			ifc_chnl_free(chnl_ctx_list[i]);
		chnl_ctx_list[i] = NULL;
	}
	ifc_core_free_all(dev_ctx);
}

static int ifc_mcdma_netdev_open(struct net_device *netdev)
{
	struct ifc_mcdma_dev_ctx *dev_ctx;
	int ret,i;
	struct ifc_core_info *core;
	struct ifc_queue_alloc *q, *n;

	dev_ctx = netdev_priv(netdev);
	if (dev_ctx == NULL) {
		pr_err("%s: dev_ctx NULL\n", netdev->name);
		return	-EFAULT;
	}
	/* newly added here*/
    /* Default: All Chnls mapped to core0 */
	core = &dev_ctx->cores[0];
	for (i = 0; i < dev_ctx->chnl_cnt; i++) {
		core->queues_mapped[i] = true;

		/* Allocate new queue rb node */
		q = kzalloc(sizeof(struct ifc_queue_alloc), GFP_KERNEL);
		if (q == NULL)
			goto err_out;

		/* Populate node */
		q->queue_id = i;
                //printk("q=%d qid=%d\n",i,q->queue_id);
		/* Insert it into LRU list */
		list_add_tail(&q->list, &core->lru_list);
	}
	/* newly added here ends */

	dev_ctx->gcsr = (void *)((uint64_t)dev_ctx->pctx.info[0].iaddr + 0x200000);
	dev_ctx->is_pf = true;
	ret = ifc_mcdma_chnl_init_all(dev_ctx);
	if (ret < 0) {
		pr_err("%s: Channel Initialization failed\n", netdev->name);
		kfree(dev_ctx->cores);
		return	-EFAULT;
	}
	pr_debug("%s: All Channel Initialization Successful\n", netdev->name);

#ifdef ENABLE_DEBUGFS
	ret = ifc_mcdma_debugfs_setup(netdev);
	if (ret < 0) {
		pr_err("Failed to setup debugfs\n");
		ifc_mcdma_chnl_free_all(dev_ctx);
		kfree(dev_ctx->cores);
		return -EFAULT;
	}
#endif

	ifc_mcdma_add_napi(netdev);
	ifc_mcdma_napi_enable_all(netdev);

	return 0;

	err_out:
	list_for_each_entry_safe(q, n, &core->lru_list, list) {
		list_del(&q->list);
		kfree(q);
	}
	return -ENOMEM;
}

static int ifc_mcdma_netdev_close(struct net_device *netdev)
{
	struct ifc_mcdma_dev_ctx *dev_ctx;

	pr_debug("%s: Stopping all queues\n", netdev->name);
	netif_tx_stop_all_queues(netdev);

	dev_ctx = netdev_priv(netdev);
	if (dev_ctx == NULL) {
		pr_err("%s: dev_ctx NULL\n", netdev->name);
		return	-EFAULT;
	}
#ifdef ENABLE_DEBUGFS
	/* Remove DebugFS entries */
	ifc_mcdma_debugfs_remove(netdev);
#endif
	/* Disable NAPI */
	ifc_mcdma_napi_disable_all(netdev);
	ifc_mcdma_del_napi(netdev);

	/* Free Channel Context */
	pr_debug("%s: Freeing all queue contexts\n", netdev->name);
	ifc_mcdma_chnl_free_all(dev_ctx);
	return 0;
}

#ifdef MCDMA_ALIGN_ADDRESS
static void skb_align(struct sk_buff *skb, int align)
{
	int off = ((unsigned long)skb->data) & (align - 1);

	if (off)
		skb_reserve(skb, align - off);
}

static struct sk_buff *tx_skb_align_workaround(struct net_device *netdev,
					       struct sk_buff *skb)
{
	struct sk_buff *new_skb;

	/* Alloc new skb */
	#if     (IFC_DATA_WIDTH == 1024)
                new_skb = netdev_alloc_skb(netdev, skb->len + 128);
        #else
		new_skb = netdev_alloc_skb(netdev, skb->len + 64);
	#endif

	if (!new_skb)
		return NULL;

	/* Make sure new skb is properly aligned */
	#if     (IFC_DATA_WIDTH == 1024)
                skb_align(new_skb, 128);
        #else
		skb_align(new_skb, 64);
	#endif

	/* Copy data to new skb ... */
	skb_copy_from_linear_data(skb, new_skb->data, skb->len);
	skb_put(new_skb, skb->len);

	/* ... and free an old one */
	dev_kfree_skb_any(skb);

	return new_skb;
}
#endif

static netdev_tx_t ifc_mcdma_netdev_xmit(struct sk_buff *skb,
					 struct net_device *netdev)
{
	struct ifc_mcdma_dev_ctx *dev_ctx;
	struct ifc_mcdma_channel *chnl_ctx;
	struct ifc_mcdma_queue *q;
	uint64_t mem_hw_addr;
	struct pci_dev *pdev;
#ifdef IFC_MCDMA_IOMMU_SUPPORT
	dma_addr_t dma_addr;
#endif
	int chnl_id;
	int ret = 0;

	chnl_id = skb_get_queue_mapping(skb);
	netif_stop_subqueue(netdev, chnl_id);

	dev_ctx = netdev_priv(netdev);
	if (dev_ctx == NULL)
		goto tx_busy_wake_queue;

	chnl_ctx = dev_ctx->channel_context[chnl_id];
	if (chnl_ctx == NULL)
		goto tx_busy_wake_queue;

       q = &chnl_ctx->tx;

       /* check if descq is full */
       if (ifc_descq_empty_slots(q) <= 0) {
               /*dev_err(&pdev->dev, "chnl:%u dir:%u desc ring is full\n",
                       q->chnl->channel_id, q->dir);*/
               goto tx_busy_wake_queue;
       }

#ifdef MCDMA_ALIGN_ADDRESS
	/* Align the SKB to 128 byte aligned physical address */
        #if     (IFC_DATA_WIDTH == 1024)
                if (virt_to_phys(skb->data) % 0x80) {
        #else
        /* Align the SKB to 64 byte aligned physical address */
                if (virt_to_phys(skb->data) % 0x40) {
        #endif
                skb = tx_skb_align_workaround(netdev, skb);
                if (!skb)
                        goto tx_busy_wake_queue;
        }
#endif
	pdev = q->chnl->dev_ctx->pctx.pdev;
	mem_hw_addr = virt_to_phys(skb->data);
#ifdef IFC_MCDMA_IOMMU_SUPPORT
	dma_addr = dma_map_single(&(pdev->dev),
				skb->data, skb->len,
				DMA_TO_DEVICE);
        if (dma_mapping_error(&(pdev->dev), dma_addr)) {
		pr_err("IOMMU mapping failed\n");
		goto tx_busy_wake_queue;
	}
	mem_hw_addr = (uint64_t)dma_addr;
#endif

	ret = ifc_mcdma_desc_prep(q, mem_hw_addr, skb->len, skb);
	if (ret < 0) {
		pr_err("Failed to prepare descriptor\n");
		netdev->stats.tx_dropped++;
		goto tx_busy_wake_queue;
	}

#ifdef SKB_HEX_DUMP
	pr_debug("%s:TX--------------------------", netdev->name);
	pr_debug("SKB virtual address: %p physical address: %llx", skb->data,
						mem_hw_addr);
	print_hex_dump(KERN_ERR, "", DUMP_PREFIX_ADDRESS, 16, 1,
		       skb->data, skb->len, true);
	pr_debug("-------------------------------------------------------------");
#endif
	ret = ifc_mcdma_desc_submit(q);
	if (ret < 0) {
		pr_err("Failed to submit descriptor");
		netdev->stats.tx_dropped++;
		goto tx_busy_wake_queue;
	}
	skb_tx_timestamp(skb);

	netif_wake_subqueue(netdev, chnl_id);
	return NETDEV_TX_OK;

tx_busy_wake_queue:
	netif_wake_subqueue(netdev, chnl_id);
	return NETDEV_TX_BUSY;
}

static struct net_device_stats
	*ifc_mcdma_netdev_get_stats(struct net_device *netdev)
{
	return &netdev->stats;
}

#ifdef IFC_SELECT_QUEUE_ALGO
static struct ifc_queue_alloc *rb_search_hash(struct ifc_core_info *core, u32 hash)
{
	struct rb_root *root = &core->root;
	struct rb_node *node = root->rb_node;
	struct ifc_queue_alloc *cur_node, *rb_qcache = core->rb_qcache;

	/* Check the cache first */
	if (rb_qcache != NULL && hash == rb_qcache->hash)
		return rb_qcache;

	while (node) {
		cur_node = rb_entry(node, struct ifc_queue_alloc, node);

		if (hash < cur_node->hash)
			node = node->rb_left;
		else if (hash > cur_node->hash)
			node = node->rb_right;
		else {
			core->rb_qcache = cur_node;
			return cur_node;
		}
	}
	return NULL;
}

static int rb_insert_hash(struct rb_root *root, struct ifc_queue_alloc *q)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	struct ifc_queue_alloc *this;

	/* Figure out where to put new node */
	while (*new) {
		this = rb_entry(*new, struct ifc_queue_alloc, node);
		parent = *new;

		if (q->hash < this->hash)
			new = &((*new)->rb_left);
		else if (q->hash > this->hash)
			new = &((*new)->rb_right);
		else
			return false;
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&q->node, parent, new);
	rb_insert_color(&q->node, root);

	return true;
}

static int alloc_tx_queue(struct ifc_mcdma_dev_ctx *dev_ctx, u32 hash, int cpu)
{
	struct ifc_queue_alloc *q;
	struct ifc_core_info *core;

	core = &dev_ctx->cores[cpu];
	q = list_first_entry_or_null(&core->lru_list, struct ifc_queue_alloc, list);
	if (q == NULL)
		return -1;

	list_del(&q->list);
	if (q->hash) {
		rb_erase(&q->node, &core->root);
		if (core->rb_qcache && q->hash == core->rb_qcache->hash)
			core->rb_qcache = NULL;
	}

	/* Populate the rb node */
	q->hash = hash;

	/* Insert it into RB tree & LRU list */
	rb_insert_hash(&core->root, q);
	list_add_tail(&q->list, &core->lru_list);
	core->rb_qcache = q;
	return q->queue_id;
}
#else

static inline u32 ifc_mcdma_netdev_get_hash(struct sk_buff *skb)
{
	struct flow_keys flow;
	u32 hash;
	static u32 hashrnd __read_mostly;

	net_get_random_once(&hashrnd, sizeof(hashrnd));

	if (!skb_flow_dissect_flow_keys(skb, &flow, 0))
		return 0;

	if (flow.basic.n_proto == htons(ETH_P_IP))
		hash = jhash2((u32 *)&flow.addrs.v4addrs, 2, hashrnd);
	else if (flow.basic.n_proto == htons(ETH_P_IPV6))
		hash = jhash2((u32 *)&flow.addrs.v6addrs, 8, hashrnd);
	else
		hash = 0;

	skb_set_hash(skb, hash, PKT_HASH_TYPE_L3);

	return hash;
}

static inline int ifc_mcdma_netdev_get_tx_queue(struct net_device *ndev,
				      struct sk_buff *skb, int old_idx)
{
	int qid;

	qid = ifc_mcdma_netdev_get_hash(skb) &
				   (IFC_MCDMA_H2D_MAP_TABLE_SIZE - 1);

	return qid;
}

#endif

#if 0
static void show_core_info(struct ifc_mcdma_dev_ctx *dev_ctx)
{
	struct ifc_queue_alloc *q;
	struct ifc_core_info *core;
	int i;

	pr_info("%s: Core-Channel map after updation", dev_ctx->netdev->name);
	for (i = 0; i < dev_ctx->num_cores; i++) {
		core = &dev_ctx->cores[i];
		list_for_each_entry(q, &core->lru_list, list) {
			pr_info("%s:core:%d --> chnl:%d",
				dev_ctx->netdev->name, i, q->queue_id);
		}
	}
}
#endif

static int
update_core_mapping(struct ifc_core_info *prev_core,
			struct ifc_core_info *new_core, int qid)
{
	struct ifc_queue_alloc *q, *n;

	list_for_each_entry_safe(q, n, &prev_core->lru_list, list) {
		if (q->queue_id != qid)
			continue;
		list_del(&q->list);
		if (q->hash) {
			rb_erase(&q->node, &prev_core->root);
			if (prev_core->rb_qcache && q->hash == prev_core->rb_qcache->hash)
				prev_core->rb_qcache = NULL;
			q->hash = 0U;
		}
		break;
	}
	list_add_tail(&q->list, &new_core->lru_list);
	return 0;

}

/* Please ensure calling this function before updating queue_core_mapping */
int
ifc_refresh_core_info(struct ifc_mcdma_dev_ctx *dev_ctx, int qid, int ncoreid)
{
	struct ifc_core_info *prev_core, *new_core;
	int pcoreid;

	pcoreid = dev_ctx->queue_core_mapping[qid];
	prev_core = &dev_ctx->cores[pcoreid];
	new_core = &dev_ctx->cores[ncoreid];

	mutex_lock(&dev_ctx->chnl_lock);

	netif_stop_subqueue(dev_ctx->netdev, qid);
	prev_core->queues_mapped[qid] = false;
	new_core->queues_mapped[qid] = true;
	update_core_mapping(prev_core, new_core, qid);
        dev_ctx->queue_core_mapping[qid] = ncoreid;
	netif_wake_subqueue(dev_ctx->netdev, qid);

	mutex_unlock(&dev_ctx->chnl_lock);
	return 0;
}

#ifdef IFC_SELECT_QUEUE_ALGO
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0)
static u16 ifc_mcdma_select_queue(struct net_device *dev, struct sk_buff *skb,
                         void *accel_priv, select_queue_fallback_t fallback)
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5, 2, 0)
static u16 ifc_mcdma_select_queue(struct net_device *dev, struct sk_buff *skb,
			struct net_device *sb_dev, select_queue_fallback_t fallback)
#else
	static u16 ifc_mcdma_select_queue(struct net_device *dev, struct sk_buff *skb,
			struct net_device *sb_dev)
#endif
{
	u16 qid;

	struct ifc_mcdma_dev_ctx *dev_ctx = netdev_priv(dev);
	struct ifc_queue_alloc *q;
	struct ifc_core_info *core;
	u32 hash;
	int cpu;
	int ret;

	if (skb->sk && skb->sk->sk_hash)
		hash = skb->sk->sk_hash;
	else
		hash = skb_get_hash(skb);

	cpu = smp_processor_id();

	/* Search in the RB tree if that hash is already present */
	core = &dev_ctx->cores[cpu];
	q = rb_search_hash(core, hash);
	if (q == NULL) {
		/*  If not found allocate a new queue */
		ret = alloc_tx_queue(dev_ctx, hash, cpu);
		qid = ret == -1 ? dev_ctx->chnl_cnt - 1 : ret;
	} else
		qid = q->queue_id;

    #if 0
	qid = sk_tx_queue_get(skb->sk);

	if (qid == 65535 || skb->ooo_okay) {
		/* use cache locality of reference */
		if (skb_rx_queue_recorded(skb))
			qid = skb_get_rx_queue(skb);
		else
			qid = ifc_mcdma_netdev_get_tx_queue(dev, skb, qid);
	}
#endif
        return qid;
}
#endif

static int ifc_mcdma_change_mtu(struct net_device *dev, int new_mtu)
{
	/* MTU change not possible if device is in use */
	if (dev->flags & IFF_UP)
		return -EBUSY;

	/* Validate the new MTU */
	if (new_mtu <= 0 || new_mtu > IFC_MCDMA_BUF_LIMIT)
		return -EINVAL;

	WRITE_ONCE(dev->mtu, new_mtu);
	pr_debug("%s: Changed MTU to %d successfully", dev->name, new_mtu);
	return 0;
}

static const struct net_device_ops ifc_mcdma_netdev_ops = {
	.ndo_open		= ifc_mcdma_netdev_open,
	.ndo_stop		= ifc_mcdma_netdev_close,
	.ndo_start_xmit		= ifc_mcdma_netdev_xmit,
#ifdef IFC_SELECT_QUEUE_ALGO
	.ndo_select_queue       = ifc_mcdma_select_queue,
#endif
#ifdef RHEL_RELEASE_CODE
	.ndo_change_mtu_rh74	= ifc_mcdma_change_mtu,
#else
	.ndo_change_mtu		= ifc_mcdma_change_mtu,
#endif
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_get_stats		= ifc_mcdma_netdev_get_stats,
#ifdef RHEL_RELEASE_CODE
        .ndo_do_ioctl           = ifc_mcdma_netdev_ioctl,
#else
        .ndo_siocdevprivate     = ifc_mcdma_netdev_ioctl,
#endif

};

int ifc_mcdma_netdev_setup(struct net_device *dev,u32 tot_chn_aval)
{
	struct ifc_mcdma_dev_ctx *dev_ctx = netdev_priv(dev);
	struct ifc_core_info *core;
	int err = 0;
	int i;

	strncpy(dev->name, NETDEV_NAME, sizeof(dev->name) - 1);

	/* Initialize the device structure. */
	dev->netdev_ops = &ifc_mcdma_netdev_ops;
	ifc_mcdma_set_ethtool_ops(dev);

	/* Fill in device structure with ethernet-generic values. */
	dev->features	|= NETIF_F_FRAGLIST;
	dev->features	|= NETIF_F_GSO_SOFTWARE;
	dev->hw_features |= dev->features;
	dev->hw_enc_features |= dev->features;
	dev_ctx->msg_enable = netif_msg_init(debug, DEFAULT_MSG_ENABLE);
	eth_hw_addr_random(dev);

	/* Update the number of pages */
	dev_ctx->num_of_pages = IFC_MCDMA_DESC_PAGES;

	/* Allocate memory for core info */
	dev_ctx->num_cores = num_online_cpus();
	dev_ctx->cores = kcalloc(dev_ctx->num_cores, sizeof(struct ifc_core_info), GFP_KERNEL);
	if (dev_ctx->cores == NULL) {
		pr_err("%s: Memory allocation fail\n", dev->name);
		return -ENOMEM;
	}

         /* Set max channels */
         dev_ctx->max_rx_cnt = tot_chn_aval;
         dev_ctx->max_tx_cnt = tot_chn_aval;
         dev_ctx->rx_chnl_cnt = tot_chn_aval;
         dev_ctx->tx_chnl_cnt = tot_chn_aval;
         dev_ctx->chnl_cnt = tot_chn_aval;

        //printk("max channel=%d conf=%d\n",IFC_MCDMA_CHANNEL_MAX,dev_ctx->chnl_cnt);
	/* Core info initialization */
	for (i = 0; i < dev_ctx->num_cores; i++) {
		core = &dev_ctx->cores[i];
		core->root = RB_ROOT;
		INIT_LIST_HEAD(&core->lru_list);
	}
    #if 0
	/* Default: All Chnls mapped to core0 */
	core = &dev_ctx->cores[0];
	for (i = 0; i < dev_ctx->chnl_cnt; i++) {
		core->queues_mapped[i] = true;

		/* Allocate new queue rb node */
		q = kzalloc(sizeof(struct ifc_queue_alloc), GFP_KERNEL);
		if (q == NULL)
			goto err_out;

		/* Populate node */
		q->queue_id = i;

		/* Insert it into LRU list */
		list_add_tail(&q->list, &core->lru_list);
	}
	#endif



	return err;

}
