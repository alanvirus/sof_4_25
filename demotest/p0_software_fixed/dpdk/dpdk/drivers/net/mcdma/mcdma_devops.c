/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <rte_memzone.h>
#include <rte_string_fns.h>
#ifndef DPDK_21_11_RC2
#include <rte_ethdev_pci.h>
#else
#include <ethdev_pci.h>
#endif
#include <rte_malloc.h>
#include <rte_dev.h>
#include <rte_pci.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <net/ethernet.h>
#include <rte_alarm.h>
#include <rte_cycles.h>
#include <unistd.h>
#include <string.h>
#include <sys/eventfd.h>

#include "mcdma.h"
#include "rte_pmd_mcdma.h"
#include "mcdma_access.h"
#include "qdma_regs_2_registers.h"
#include "mcdma_platform.h"
#include <pio_reg_registers.h>
#include "mcdma_dca.h"

#ifdef IFC_QDMA_DYN_CHAN
#ifdef IFC_QDMA_TELEMETRY
extern uint64_t *h2dwb;
extern uint64_t *d2hwb;	
#endif
#endif

#ifndef UIO_SUPPORT
static enum
ifc_config_mcdma_cmpl_proc config_mcdma_cmpl_proc = IFC_CONFIG_QDMA_COMPL_PROC;
#endif

#if 0 //def IFC_QDMA_INTF_ST
static int check_payload(int payload)
{
	int new_payload = 0;
	int remainder = payload % 64;

	if (remainder == 0)
		return payload;

	/* Rounding up to the nearest multiple of a 64 */
	new_payload = payload + 64 - remainder;

	return new_payload;
}
#endif

static void ifc_mcdma_set_avoid_hol(struct ifc_mcdma_device *mcdma_dev)
{
	/* avoid hol */
#ifdef LOOPBACK
	ifc_writel(mcdma_dev->qcsr + QDMA_REGS_2_RSVD_9, 0);
#else
	ifc_writel(mcdma_dev->qcsr + QDMA_REGS_2_RSVD_9, 0xff);
#endif
}

#ifdef IFC_QDMA_DYN_CHAN
#ifdef UIO_SUPPORT
static int ifc_mcdma_msix_start(uint16_t port_id)
{
	struct rte_eth_dev *dev = &rte_eth_devices[port_id];
	struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;
	int err, command = -1;

	command = 0xfffff;
	err = pwrite(mcdma_dev->uio_fd, &command, 4, 0);
	if (err == 0)
		PMD_DRV_LOG(ERR, "error while writing %u\n", errno);

	return 0;
}
#endif
#endif

static int ifc_mcdma_get_poll_ctx(struct ifc_mcdma_device *dev)
{
	int i;

	for (i = 0; i < MAX_POLL_CTX; i++) {
		if (dev->poll_ctx[i].valid == 0)
			return i;
	}
	return -1;
}

void *ifc_mcdma_poll_init(uint16_t portno)
{
	uint32_t i;
	struct rte_eth_dev *dev = &rte_eth_devices[portno];
	struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;

	if (unlikely(!mcdma_dev || mcdma_dev->uio_id > UIO_MAX_DEVICE)) {
		PMD_DRV_LOG(ERR, "Invalid Device Context\n");
		return NULL;
	}

	/* get available polling context*/
	i = ifc_mcdma_get_poll_ctx(mcdma_dev);
	if (i >= MAX_POLL_CTX)
		return NULL;
	mcdma_dev->poll_ctx[i].valid = 1;

	/* create epoll context */
	mcdma_dev->poll_ctx[i].epollfd = epoll_create1(0);
	if (mcdma_dev->poll_ctx[i].epollfd == -1)
		return NULL;
	return &mcdma_dev->poll_ctx[i];
}

int ifc_mcdma_usmix_poll_add(uint16_t port, int qid, int dir, void *ctx)
{
	struct ifc_mcdma_queue *que;
	struct epoll_event ev;
	struct fd_info_s *data_ptr;
	struct ifc_poll_ctx *poll_ctx = (struct ifc_poll_ctx *)ctx;
	struct rte_eth_dev *dev = &rte_eth_devices[port];
#ifndef UIO_SUPPORT
	struct rte_pci_device *pci_dev = RTE_ETH_DEV_TO_PCI(dev);
	int msix_base;
#endif

	if (dir == IFC_QDMA_DIRECTION_TX) {
		que = (struct ifc_mcdma_queue *)dev->data->tx_queues[qid];
#ifndef UIO_SUPPORT
		msix_base = (qid * 4);
#endif
	} else {
		que = (struct ifc_mcdma_queue *)dev->data->rx_queues[qid];
#ifndef UIO_SUPPORT
		msix_base = (qid * 4 + 2);
#endif
	}

	data_ptr = (struct fd_info_s *)rte_zmalloc("umsix_FD_info",
						   sizeof(struct fd_info_s),
						   0);
	if (!data_ptr)
		return -1;
	data_ptr->qid = qid;
	data_ptr->dir = dir;
	data_ptr->umsix = 0xAA;
#ifdef UIO_SUPPORT
	data_ptr->eventfd = que->event_irqfd;
#else
#ifndef DPDK_21_11_RC2
	que->event_irqfd = pci_dev->intr_handle.efds[msix_base + 1];
	data_ptr->eventfd = pci_dev->intr_handle.efds[msix_base + 1];
#else
	que->event_irqfd = pci_dev->intr_handle->efds[msix_base + 1];
	data_ptr->eventfd = pci_dev->intr_handle->efds[msix_base + 1];
#endif
#endif
	ev.events = EPOLLIN;
	ev.data.ptr = data_ptr;
	if (epoll_ctl(poll_ctx->epollfd, EPOLL_CTL_ADD, que->event_irqfd, &ev)
	    == -1) {
		free(data_ptr);
		return -1;
	}
	return 0;

}

int ifc_mcdma_poll_add(uint16_t port, int qid, int dir, void *ctx)
{
	struct ifc_mcdma_queue *que;
	struct epoll_event ev;
	struct fd_info_s *data_ptr;
	struct ifc_poll_ctx *poll_ctx = (struct ifc_poll_ctx *)ctx;
	struct rte_eth_dev *dev = &rte_eth_devices[port];
#ifndef UIO_SUPPORT
	struct rte_pci_device *pci_dev = RTE_ETH_DEV_TO_PCI(dev);
	int msix_base;
#endif

	if (dir == IFC_QDMA_DIRECTION_TX) {
		que = (struct ifc_mcdma_queue *)dev->data->tx_queues[qid];
#ifndef UIO_SUPPORT
		msix_base = (qid * 4);
#endif
	} else {
		que = (struct ifc_mcdma_queue *)dev->data->rx_queues[qid];
#ifndef UIO_SUPPORT
		msix_base = (qid * 4 + 2);
#endif
	}

	data_ptr = (struct fd_info_s *)rte_zmalloc("msix_FD_info",
						   sizeof(struct fd_info_s),
						   0);
	if (!data_ptr)
		return -1;
	data_ptr->qid = qid;
	data_ptr->dir = dir;
	data_ptr->umsix = 0;
#ifdef UIO_SUPPORT
	data_ptr->eventfd = que->dma_irqfd;
#else
#ifndef DPDK_21_11_RC2
	que->dma_irqfd = pci_dev->intr_handle.efds[msix_base];
	data_ptr->eventfd = pci_dev->intr_handle.efds[msix_base];
#else
	que->dma_irqfd = pci_dev->intr_handle->efds[msix_base];
	data_ptr->eventfd = pci_dev->intr_handle->efds[msix_base];
#endif
#endif
	ev.events = EPOLLIN;
	ev.data.ptr = data_ptr;
	if (epoll_ctl(poll_ctx->epollfd, EPOLL_CTL_ADD, que->dma_irqfd, &ev) ==
	    -1) {
		free(data_ptr);
		return -1;
	}
	return 0;
}

int ifc_mcdma_eventfd_poll_add(uint16_t port, int dir, void *ctx)
{
	struct epoll_event ev;
	struct fd_info_s *data_ptr;
	struct ifc_poll_ctx *poll_ctx = (struct ifc_poll_ctx *)ctx;
	uint32_t command, vec, err;
	struct rte_eth_dev *dev = &rte_eth_devices[port];
	struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;

	if (dir == IFC_QDMA_DIRECTION_TX) {
		vec = 1;
	} else {
		vec = 0;
	}

	data_ptr = (struct fd_info_s *)rte_zmalloc("msix_bas_FD_info",
						   sizeof(struct fd_info_s),
						   0);
	if (!data_ptr)
		return -1;
	data_ptr->eventfd = eventfd(0, 0);
	ev.events = EPOLLIN;
	ev.data.ptr = data_ptr;
	if (epoll_ctl(poll_ctx->epollfd, EPOLL_CTL_ADD, data_ptr->eventfd, &ev) ==
	    -1) {
		free(data_ptr);
		return -1;
	}

	command = data_ptr->eventfd | vec << 24;
	err = pwrite(mcdma_dev->uio_fd, &command, 4, 0);
	if (err == 0)
		PMD_DRV_LOG(ERR, "error while writing %u\n", errno);

	return 0;
}

/**
 * mcdma_get_error - read error bits
 */
static uint32_t ifc_mcdma_read_error(uint16_t port, int qid,
				    int dir)
{
	struct ifc_mcdma_queue *que;
	struct rte_eth_dev *dev = &rte_eth_devices[port];
	uint32_t err = 0;
	volatile uint32_t head;
	volatile uint32_t qhead;
	volatile uint16_t total_drops_count = 0;
	if (dir == IFC_QDMA_DIRECTION_RX)
		que = (struct ifc_mcdma_queue *)
			dev->data->rx_queues[qid];
	else
		que = (struct ifc_mcdma_queue *)
			dev->data->tx_queues[qid];

	head = ifc_readl(que->qcsr + QDMA_REGS_2_Q_DATA_DRP_ERR_CTR);
	if (head & (1 << 20))
	{
	    total_drops_count = head & 0xFFFF;
	    que->data_drops_cnts =  que->data_drops_cnts + total_drops_count;
	    head &= ~0x10FFFF ;
	    ifc_writel(que->qcsr + QDMA_REGS_2_Q_DATA_DRP_ERR_CTR, head);
	    que->stats.tid_drops++;
	    err |= TID_ERROR;
	}

	/* Read head pointer to test descriptor fetch error event */
	qhead = ifc_readl(que->qcsr + QDMA_REGS_2_Q_HEAD_POINTER);
	if(qhead & (1 << 24))
		err |= DESC_FETCH_EVENT;

	/* Read completed pointer to test data fetch error event */
	qhead = ifc_readl(que->qcsr + QDMA_REGS_2_Q_COMPLETED_POINTER);
	if(qhead & (1 << 24))
		err |= DATA_FETCH_EVENT;

	/* Read completion timeout regster 0x4C*/
	head = ifc_readl(que->qcsr + QDMA_REGS_2_Q_CPL_TIMEOUT);
	/*check if completion timeout bit is set [0th bit - q_cpl_timeout] */
	if(head & 1)
	{
		head &= 0xFFFFFFFE ;
		/* Clear q_cpl_timeout bit  */
		ifc_writel(que->qcsr + QDMA_REGS_2_Q_CPL_TIMEOUT, head);
		que->stats.cto_drops++;
		err |= COMPLETION_TIME_OUT_ERROR;
	}

	return err;
}

int ifc_mcdma_poll_wait(uint16_t port,
		       void *ctx,
		       int timeout,
		       int *qid,
		       int *dir)
{
	(void)port;
	struct ifc_mcdma_queue *que;
	struct rte_eth_dev *dev = &rte_eth_devices[port];
	struct epoll_event events[MAX_EVENTS];
	int nfds, n;
	struct ifc_poll_ctx *poll_ctx = (struct ifc_poll_ctx *)ctx;
	int errinfo;

	nfds = epoll_wait(poll_ctx->epollfd, events, MAX_EVENTS, timeout);
	if (nfds == -1)
		return -1;

	for (n = 0; n < nfds; ++n) {
		if (events[n].data.ptr) {
			struct fd_info_s *dataptr =
					(struct fd_info_s *)events[n].data.ptr;
			*qid = dataptr->qid;
			*dir = dataptr->dir;

			if (*dir == IFC_QDMA_DIRECTION_TX){
				que = (struct ifc_mcdma_queue *)
					dev->data->tx_queues[*qid];
			} else {
				que = (struct ifc_mcdma_queue *)
					dev->data->rx_queues[*qid];
			}

			if (dataptr->umsix == 0xAA) {
				errinfo = ifc_mcdma_read_error(port, *qid,
						*dir);
				/* call the user MSIx handler */
				if (*dir == IFC_QDMA_DIRECTION_RX)
					que->irq_handler
						(port,
						 *qid,
						 *dir,
						 &errinfo);
				else
					que->irq_handler
						(port,
						 *qid,
						 *dir,
						 &errinfo);
			}
#ifdef IFC_DEBUG_STATS
			if (*dir == IFC_QDMA_DIRECTION_TX){
				que = (struct ifc_mcdma_queue *)
					dev->data->tx_queues[*qid];
				que->stats.last_intr_cons_head =
					que->consumed_head;
				que->stats.last_intr_reg = ifc_readl(
					que->qcsr + QDMA_REGS_2_Q_COMPLETED_POINTER);
			} else {
				que = (struct ifc_mcdma_queue *)
					dev->data->rx_queues[*qid];
				que->stats.last_intr_cons_head =
					que->consumed_head;
				que->stats.last_intr_reg = ifc_readl(
					que->qcsr + QDMA_REGS_2_Q_COMPLETED_POINTER);
			}
#endif
		}
	}
	return nfds;
}

int ifc_mcdma_poll_wait_for_intr(void *ctx,
		       int timeout,
		       int *dir)
{
	struct epoll_event events[MAX_EVENTS];
	int nfds, n;
	struct ifc_poll_ctx *poll_ctx = (struct ifc_poll_ctx *)ctx;

	nfds = epoll_wait(poll_ctx->epollfd, events, MAX_EVENTS, timeout);
	if (nfds == -1)
		return -1;

	for (n = 0; n < nfds; ++n) {
		if (events[n].data.ptr) {
			struct fd_info_s *dataptr =
					(struct fd_info_s *)events[n].data.ptr;

			*dir = dataptr->umsix;
		}
	}
	return nfds;
}


static inline const
struct rte_memzone *ifc_mcdma_zone_reserve(struct rte_eth_dev *dev,
				      	   const char *ring_name,
					   uint32_t queue_id,
					   uint32_t ring_size,
					   int socket_id)
{
	char z_name[RTE_MEMZONE_NAMESIZE];

	snprintf(z_name, sizeof(z_name), "%s%s%d_%d",
		 dev->device->driver->name, ring_name,
			dev->data->port_id, queue_id);
	return rte_memzone_reserve_bounded(z_name, (uint64_t)ring_size,
						socket_id,
						RTE_MEMZONE_IOVA_CONTIG |
						RTE_MEMZONE_1GB |
						RTE_MEMZONE_SIZE_HINT_ONLY,
						64, ring_size);
}

/**
 * ifc_mcdma_descq_init - internal, allocate and initialize descr ring
 */
static int ifc_mcdma_descq_init(struct ifc_mcdma_queue *q)
{
	QDMA_REGS_2_Q_CTRL_t q_ctrl = { { 0 } };
	struct rte_eth_dev *ethdev = q->ethdev;
	const struct rte_memzone *qzone;
	struct ifc_mcdma_desc *desc;
	struct ifc_mcdma_desc_sw *sw;
	uint64_t cons_head;
	int desc_per_page;
	uint32_t log_len;
	uint32_t i;
	int len;
#ifdef IFC_QDMA_INTF_ST
	uint32_t payload = ifc_get_aligned_payload(
#ifndef DPDK_21_11_RC2
				ethdev->data->dev_conf.rxmode.max_rx_pkt_len);
#else
				ethdev->data->dev_conf.rxmode.max_lro_pkt_size);
#endif
#endif
	struct ifc_mcdma_device *mcdma_dev = ethdev->data->dev_private;

	if ((q->num_desc_pages & (q->num_desc_pages - 1)) ||
	    q->num_desc_pages > 64) {
		PMD_DRV_LOG(ERR, "Please configure number of pages %u as power "
			    "of 2 and <= 64\n", q->num_desc_pages);
		return -1;
	}
	/* descriptors per page */
	desc_per_page = (IFC_MCDMA_RING_SIZE / sizeof(*desc));
	/* ring buffer size */
	len = (IFC_MCDMA_RING_SIZE * q->num_desc_pages);
	q->qlen = len / sizeof(*desc);

	if (q->dir)
		qzone = ifc_mcdma_zone_reserve(q->ethdev, TX_MZONE_NAME,
					       q->qid, len, 0);
	else
		qzone = ifc_mcdma_zone_reserve(q->ethdev, RX_MZONE_NAME,
					       q->qid, len, 0);
	if (qzone == NULL) {
		PMD_DRV_LOG(ERR, "qbuf zone allocation failed ret:0x%p\n",
			    q->qbuf);
		return -1;
	}
	q->qbuf = qzone->addr;

	log_len = ceil(log2(q->qlen));
	q->num_desc_bits = log_len;
	if (!q->qbuf) {
		PMD_DRV_LOG(ERR, "qbuf allocation failed\n");
		return -1;
	}

	q->ctx = (struct ifc_mcdma_desc_sw *)rte_zmalloc("desc_sw_ctx",
							sizeof(*sw) * q->qlen,
							0);
	if (!q->ctx) {
		rte_free(q->qbuf);
		return -1;
	}

	memset(q->qbuf, 0, sizeof(*q));
#if defined(UIO_SUPPORT) || defined (NO_IOMMU_MODE)
	q->qbuf_dma = rte_mem_virt2phy(q->qbuf);
#else
	q->qbuf_dma = rte_mem_virt2iova(q->qbuf);
#endif
	if (q->qbuf_dma == 0)
		return -1;
	desc = (struct ifc_mcdma_desc *)q->qbuf;

	for (i = 0; i < q->qlen; i++)
		desc[i].link = 0;

	/* let last one point back to first with link = 1 */
	desc[i - 1].src = q->qbuf_dma;
	desc[i - 1].link = 1;

	for (i = desc_per_page; i <= q->qlen; i += desc_per_page) {
		desc[i - 1].link = 1;
		if (i == q->qlen)
#if defined(UIO_SUPPORT) || defined (NO_IOMMU_MODE)
			desc[i - 1].src = rte_mem_virt2phy(q->qbuf);
#else
			desc[i - 1].src = rte_mem_virt2iova(q->qbuf);
#endif
		else
#if defined(UIO_SUPPORT) || defined (NO_IOMMU_MODE)
			desc[i - 1].src = rte_mem_virt2phy(desc + i);
#else
			desc[i - 1].src = rte_mem_virt2iova(desc + i);
#endif
	}

	/* program regs */
	ifc_writel(q->qcsr + QDMA_REGS_2_Q_START_ADDR_L, q->qbuf_dma);
	ifc_writel(q->qcsr + QDMA_REGS_2_Q_START_ADDR_H, q->qbuf_dma >> 32);
	ifc_writel(q->qcsr + QDMA_REGS_2_Q_SIZE, log_len);
	//ifc_writel(q->qcsr + QDMA_REGS_2_Q_TAIL_POINTER, 0);

	/* configure head address to write back */
#if defined(UIO_SUPPORT) || defined (NO_IOMMU_MODE)
	cons_head = (uint64_t)rte_mem_virt2phy(&q->consumed_head);
#else
	cons_head = (uint64_t)rte_mem_virt2iova(&q->consumed_head);
#endif
	ifc_writel(q->qcsr + QDMA_REGS_2_Q_CONSUMED_HEAD_ADDR_L, cons_head);
	ifc_writel(q->qcsr + QDMA_REGS_2_Q_CONSUMED_HEAD_ADDR_H,
		   (cons_head >> 32));

	/* configure batch delay */
	ifc_writel(q->qcsr + QDMA_REGS_2_Q_BATCH_DELAY, IFC_QDMA_Q_BATCH_DELAY);

#ifdef IFC_QDMA_INTF_ST
	/* configure paylaod */
	ifc_writel(q->qcsr + QDMA_REGS_2_Q_DEBUG_STATUS_4, payload);
#endif

	/* configure the control register*/
	q_ctrl.field.q_en = 1;

	/* set descriptor completion mechanism */
	q->wbe = 0;
	switch (mcdma_dev->completion_method) {
	case CONFIG_QDMA_QUEUE_WB:
		q_ctrl.field.q_wb_en = 1;
		q->wbe = 1;
		break;
	case CONFIG_QDMA_QUEUE_REG:
		PMD_DRV_LOG(ERR, "Register method of completion reporting not allowed\n");
		return -2;
	case CONFIG_QDMA_QUEUE_MSIX:
		q_ctrl.field.q_intr_en = 1;
		q_ctrl.field.q_wb_en = 1;
		q->qie = 1;
		break;
	default:
		return -2;
	}

	if (q->dir == IFC_QDMA_DIRECTION_RX)
		q_ctrl.field.q_wb_en = 1;

	/* write in QCSR  register*/
	ifc_writel(q->qcsr + QDMA_REGS_2_Q_CTRL, q_ctrl.val);

	/* set defaults */
	q->head = 0;
	q->consumed_head = 0;

	return 0;
}

static int ifc_mcdma_queue_init(struct ifc_mcdma_queue *q)
{
	int ret;

#if 0
	/* Wait for head reaching to tail */
	if (q->dir == IFC_QDMA_DIRECTION_TX)
		ifc_mcdma_wait_for_queue_comp(q);
#endif

	ifc_writel(q->qcsr + QDMA_REGS_2_Q_DEBUG_STATUS_3, 0);
	ret = ifc_mcdma_reset_queue(q);
	if (ret < 0) {
		PMD_DRV_LOG(ERR, "Queue reset failed\n");
		return ret;
	}

	return ifc_mcdma_descq_init(q);
}

int ifc_mcdma_dev_tx_queue_start(struct rte_eth_dev *dev, uint16_t qid)
{
	struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;
	struct ifc_mcdma_queue *txq;
	int err;

	txq = (struct ifc_mcdma_queue *)dev->data->tx_queues[qid];

	/* Init descriptors and QCSR */
	err = ifc_mcdma_queue_init(txq);
	if (err)
		return err;

#ifdef UIO_SUPPORT
	/* Init TX MSIX context */
	if (txq->qie) {
		err = ifc_mcdma_queue_init_msix(txq->qid, 1,
						dev->data->port_id);
		if (err)
			return err;

		err = ifc_mcdma_queue_umsix_init(txq->qid, 1, dev->data->port_id);
		if (err)
			return err;
	}
#endif

	ifc_mcdma_set_avoid_hol(mcdma_dev);
	dev->data->tx_queue_state[qid] = RTE_ETH_QUEUE_STATE_STARTED;

	return 0;
}

int ifc_mcdma_dev_rx_queue_start(struct rte_eth_dev *dev, uint16_t qid)
{
	struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;
	struct ifc_mcdma_queue *rxq;
	int ret;

	rxq = (struct ifc_mcdma_queue *)dev->data->rx_queues[qid];

	/* Init descriptors and QCSR */
	ret = ifc_mcdma_queue_init(rxq);
	if (ret)
		return ret;

#ifdef UIO_SUPPORT
	/* Init RX MSIX context */
	if (rxq->qie) {
		ret = ifc_mcdma_queue_init_msix(rxq->qid, 0,
						dev->data->port_id);
		if (ret)
			return ret;

		ret = ifc_mcdma_queue_umsix_init(rxq->qid, 0, dev->data->port_id);
		if (ret)
			return ret;
	}
#endif

	ifc_mcdma_set_avoid_hol(mcdma_dev);
	dev->data->rx_queue_state[qid] = RTE_ETH_QUEUE_STATE_STARTED;


	return 0;
}

int ifc_mcdma_dev_rx_queue_stop(struct rte_eth_dev *dev,
				uint16_t qid)
{
	struct ifc_mcdma_queue *rxq;
	int ret;

	if (!dev->data->rx_queue_state[qid])
		return 0;

	rxq = (struct ifc_mcdma_queue *)dev->data->rx_queues[qid];
	ret = ifc_mcdma_reset_queue(rxq);
	if (ret < 0) {
		PMD_DRV_LOG(ERR, "RX Queue reset failed\n");
	}
	//ifc_writel(rxq->qcsr + QDMA_REGS_2_Q_TAIL_POINTER, 0);
	dev->data->rx_queue_state[qid] = RTE_ETH_QUEUE_STATE_STOPPED;
#ifdef IFC_QDMA_DYN_CHAN
	if ((dev->data->tx_queue_state[qid] == RTE_ETH_QUEUE_STATE_STOPPED) &&
	    (dev->data->rx_queue_state[qid] == RTE_ETH_QUEUE_STATE_STOPPED)) {
		/* Release the channel */
		ifc_mcdma_queue_stop(dev, qid);
	}
#else
	ifc_mcdma_dev_rx_queue_release(dev->data->rx_queues[qid]);
#endif

	return 0;
}

int ifc_mcdma_dev_tx_queue_stop(struct rte_eth_dev *dev,
				uint16_t qid)
{
	struct ifc_mcdma_queue *txq;
	int ret;
	if (!dev->data->tx_queue_state[qid])
		return 0;

	txq = (struct ifc_mcdma_queue *)dev->data->tx_queues[qid];
	ret = ifc_mcdma_wait_for_comp(txq);
	if (ret < 0) {
		PMD_DRV_LOG(DEBUG, " cid:%u Not all H2D Completions received sub:%u con:%u\n",
			qid, txq->stats.submitted_didx, txq->consumed_head);
	}
	ret = ifc_mcdma_reset_queue(dev->data->tx_queues[qid]);
	if (ret < 0) {
		PMD_DRV_LOG(ERR, "TX Queue reset failed\n");
	}
	//ifc_writel(txq->qcsr + QDMA_REGS_2_Q_TAIL_POINTER, 0);
	dev->data->tx_queue_state[qid] = RTE_ETH_QUEUE_STATE_STOPPED;
#ifdef IFC_QDMA_DYN_CHAN
	if ((dev->data->tx_queue_state[qid] == RTE_ETH_QUEUE_STATE_STOPPED) &&
	    (dev->data->rx_queue_state[qid] == RTE_ETH_QUEUE_STATE_STOPPED)) {
		/* Release the channel */
		ifc_mcdma_queue_stop(dev, qid);
	}
#else
	ifc_mcdma_dev_tx_queue_release(dev->data->tx_queues[qid]);
#endif

	return 0;
}

int
ifc_mcdma_dev_rx_queue_setup(struct rte_eth_dev *dev, uint16_t rx_queue_id,
			     uint16_t nb_rx_desc,
			     unsigned int socket_id __rte_unused,
			     const struct rte_eth_rxconf *rx_conf,
			     struct rte_mempool *mb_pool)
{
	struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;
	struct ifc_mcdma_queue *rxq = NULL;

	__rte_unused struct rte_pci_device *pci_dev = RTE_ETH_DEV_TO_PCI(dev);
	int err = 0;

	if (nb_rx_desc == 0 && ((nb_rx_desc % 128) != 0)) {
		PMD_DRV_LOG(ERR, "Invalid descriptor ring size %d\n",
			    nb_rx_desc);
		return -EINVAL;
	}

	/* allocate rx queue data structure */
	rxq = rte_zmalloc("QDMA_RxQ", sizeof(struct ifc_mcdma_queue),
			  RTE_CACHE_LINE_SIZE);
	if (!rxq) {
		PMD_DRV_LOG(ERR,
			    "Unable to allocate structure rxq of size %d\n",
			    (int)(sizeof(struct ifc_mcdma_queue)));
		err = -ENOMEM;
		goto last;
	}

	rxq->qcsr = mcdma_dev->bar_addr[0] + (rx_queue_id * 256);
	rxq->qid = rx_queue_id;
	rxq->mb_pool = mb_pool;
	rxq->ethdev = dev;
	rxq->dir = IFC_QDMA_DIRECTION_RX;
	rxq->num_desc_pages = (((nb_rx_desc + 1) * 32) / IFC_MCDMA_RING_SIZE);
	/* populated with user MSIX callback */
	rxq->irq_handler = rx_conf->reserved_ptrs[0];

	rxq->base = rx_conf->reserved_64s[0];
	rxq->limit = rx_conf->reserved_64s[1];
	rxq->addr = rxq->base;

	dev->data->rx_queues[rx_queue_id] = rxq;

#ifdef IFC_QDMA_DYN_CHAN
	int ret;
	if (mcdma_dev->channel_context[rx_queue_id].valid == 0) {
		/* acquire channels */
		ret = ifc_mcdma_acquire_channel(mcdma_dev, rx_queue_id);
		if (ret < 0) {
			PMD_DRV_LOG(ERR, "Could not acquire the channels : Exiting\n");
			return -1;
		}
		rxq->ph_chno = ret;
	} else
		rxq->ph_chno = mcdma_dev->channel_context[rx_queue_id].ph_chno;
#else
#ifdef IFC_MCDMA_ERR_CHANNEL
	/* check for max channels */
	if (ifc_mcdma_check_ch_sup(mcdma_dev, rx_queue_id)) {
		PMD_DRV_LOG(ERR, "Channel not supported : Exiting\n");
		return -1;
	}
#endif //IFC_MCDMA_ERR_CHANNEL
#endif

#ifdef IFC_QDMA_DYN_CHAN
	return mcdma_dev->channel_context[rx_queue_id].ph_chno;
#endif

last:
	return err;
}

int
ifc_mcdma_dev_tx_queue_setup(struct rte_eth_dev *dev, uint16_t tx_queue_id,
			     uint16_t nb_tx_desc,
			     unsigned int socket_id __rte_unused,
			     const struct rte_eth_txconf *tx_conf)
{
	struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;
	struct ifc_mcdma_queue *txq = NULL;
	__rte_unused struct rte_pci_device *pci_dev = RTE_ETH_DEV_TO_PCI(dev);
	int err = 0;

	if (nb_tx_desc == 0 && ((nb_tx_desc % 128) != 0)) {
		PMD_DRV_LOG(ERR, "Invalid descriptor ring size %d\n",
			    nb_tx_desc);
		return -EINVAL;
	}

	/* allocate rx queue data structure */
	txq = rte_zmalloc("IFC_MCDMA_RxQ", sizeof(struct ifc_mcdma_queue),
			  RTE_CACHE_LINE_SIZE);
	if (!txq) {
		PMD_DRV_LOG(ERR,
			    "Unable to allocate structure txq of size %d\n",
			    (int)(sizeof(struct ifc_mcdma_queue)));
		err = -ENOMEM;
		goto last;
	}

	txq->qcsr = mcdma_dev->bar_addr[0] + (512 << 10) +
		    (tx_queue_id * 256);
	txq->qid = tx_queue_id;
	txq->ethdev = dev;
	txq->dir = IFC_QDMA_DIRECTION_TX;
	txq->num_desc_pages = (((nb_tx_desc + 1) * 32) / IFC_MCDMA_RING_SIZE);
	/* populated with user MSIX callback */
	txq->irq_handler = tx_conf->reserved_ptrs[0];

	txq->base = tx_conf->reserved_64s[0];
	txq->limit = tx_conf->reserved_64s[1];
	txq->addr = txq->base;

	dev->data->tx_queues[tx_queue_id] = txq;

#ifdef IFC_QDMA_DYN_CHAN
	int ret;
	if (mcdma_dev->channel_context[tx_queue_id].valid == 0) {
		/* acquire channels */
		ret = ifc_mcdma_acquire_channel(mcdma_dev, tx_queue_id);
		if (ret < 0) {
			PMD_DRV_LOG(ERR, "Could not acquire the channels : Exiting\n");
			return -1;
		}
		txq->ph_chno = ret;
	} else
		txq->ph_chno = mcdma_dev->channel_context[tx_queue_id].ph_chno;
#else
#ifdef IFC_MCDMA_ERR_CHANNEL
	/* check for max channels */
	if (ifc_mcdma_check_ch_sup(mcdma_dev, tx_queue_id)) {
		PMD_DRV_LOG(ERR, "Channel not supported : Exiting\n");
		return -1;
	}
#endif //IFC_MCDMA_ERR_CHANNEL
#endif

#ifdef IFC_QDMA_DYN_CHAN
	return mcdma_dev->channel_context[tx_queue_id].ph_chno;
#endif

last:
	return err;
}

/**
 * ifc_mcdma_descq_disable - disable/abort the queue desc processing
 */
static void ifc_mcdma_descq_disable(struct ifc_mcdma_queue *q)
{
	/* configure the control register*/
	ifc_writel(q->qcsr + QDMA_REGS_2_Q_CTRL, 0);
}

static void ifc_desc_ring_free(struct ifc_mcdma_queue *q)
{
	const struct rte_memzone *qzone;
	char z_name[RTE_MEMZONE_NAMESIZE];
	const char *ring_name;

	if (q->dir)
		ring_name = TX_MZONE_NAME;
	else
		ring_name = RX_MZONE_NAME;

	snprintf(z_name, sizeof(z_name), "%s%s%d_%d",
		q->ethdev->device->driver->name, ring_name,
		q->ethdev->data->port_id, q->qid);

	qzone = rte_memzone_lookup(z_name);
	if (qzone == NULL)
		return;
	rte_memzone_free(qzone);
}

void ifc_mcdma_dev_tx_queue_release(void *tqueue)
{
	struct ifc_mcdma_queue *txq = (struct ifc_mcdma_queue *)tqueue;

	if (!txq)
		return;
	if (!txq->qcsr)
		return;
	ifc_mcdma_descq_disable(txq);
	ifc_desc_ring_free(txq);
	rte_free(txq);
}

void ifc_mcdma_dev_rx_queue_release(void *rqueue)
{
	struct ifc_mcdma_queue *rxq = (struct ifc_mcdma_queue *)rqueue;

	if (!rxq)
		return;

	if (!rxq->qcsr)
		return;

	ifc_mcdma_descq_disable(rxq);
	ifc_desc_ring_free(rxq);
	rte_mempool_free(rxq->mb_pool);
	rte_free(rxq);
}

static int ifc_mcdma_dev_start(struct rte_eth_dev *dev)
{
	struct ifc_mcdma_queue *txq __rte_unused;
	struct ifc_mcdma_queue *rxq __rte_unused;
	uint16_t qid;
	int err;
#ifndef UIO_SUPPORT
	uint32_t num_chan;
	struct rte_pci_device *pci_dev = RTE_ETH_DEV_TO_PCI(dev);
#endif

	if ((dev == NULL) || (dev->data == NULL)) {
		PMD_DRV_LOG(ERR, "dev is NULL. Returning\n");
		return -1;
	}

	for (qid = 0; qid < dev->data->nb_tx_queues; qid++) {
		if(dev->data->tx_queues[qid]) {
			err = ifc_mcdma_dev_tx_queue_start(dev, qid);
			if (err != 0)
				return err;
		}
	}

	for (qid = 0; qid < dev->data->nb_rx_queues; qid++) {
		if(dev->data->rx_queues[qid]) {
			err = ifc_mcdma_dev_rx_queue_start(dev, qid);
			if (err != 0)
				return err;
		}
	}

#ifdef IFC_QDMA_DYN_CHAN
#ifdef UIO_SUPPORT
	ifc_mcdma_msix_start(dev->data->port_id);
#endif
#endif
#ifndef UIO_SUPPORT
	num_chan = RTE_MAX(dev->data->nb_rx_queues,
		dev->data->nb_tx_queues);
	if (ifc_mcdma_vfio_enable_msix(pci_dev, num_chan * 4)) {
		PMD_DRV_LOG(ERR, "Enabling MSIX failed\n");
		if (config_mcdma_cmpl_proc == CONFIG_QDMA_QUEUE_MSIX) {
			return -1;
		}
	}
#endif
	return 0;
}

static int ifc_mcdma_dev_link_update(struct rte_eth_dev *dev,
				     __rte_unused int wait_to_complete)
{
	dev->data->dev_link.link_status = ETH_LINK_UP;
	dev->data->dev_link.link_duplex = ETH_LINK_FULL_DUPLEX;
	return 0;
}

static int ifc_mcdma_dev_infos_get(__rte_unused struct rte_eth_dev *dev,
				   struct rte_eth_dev_info *dev_info)
{

#ifdef IFC_QDMA_DYN_CHAN
	struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;
	dev_info->max_rx_queues = ifc_mcdma_get_avail_channel_count(mcdma_dev);
	dev_info->max_tx_queues = ifc_mcdma_get_avail_channel_count(mcdma_dev);;
#else
	dev_info->max_rx_queues = IFC_MCDMA_QUEUES_NUM_MAX;
	dev_info->max_tx_queues = IFC_MCDMA_QUEUES_NUM_MAX;
#endif
	dev_info->max_rx_pktlen = DMA_BRAM_SIZE;
	dev_info->max_mac_addrs = 1;
	dev_info->default_rxportconf.burst_size = IFC_DEF_BURST_SIZE;
	dev_info->dev_capa =
		RTE_ETH_DEV_CAPA_RUNTIME_RX_QUEUE_SETUP |
		RTE_ETH_DEV_CAPA_RUNTIME_TX_QUEUE_SETUP;
#ifndef DPDK_21_11_RC2
	dev_info->rx_offload_capa |= DEV_RX_OFFLOAD_JUMBO_FRAME;
#endif
	dev_info->min_mtu = MCDMA_MIN_MTU;
	dev_info->max_mtu = MCDMA_MAX_MTU;

	return 0;
}

static void ifc_mcdma_dev_stop(struct rte_eth_dev *dev)
{
	uint32_t qid;

	for (qid = 0; qid < dev->data->nb_tx_queues; qid++)
		ifc_mcdma_dev_tx_queue_stop(dev, qid);
	for (qid = 0; qid < dev->data->nb_rx_queues; qid++)
		ifc_mcdma_dev_rx_queue_stop(dev, qid);
#ifdef IFC_QDMA_DYN_CHAN
#ifdef IFC_QDMA_TELEMETRY
	if(h2dwb != NULL){
		rte_free(h2dwb);
	}

	if(d2hwb != NULL){
		rte_free(d2hwb);
	}
#endif
#endif
}

static void ifc_mcdma_dev_close(struct rte_eth_dev *dev __rte_unused)
{
}

static void ifc_mcdma_hw_init(struct ifc_mcdma_device *mcdma_dev)
{
	int i = 0;

	mcdma_dev->tx_bitmap[0] = ~(BIT(mcdma_dev->ipcap.num_chan & 31) - 1);
	mcdma_dev->rx_bitmap[0] = ~(BIT(mcdma_dev->ipcap.num_chan & 31) - 1);
	mcdma_dev->completion_method = IFC_CONFIG_QDMA_COMPL_PROC;

	for (i = 0; i < IFC_MCDMA_QUEUES_NUM_MAX; i++) {
		mcdma_dev->channel_context[i].valid = 0;
		mcdma_dev->channel_context[i].ctx = NULL;
		mcdma_dev->channel_context[i].ph_chno = 0;
        }

	for (i = 0; i < (IFC_MCDMA_QUEUES_NUM_MAX* 2); i++)
		mcdma_dev->que_context[i] = NULL;
}

int ifc_mcdma_queue_umsix_init(int qid, int dir, uint16_t port_id)
{
	struct rte_eth_dev *dev = &rte_eth_devices[port_id];
	struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;
	struct ifc_mcdma_queue *que;
	int efd, err;
	int msix_base;
	int command;

	if (dir == IFC_QDMA_DIRECTION_TX) {
		que = (struct ifc_mcdma_queue *)dev->data->tx_queues[qid];
		msix_base = (qid * 4);
	} else {
		que = (struct ifc_mcdma_queue *)dev->data->rx_queues[qid];
		msix_base = (qid * 4 + 2);
	}

	/* Register Events interrpt */
	efd = eventfd(0, 0);
	if ((efd == -1) || efd >= MAX_IRQ_FD) {
		PMD_DRV_LOG(ERR, "eventfd creation failed or max reached. "
			    "Exiting...\n");
		return -1;
	}
	que->event_irqfd = efd;
	command = (((msix_base + 1) << IRQ_FD_BITS) | efd);
	err = pwrite(mcdma_dev->uio_fd, &command, 4, 0);
	if (err == 0)
		PMD_DRV_LOG(ERR, "error while writing %u\n", errno);
	return 0;
}
#ifdef IFC_QDMA_DYN_CHAN
#ifdef IFC_QDMA_TELEMETRY
int ifc_mcdma_vf_wb_error(uint16_t port_id, int qid, int dir)
{
       struct rte_eth_dev *dev = &rte_eth_devices[port_id];
       struct ifc_mcdma_queue *rxq;
       struct ifc_mcdma_queue *txq;
       volatile uint32_t err_desc_fetch = 0;
       volatile uint32_t err_data_fetch = 0;
       uint32_t completion_timeout = 0;
       uint32_t ur_error = 1;
       uint32_t err = 0;
       int  ret = 0;
       uint32_t reg_data = 0;
       int  scp_timeout = 2;
       if(dir == IFC_QDMA_DIRECTION_RX)
       {
               rxq = (struct ifc_mcdma_queue *)dev->data->rx_queues[qid];
               if(rxq){
		       /* Check if descriptor fetch or data fetch error happend and d2h got stuck */
                       err_desc_fetch = (volatile uint32_t)rxq->consumed_head & ERROR_DESC_FETCH_MASK;
                       err_data_fetch = (volatile uint32_t)rxq->consumed_head & ERROR_DATA_FETCH_MASK;
                       if(err_desc_fetch)
                       {
                               completion_timeout = ifc_readl(rxq->qcsr + 0x4c);
                               if(completion_timeout)
                               {
                                       err = err_desc_fetch | completion_timeout;
                               }
                               else
                               {
                                       err = err_desc_fetch | ur_error;
                               }
                       }
                       if(err_desc_fetch || err_data_fetch)
                      {
                               err = err_desc_fetch | err_data_fetch ;
                               reg_data = ifc_mcdma_poll_scratch_reg(dev, MCDMA_ERROR_HANDLER_SCRATCH_D2H, 1);
                               //ERROR_HANDLER_SCRATCH_D2H
                               /* Reset the queue from VF when it is active because PF0 is not cheked yet
                                * incase of VF active*/
                               if(reg_data == 1.)
                               {
                                       ret = ifc_mcdma_reset_queue(rxq);
                                       if(ret < 0) {
                                               printf("RX Queue reset failed\n");
                                       }
                                       ifc_mcdma_reg_write(dev, MCDMA_ERROR_HANDLER_SCRATCH_D2H, 0UL);
                               }
                               else
                               {
                                       /* Timeout error happened */
                                       err = err | scp_timeout;
                               }
                       rxq->consumed_head = rxq->consumed_head & 0X3FFFFFFF;
                       }
               }
       }
       if(dir == IFC_QDMA_DIRECTION_TX)
       {
               txq = (struct ifc_mcdma_queue *)dev->data->tx_queues[qid];
               if(txq) {
                       err_desc_fetch = (volatile uint32_t)txq->consumed_head & ERROR_DESC_FETCH_MASK;
                       err_data_fetch = (volatile uint32_t)txq->consumed_head & ERROR_DATA_FETCH_MASK;
			/* Check if descriptor fetch or data fetch error happend and h2d  got stuck */
                       if(err_desc_fetch || err_data_fetch)
                       {
                               completion_timeout = ifc_readl(txq->qcsr + 0x4c);
                               if(completion_timeout)
                               {
                                       err = err_desc_fetch | err_data_fetch | completion_timeout;
                               }
                               else
                               {
                                       err = err_desc_fetch | err_data_fetch | ur_error;
                               }
                               reg_data = ifc_mcdma_poll_scratch_reg(dev, MCDMA_ERROR_HANDLER_SCRATCH_H2D, 1);
                               //ERROR_HANDLER_SCRATCH_H2D
                               /* Reset the queue from VF when it is active because PF0 is not cheked yet 
 				* incase of VF active*/
                               if(reg_data == 1) {
                                       ret = ifc_mcdma_reset_queue(txq);
                                       if (ret < 0) {
                                               printf("TX Queue reset failed\n");
                                       }
                                       ifc_mcdma_reg_write(dev, MCDMA_ERROR_HANDLER_SCRATCH_H2D, 0UL);
                               }
                               else
                               {
                                       /* Timeout error happened */
                                       err = err | scp_timeout;
                               }
                               rxq->consumed_head = rxq->consumed_head & 0X3FFFFFFF;
                       }
               }
       }
       return err;
}
#endif
#endif

int ifc_mcdma_queue_init_msix(int qid, int dir, uint16_t port_id)
{
	struct rte_eth_dev *dev = &rte_eth_devices[port_id];
	struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;
	struct ifc_mcdma_queue *que;
	int efd, err;
	int msix_base;
	int command;

	if (dir == IFC_QDMA_DIRECTION_TX) {
		que = (struct ifc_mcdma_queue *)dev->data->tx_queues[qid];
		msix_base = (qid * 4);
	} else {
		que = (struct ifc_mcdma_queue *)dev->data->rx_queues[qid];
		msix_base = (qid * 4 + 2);
	}

	/* Register DMA interrupt */
	efd = eventfd(0, 0);
	if ((efd == -1) || efd >= MAX_IRQ_FD) {
		PMD_DRV_LOG(ERR, "eventfd creation failed or max reached. "
			    "Exiting...\n");
		return -1;
	}
	que->dma_irqfd = efd;
	command = ((msix_base << IRQ_FD_BITS) | efd);
	err = pwrite(mcdma_dev->uio_fd, &command, 4, 0);
	if (err == 0)
		PMD_DRV_LOG(ERR, "error while writing %u\n", errno);

	return 0;
}

static int ifc_mcdma_dev_configure(struct rte_eth_dev *dev)
{
	struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;

	mcdma_dev->num_chan = RTE_MAX(dev->data->nb_rx_queues,
				     dev->data->nb_tx_queues);
	if (mcdma_dev->num_chan > mcdma_dev->ipcap.num_chan) {
		PMD_DRV_LOG(ERR, "Max number of channels supported %u",
			    mcdma_dev->ipcap.num_chan);
		mcdma_dev->num_chan = 0;
		return -1;
	}

#ifdef IFC_QDMA_IP_RESET
	/* perform IP Reset */
	ifc_mcdma_ip_reset(mcdma_dev);
#endif

	ifc_mcdma_hw_init(mcdma_dev);
	return 0;
}

static int ifc_mcdma_dev_stats_get(struct rte_eth_dev *dev,
				   struct rte_eth_stats *eth_stats)
{
	unsigned int i;

	eth_stats->opackets = 0;
	eth_stats->obytes = 0;
	eth_stats->ipackets = 0;
	eth_stats->ibytes = 0;

	for (i = 0; i < dev->data->nb_rx_queues; i++) {
		struct ifc_mcdma_queue *rxq =
			(struct ifc_mcdma_queue *)dev->data->rx_queues[i];

		eth_stats->q_ipackets[i] = rxq->stats.pkt_cnt;
		eth_stats->q_ibytes[i] = rxq->stats.byte_cnt;
		eth_stats->ipackets += eth_stats->q_ipackets[i];
		eth_stats->ibytes += eth_stats->q_ibytes[i];
	}

	for (i = 0; i < dev->data->nb_tx_queues; i++) {
		struct ifc_mcdma_queue *txq =
			(struct ifc_mcdma_queue *)dev->data->tx_queues[i];

		eth_stats->q_opackets[i] = txq->stats.pkt_cnt;
		eth_stats->q_obytes[i] = txq->stats.byte_cnt;
		eth_stats->opackets += eth_stats->q_opackets[i];
		eth_stats->obytes   += eth_stats->q_obytes[i];
	}
	return 0;
}

int ifc_mcdma_dev_mtu_set(struct rte_eth_dev *dev, uint32_t mtu)
{
	if (mtu < MCDMA_MAX_MTU)
#ifndef DPDK_21_11_RC2
		dev->data->dev_conf.rxmode.max_rx_pkt_len = mtu;
#else
		dev->data->dev_conf.rxmode.max_lro_pkt_size = mtu;
#endif
	else
#ifndef DPDK_21_11_RC2
		dev->data->dev_conf.rxmode.max_rx_pkt_len = MCDMA_MAX_MTU;
#else
		dev->data->dev_conf.rxmode.max_lro_pkt_size = MCDMA_MAX_MTU;
#endif
	return 0;
}

static struct eth_dev_ops ifc_mcdma_eth_dev_ops = {
	.dev_configure        = ifc_mcdma_dev_configure,
	.dev_infos_get        = ifc_mcdma_dev_infos_get,
	.dev_start            = ifc_mcdma_dev_start,
	.dev_stop             = ifc_mcdma_dev_stop,
	.dev_close            = ifc_mcdma_dev_close,
	.link_update          = ifc_mcdma_dev_link_update,
	.rx_queue_setup       = ifc_mcdma_dev_rx_queue_setup,
	.tx_queue_setup       = ifc_mcdma_dev_tx_queue_setup,
	.rx_queue_release     = ifc_mcdma_dev_rx_queue_release,
	.tx_queue_release     = ifc_mcdma_dev_tx_queue_release,
	.rx_queue_start	      = ifc_mcdma_dev_rx_queue_start,
	.rx_queue_stop	      = ifc_mcdma_dev_rx_queue_stop,
	.tx_queue_start	      = ifc_mcdma_dev_tx_queue_start,
	.tx_queue_stop	      = ifc_mcdma_dev_tx_queue_stop,
	.stats_get	      = ifc_mcdma_dev_stats_get,
	.mtu_set	      = ifc_mcdma_dev_mtu_set,
};

void ifc_mcdma_dev_ops_init(struct rte_eth_dev *dev)
{
	dev->dev_ops = &ifc_mcdma_eth_dev_ops;
	dev->rx_pkt_burst = &ifc_mcdma_recv_pkts;
	dev->tx_pkt_burst = &ifc_mcdma_xmit_pkts;
}
