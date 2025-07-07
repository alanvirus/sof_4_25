/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#ifndef _RTE_PMD_MCDMA_H_
#define _RTE_PMD_MCDMA_H_

#include <rte_vect.h>
#include <immintrin.h>

/**
 * Payload Limit
 */
#define IFC_MCDMA_BUF_LIMIT             (1UL << 20)

/**
 * McDMA PIO BAR
 */
#define IFC_MCDMA_PIO_BAR		2

/**
 * McDMA BAS BAR
 */
#define IFC_MCDMA_BAS_BAR		4

/**
 * McDMA Queue Configuration BAR
 */
#define IFC_MCDMA_CONFIG_BAR		0

/**
 * Number of Max UIO Supported devices
 */
#define UIO_MAX_DEVICE			8

/**
 * DMA direction from device
 */
#define IFC_QDMA_DIRECTION_RX   0
/**
 * DMA direction to device
 */
#define IFC_QDMA_DIRECTION_TX   1

/**
 * Bi-directional DMA - first TX and then RX
 */
#define IFC_QDMA_DIRECTION_BOTH 2

/**
 * SOF - Flag can be passed to driver
 */
#define IFC_QDMA_SOF_MASK		0x1ULL

/**
 * EOF - Flag can be passed to driver
 */
#define IFC_QDMA_EOF_MASK		0x2ULL

/**
 * Flag can be passed to driver
 */
#define IFC_QDMA_Q_BATCH_DELAY		1

/*
 * ED selection
 * Disable for 1MB Support
 */
#undef ED_VER0

/**
 * Default data-buffer memory size in each descriptor is 1MB (2^20)
 * In case to reduce it, change below macro accordingly.
 *
 * BUFFER_MEMORY_BIT_POSITION : exponet of 2, to represent req size
 */
#define BUFFER_MEMORY_BIT_POSITION	20

#define IFC_HUGEPAGE_REQ_SZ		(1 << BUFFER_MEMORY_BIT_POSITION)

#define LOOPBACK

/**
 * Enable/Disable debug statistics
 */
#undef IFC_DEBUG_STATS

/**
 * Enable/Disable 64Byte descriptor
 */
#undef IFC_64B_DESC_FETCH

/**
 * Reuse Tx buffer Memory for PERQ App
 */
#define IFC_BUF_REUSE

/**
 * By default allow drop count
 */
#undef AVOID_TX_DROP_COUNT

/**
 * Enable/Disable UIO Support
 */
#define UIO_SUPPORT

/**
 * Enable/Disable IOMMU Mode
 */
#undef NO_IOMMU_MODE

/**
 * Enable/Disable DCA Debug statistics
 */
#undef DEBUG_DCA

/**
 * Enable/Disable to check FIFO space
 */
#undef TID_FIFO_ENABLED

/**
 * Enable/Disable to check HW register to know FIFO space
 */
#undef HW_FIFO_ENABLED

/**
 * Enable/Disable to track descriptor fetch is moved
 */
#undef TRACK_DF_HEAD

#undef IFC_MCDMA_DIDF

/**
 * number of descriptors per page
 */
#define IFC_MCDMA_CONFIG_NUM_DESC_PER_PAGE             127

/*
 * Number of pages of descriptors per queue
 */
#ifdef IFC_MCDMA_DIDF
#define IFC_CONFIG_QDMA_NUM_DESC_PAGES_PER_QUEUE        1
#else
#define IFC_CONFIG_QDMA_NUM_DESC_PAGES_PER_QUEUE        8
#endif

/*
 * Max number of descriptors we can put on queue at prefill * time
 */
#if IFC_CONFIG_QDMA_NUM_DESC_PAGES_PER_QUEUE > 1
#define QDEPTH ((IFC_CONFIG_QDMA_NUM_DESC_PAGES_PER_QUEUE/2) * IFC_MCDMA_CONFIG_NUM_DESC_PER_PAGE)
#else
#define QDEPTH ((IFC_MCDMA_CONFIG_NUM_DESC_PER_PAGE/2) + 1)
#endif

/**
 * Enable/Disable TID Latency statistics
 */
#undef TID_LATENCY_STATS

/**
 * Enable/Disable 8 Byte META DATA
 */
#undef IFC_QDMA_META_DATA

/**
 * Enable/Disable IP reset functionality
 */
#undef IFC_QDMA_IP_RESET

/**
 * Enable/disable PIO 256
 */
#undef IFC_PIO_256

/**
 * Enable/disable PIO 128
 */
#undef IFC_PIO_128

/**
 * Write back value in descriptor during loading time
 */
#define IFC_QDMA_RX_DESC_CMPL_PROC	0

/*
 * Max transferrable unit
 * Special value of payload lengh
 */
#define IFC_MTU_LEN 1518

/*
 * Adds error handling while acquiring channels
 */
#undef IFC_MCDMA_ERR_CHANNEL

/* PF count starts from 1 */
#define IFC_MCDMA_CUR_PF         1
/* VF count starts from 1. Zero implies PF was used instead of VF */
#define IFC_MCDMA_CUR_VF         0

/*
 * PCIe_SLOT
 * 0 - x16
 * 1 - x8
 * 2 - x4
 */
#define PCIe_SLOT	0

/* Error code to indicate HW Fetch Head is not moved */
#define DF_HEAD_NOT_MOVED	0xFFFF

#define IFC_MCDMA_ERR_POLL_TIME                1000000

enum ifc_mcdma_queue_param {
	IFC_QDMA_CHNL_QUEUE_CMPL,
	IFC_QDMA_CHNL_WEIGHTAGE,
};

/**
 * QDMA Descriptor completion reporting procedure enum
 *
 * While initialiing the queue specific parameters, driver uses
 * this parameter to set the descriptor completion reporting mechanism
 * in FPGA.
 * This values would be stored in queue context and based on this
 * parameter, driver reads the DMA completion status
 *
 */
enum ifc_config_mcdma_cmpl_proc {
	/**
	 * write back mechanism
	 * hardware writes the completion status at the
	 * configured host address.
	 * configuration done at the time of queue initialization
	 */
	CONFIG_QDMA_QUEUE_WB,
	/**
	 * driver responsible to read the status from
	 * PCIe registers available in register spec
	 */
	CONFIG_QDMA_QUEUE_REG,
	/**
	 * during queue initialization, driver registers the eventfds
	 * and notify to PCIe end point module.
	 * When interrupt received, kernel notify the event to user space
	 */
	CONFIG_QDMA_QUEUE_MSIX,
};

/**
 * Set default descriptor completion procedure
 */
#define IFC_CONFIG_QDMA_COMPL_PROC CONFIG_QDMA_QUEUE_WB

/* statistics */
struct ifc_mcdma_stats {
	uint64_t ifc_mcdma_bad_ds;
	uint64_t ifc_mcdma_zero_fifo_len;
	uint64_t ifc_mcdma_no_queue_space;
	uint64_t ifc_mcdma_tx_load_fail;
	uint64_t ifc_mcdma_rx_load_fail;
	uint64_t ifc_mcdma_inv_len;
};

/**
 * QDMA device
 *
 * This structure contains QDMA context.
 * From application context, QDMA device would be differentiated by
 * BDF. User needs to pass the BDF to above specified API to get
 * the context.
 *
 * Once usage of device is completed, application must release the
 * context.
 */
struct ifc_mcdma_device;

/**
 * ifc_mcdma_pio_read64 - Read the value from BAR2 address
 * @portid: port ID
 * @addr: Address to read
 *
 * @returns value at address in BAR2
 */
uint64_t ifc_mcdma_pio_read64(uint16_t portid, uint64_t addr);
int ifc_mcdma_pio_read128(uint16_t portid, uint64_t offset, uint64_t *buf, int bar_num);
int ifc_mcdma_pio_read256(uint16_t portid, uint64_t offset, uint64_t *buf, int bar_num);
int ifc_mcdma_pio_read512(uint16_t portid, uint64_t offset, uint64_t *buf, int bar_num);

/**
 * ifc_mcdma_pio_write64 - Read the value from BAR2 address
 * @portid: port id
 * @addr: Address to write
 * @val: Value to write
 *
 * @Returns: void
 */
void ifc_mcdma_pio_write64(uint16_t portid, uint64_t addr,
			   uint64_t val);
int ifc_mcdma_pio_write128(uint16_t portid, uint64_t offset, uint64_t *val, int bar_num);
int ifc_mcdma_pio_write256(uint16_t portid, uint64_t offset, uint64_t *val, int bar_num);
int ifc_mcdma_pio_write512(uint16_t portid, uint64_t offset, uint64_t *val, int bar_num);

uint64_t ifc_mcdma_read64(uint16_t portno, uint64_t addr, int bar_num);
void ifc_mcdma_write64(uint16_t port, uint64_t addr,
			  uint64_t value, int bar_num);
/**
 * ifc_mcdma_poll_init - initilizes the polling instance
 * to be used in case MSIX interrupts are enabled
 * @portid: Port ID to get the ethernet device
 *
 * Application should use the returned context to add the
 *	queues to get monitored
 *
 * Returns, pointer to polling context on success
 *	    NULL, in case of failes
 */
void *ifc_mcdma_poll_init(uint16_t portid);

/**
 * ifc_mcdma_poll_add - adds event descriptor to polling context
 * @port: Port ID to get the ethernet device
 * @qid: Queue ID to monitor for interrupts
 * @dir: Direction to monitor for interrupts
 * @ctx: polling context returned by ifc_mcdma_poll_init
 *
 * @returns 0, on success. negative otherwise.
 */
int ifc_mcdma_poll_add(uint16_t port,
		      int qid,
		      int dir,
		      void *ctx);

/**
 * ifc_mcdma_poll_add - wait for interrupt for specied number
 * of micro seconds in blocking mode
 * @port: Port ID to get the ethernet device
 * @ctx: polling context returned by ifc_mcdma_poll_init
 * @timeout: blocking time to wait for interrupts
 * @qid: Pointer where interrupted queue
 *	  would be returned
 * @dir: Pointer where interrupted direction
 *	 would be returned
 *
 * @returns 0, on success. negative otherwise
 */
int ifc_mcdma_poll_wait(uint16_t port,
		       void *ctx,
		       int timeout,
		       int *qid,
		       int *dir);

int mcdma_ip_reset(uint16_t portid);

/**
 * umsix_irq_handler - adds event descriptor to polling context
 * @port: port ID on which interrupt occured
 * @qid: queue ID on which interrupt occured
 * @dir: Direction of queue
 * @errinfo: Error reported by hardware
 *
 * @returns 0, on success. negative otherwise.
 */
typedef int (*umsix_irq_handler)(int port_id, int qid,
                                 int dir, int *errinfo);

int ifc_mcdma_usmix_poll_add(uint16_t port, int qid, int dir, void *ctx);

int ifc_mcdma_release_all_channels(uint16_t port_id);

int ifc_mcdma_eventfd_poll_add(uint16_t port, int dir, void *ctx);

int ifc_mcdma_poll_wait_for_intr(void *ctx,
		       int timeout,
		       int *dir);

/**
 * ifc_mcdma_get_drop_count - get data drop count
 * @port - port ID
 * @qid: queue ID on which interrupt occured
 * @dir: Direction of queue
 * 
 * @returns data drop count.
 */
int ifc_mcdma_get_drop_count(int port, int qid, int dir);

#ifdef IFC_DEBUG_STATS
/**
 * ifc_mcdma_dump_stats - Dump QDMA statistics
 * @port - port ID
 */

void ifc_mcdma_dump_stats(int port);
void ifc_mcdma_print_stats(int port, int qid, int dir);

#else
#define ifc_mcdma_dump_stats(a)                  do {} while (0)
#define ifc_mcdma_print_stats(a, b, c)                  do {} while (0)
#endif

void ifc_mcdma_wb_event_error(uint16_t port, int qid, int dir);

#ifdef IFC_QDMA_DYN_CHAN
#ifdef IFC_QDMA_TELEMETRY
void ifc_mcdma_check_errors(void *arg);
int ifc_mcdma_vf_wb_error(uint16_t port_id, int qid, int dir);
int ifc_mcdma_pf_logger_dump(void *dev_hndl);
int ifc_mcdma_device_err_dca_pf(void *dev);
int ifc_mcdma_host_err_dca_pf(void *dev);
#endif
#endif

#endif
