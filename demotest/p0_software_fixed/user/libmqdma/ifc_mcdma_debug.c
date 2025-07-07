// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <ifc_mqdma.h>
#include <ifc_mcdma_debug.h>
#include <unistd.h>

void qdma_hexdump(FILE *f, unsigned char *base, int len)
{
        int i;

        for (i = 0; i < len; i+=4) {
                if ((i % 32) == 0)
#ifdef IFC_32BIT_SUPPORT
                        fprintf(f, "\n%8llx ", (uint64_t)(uintptr_t) base + i);
#else
                        fprintf(f, "\n%8lx ", (uint64_t) base + i);
#endif
                fprintf(f, "%08x ", *(uint32_t *)(base+i));
        }
        fprintf (f, "\n");
}

#ifdef TID_LATENCY_STATS
static int time_diff(struct timespec *a, struct timespec *b, struct timespec *result)
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
#endif
#ifdef IFC_DEBUG_STATS
struct ifc_qdma_hw_stats_s ifc_qdma_hw_h2d_stats[16] = {
	{ 0x150 , "MRd tlp received by RX_RQ" },
	{ 0x154 , "completions tlp sent from RX_RQ" },
	{ 0x158 , "RX TLPs received from HIP" },
	{ 0x15c , "H2D MRd tlp sent from H2D" },
	{ 0x160 , "H2D MRd tag0 num of cycles till CpLD" },
	{ 0x168 , "hardware instruction ack count. 8 bits per port H2D[Port4:Port3:Port2:Port1]" },
	{ 0x16c , "hardware instruction ack count. 8 bits per port D2H[Port4:Port3:Port2:Port1]" },
	{ 0x170 , "SOF sent on port 1" },
	{ 0x174 , "SOF sent on port 2" },
	{ 0x178 , "SOF sent on port 3" },
	{ 0x17c , "SOF sent on port 4" },
	{ 0x180 , "EOF sent on port 1" },
	{ 0x184 , "EOF sent on port 2" },
	{ 0x188 , "EOF sent on port 3" },
	{ 0x18c , "EOF sent on port 4" },
};

struct ifc_qdma_hw_stats_s ifc_qdma_hw_d2h_stats[16] = {
	{ 0x190 , "d2h tlp at tx schd i/p" },
	{ 0x194 , "cmpl tlp at tx schd i/p"},
	{ 0x198 , "msi/wb/desc_update tlp at tx schd i/p" },
	{ 0x19c , "h2d tlp at tx schd i/p" },
	{ 0x1a0 , "EOF received on port 1 AVST" },
	{ 0x1a4 , "EOF received on port 2 AVST" },
	{ 0x1a8 , "EOF received on port 3 AVST" },
	{ 0x1ac , "EOF received on port 4 AVST" },
	{ 0x1b0 , "descriptors processed  successfully on port 1" },
	{ 0x1b4 , "descriptors processed  successfully on port 2" },
	{ 0x1b8 , "descriptors processed  successfully on port 3" },
	{ 0x1bc , "descriptors processed  successfully on port 4" },
	{ 0x1c0 , "D2H received Descriptors on port 1" },
	{ 0x1c4 , "D2H received descriptors on port 2" },
	{ 0x1c8 , "D2H received descriptors on port 3" },
	{ 0x1cc , "D2H received descriptors on port 4" },
};
#endif

#ifdef IFC_MCDMA_EXTERNL_DESC
int ifc_qdma_dbg_ext_fetch_chnl_qcsr(struct ifc_qdma_device *qdev,
			    int dir, int qid)
{
	int channel_base = 0;

	if (qdev == NULL)
		return -1;
	if (qdev->pdev == NULL)
		return -1;

	channel_base = 0x10000 + (qid * 256) + (dir * 128);

	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR, "channel ID : %0u\n",
		     qid);

	if (dir == IFC_QDMA_DIRECTION_RX) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "RX Queue Config:\n");
	} else {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "TX Queue Config:\n");
	}
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
		     "start addr:\toff 0x%x\t: 0x%0lx\n",
		     channel_base + QDMA_REGS_2_Q_START_ADDR_L,
		     ifc_readq(qdev->pdev->r[0].map + channel_base + QDMA_REGS_2_Q_START_ADDR_L));
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
		     "Buf size:\toff 0x%x\t: 0x%0lx\n",
		     channel_base + QDMA_REGS_2_Q_SIZE,
		     ifc_readq(qdev->pdev->r[0].map + channel_base + QDMA_REGS_2_Q_SIZE));
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
		     "Tail ptr:\toff 0x%x\t: 0x%lx\n",
		     channel_base + QDMA_REGS_2_Q_TAIL_POINTER,
		     ifc_readq(qdev->pdev->r[0].map + channel_base + QDMA_REGS_2_Q_TAIL_POINTER));
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
		     "Head ptr:\toff 0x%x\t: 0x%lx\n",
		     channel_base + QDMA_REGS_2_Q_HEAD_POINTER,
		     ifc_readq(qdev->pdev->r[0].map + channel_base + QDMA_REGS_2_Q_HEAD_POINTER));
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
	             "WB ADDR:\toff 0x%x\t: 0x%lx\n",
		     channel_base + QDMA_REGS_2_Q_CONSUMED_HEAD_ADDR_L,
		     ifc_readq(qdev->pdev->r[0].map + channel_base + QDMA_REGS_2_Q_CONSUMED_HEAD_ADDR_L));
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
		     "Comp ptr:\toff 0x%x\t: 0x%lx\n",
		     channel_base + QDMA_REGS_2_Q_COMPLETED_POINTER,
		     ifc_readq(qdev->pdev->r[0].map + channel_base + QDMA_REGS_2_Q_COMPLETED_POINTER));
	return 0;
}
#endif

#ifdef DEBUG
int ifc_qdma_dump_config(struct ifc_qdma_device *qd)
{
	uint32_t ip_params;
	uint32_t num_pfs;
	uint32_t i;

	IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_DEBUG,
		     "ifc qdma config\n");
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
		     "ctrl : %0xu\n", ifc_readl(qd->qcsr + 0x00200000U));
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
		     "wbid : %0xu\n", ifc_readl(qd->qcsr + 0x00200008U));
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
		     "version : %0xu\n", ifc_readl(qd->qcsr + 0x00200070U));
	ip_params = ifc_readl(qd->qcsr + 0x00200070U);
	num_pfs = ip_params & 0x000000F0U;
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "ip_params : %0xu\n",
		     ip_params);
	for (i = 0; i < num_pfs; i++) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			     " PF %u parameters:", i);
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			     "qdma_ip_params_1 : %0xu\n",
			     ifc_readl(qd->qcsr + 0x00200000U + (i * 16)));
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			     "qdma_ip_params_2 : %0xu\n",
			     ifc_readl(qd->qcsr + 0x00200000U + (i * 16) + 4));
	}
	return 0;
}

#ifndef IFC_MCDMA_EXTERNL_DESC
int ifc_qdma_dump_chnl_qcsr(struct ifc_qdma_device *dev,
			    struct ifc_qdma_channel *chnl,
			    int dir)
{
	struct ifc_qdma_queue que;

	if (dev == NULL)
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "device is NULL");
	else
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			     "device QCSR: 0x%0lx\n", (uint64_t)dev->qcsr);

	if (chnl == NULL) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "chnl is NULL");
		return -1;
	}
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "channel ID : %0u\n",
		     chnl->channel_id);
	if (dir == IFC_QDMA_DIRECTION_RX) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			     "RX Queue Config:\n");
		que = chnl->rx;
	} else {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			     "TX Queue Config:\n");
		que = chnl->tx;
	}
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "start addr\t\t\t: 0x%0lx\n",
		     ifc_readq(que.qcsr + QDMA_REGS_2_Q_START_ADDR_L));
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "Buf size\t\t\t: 0x%0lx\n",
		     ifc_readq(que.qcsr + QDMA_REGS_2_Q_SIZE));
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "tail ptr\t\t\t: 0x%lx\n",
			ifc_readq(que.qcsr + QDMA_REGS_2_Q_TAIL_POINTER));
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "head ptr\t\t\t: 0x%lx\n",
			ifc_readq(que.qcsr + QDMA_REGS_2_Q_HEAD_POINTER));
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "WB mem content\t\t\t: %u\n",
			que.consumed_head);
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "WB ADDR\t\t\t\t: 0x%lx\n",
			ifc_readq(que.qcsr + QDMA_REGS_2_Q_CONSUMED_HEAD_ADDR_L));
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "comp ptr\t\t\t: 0x%lx\n",
			ifc_readq(que.qcsr + QDMA_REGS_2_Q_COMPLETED_POINTER));
	return 0;
}
#else
int ifc_qdma_dump_chnl_qcsr(struct ifc_qdma_device *dev,
			    struct ifc_qdma_channel *chnl,
			    int dir)
{
	struct ifc_qdma_queue que;
	int i = 0;

	if (dev == NULL)
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "device is NULL");
	else
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			     "device QCSR: 0x%0lx\n", (uint64_t)dev->qcsr);

	if (chnl == NULL) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "chnl is NULL");
		return -1;
	}
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "ifc qdma config\n");
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "channel ID : %0xu\n",
		     chnl->channel_id);
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
		     "env_ctx.hugepage_sz = %lu\n",
		     env_ctx.hugepage_sz);
	for (i = 0; i < env_ctx.nr_hugepages; i++) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			     "virt = 0x%0lx", (uint64_t)env_ctx.hp[i].virt);
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			     "phyl = 0x%0lx", env_ctx.hp[i].phys);
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			     "phyh = 0x%0lx", env_ctx.hp[i].phys >> 32);
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			     "size = 0x%0lx", env_ctx.hp[i].size);
	}

	if (dir == IFC_QDMA_DIRECTION_RX) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			     "RX Queue Config:\n");
		que = chnl->rx;
	} else {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			     "TX Queue Config:\n");
		que = chnl->tx;
	}
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
		     "qcsr           : 0x%0lx\n", (int64_t)que.qcsr);
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
		     "qbuf           : 0x%0lx\n", (uint64_t)que.qbuf);
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
		     "qbuf_dma       : 0x%0x\n", (uint32_t)que.qbuf_dma);
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "start addr     : 0x%lx\n",
		     ifc_readq(que.qcsr + QDMA_REGS_2_Q_START_ADDR_L));
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "qlen           : 0x%lx\n",
			ifc_readq(que.qcsr + QDMA_REGS_2_Q_SIZE));
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "tail           : 0x%lx\n",
			ifc_readq(que.qcsr + QDMA_REGS_2_Q_TAIL_POINTER));
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "consumed head  : 0x%x\n",
			que.consumed_head);
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "head           : 0x%lx\n",
			ifc_readq(que.qcsr + QDMA_REGS_2_Q_HEAD_POINTER));
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "comp           : 0x%lx\n",
			ifc_readq(que.qcsr + QDMA_REGS_2_Q_COMPLETED_POINTER));
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "ctrl           : %u\n",
			ifc_readl(que.qcsr + QDMA_REGS_2_Q_CTRL));
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "reset          : %u\n",
			ifc_readl(que.qcsr + QDMA_REGS_2_Q_RESET));
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG, "Dump:\n");

	return 0;
}
#endif
#else
#define ifc_qdma_dump_config(a)			do {} while (0)
#define ifc_qdma_dump_chnl_qcsr(a, b, c)	do {} while (0)
#endif
 
#ifdef VERIFY_HOL
void ifc_qdma_channel_block(struct ifc_qdma_channel *qchnl)
{
	uint32_t val;

	if (unlikely(qchnl == NULL || qchnl->channel_id > NUM_MAX_CHANNEL)) {
		IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR,
			     "Invalid channel context\n");
		return;
	}

	val = ifc_readl(qchnl->qdev->qcsr + QDMA_REGS_2_RSVD_9);

	val |= 1 << (qchnl->channel_id + 28);
	val |= 1 << (qchnl->channel_id + 24);

	ifc_writel(qchnl->qdev->qcsr + QDMA_REGS_2_RSVD_9, val);
}

void ifc_qdma_channel_unblock(struct ifc_qdma_channel *qchnl)
{
	uint32_t val;

	if (unlikely(qchnl == NULL || qchnl->channel_id > NUM_MAX_CHANNEL)) {
		IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR,
			     "Invalid channel context\n");
		return;
	}

	val = ifc_readl(qchnl->qdev->qcsr + QDMA_REGS_2_RSVD_9);

	val &= ~(1 << (qchnl->channel_id + 28));
	val &= ~(1 << (qchnl->channel_id + 24));

	ifc_writel(qchnl->qdev->qcsr + QDMA_REGS_2_RSVD_9, val);
}
#endif
#ifdef IFC_DEBUG_STATS
void ifc_qdma_dump_stats(struct ifc_qdma_device *qdev)
{
	int i = 0;
	void *gcsr;

	if (unlikely(qdev == NULL || qdev->pdev->uio_id > UIO_MAX_DEVICE)) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Invalid Device Context\n");
		return;
	}

	gcsr = qdev->gcsr;

	/* H2D statistics */
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR, "Counters on RX and H2D path\n");
	for (i = 0; i < 14; i++) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR, "%s:%u\n",
			     ifc_qdma_hw_h2d_stats[i].desc,
			     ifc_readl(gcsr + ifc_qdma_hw_h2d_stats[i].off));
	}

	/* D2H statistics */
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR, "\nCounters on TX and D2H path: \n");
	for (i = 0; i < 16; i++) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR, "%s:%u\n",
			     ifc_qdma_hw_d2h_stats[i].desc,
			     ifc_readl(gcsr + ifc_qdma_hw_d2h_stats[i].off));
	}
}
#else
#define ifc_qdma_dump_stats(a)			do {} while (0)
#endif
