/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2020-21 Intel Corporation. All rights reserved.
 */
#ifndef _IFC_MCDMA_NET_H_
#define _IFC_MCDMA_NET_H_

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/irqreturn.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/skbuff.h>
#include <linux/jhash.h>
#include <regs/qdma_regs_2_registers.h>
#include <linux/moduleparam.h>

#ifdef ENABLE_DEBUGFS
#include "ifc_mcdma_debugfs.h"
#endif

#define VENDOR_ID		0x1172
#define DEVICE_ID		0x0000
#define DRV_VERSION		"0.0.0.1"
#define DRV_SUMMARY		"ifc_mcdma_netdev Intel(R) PCIe end point drv"
#define NETDEV_NAME		"ifc_mcdma%d"
#define IFC_MCDMA_DRIVER_NAME	"ifc_mcdma_netdev"

/* Number of channels per PF
 * Based on BS, need to configure value
 */
#define IFC_MCDMA_CHAN_PER_PF	512

/* Number of Channels */
#define IFC_MCDMA_CHANNEL_MAX	512

/* Max number of Desc per queue */
#define IFC_MCDMA_MAX_DESC	2048

/* Min number of Desc per queue */
#define IFC_MCDMA_MIN_DESC	128

/* Page size */
#define IFC_MCDMA_PAGE_SIZE	128

/* Hash table size
 */
#define IFC_MCDMA_H2D_MAP_TABLE_SIZE	IFC_MCDMA_CHANNEL_MAX

/* Queue Batch Delay */
#define IFC_MCDMA_Q_BATCH_DELAY	1

 /* Payload Limit */
#define IFC_MCDMA_BUF_LIMIT	(1UL << 20)

/* Number of pages of descriptors per queue */
#define IFC_MCDMA_DESC_PAGES	1

/**
 *Number of descriptors per page
 */
#define IFC_MCDMA_CONFIG_NUM_DESC_PER_PAGE             127

/* Descriptors submission limit */
#if IFC_MCDMA_DESC_PAGES > 1
#define IFC_MCDMA_QDEPTH   ((IFC_MCDMA_DESC_PAGES/2) * IFC_MCDMA_CONFIG_NUM_DESC_PER_PAGE)
#else
#define IFC_MCDMA_QDEPTH  ((IFC_MCDMA_CONFIG_NUM_DESC_PER_PAGE/2) + 1)
#endif

/* DMA direction: From Device to Host(D2H) & From Host to Device (H2D) */
enum mcdma_dir {
	IFC_MCDMA_DIR_RX,
	IFC_MCDMA_DIR_TX
};

/* Descriptor Index Limit */
#define IFC_NUM_DESC_INDEXES	65536

#define IFC_MCDMA_DEF_PAYLOAD	2048

/* externs */
extern struct ifc_mcdma_loopback_config ifc_mcdma_lb_config;

/* BAR Context */
struct ifc_mcdma_bar_info {
	const char *name;
	unsigned long size;
	void __iomem *iaddr;
};

/* PCIe Device Context */
struct ifc_pci_dev {
	struct pci_dev *pdev;
	struct ifc_mcdma_bar_info *info;
};

/* HW Descriptor Structure*/
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

struct ifc_mcdma_desc_sw {
	uint64_t phy_addr;
	dma_addr_t dma_addr;
	uint64_t hw_addr;
	struct sk_buff *skb;
	DEFINE_DMA_UNMAP_LEN(len);
	DEFINE_DMA_UNMAP_ADDR(dma);
};

#ifdef ENABLE_DEBUGFS
/* Queue Stats */
struct ifc_qdma_queue_stats {
	uint64_t tid_update;
	uint64_t processed;
	uint64_t processed_total;
	uint64_t processed_skip;
	uint32_t submitted_didx;
	uint32_t last_intr_cons_head;
	uint32_t last_intr_reg;
	uint32_t tid_skip;
};
#endif

struct ifc_queue_alloc {
	u32 queue_id;
	u32 hash;
	struct rb_node node;
	struct list_head list;
};

struct ifc_core_info {
	bool queues_mapped[IFC_MCDMA_CHANNEL_MAX];
	bool queues_allocated[IFC_MCDMA_CHANNEL_MAX];
	struct ifc_queue_alloc *rb_qcache;
	struct rb_root root;
	struct list_head lru_list;
};

/* Queue Context */
struct ifc_mcdma_queue {
	void *qcsr;
	enum mcdma_dir dir;

	void *ring_virt_addr;		/* Ring Virtual Address */
	dma_addr_t ring_dma_addr;	/* Ring DMA mapped Address */
	uint64_t ring_hw_addr;		/* Ring Address passed to HW */

	uint32_t qlen;			/* Descriptor ring length */

	int consumed_head;		/* WB live copy */
	int cur_con_head;		/* WB offline copy */
	int head;			/* Ring Head */
	int tail;			/* Ring Tail */
	int processed_tail;

	int head_didx;
	int tail_didx;

	uint16_t didx;			/* descriptor index */
	uint16_t ite_count;		/* descriptor index */
	uint16_t num_desc_bits;		/* bits required for didx */
	uint32_t wbe:1;			/* write back */
	uint32_t ire:1;			/* interrupt enable */
	uint32_t sof_rcvd:1;
	uint32_t num_desc_pages:11;
	uint32_t rsvd:18;

	int irq_index;			/* index to interrupt context */
	struct ifc_mcdma_desc_sw *ctx;
	struct ifc_mcdma_channel *chnl;
	struct napi_struct napi;
#ifdef ENABLE_DEBUGFS
	struct ifc_qdma_queue_stats stats;
#endif
};

/* Channel Context */
struct ifc_mcdma_channel {
	uint32_t channel_id;
	struct ifc_mcdma_queue rx;
	struct ifc_mcdma_queue tx;
	struct ifc_mcdma_dev_ctx *dev_ctx;
};

/* PCIe MSIX Context */
struct ifc_msix_info {
	u32 nvectors;
	struct msix_entry *table;
};

/* Device Context */
struct ifc_mcdma_dev_ctx {
	u32 msg_enable			____cacheline_aligned;
	struct ifc_pci_dev pctx;
	struct ifc_msix_info msix_info;
	struct net_device *netdev;
	void *qcsr;
	void *gcsr;
	struct dentry *rootdir;
	char bdf[32];

	struct mutex chnl_lock;
	struct ifc_mcdma_channel *channel_context[IFC_MCDMA_CHANNEL_MAX];
	int queue_core_mapping[IFC_MCDMA_CHANNEL_MAX];
	bool channel_bitmap[IFC_MCDMA_CHANNEL_MAX];
	unsigned long num_cores;
	struct ifc_core_info *cores;

	u32 h2d_mapping[IFC_MCDMA_H2D_MAP_TABLE_SIZE];
	int channel_core_mapping[IFC_MCDMA_CHANNEL_MAX];
	struct list_head list;
	u32 num_of_pages;

	/* Debugfs inputs */
	u32 start_chnl;
	u32 end_chnl;
	u32 core;
	u32 max_rx_cnt;
	u32 max_tx_cnt;
	u32 rx_chnl_cnt;
	u32 tx_chnl_cnt;
	u32 chnl_cnt;
    u32 tot_chnl_avl;
	bool is_pf;
};

struct ifc_mcdma_chnl_map {
	bool valid;
	uint32_t chnl;
};

struct ifc_mcdma_loopback_config {
	struct dentry *d;
	struct ifc_mcdma_dev_ctx *dev_ctx;
	uint32_t src_chnl;
	uint32_t dst_chnl;
	struct ifc_mcdma_chnl_map mapping[2048];
};

int ifc_mcdma_netdev_setup(struct net_device *dev,u32 tot_chn_aval);
int ifc_mcdma_ip_reset(void *dev_qcsr);
void ifc_mcdma_update_lb_config(struct ifc_mcdma_loopback_config *lbconfig);
void ifc_core_free_all(struct ifc_mcdma_dev_ctx *dev_ctx);
int ifc_refresh_core_info(struct ifc_mcdma_dev_ctx *dev_ctx, int q, int c);
int ifc_mcdma_chnl_init_all(struct ifc_mcdma_dev_ctx *dev_ctx);
void ifc_mcdma_chnl_free_all(struct ifc_mcdma_dev_ctx *dev_ctx);

#ifdef ENABLE_DEBUGFS
int ifc_mcdma_debugfs_setup(struct net_device *dev);
void ifc_mcdma_debugfs_remove(struct net_device *dev);
#endif
void ifc_mcdma_add_napi(struct net_device *netdev);
void ifc_mcdma_napi_enable_all(struct net_device *netdev);

void ifc_mcdma_napi_disable_all(struct net_device *netdev);
void ifc_mcdma_del_napi(struct net_device *netdev);
#endif	/* _IFC_MCDMA_NET_H_ */
