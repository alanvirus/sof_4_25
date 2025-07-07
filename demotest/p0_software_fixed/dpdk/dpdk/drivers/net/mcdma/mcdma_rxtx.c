/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#include <rte_mbuf.h>
#include <rte_cycles.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#ifndef DPDK_21_11_RC2
#include <rte_ethdev_pci.h>
#else
#include <ethdev_pci.h>
#endif
#include <fcntl.h>
#include <unistd.h>
#include "mcdma.h"
#include "mcdma_debug.h"
#include "mcdma_access.h"
#include "qdma_regs_2_registers.h"
#include "rte_pmd_mcdma.h"
#include "mcdma_platform.h"
#include "mcdma_mailbox.h"
#include <pio_reg_registers.h>

#define IFC_TID_CNT_TO_CHEK_FOR_HOL     1

int time_dif(struct timespec *a, struct timespec *b, struct timespec *result)
{
	long nsec_diff = a->tv_nsec - b->tv_nsec;
	long sec_diff = difftime(a->tv_sec, b->tv_sec);

	if (!sec_diff) {
		result->tv_sec = sec_diff;
		result->tv_nsec = a->tv_nsec - b->tv_nsec;
		return 0;
	}

	if (nsec_diff < 0) {
		result->tv_sec = sec_diff - 1;
		result->tv_nsec = (a->tv_nsec + 1e9) - b->tv_nsec;
		return 0;
	}
	result->tv_sec = sec_diff;
	result->tv_nsec = nsec_diff;
	return 0;
}

/* returns the starting address for the next packet */
uint64_t avmm_addr_manager(struct ifc_mcdma_queue *q, uint64_t *addr,
			   unsigned long payload)
{
	uint64_t start_addr;

	*addr = (*addr + payload < q->limit) ? *addr : q->base;

	start_addr = *addr;
	*addr = (*addr + payload);

	return start_addr;
}

/**
 * ifc_mcdma_descq_nb_free - number of free
 * place available for queuing new request
 */
static uint32_t ifc_mcdma_descq_nb_free(struct ifc_mcdma_queue *q)
{
	volatile uint32_t head;

	if (q->wbe || q->qie)
		head = (volatile uint32_t)q->consumed_head;
	else
		head = ifc_readl(q->qcsr + QDMA_REGS_2_Q_COMPLETED_POINTER);

	head = (head % q->qlen);

	return (head <= q->tail) ?
		(q->qlen - q->tail + head) : (head - q->tail);
}

/**
 * ifc_mcdma_wb_event_error - return the event error in case of
 * CTO, desc fetch or data fetch error in WB case.
 */
void ifc_mcdma_wb_event_error(uint16_t port, int qid, int dir){


	uint32_t err_desc_fetch, err_data_fetch;
	volatile uint32_t qhead;
	int err = 0;
	struct ifc_mcdma_queue *q;
	struct rte_eth_dev *dev = &rte_eth_devices[port];

	/*extract the queue based on qid and direction*/
	if (dir == IFC_QDMA_DIRECTION_TX){
		q = (struct ifc_mcdma_queue *)
			dev->data->tx_queues[qid];
	} else {
		q = (struct ifc_mcdma_queue *)
			dev->data->rx_queues[qid];
	}


	if(q->wbe == 1){
		/* check descriptor fetch or data fetch complto bit*/
		err_desc_fetch = q->consumed_head & 0x80000000;
		err_data_fetch = q->consumed_head & 0x40000000;

		if(err_desc_fetch)
			err |= DESC_FETCH_EVENT;
		if(err_data_fetch)
			err |= DATA_FETCH_EVENT;

		/* Read completion timeout regster 0x4C*/
		qhead = ifc_readl(q->qcsr + QDMA_REGS_2_Q_CPL_TIMEOUT);
		/*check if completion timeout bit is set [0th bit - q_cpl_timeout] */
		if(qhead & 1)
		{
			qhead &= 0xFFFFFFFE ;
			/* Clear q_cpl_timeout bit  */
			ifc_writel(q->qcsr + QDMA_REGS_2_Q_CPL_TIMEOUT, qhead);
			q->stats.cto_drops++;
			err |= COMPLETION_TIME_OUT_ERROR;

			q->irq_handler(port, qid, dir, &err);

			return;
		}

	}
	return;
}

/**
 * ifc_mcdma_descq_nb_used - number of descriptor
 * consumed by H/W since last sampling
 */

static uint32_t ifc_mcdma_descq_nb_used(struct ifc_mcdma_queue *q, uint32_t *id)
{
	uint32_t cur_head;
	volatile uint32_t head;

	if (q->wbe || q->qie)
		head = (volatile uint32_t)q->consumed_head;
	else
		head = ifc_readl(q->qcsr + QDMA_REGS_2_Q_COMPLETED_POINTER);

	cur_head = head;
	head = (head % q->qlen);

	rte_mb();
	if (id)
		*id = head;
	if (q->head == head && q->processed_head != cur_head) {
		q->processed_head = cur_head;
		return q->qlen;
	}
	q->processed_head = cur_head;

	return (head >= q->head) ?
		(head - q->head) : (q->qlen - q->head + head);
}

/**
 * ifc_mcdma_complete_requests - complete pending requests, if processed
 * @q: channel descriptor ring
 * @pkts: address where completed requests to be copied
 * @quota: maximum number of requests to search
 * @dir: direction of queue
 *
 * Complete processing all the outstanding requests, if processed
 * by DMA engine.
 *
 * @return number of requests completed in this iteration
 */
static int ifc_mcdma_complete_requests(struct ifc_mcdma_queue *q,
				      struct rte_mbuf **pkts,
				      uint32_t quota, int dir)
{
	struct rte_mbuf *mb = NULL;
	struct ifc_mcdma_desc_sw *sw;
	struct ifc_mcdma_desc *desc;
	uint32_t head;
	uint32_t i = 0;
	uint32_t t;
	uint32_t nr;

	/*
 	//Below variables will be used when Polling of WB in TID is enabled
	volatile uint32_t total_drops_count = 0;
	*/
#ifdef IFC_QDMA_INTF_ST
	int sof = 0, eof = 0;
#endif
	/*  if anything consumed */

	//Commenting the below feature for Polling of WB in TID as HW  has not implemented this now
	/*
	if (dir == IFC_QDMA_DIRECTION_RX) {
         	qhead = ifc_readl(q->qcsr + QDMA_REGS_2_Q_DATA_DRP_ERR_CTR);
         	if (qhead & (1 << 20)) {
			total_drops_count = qhead & 0xFFFF;
			q->data_drops_cnts =  q->data_drops_cnts + total_drops_count;
			qhead &= ~0x10FFFF;
             		ifc_writel(q->qcsr + QDMA_REGS_2_Q_DATA_DRP_ERR_CTR, qhead);
	     		q->stats.tid_drops++;
          }
	}
	*/

	nr = ifc_mcdma_descq_nb_used(q, &head);
	desc = (struct ifc_mcdma_desc *)(q->qbuf + sizeof(*desc) * q->head);
	for (t = q->head; i < nr ; t = (t + 1) % q->qlen) {
		if (desc->link || desc->desc_invalid) {
			if (desc->src == (uint64_t)q->qbuf_dma) {
				desc = (struct ifc_mcdma_desc *)
					((uint64_t)q->qbuf);
			} else {
				desc++;
			}
			--nr;
			continue;
		}
		sw = (struct ifc_mcdma_desc_sw *)&q->ctx[t];
		mb = (struct rte_mbuf *)sw->ctx_data;
		if (mb == NULL) {
			PMD_DRV_LOG(ERR, "Something bad with datastructures"
				    " qid:%u %u %d\n", q->qid, t, head);
			break;
		}
#ifdef IFC_QDMA_INTF_ST
		if (dir == IFC_QDMA_DIRECTION_RX) {
			mb->dynfield1[2] = 0;
			eof = desc->eof;
			sof = desc->sof;
			if (eof) {
				mb->dynfield1[2] |= IFC_QDMA_EOF_MASK;
				mb->pkt_len = desc->rx_pyld_cnt;
				/**
				 * pyld_cnt 0 is the special value.
				 * Represents 1MB
				 */
				if (desc->rx_pyld_cnt == 0)
					mb->pkt_len = IFC_MCDMA_MB;
			} else {
				mb->pkt_len = desc->len;
			}
			if (sof)
				mb->dynfield1[2] |= IFC_QDMA_SOF_MASK;
			rte_pktmbuf_data_len(mb) = mb->pkt_len;
#ifdef IFC_QDMA_META_DATA
			/*In case of D2H src address contain metadata. 
			* Meta data is 64 bit, and the array is 32 bit
			* So changed it to index 0 and 1 for metadata & index 2 for flags */
			mb->dynfield1[1] = desc->src;
                        mb->dynfield1[0] = (desc->src >> 32);
#endif

		}
#else
		if (dir == IFC_QDMA_DIRECTION_RX) {
			mb->dynfield1[2] = 0;
			mb->pkt_len = desc->len;
		}
#endif
		/* return requests to applications for further processing */
		pkts[i++] = (void *)sw->ctx_data;
		if (i >= quota) {
			q->head = (t + 1) % q->qlen;
			break;
		}
		desc++;
		q->head = head;
	}
#ifdef IFC_DEBUG_STATS
	q->stats.processed += i;
#endif
	return i;
}

/**
 * ifc_mcdma_desc_queue - internal, prepare request for prcessing
 */
static int ifc_mcdma_descq_queue_prepare(struct ifc_mcdma_queue *q,
					struct rte_mbuf *mb,
					int rx_dir)
{
	struct ifc_mcdma_desc *d;
	struct rte_eth_dev *dev = q->ethdev;
	uint64_t *dma_buf;
	uint32_t tail;

	if (ifc_mcdma_descq_nb_free(q) <= 0)
		return -1;

	tail = q->tail;
	d = (struct ifc_mcdma_desc *)((uint64_t)q->qbuf + sizeof(*d) * tail);
	if (d->link) {	/* skip the link */
		tail = (tail + 1) % q->qlen;
		q->didx = ((q->didx + 1) % IFC_NUM_DESC_INDEXES);
		d->didx = q->didx;
		d = (struct ifc_mcdma_desc *)((uint64_t)q->qbuf +
					     sizeof(*d) * tail);
	}
	memset(d, 0, sizeof(struct ifc_mcdma_desc));
#ifndef DPDK_21_11_RC2
	dma_buf = &(mb->buf_physaddr);
#else
	dma_buf = &(mb->buf_iova);
#endif

	if (q->dir)
		d->len = rte_pktmbuf_pkt_len(mb);
	else
#ifndef DPDK_21_11_RC2
		d->len = dev->data->dev_conf.rxmode.max_rx_pkt_len;
#else
		d->len = dev->data->dev_conf.rxmode.max_lro_pkt_size;
#endif

	switch (rx_dir) {
	case IFC_QDMA_DIRECTION_RX:
		#if (IFC_DATA_WIDTH == 1024)
                        /*Check the dest address is aligned to 128 bytes*/
                        if (*dma_buf % 128 != 0) {
				mb->buf_addr= mb->buf_addr  + (((*dma_buf + 127) & ~127) - *dma_buf) ;
				*dma_buf = (*dma_buf + 127) & ~127;
			}
                #endif
                        d->src = avmm_addr_manager(q, (uint64_t *)&q->addr, d->len);
                        d->dest = *dma_buf;
		break;
	case IFC_QDMA_DIRECTION_TX:
		#if (IFC_DATA_WIDTH == 1024)
                	/*Check the src address is aligned to 128 bytes*/
			if (*dma_buf % 128 != 0) {
				mb->buf_addr= mb->buf_addr  + (((*dma_buf + 127) & ~127) - *dma_buf) ;
				*dma_buf = (*dma_buf + 127) & ~127; // Align
		 	}
                #endif
                        d->src = *dma_buf;
                        d->dest = avmm_addr_manager(q, (uint64_t *)&q->addr, d->len);
#ifdef IFC_QDMA_INTF_ST
		d->sof = 0;
		d->eof = 0;

#ifndef IFC_BUF_REUSE
		d->sof = 1;
		d->eof = 1;
#else
#ifdef IFC_QDMA_META_DATA
		d->dest = mb->dynfield1[0];
		d->dest =d->dest<<32;
                d->dest = mb->dynfield1[1];
#endif

		if (IFC_QDMA_SOF_MASK & mb->dynfield1[2])  {
			d->sof = 1;
			q->sof_rcvd = 1;
		}
		if (IFC_QDMA_EOF_MASK & mb->dynfield1[2]) {
			if (q->sof_rcvd == 0) {
				PMD_DRV_LOG(ERR, "EOF Received without SOF\n");
				return -1;
			}
			d->eof = 1;
			q->sof_rcvd = 0;
		}
#endif
#endif
		break;
	default:
		break;
	}

#ifdef IFC_QDMA_INTF_ST
#if 0
	/* For Rx, we don't know the start and end of the file */
	if (rx_dir == IFC_QDMA_DIRECTION_TX) {
		if  ((d->len != IFC_MTU_LEN) && ((d->len % 64) != 0) &&
		     ((IFC_QDMA_EOF_MASK & mb->dynfield1[1]) == 0)) {
			PMD_DRV_LOG(ERR,
				    "Invalid length for Non-EOF descriptor\n");
			return -2;
		}
	}
#endif
#endif
	/* add mem barrier */
	rte_wmb();
	q->ctx[tail].ctx_data = (void *)mb;
	q->tail = (tail + 1) % q->qlen;
	q->didx = ((q->didx + 1) % IFC_NUM_DESC_INDEXES);
	d->didx = q->didx;
	d->desc_invalid = 0;
#ifdef IFC_64B_DESC_FETCH
	q->count_desc += 1;
#endif

	/* set write back in D2H direction*/
	if (q->dir == IFC_QDMA_DIRECTION_RX)
		d->wb_en = IFC_QDMA_RX_DESC_CMPL_PROC;
	else
		d->wb_en = 0;

	d->msix_en = 0;
	return 0;
}

#ifdef HW_FIFO_ENABLED
/**
 * ifc_mcdma_check_hw_fifo - internal, check for HW register to know
 *                          available space
 */
static int ifc_mcdma_check_hw_fifo(struct ifc_mcdma_queue *q)
{
        int off, regoff = 0;
        uint32_t space = 32;
	struct rte_eth_dev  *dev = q->ethdev;
	struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;
#ifdef IFC_QDMA_ST_MULTI_PORT
        space = 0;
        regoff = (q->qid * 4);
#endif
#ifdef IFC_QDMA_INTF_ST
        /*
         * check for fifo_len. In case if it reaches 0, read latest
         * values from DMQ CSR
         */
        if (q->dir)
                off = QDMA_REGS_2_P0_H2_D_TPTR_AVL + regoff;
        else
                off = QDMA_REGS_2_P0_D2_H_TPTR_AVL + regoff;

        q->fifo_len = ifc_readl((char *)mcdma_dev->qcsr + off);
        if (q->fifo_len <=  space) {
                return -1;
        }
#else // AVST
#ifdef IFC_QDMA_ST_MULTI_PORT
        if (pthread_mutex_lock(&mcdma_dev->tid_lock) != 0) {
                PMD_DRV_LOG(ERR, "Acquiring mutex got failed \n");
                return -1;
        }
#endif
        if (q->dir)
		off = QDMA_REGS_2_P0_H2_D_TPTR_AVL + regoff;
        else
                off = QDMA_REGS_2_P0_D2_H_TPTR_AVL + regoff;

        q->fifo_len = ifc_readl((char *)mcdma_dev->qcsr + off);
        if (q->fifo_len <= space) {
		PMD_DRV_LOG(DEBUG, "No FIFO Space\n");
                if (pthread_mutex_unlock(&mcdma_dev->tid_lock) != 0) {
                        PMD_DRV_LOG(ERR, "Releasing mutex got failed \n");
                        return -1;
                }
                return -1;
        }
#ifdef IFC_QDMA_ST_MULTI_PORT
        if (pthread_mutex_unlock(&mcdma_dev->tid_lock) != 0) {
                PMD_DRV_LOG(ERR, "Releasing mutex got failed \n");
                return -1;
        }
#endif
#endif
        return 0;
}
#endif

#ifdef TRACK_DF_HEAD
/**
 * ifc_mcdma_check_df_head - internal, check if descriptor fetch head is moved
 */
static int ifc_mcdma_check_df_head(struct ifc_mcdma_queue *q)
{
        if ((q->tid_updates) &&
           ((q->tid_updates % IFC_TID_CNT_TO_CHEK_FOR_HOL) == 0)) {
                uint32_t qhead = ifc_readl(q->qcsr + QDMA_REGS_2_Q_HEAD_POINTER);
                if (q->processed_tail != qhead) {
                        /* HOL hit */
#ifdef IFC_DEBUG_STATS
                        q->stats.tid_skip++;
#endif
                        /* No space available */
                        return -1;
                }
                q->last_head = qhead;
                q->tid_updates = 0;
        }
        /* space available */
        return 0;
}
#endif

#ifdef TID_FIFO_ENABLED
/**
 * ifc_mcdma_check_fifo_len - internal, check for fifo space
 */
static int ifc_mcdma_check_fifo_len(struct ifc_mcdma_queue *q)
{
#ifdef HW_FIFO_ENABLED
        return ifc_mcdma_check_hw_fifo(q);
#else
        /* HW fifo disabled */
        if ((q->tid_updates) &&
           ((q->tid_updates % IFC_TID_CNT_TO_CHEK_FOR_HOL) == 0)) {
                uint32_t qhead = ifc_readl(q->qcsr + QDMA_REGS_2_Q_HEAD_POINTER);
                if (q->processed_tail != qhead) {
                        /* HOL hit */
#ifdef IFC_DEBUG_STATS
                        q->stats.tid_skip++;
#endif
                        /* No space available */
                        return -1;
                }
                q->last_head = qhead;
                q->tid_updates = 0;
        }
#endif
        /* space available */
        return 0;
}
#endif

/**
 * ifc_mcdma_desc_queue_submit - internal, submits request for prcessing
 */
static int ifc_mcdma_descq_queue_submit(struct ifc_mcdma_queue *q)
{
#ifdef TID_LATENCY_STATS
        static struct timespec last;
        struct timespec timediff;
        struct timespec timediff1;
#endif
	struct ifc_mcdma_desc *d;
	int didx;
	uint32_t qhead;
	int count = 0;
#ifdef TID_FIFO_ENABLED
        if (ifc_mcdma_check_fifo_len(q) < 0)
                return -1;
#endif

	/* update completion mechanism */
	didx = q->tail - 1;
	d = (struct ifc_mcdma_desc *)((uint64_t)q->qbuf + sizeof(*d) * didx);

	if (q->wbe || q->qie)
		d->wb_en = 1;
	d->msix_en = q->qie;

#ifdef IFC_64B_DESC_FETCH
	if((q->count_desc % 2) && (q->tail != q->qlen -1)){
		q->tail = (q->tail + 1) % q->qlen;
		q->didx = ((q->didx + 1) % IFC_NUM_DESC_INDEXES);
		didx = q->tail -1;
		d = (struct ifc_mcdma_desc *)((uint64_t)q->qbuf +
				sizeof(*d) * didx);
		d->didx = q->didx;
		if(d->link == 0)
			d->desc_invalid = 1;
	}
	q->count_desc = 0;
#endif

	/* Inacse of current tail same as processed_tail, update tail + 1,
	 * and check if head is moved.
	 */
	if (q->tail == q->processed_tail) {
		ifc_writel(q->qcsr + QDMA_REGS_2_Q_TAIL_POINTER,
			(q->processed_tail + 1));
		while (count < IFC_CNT_HEAD_MOVE) {
			qhead = ifc_readl(q->qcsr + QDMA_REGS_2_Q_HEAD_POINTER);
			if (qhead == q->processed_tail + 1)
				break;
			count++;
		}
	}

#ifdef IFC_DEBUG_STATS
	q->stats.tid_update += (q->tail <= q->processed_tail) ?
		(q->qlen - 1 - q->processed_tail + q->tail) :
		(q->tail - q->processed_tail);
#endif
	q->stats.submitted_didx = q->didx;
	/* add mem barrier */
	rte_wmb();
	/* update tail pointer */
	ifc_writel(q->qcsr + QDMA_REGS_2_Q_TAIL_POINTER, q->tail);

#ifdef TID_LATENCY_STATS
        uint32_t cnt = (q->tail <= q->processed_tail) ?
		(q->qlen - 1 - q->processed_tail + q->tail) :
		(q->tail - q->processed_tail);

        clock_gettime(CLOCK_MONOTONIC, &q->cur_time);
        time_dif(&q->cur_time, &q->last, &timediff);
        time_dif(&q->cur_time, &last, &timediff1);
        q->last = q->cur_time;
        PMD_DRV_LOG(ERR, "TEST%u%u: %u,%lu,%lu ns: %lu %lu\n",
                        q->qid, q->dir, cnt,
                        (uint64_t)(timediff.tv_nsec/1e3),
                        (uint64_t)(timediff1.tv_nsec/1e3),
                        last.tv_nsec ,q->cur_time.tv_nsec);
        last = q->cur_time;
#endif
	/* update last update tail */
	q->processed_tail = q->tail;
	q->tid_updates++;

	return 0;
}

uint16_t ifc_mcdma_recv_desc(struct ifc_mcdma_queue *rxq,
			     struct rte_mbuf **rx_pkts,
			     uint16_t nb_pkts)
{
	struct rte_mbuf *mb;
	uint16_t mbuf_index = 0;
	int num_desc = 0;
	uint32_t cnt = 0, i = 0;
	int ret = 0;

#ifdef TRACK_DF_HEAD
	/* Track descriptor fetch head before doing TID update */
	if (nb_pkts) {
		ret = ifc_mcdma_check_df_head(rxq);
		if (ret < 0)
#ifndef IFC_BUF_REUSE
			return 0;
#else
			return DF_HEAD_NOT_MOVED;
#endif
	}
#endif

#ifndef IFC_BUF_REUSE
	cnt = ifc_mcdma_complete_requests(rxq, rx_pkts, rxq->qlen, 0);

	for (i = 0; i < cnt; i++) {
		rxq->stats.pkt_cnt++;
		rxq->stats.byte_cnt += rte_pktmbuf_pkt_len(rx_pkts[i]);
	}

	/* For requests greater than QDEPTH, prepare as per completions */
	if ((rxq->prev_prepared + nb_pkts) <= QDEPTH)
		num_desc = nb_pkts;
	else
		num_desc = cnt;

	rxq->prev_prepared += num_desc;
#else
	if (!nb_pkts) {
		cnt = ifc_mcdma_complete_requests(rxq, rx_pkts, rxq->qlen, 0);

		for (i = 0; i < cnt; i++) {
			rxq->stats.pkt_cnt++;
			rxq->stats.byte_cnt += rte_pktmbuf_pkt_len(rx_pkts[i]);
		}
	}

	num_desc = nb_pkts;
#endif

	rxq->stats.req_cnt += num_desc;

	if (num_desc > 0) {
		struct rte_mbuf *tmp_sw_ring[num_desc];

		if (ifc_mcdma_descq_nb_free(rxq) <= 0)
			goto last;
		/* allocate new buffer */
		if (rte_mempool_get_bulk(rxq->mb_pool, (void *)tmp_sw_ring,
					 num_desc) != 0){
			PMD_DRV_LOG(ERR, "%s(): %d: No MBUFS, queue id = %d, "
				    "mbuf_avail_count = %d, mbuf_in_use_count ="
				    " %d, num_desc = %d\n",
				    __func__, __LINE__, rxq->qid,
				    rte_mempool_avail_count(rxq->mb_pool),
				    rte_mempool_in_use_count(rxq->mb_pool),
				    num_desc);
			return cnt;
		}

		for (mbuf_index = 0; mbuf_index < num_desc; mbuf_index++) {
			mb = tmp_sw_ring[mbuf_index];
			mb->next = 0;
			mb->nb_segs = 1;

			/* make it so the data pointer starts there too... */
			mb->data_off = RTE_PKTMBUF_HEADROOM;

			ifc_mcdma_descq_queue_prepare(rxq, mb, 0);
		}

		rte_wmb();
		ret = ifc_mcdma_descq_queue_submit(rxq);
		while (ret) {
			pthread_yield();
			ret = ifc_mcdma_descq_queue_submit(rxq);
			rxq->stats.failed_attempts++;
		}
	}

last:
	return cnt;
}

uint16_t ifc_mcdma_recv_pkts(void *rx_queue, struct rte_mbuf **rx_pkts,
			     uint16_t nb_pkts)
{
	struct ifc_mcdma_queue *rxq = rx_queue;
	uint16_t count;

	count = ifc_mcdma_recv_desc(rxq, rx_pkts, nb_pkts);
	return count;
}

#ifndef IFC_BUF_REUSE
static int ifc_mcdma_free_tx_mbuf(struct ifc_mcdma_queue *q)
{
	struct rte_mbuf *mb = NULL;
	struct ifc_mcdma_desc_sw *sw;
	struct ifc_mcdma_desc *desc;
	uint32_t head;
	uint32_t i = 0;
	uint32_t t;
	uint32_t nr;

	/* if anything consumed */
	nr = ifc_mcdma_descq_nb_used(q, &head);
	desc = (struct ifc_mcdma_desc *)(q->qbuf + sizeof(*desc) * q->head);
	for (t = q->head; i < nr ; t = (t + 1) % q->qlen) {
		if (desc->link) {
			if (desc->src == (uint64_t)q->qbuf_dma)
				desc = (struct ifc_mcdma_desc *)
					((uint64_t)q->qbuf);
			else
				desc++;
			--nr;
			q->stats.processed_skip += 1;
			continue;
		}
		sw = (struct ifc_mcdma_desc_sw *)&q->ctx[t];
		mb = (struct rte_mbuf *)sw->ctx_data;
		if (mb == NULL) {
			PMD_DRV_LOG(ERR,
				    "something bad with datastructures %u %d\n",
				    t, head);
			break;
		}
		rte_pktmbuf_free(mb);
		q->ctx[t].ctx_data = NULL;
		i++;
		desc++;
		q->head = head;
	}
#ifdef IFC_DEBUG_STATS
	q->stats.processed += i;
#endif
	return i;
}
#endif

uint16_t ifc_mcdma_xmit_desc(struct ifc_mcdma_queue *txq,
			     struct rte_mbuf **tx_pkts,
			     uint16_t nb_pkts, int dir)
{
	struct rte_mbuf *mb;
	uint16_t count;
	int nsegs, avail;
	int ret = 0;
	uint16_t num_desc;
	uint16_t nr = 0;

	num_desc = nb_pkts;

#ifdef TRACK_DF_HEAD
	/* Track descriptor fetch head before doing TID update */
	if (nb_pkts) {
		ret = ifc_mcdma_check_df_head(txq);
		if (ret < 0)
#ifndef IFC_BUF_REUSE
			return 0;
#else
			return DF_HEAD_NOT_MOVED;
#endif
	}
#endif

#ifdef IFC_BUF_REUSE
        if (!nb_pkts)
                nr = ifc_mcdma_complete_requests(txq, tx_pkts, txq->qlen, 1);

#else
	/**
	 * Incase of avoiding drop cnt prepare requests as per nb_pkts
	 * and not as per completions
	 */
#ifndef AVOID_TX_DROP_COUNT
	uint16_t free_desc,i;
	/* Calculate number of free descq */
        free_desc = ifc_mcdma_descq_nb_free(txq) - 1;
        nr = ifc_mcdma_free_tx_mbuf(txq);

	/**
	 * For requests greater than QDEPTH, prepare as per completions or
	 * available descq
	 */
        if ((txq->prev_prepared + nb_pkts) > QDEPTH) {
                if (free_desc <= nb_pkts) {
			/**
			 * Chances of completions > number of requesting
			 * buffers, hence to avoid accesing NULL buffer prepare
			 * as per availability of buffers
			 */
                        if (nr < nb_pkts)
                                num_desc = nr;
                        else
                                num_desc = free_desc;
			/* Free up unprepared buffers */
                        for (i = num_desc; i < nb_pkts; i++)
                                rte_pktmbuf_free(tx_pkts[i]);
                }
        }
#endif
#endif
	txq->stats.req_cnt += num_desc;

	for (count = 0; count < num_desc; count++) {
		mb = tx_pkts[count];
		nsegs = mb->nb_segs;
		avail = ifc_mcdma_descq_nb_free(txq);
		if (nsegs > avail)
			break;
		avail -= nsegs;
		while (nsegs && mb) {
			ifc_mcdma_descq_queue_prepare(txq, mb, dir);
			--nsegs;
			txq->stats.pkt_cnt++;
			txq->stats.byte_cnt += rte_pktmbuf_pkt_len(mb);
			mb = mb->next;
		}
	}

	rte_wmb();
	if (count) {
		ret = ifc_mcdma_descq_queue_submit(txq);
		while (ret) {
			pthread_yield();
			ret = ifc_mcdma_descq_queue_submit(txq);
			txq->stats.failed_attempts++;
		}
	}
#ifndef IFC_BUF_REUSE
#ifdef AVOID_TX_DROP_COUNT
	/**
	 * To avoid drop count poll the completion status,
	 * till it is same as that of submited requests
	 */
	uint16_t num_resp = 0;

	while (num_resp != count) {
		nr = ifc_mcdma_free_tx_mbuf(txq);
		num_resp += nr;
	}
#endif

	txq->prev_prepared += count;
	return count;
#else
	return nr;
#endif
}

uint16_t ifc_mcdma_xmit_pkts(void *tx_queue, struct rte_mbuf **tx_pkts,
			     uint16_t nb_pkts)
{
	struct ifc_mcdma_queue *txq = tx_queue;
	uint16_t count;
	int dir = 1;

	count =	ifc_mcdma_xmit_desc(txq, tx_pkts, nb_pkts, dir);

	return count;
}

#define IFC_QDMA_DUMMY_AVMM	0x0

/**
 * ifc_mcdma_read_pio - Read the value from BAR2 address
 */
uint64_t ifc_mcdma_pio_read64(uint16_t portno, uint64_t addr)
{
	struct rte_eth_dev *dev = &rte_eth_devices[portno];
	struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;

	if (mcdma_dev == NULL)
		return 0;

	if (mcdma_dev == NULL)
		return 0;

	if (unlikely(mcdma_dev->uio_id > UIO_MAX_DEVICE)) {
		PMD_DRV_LOG(DEBUG, "Invalid Device Context\n");
		return 0;
	}

	return ifc_readll(mcdma_dev->bar_addr[IFC_MCDMA_PIO_BAR] + addr);
}

/**
 * ifc_mcdma_read_pio - Read the value from BAR2 address
 */
void ifc_mcdma_pio_write64(uint16_t port, uint64_t addr,
			  uint64_t value)
{
	struct rte_eth_dev *dev = &rte_eth_devices[port];
	struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;

	if (mcdma_dev == NULL)
		return;

	if (mcdma_dev == NULL)
		return;

	if (unlikely(mcdma_dev->uio_id > UIO_MAX_DEVICE)) {
		PMD_DRV_LOG(DEBUG, "Invalid Device Context\n");
		return;
	}

	ifc_writell(mcdma_dev->bar_addr[IFC_MCDMA_PIO_BAR] + addr, value);
}

uint64_t ifc_mcdma_read64(uint16_t portno, uint64_t addr, int bar_num)
{
	struct rte_eth_dev *dev = &rte_eth_devices[portno];
	struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;

	if (mcdma_dev == NULL)
		return 0;

	if (mcdma_dev == NULL)
		return 0;

	if (unlikely(mcdma_dev->uio_id > UIO_MAX_DEVICE)) {
		PMD_DRV_LOG(DEBUG, "Invalid Device Context\n");
		return 0;
	}

	return ifc_readll(mcdma_dev->bar_addr[bar_num] + addr);
}


void ifc_mcdma_write64(uint16_t port, uint64_t addr,
			  uint64_t value, int bar_num)
{
	struct rte_eth_dev *dev = &rte_eth_devices[port];
	struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;

	if (mcdma_dev == NULL)
		return;

	if (mcdma_dev == NULL)
		return;

	if (unlikely(mcdma_dev->uio_id > UIO_MAX_DEVICE)) {
		PMD_DRV_LOG(DEBUG, "Invalid Device Context\n");
		return;
	}

	ifc_writell(mcdma_dev->bar_addr[bar_num] + addr, value);
}

#ifdef IFC_PIO_128
/**
 * ifc_mcdma_pio_read128 - Read the value from BAR2 address
 */
int ifc_mcdma_pio_read128(uint16_t portid, uint64_t offset, uint64_t *buf, int bar_num)
{
	struct rte_eth_dev *qdev = &rte_eth_devices[portid];
	struct ifc_mcdma_device *mcdma_dev = qdev->data->dev_private;
	uint64_t base;
	uint64_t addr;
	__m128i temp;

	if (qdev == NULL)
		return -1;

	if (mcdma_dev == NULL)
		return -1;

	if (unlikely(mcdma_dev->uio_id > UIO_MAX_DEVICE)) {
		PMD_DRV_LOG(DEBUG, "Invalid Device Context\n");
		return -1;
	}

	base = (uint64_t)mcdma_dev->bar_addr[bar_num];
	addr = base + offset;

	temp = _mm_loadu_si128((__m128i *)addr);
	_mm_storeu_si128((__m128i *)buf, temp);

	return 0;
}

/**
 * ifc_mcdma_pio_write128 - Write the value to BAR2 address
 */
int ifc_mcdma_pio_write128(uint16_t portid, uint64_t offset, uint64_t *val, int bar_num)
{
	struct rte_eth_dev *qdev = &rte_eth_devices[portid];
	struct ifc_mcdma_device *mcdma_dev = qdev->data->dev_private;
	uint64_t addr;
	uint64_t base;
	__m128i temp;

	if (qdev == NULL)
		return -1;

	if (mcdma_dev == NULL)
		return -1;

	if (unlikely(mcdma_dev->uio_id > UIO_MAX_DEVICE)) {
		PMD_DRV_LOG(DEBUG, "Invalid Device Context\n");
		return -1;
	}

	base = (uint64_t)mcdma_dev->bar_addr[bar_num];
	addr = base + offset;

	temp =  _mm_loadu_si128((__m128i *)val);
	_mm_storeu_si128((__m128i *)addr, temp);

	return 0;
}
#endif

#ifdef IFC_PIO_256
/**
 * ifc_mcdma_pio_read256 - Read the value from BAR2 address
 */
int ifc_mcdma_pio_read256(uint16_t portid, uint64_t offset, uint64_t *buf,
			  int bar_num)
{
	struct rte_eth_dev *qdev = &rte_eth_devices[portid];
	struct ifc_mcdma_device *mcdma_dev = qdev->data->dev_private;
	uint64_t base;
	uint64_t addr;
	__m256i temp;

	if (qdev == NULL)
		return -1;

	if (mcdma_dev == NULL)
		return -1;

	if (unlikely(mcdma_dev->uio_id > UIO_MAX_DEVICE)) {
		PMD_DRV_LOG(DEBUG, "Invalid Device Context\n");
		return -1;
	}

	base = (uint64_t)mcdma_dev->bar_addr[bar_num];
	addr = base + offset;

	temp = _mm256_loadu_si256((__m256i *)addr);
	_mm256_storeu_si256((__m256i *)buf, temp);

	return 0;
}

/**
 * ifc_mcdma_pio_write256 - Write the value to BAR2 address
 */
int ifc_mcdma_pio_write256(uint16_t portid, uint64_t offset, uint64_t *val, 
			   int bar_num)
{
	struct rte_eth_dev *qdev = &rte_eth_devices[portid];
	struct ifc_mcdma_device *mcdma_dev = qdev->data->dev_private;
	uint64_t addr;
	uint64_t base;
	__m256i temp;

	if (qdev == NULL)
		return -1;

	if (mcdma_dev == NULL)
		return -1;

	if (unlikely(mcdma_dev->uio_id > UIO_MAX_DEVICE)) {
		PMD_DRV_LOG(DEBUG, "Invalid Device Context\n");
		return -1;
	}

	base = (uint64_t)mcdma_dev->bar_addr[bar_num];
	addr = base + offset;

	temp = _mm256_loadu_si256((__m256i *)val);
	_mm256_storeu_si256((__m256i *)addr, temp);

	return 0;
}
#endif

#ifdef IFC_PIO_512
/**
 * ifc_mcdma_pio_read512 - Read the value from BAR2 address
 */
int ifc_mcdma_pio_read512(uint16_t portid, uint64_t offset, uint64_t *buf,
			  int bar_num)
{
	struct rte_eth_dev *qdev = &rte_eth_devices[portid];
	struct ifc_mcdma_device *mcdma_dev = qdev->data->dev_private;
	uint64_t base;
	uint64_t addr;
	__m512i temp;

	if (qdev == NULL)
		return -1;

	if (mcdma_dev == NULL)
		return -1;

	if (unlikely(mcdma_dev->uio_id > UIO_MAX_DEVICE)) {
		PMD_DRV_LOG(DEBUG, "Invalid Device Context\n");
		return -1;
	}

	base = (uint64_t)mcdma_dev->bar_addr[bar_num];
	addr = base + offset;

	temp = _mm512_loadu_si512((const void *)addr);
	_mm512_storeu_si512((void *)buf, temp);

	return 0;
}

/**
 * ifc_mcdma_pio_write512 - Write the value to BAR2 address
 */
int ifc_mcdma_pio_write512(uint16_t portid, uint64_t offset, uint64_t *val,
			   int bar_num)
{
	struct rte_eth_dev *qdev = &rte_eth_devices[portid];
	struct ifc_mcdma_device *mcdma_dev = qdev->data->dev_private;
	uint64_t addr;
	uint64_t base;
	__m512i temp;

	if (qdev == NULL)
		return -1;

	if (mcdma_dev == NULL)
		return -1;

	if (unlikely(mcdma_dev->uio_id > UIO_MAX_DEVICE)) {
		PMD_DRV_LOG(DEBUG, "Invalid Device Context\n");
		return -1;
	}

	base = (uint64_t)mcdma_dev->bar_addr[bar_num];
	addr = base + offset;

	temp = _mm512_loadu_si512((const void *)val);
	_mm512_storeu_si512((const void *)addr, temp);

	return 0;
}
#endif

int mcdma_ip_reset(uint16_t portid)
{
	struct rte_eth_dev *dev = &rte_eth_devices[portid];
	struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;
	int ret;

	ret = ifc_mcdma_ip_reset(mcdma_dev);
	if (!ret)
		PMD_DRV_LOG(ERR, "IP reset successful\n");
	else
		PMD_DRV_LOG(ERR, "IP reset unsuccessful\n");

	return ret;
}
