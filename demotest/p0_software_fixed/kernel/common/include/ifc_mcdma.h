/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020-21, Intel Corporation. */

#ifndef _IFC_MCDMA_H_
#define _IFC_MCDMA_H_

/**
 * x8 or x16 PCIe Data Lanes 
 */
#define IFC_MCDMA_X16
#undef IFC_MCDMA_X8

/**
 * Payload Limit
 */
#define IFC_MCDMA_BUF_LIMIT 		(1UL << 20)

/**
 * Multi-Port & Single Port Settings
 */
#ifdef IFC_QDMA_ST_MULTI_PORT

#ifdef IFC_QDMA_INTF_ST
#define IFC_MCDMA_CHANNEL_MAX		4
#else		/* AVMM */
#define IFC_MCDMA_CHANNEL_MAX		8
#define IFC_AVMM_BUF_LIMIT 		(1UL << 15)
#endif		/* IFC_QDMA_INTF_ST */

#else		/* Single Port */

#ifdef IFC_QDMA_INTF_ST
#define IFC_MCDMA_CHANNEL_MAX		256
#else		/* AVMM */
#define IFC_MCDMA_CHANNEL_MAX		512
#ifdef IFC_MCDMA_X16
#define IFC_AVMM_BUF_LIMIT		(1UL << 17)
#else		/* IFC_MCDMA_X8 */
#define IFC_AVMM_BUF_LIMIT		(1UL << 16)
#endif		/* IFC_MCDMA_X16 */
#endif		/* IFC_QDMA_INTF_ST */

#endif		/* IFC_QDMA_ST_MULTI_PORT */

/**
 * Character Device Name Prefix
 */
#define IFC_CHARDEV_NAME_PREFIX	"mcdma_device_"

/**
 * Number of PFs supported
 */
#define IFC_MCDMA_DEV_MAX	4

/*
 * IFC_MCDMA_DESC_PAGES: Number of pages of descriptors per queue
 * IFC_MCDMA_QDEPTH: Descriptors submission limit
 */
#ifdef RESTRICTED_BATCH_SIZE
#define IFC_MCDMA_DESC_PAGES	8
#define IFC_MCDMA_QDEPTH ((IFC_MCDMA_DESC_PAGES/2) * 127)
#else
#define IFC_MCDMA_DESC_PAGES	4
#define IFC_MCDMA_QDEPTH ((IFC_MCDMA_DESC_PAGES) * 127)
#endif

/**
 * DMA direction: From Device to Host
 */
#define IFC_MCDMA_DIR_RX	0

/**
 * DMA direction: From Host to Device 
 */
#define IFC_MCDMA_DIR_TX	1

/**
 * Driver Memory Test Pattern
 */
#define DRIVER_PATTERN	0xfafafafa

/**
 * Application Memory Test Pattern
 */
#define APP_PATTERN	0xdadadada

/**
 * SOF Mask 
 */
#define IFC_MCDMA_SOF_MASK               0x1

/**
 * EOF Mask 
 */
#define IFC_MCDMA_EOF_MASK               0x2

/**
 * Queue Batch Delay
 */
#define IFC_MCDMA_Q_BATCH_DELAY		1

/**
 * MCDMA Descriptor completion reporting procedure enum
 *
 * While initialiing the queue specific parameters, driver uses
 * this parameter to set the descriptor completion reporting mechanism
 * in FPGA.
 * This values would be stored in queue context and based on this
 * parameter, driver reads the DMA completion status
 */
enum ifc_config_mcdma_cmpl_proc {
	/*
	 * write back mechanism
	 * hardware writes the completion status at the
	 * configured host address.
	 * configuration done at the time of queue initilization
	 */
	CONFIG_MCDMA_QUEUE_WB,
	 /*
	  * driver responsible to read the status from
	  * PCIe registers available in register spec
	  */
	CONFIG_MCDMA_QUEUE_REG,
	/*
	 * during queue initilization, driver registers the eventfds
	 * and notify to PCIe end point module.
	 * When interrupt received, kernel notify the event to user space
	 */
        CONFIG_MCDMA_QUEUE_MSIX,
};

/*
 * Set default descriptor completion procedure
 */
#define IFC_CONFIG_MCDMA_COMPL_PROC CONFIG_MCDMA_QUEUE_WB

/**
 * Memory allocation strategy
 */
#define IFC_MCDMA_HUGEPAGE

/**
 * MCDMA structure used by the application to pass control messages to
 * the Kernel Driver.
 */
struct ctrl_message {
	uint32_t fefd_tx:1;		/* Contains valid eventfd for Tx */
	uint32_t fefd_rx:1;		/* Contains valid eventfd for Rx */
	uint32_t reserved:30;		/* Reserved for future use */
	int efd_tx;			/* eventfd for Tx */
	int efd_rx;			/* eventfd for Rx */
	int tx_fsize;			/* Descriptors per Tx file size, if 0 defaults to batch size */
	int rx_fsize;			/* Descriptors per Rx file size, if 0 defaults to batch size */
	int tx_payload;                 /* Payload value of Tx Descriptors */
	int rx_payload;                 /* Payload value of Rx Descriptors */
};
#endif	/* _IFC_MCDMA_H_ */
