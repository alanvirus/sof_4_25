/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#ifndef _MCDMA_H_
#define _MCDMA_H_

#include <stdbool.h>
#ifdef DPDK_21_11_RC2
#include <ethdev_driver.h>
#include <eal_interrupts.h>
#endif
#include <rte_dev.h>
#include <rte_ethdev.h>
#include <rte_spinlock.h>
#include <rte_log.h>
#include <rte_bus_pci.h>
#include <rte_byteorder.h>
#include <rte_memzone.h>
#include <linux/pci.h>
#include <math.h>
#include <sys/stat.h>
#include <mcdma_access.h>
#include <rte_pmd_mcdma.h>

#ifdef IFC_QDMA_DYN_CHAN
#ifdef IFC_QDMA_TELEMETRY
#define IFC_MCDMA_SCP_WAIT_COUNT       		100000 * 10
#define IFC_MCDMA_ERROR_SPACE 			0x200300
#define IFC_MCDMA_DF_LOGGER_SPACE 		0x200400
#define IFC_MCDMA_H2D_LOGGER 			0x200600
#define IFC_MCDMA_D2H_LOGGER 			0x200800
#define IFC_MCDMA_SH_LOGGER 			0x200A00
#define ERROR_DESC_FETCH_MASK 			0x80000000
#define ERROR_DATA_FETCH_MASK 			0x40000000
#define MCDMA_ERROR_HANDLER_SCRATCH_D2H 	0x020030C
#define MCDMA_ERROR_HANDLER_SCRATCH_H2D        	0x200308
#endif
#endif

#define PMD_DRV_LOG(level, fmt, args...) \
	RTE_LOG(level, PMD, "%s(): " fmt "\n", __func__, ## args)

#define BIT(x)  (1u << (x))
#define BIT_ULL(x)      (1ull << (x))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/*Increased the Qreset timeout to 300ms part of DCA */
#if (defined IFC_INTF_AVST_256_CHANNEL) || (defined IFC_QDMA_DYN_CHAN)
#define IFC_MCDMA_RESET_WAIT_COUNT       300000
#else
#define IFC_MCDMA_RESET_WAIT_COUNT       2048
#endif
#define IFC_MCDMA_RING_SIZE              4096
#define IRQ_FD_BITS             20
#define MAX_IRQ_FD              (1 << 20)
#define IFC_MCDMA_MB             0x100000
#define IFC_CNT_HEAD_MOVE	(100000)
#define IFC_NUM_DESC_INDEXES	65536

#define IFC_DEF_BURST_SIZE	16
#define IFC_QDMA_CHANNEL_MAX_INTR    4

#define IFC_QDMA_CHANNEL_INTR_VECTOR_LEN 16

/* H2D Descriptor completion */
#define IFC_QDMA_CHANNEL_H2D_DMA_INDEX    0

/* H2D Event completion */
#define IFC_QDMA_CHANNEL_H2D_EVENT_INDEX  1

/* D2H Descriptor completion */
#define IFC_QDMA_CHANNEL_D2H_DMA_INDEX    2

/* D2H Event completion */
#define IFC_QDMA_CHANNEL_D2H_EVENT_INDEX  3

#define MAX_EVENTS 10
#define MAX_POLL_CTX		2048

#define DMA_BRAM_SIZE		(1048576)
#define MCDMA_MAX_MTU	0x1FFFFF
#define MCDMA_MIN_MTU	0x40

#define IFC_MAX_RXTX_INTR_VEC_ID        2048
#define IFC_QDMA_IRQ_SET_BUF_LEN (sizeof(struct vfio_irq_set) + \
                              sizeof(int) * (IFC_MAX_RXTX_INTR_VEC_ID + 1))
#define IRQ_SET_BUF_LEN  (sizeof(struct vfio_irq_set) + sizeof(int))

/*
 *  * Some macros for bit fields management
 *   */
#define IFC_QDMA_MASK(width)           ((uint32_t)((1UL << (width)) - 1))
#define IFC_QDMA_SMASK(shift, width)   ((uint32_t)(IFC_QDMA_MASK(width) << (shift)))
#define IFC_QDMA_RD_FIELD(val, shift, width) \
        (((val) & IFC_QDMA_SMASK((shift), (width)) ) >> (shift))
#define IFC_QDMA_WR_FIELD(val, shift, width) \
        (((val) & IFC_QDMA_MASK(width)) << (shift))

#ifdef IFC_QDMA_INTF_ST
#define IFC_MCDMA_QUEUES_NUM_MAX	2048
#else
#define IFC_MCDMA_QUEUES_NUM_MAX	512
#endif

#define TX_MZONE_NAME "TxHwRn"
#define RX_MZONE_NAME "RxHwRn"

struct ifc_poll_ctx {
	int epollfd;
	int valid;
};

struct chnl_context {
        uint16_t  valid;
        uint16_t  ph_chno;
        void *ctx;
};

struct ifc_mcdma_device {
	struct ifc_pci_device *pdev;
	struct rte_eth_dev *dev;
	int uio_fd;
	char uio_id;

	/* Device capabilities  */
	struct mcdma_dev_cap ipcap;

	/*  PCIe info*/
	uint16_t num_chan;
	uint16_t pf;
	uint16_t vf;

	uint8_t is_pf;
	uint8_t is_vf:1;
	uint8_t is_master:1;

	/*version info */
	uint32_t rtl_version;

	uint32_t completion_method;

	void *bar_addr[PCI_BAR_MAX];

	void *qcsr;
	uint32_t nchannel;
	uint32_t tx_bitmap[IFC_MCDMA_QUEUES_NUM_MAX/ 32]; /* alloc mask */
	uint32_t rx_bitmap[IFC_MCDMA_QUEUES_NUM_MAX/ 32]; /* alloc mask */
	struct chnl_context channel_context[IFC_MCDMA_QUEUES_NUM_MAX];
	void *que_context[IFC_MCDMA_QUEUES_NUM_MAX* 2];
	struct ifc_poll_ctx poll_ctx[MAX_POLL_CTX];
	pthread_mutex_t lock;
	pthread_mutex_t tid_lock;
};

struct ifc_mcdma_desc_sw {
	void *ctx_data;
};

struct ifc_mcdma_io_req_sw {
	int index;
};

struct ifc_mcdma_queue_stats {
	uint64_t req_cnt;
	uint64_t pkt_cnt;
	uint64_t byte_cnt;
	uint64_t tid_update;
	uint64_t processed;
	uint64_t processed_total;
	uint64_t processed_skip;
	uint64_t failed_attempts;
	uint32_t submitted_didx;
	uint32_t last_intr_cons_head;
	uint32_t last_intr_reg;
	uint32_t tid_skip;
	uint32_t tid_drops;
	uint32_t cto_drops;
};

struct ifc_mcdma_queue {
	void *qcsr;
	void *qbuf;
	uint64_t qbuf_dma;
	uint32_t qlen;          /* num descriptors */
	uint32_t tail;          /* live copy */
	uint32_t fifo_len;    /* last submitted value */
	uint32_t consumed_head; /* live copy */
	uint32_t head;          /* shadow copy */
	uint32_t processed_head;
	uint32_t last_head; /* head when processing tail */
	uint32_t tid_updates; /* tid update count to check for HOL */
	uint32_t processed_tail;
	uint32_t qid;
	uint16_t weight;
	uint16_t didx; /* descriptor index */
	uint16_t ite_count; /* descriptor index */
	uint16_t num_desc_bits; /* Number of bits requierd for didx */
	uint32_t wbe:1; /* write back */
	uint32_t qie:1; /* interrupt enable */
	uint32_t sof_rcvd:1; /* interrupt enable */
	uint32_t num_desc_pages:11;
	uint32_t ph_chno:12;
	uint32_t rsvd:7;
	fd_set rfds;
	uint32_t dma_irqfd;
	uint32_t event_irqfd;
	uint32_t dir;
	struct ifc_mcdma_desc_sw *ctx;
	struct rte_mempool *mb_pool;
	struct rte_eth_dev *ethdev;
	uint32_t base;
	uint32_t limit;
	uint32_t addr;
	struct ifc_mcdma_queue_stats stats;
	uint64_t prev_prepared;
	uint64_t desc_cnt;
	umsix_irq_handler irq_handler;

#ifdef IFC_64B_DESC_FETCH
	uint32_t count_desc; /* counts total descriptor to update*/
#endif

#ifdef TID_LATENCY_STATS
	struct timespec last;
	struct timespec cur_time;
#endif
	uint64_t data_drops_cnts;
};

struct ifc_mcdma_desc {
	/* word 1,2 */
	uint64_t src;
	/* word 3,4 */
	uint64_t dest;
	/* word 5 */
	uint32_t len:20;
	uint32_t rsvd:12;
	/* word 6 */
	uint32_t didx:16;
	uint32_t msix_en:1;
	uint32_t wb_en:1;
	uint32_t rsvd2:14;
	/* word 7 */
	uint32_t rx_pyld_cnt:20;
	uint32_t rsvd3:10;
#ifdef IFC_QDMA_INTF_ST
	uint32_t sof:1;
	uint32_t eof:1;
#else
	uint32_t rsvd4:2;
#endif
	/* word 8 */
	uint32_t rsvd5:28;
	uint32_t pad_len:2;
	uint32_t desc_invalid:1;
	uint32_t link:1;
};

struct fd_info_s {
	int qid;
	int dir;
	int umsix;
	int eventfd;
};

struct ifc_mcdma_hw_stats_s {
	uint32_t off;
	char desc[128];
};

uint16_t ifc_mcdma_recv_pkts(void *rx_queue, struct rte_mbuf **rx_pkts,
			     uint16_t nb_pkts);
uint16_t ifc_mcdma_xmit_pkts(void *tx_queue, struct rte_mbuf **tx_pkts,
			     uint16_t nb_pkts);
int ifc_mcdma_dev_mtu_set(struct rte_eth_dev *dev, uint32_t mtu);
int ifc_mcdma_ip_reset(struct ifc_mcdma_device *dev);
int ifc_mcdma_get_hw_version(struct rte_eth_dev *dev);
void ifc_mcdma_dev_ops_init(struct rte_eth_dev *dev);
int ifc_mcdma_reset_queue(struct ifc_mcdma_queue *q);
int ifc_mcdma_wait_for_comp(struct ifc_mcdma_queue *q);
int ifc_mcdma_wait_for_queue_comp(struct ifc_mcdma_queue *q);
uint64_t avmm_addr_manager(struct ifc_mcdma_queue *q, uint64_t *addr,
			   unsigned long payload);
int ifc_mcdma_queue_init_msix(int qid, int dir, uint16_t port_id);
void ifc_mcdma_dev_rx_queue_release(void *rqueue);
void ifc_mcdma_dev_tx_queue_release(void *tqueue);
uint16_t ifc_mcdma_xmit_desc(struct ifc_mcdma_queue *txq,
			     struct rte_mbuf **tx_pkts,
			     uint16_t nb_pkts, int dir);
uint16_t ifc_mcdma_recv_desc(struct ifc_mcdma_queue *rxq,
			     struct rte_mbuf **rx_pkts,
			     uint16_t nb_pkts);
int ifc_mcdma_dev_tx_queue_start(struct rte_eth_dev *dev, uint16_t qid);
int ifc_mcdma_dev_rx_queue_start(struct rte_eth_dev *dev, uint16_t qid);
int ifc_mcdma_dev_rx_queue_stop(struct rte_eth_dev *dev,
				uint16_t qid);
int ifc_mcdma_dev_tx_queue_stop(struct rte_eth_dev *dev,
				uint16_t qid);
int
ifc_mcdma_dev_rx_queue_setup(struct rte_eth_dev *dev, uint16_t rx_queue_id,
			     uint16_t nb_rx_desc,
			     unsigned int socket_id __rte_unused,
			     const struct rte_eth_rxconf *rx_conf __rte_unused,
			     struct rte_mempool *mb_pool);
int
ifc_mcdma_dev_tx_queue_setup(struct rte_eth_dev *dev, uint16_t tx_queue_id,
			     uint16_t nb_tx_desc,
			     unsigned int socket_id __rte_unused,
			     const struct rte_eth_txconf *tx_conf __rte_unused);
int time_dif(struct timespec *a, struct timespec *b, struct timespec *result);
int ifc_mcdma_queue_umsix_init(int qid, int dir, uint16_t port_id);
int mcdma_open(const char *file_name, int mode);
#ifdef IFC_QDMA_DYN_CHAN
#ifdef IFC_QDMA_TELEMETRY
int ifc_mcdma_poll_scratch_reg(struct rte_eth_dev *dev, uint32_t offset, int exp_val);
#endif
#endif
int ifc_mcdma_vfio_enable_msix(struct rte_pci_device *pci_dev,
					uint32_t num_intr);
int ifc_mcdma_check_ch_sup(struct ifc_mcdma_device *dev, uint16_t ch);
int ifc_get_aligned_payload(int payload);
#endif /* _IFC_PERFQ_H_ */
