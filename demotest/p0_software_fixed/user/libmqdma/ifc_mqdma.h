// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _IFC_PERFQ_H_
#define _IFC_PERFQ_H_

#include <mcdma_ip_params.h>
#include <ifc_libmqdma.h>
#include <linux/vfio.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/sysmacros.h>
#include <sys/epoll.h>
#include <ifc_vfio.h>

#define TID_ERROR 1
#define CTO_EVENT 2
#define DESC_FETCH_EVENT 4
#define DATA_FETCH_EVENT 8

#define IFC_QDMA_CHANNEL_MAX    2048
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
#define MAX_POLL_CTX	2048

#define IFC_NUM_CHUNKS_PER_HUGE_PAGE    ((1<<30) / (env_ctx.buf_size))
#define IFC_MAX_CHUNKS_PER_HUGE_PAGE    ((1<<30) / (1<<6))
#define IFC_HUGEPAGE_REQ_NR		(NUM_HUGE_PAGES * IFC_MAX_CHUNKS_PER_HUGE_PAGE)

#define IFC_GCSR_OFFSET(off) (off - 0x00200000U)


/*
 * Some macros for bit fields management
 */
#define IFC_QDMA_MASK(width)           ((uint32_t)((1UL << (width)) - 1))
#define IFC_QDMA_SMASK(shift, width)   ((uint32_t)(IFC_QDMA_MASK(width) << (shift)))
#define IFC_QDMA_RD_FIELD(val, shift, width) \
        (((val) & IFC_QDMA_SMASK((shift), (width)) ) >> (shift))
#define IFC_QDMA_WR_FIELD(val, shift, width) \
        (((val) & IFC_QDMA_MASK(width)) << (shift))

extern uint64_t hugepage_req_alloc_mask[IFC_HUGEPAGE_REQ_NR / 64];
extern uint32_t ifc_qdma_log_level;
extern uint32_t ifc_qdma_log_area;
extern struct ifc_env_ctx env_ctx;

/* Logging */
#define IFC_QDMA_LOG(area, level, ...)                                               \
        do {                                                                    \
                if (((1  << area) & ifc_qdma_log_area) && (level >= ifc_qdma_log_level))  \
                        printf(__VA_ARGS__);                                    \
        } while(0)

struct ifc_poll_ctx {
	int epollfd;
	int valid;
};

struct chnl_context {
	uint16_t  valid;
	uint16_t  ph_chno;
	void *ctx;
};

struct ifc_qdma_device {
	struct ifc_pci_device *pdev;
	void *qcsr;
	void *gcsr;
	uint32_t nchannel;
	uint32_t active_ch_cnt;
	uint32_t tx_bitmap[IFC_QDMA_CHANNEL_MAX / 32]; /* alloc mask */
	uint32_t rx_bitmap[IFC_QDMA_CHANNEL_MAX / 32]; /* alloc mask */
	struct chnl_context channel_context[IFC_QDMA_CHANNEL_MAX];
	void *que_context[IFC_QDMA_CHANNEL_MAX * 2];
	struct ifc_poll_ctx poll_ctx[MAX_POLL_CTX];
	char pci_slot_name[256];
	pthread_mutex_t lock;
	pthread_mutex_t tid_lock;
	uint16_t pf;
	uint16_t vf;
	uint8_t is_pf;
	uint8_t spare1;
	uint16_t spare2;
	uint32_t desc_per_page;
	int completion_mode;
	uint8_t pio_bar;
};

struct ifc_qdma_desc_sw {
	void *ctx_data;
};

struct ifc_qdma_io_req_sw {
	int index;
};
#ifdef IFC_DEBUG_STATS
struct ifc_qdma_queue_stats {
	uint64_t tid_update;
	uint64_t processed;
	uint64_t processed_total;
	uint64_t processed_skip;
	uint32_t submitted_didx;
	uint32_t last_intr_cons_head;
	uint32_t last_intr_reg;
	uint32_t tid_skip;
	uint32_t cto_count;
};
#endif
struct ifc_qdma_queue {
	void *qcsr;
	void *qbuf;
	uint64_t qbuf_dma;
	uint32_t qlen;          /* num descriptors */
	uint32_t tail;          /* live copy */
	uint32_t fifo_len;      /* last submitted value */
	uint32_t consumed_head; /* live copy */
	uint32_t head;          /* shadow copy */
	uint32_t processed_head;/* head when polled last time */
	uint32_t last_head;     /* head when processing tail */
	uint32_t tid_updates;   /* tid update count to check for HOL*/
	uint32_t processed_tail;/* last updated tail ID */
	uint16_t weight;	/* budget allocated */
	uint16_t didx;          /* descriptor index */
	uint16_t ite_count;     /* descriptor index */
	uint16_t num_desc_bits; /* Number of bits requierd for didx */
	uint32_t wbe:1;         /* write back */
	uint32_t qie:1;         /* interrupt enable */
	uint32_t sof_rcvd:1;    /* interrupt enable */
	uint32_t num_desc_pages:11;
	uint32_t rsvd:19;
	fd_set rfds;
	uint32_t dma_irqfd;
	uint32_t event_irqfd;
	uint32_t dir;
	struct ifc_qdma_desc_sw *ctx;
	struct ifc_qdma_channel *qchnl;
	umsix_irq_handler irq_handler;
	void *irqdata;

#ifdef IFC_64B_DESC_FETCH
	uint32_t count_desc;          /* counts total descriptor to update*/
#endif 

#ifdef IFC_DEBUG_STATS
	struct ifc_qdma_queue_stats stats;
#endif
#ifdef TID_LATENCY_STATS
	struct timespec last;
	struct timespec cur_time;
#endif
#ifdef WB_CHK_TID_UPDATE
	int batch_done;
	uint32_t first_didx;
	uint32_t expecting_wb;
#endif
	uint32_t tid_drops;
	uint64_t data_drops_cnts;
};

struct ifc_qdma_channel {
	uint32_t channel_id;
	struct ifc_qdma_queue tx;
	struct ifc_qdma_queue rx;
	struct ifc_qdma_device *qdev;
};

struct fd_info_s {
	struct ifc_qdma_channel *chnl;
	int dir;
	int umsix;
	int eventfd;
};

struct ifc_qdma_hw_stats_s {
	uint32_t off;
	char desc[128];
};

int ifc_qdma_queue_check_intr(struct ifc_qdma_queue *q);

int ifc_qdma_queue_init_msix(struct ifc_qdma_channel *chnl, int dir);

#endif /* _IFC_PERFQ_H_ */
