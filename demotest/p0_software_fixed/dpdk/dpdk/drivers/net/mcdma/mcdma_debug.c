/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#include "mcdma_access.h"
#include "mcdma_platform.h"
#include <rte_malloc.h>
#include <rte_spinlock.h>
#include "mcdma.h"
#include "qdma_regs_2_registers.h"
#include "rte_pmd_mcdma.h"
#include "mcdma_debug.h"

#ifdef IFC_DEBUG_STATS
struct ifc_mcdma_hw_stats_s ifc_mcdma_hw_h2d_stats[16] = {
	{ 0x150, "MRd tlp received by RX_RQ" },
	{ 0x154, "completions tlp sent from RX_RQ" },
	{ 0x158, "RX TLPs received from HIP" },
	{ 0x15c, "H2D MRd tlp sent from H2D" },
	{ 0x168, "hardware instruction ack count. 8 bits per port H2D[Port4:Port3:Port2:Port1]" },
	{ 0x16c, "hardware instruction ack count. 8 bits per port D2H[Port4:Port3:Port2:Port1]" },
	{ 0x170, "SOF sent on port 1" },
	{ 0x174, "SOF sent on port 2" },
	{ 0x178, "SOF sent on port 3" },
	{ 0x17c, "SOF sent on port 4" },
	{ 0x180, "EOF sent on port 1" },
	{ 0x184, "EOF sent on port 2" },
	{ 0x188, "EOF sent on port 3" },
	{ 0x18c, "EOF sent on port 4" },
};

struct ifc_mcdma_hw_stats_s ifc_mcdma_hw_d2h_stats[16] = {
	{ 0x190, "d2h tlp at tx schd i/p" },
	{ 0x194, "cmpl tlp at tx schd i/p"},
	{ 0x198, "msi/wb/desc_update tlp at tx schd i/p" },
	{ 0x19c, "h2d tlp at tx schd i/p" },
	{ 0x1a0, "EOF received on port 1 AVST" },
	{ 0x1a4, "EOF received on port 2 AVST" },
	{ 0x1a8, "EOF received on port 3 AVST" },
	{ 0x1ac, "EOF received on port 4 AVST" },
	{ 0x1b0, "descriptors processed  successfully on port 1" },
	{ 0x1b4, "descriptors processed  successfully on port 2" },
	{ 0x1b8, "descriptors processed  successfully on port 3" },
	{ 0x1bc, "descriptors processed  successfully on port 4" },
	{ 0x1c0, "D2H received Descriptors on port 1" },
	{ 0x1c4, "D2H received descriptors on port 2" },
	{ 0x1c8, "D2H received descriptors on port 3" },
	{ 0x1cc, "D2H received descriptors on port 4" },
};

int ifc_mcdma_dump_config(struct ifc_mcdma_device *qd)
{
	uint32_t ip_params;
	uint32_t num_pfs;
	uint32_t i;

	PMD_DRV_LOG(ERR,
		    "ifc mcdma config\n");
	PMD_DRV_LOG(ERR,
		    "ctrl : %0xu\n", ifc_readl(qd->qcsr + 0x00200000U));
	PMD_DRV_LOG(ERR,
		    "wbid : %0xu\n", ifc_readl(qd->qcsr + 0x00200008U));
	PMD_DRV_LOG(ERR,
		    "version : %0xu\n", ifc_readl(qd->qcsr + 0x00200070U));
	ip_params = ifc_readl(qd->qcsr + 0x00200070U);
	num_pfs = ip_params & 0x000000F0U;
	PMD_DRV_LOG(ERR,  "ip_params : %0xu\n",
		    ip_params);
	for (i = 0; i < num_pfs; i++) {
		PMD_DRV_LOG(ERR,
			    " PF %u parameters:", i);
		PMD_DRV_LOG(ERR,
			    "mcdma_ip_params_1 : %0xu\n",
			     ifc_readl(qd->qcsr + 0x00200000U + (i * 16)));
		PMD_DRV_LOG(ERR,
			    "mcdma_ip_params_2 : %0xu\n",
			     ifc_readl(qd->qcsr + 0x00200000U + (i * 16) + 4));
	}
	return 0;
}

int ifc_mcdma_dump_chnl_qcsr(int portno, int qid, int dir)
{
	struct ifc_mcdma_queue *que;
	struct rte_eth_dev *dev = &rte_eth_devices[portno];

	if (dir == IFC_QDMA_DIRECTION_TX)
		que = (struct ifc_mcdma_queue *)dev->data->tx_queues[qid];
	else
		que = (struct ifc_mcdma_queue *)dev->data->rx_queues[qid];

	PMD_DRV_LOG(ERR,  "ifc mcdma config");

	if (dir == IFC_QDMA_DIRECTION_RX) {
		PMD_DRV_LOG(ERR,
			    "RX Queue Config:");
	} else {
		PMD_DRV_LOG(ERR,
			    "TX Queue Config:");
	}
	PMD_DRV_LOG(ERR,
		    "qcsr           : 0x%0lx", (int64_t)que->qcsr);
	PMD_DRV_LOG(ERR,
		    "qbuf           : 0x%0lx", (uint64_t)que->qbuf);
	PMD_DRV_LOG(ERR,
		    "qbuf_dma       : 0x%0x", (uint32_t)que->qbuf_dma);
	PMD_DRV_LOG(ERR,  "start addr     : 0x%0x",
		    ifc_readl(que->qcsr + QDMA_REGS_2_Q_START_ADDR_L));
	PMD_DRV_LOG(ERR,  "startaddrh     : %0x",
		    ifc_readl(que->qcsr + QDMA_REGS_2_Q_START_ADDR_H));
	PMD_DRV_LOG(ERR,  "log_len        : %0xu",
		    ifc_readl(que->qcsr + QDMA_REGS_2_Q_SIZE));
	PMD_DRV_LOG(ERR,  "qlen           : %u",
		    ifc_readl(que->qcsr + QDMA_REGS_2_Q_SIZE));
	PMD_DRV_LOG(ERR,  "tail           : %u",
		    ifc_readl(que->qcsr + QDMA_REGS_2_Q_TAIL_POINTER));
	PMD_DRV_LOG(ERR,  "consumed head  : %u",
		    que->consumed_head);
	PMD_DRV_LOG(ERR,  "head           : %u",
		    ifc_readl(que->qcsr + QDMA_REGS_2_Q_HEAD_POINTER));
	PMD_DRV_LOG(ERR,  "comp           : %u",
		    ifc_readl(que->qcsr + QDMA_REGS_2_Q_COMPLETED_POINTER));
	PMD_DRV_LOG(ERR,  "ctrl           : %u",
		    ifc_readl(que->qcsr + QDMA_REGS_2_Q_CTRL));
	PMD_DRV_LOG(ERR,  "reset          : %u",
		    ifc_readl(que->qcsr + QDMA_REGS_2_Q_RESET));
	return 0;
}

void ifc_mcdma_dump_stats(int portno)
{
	void *gcsr;

	struct rte_eth_dev *dev = &rte_eth_devices[portno];
	struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;
	int i = 0;

	gcsr = mcdma_dev->bar_addr[IFC_MCDMA_CONFIG_BAR] + 0x200000;

	/* H2D statistics */
	PMD_DRV_LOG(ERR,  "Counters on RX and H2D path: ");
	for (i = 0; i < 14; i++) {
		PMD_DRV_LOG(ERR,  "%s:%u",
			    ifc_mcdma_hw_h2d_stats[i].desc,
			     ifc_readl(gcsr + ifc_mcdma_hw_h2d_stats[i].off));
	}

	/* D2H statistics */
	PMD_DRV_LOG(ERR,  "Counters on TX and D2H path: ");
	for (i = 0; i < 16; i++) {
		PMD_DRV_LOG(ERR,  "%s:%u",
			    ifc_mcdma_hw_d2h_stats[i].desc,
			     ifc_readl(gcsr + ifc_mcdma_hw_d2h_stats[i].off));
	}
}

void ifc_mcdma_print_stats(int portno, int qid, int dir)
{
	struct ifc_mcdma_queue *q;
	struct rte_eth_dev *dev = &rte_eth_devices[portno];

	if (dir == IFC_QDMA_DIRECTION_TX)
		q = (struct ifc_mcdma_queue *)dev->data->tx_queues[qid];
	else
		q = (struct ifc_mcdma_queue *)dev->data->rx_queues[qid];
	PMD_DRV_LOG(ERR, "SW Stats: qid: %u dir: %u, bdf: %s,",
		    qid, q->dir, dev->data->name);

	PMD_DRV_LOG(ERR, "TID updates: %lu, processed: %lu,"
		    " phead: %u, ptail: %u, ",
		    q->stats.tid_update, q->stats.processed,
		    q->processed_head, q->processed_tail);

	PMD_DRV_LOG(ERR, "live head: %u, shadow head: %u, proc tail: %u, submitted didx: %u,"
		    " didx: %u, head: %u,",
		    (volatile uint32_t)q->consumed_head,
		    q->head, q->tail,
		    q->stats.submitted_didx, q->didx,
		    ifc_readl(q->qcsr + QDMA_REGS_2_Q_HEAD_POINTER));

	PMD_DRV_LOG(ERR, "compreg: %u, last intr head: %u, last intr reg: %u,"
                    " failed_attempts: %lu mtu:%u tid drops:%u Current LSB 16bit drops: %u",
                    ifc_readl(q->qcsr + QDMA_REGS_2_Q_COMPLETED_POINTER),
                    q->stats.last_intr_cons_head,
                    q->stats.last_intr_reg, q->stats.failed_attempts,
                    ifc_readl(q->qcsr + QDMA_REGS_2_Q_DEBUG_STATUS_4),
                    q->stats.tid_drops, 0XFFFF & ifc_readl(q->qcsr + QDMA_REGS_2_Q_DATA_DRP_ERR_CTR));

        if (dir == IFC_QDMA_DIRECTION_RX) {
                PMD_DRV_LOG(ERR, "drops:%u Cumulative drops: %lu\n\n",
                    ifc_readl(q->qcsr + QDMA_REGS_2_Q_DEBUG_STATUS_3),
                    q->data_drops_cnts);

        }
}
#else
#define ifc_mcdma_dump_config(a)			do {} while (0)
#define ifc_mcdma_dump_chnl_qcsr(b, c)	do {} while (0)
#endif
