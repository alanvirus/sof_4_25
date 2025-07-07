// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _IFC_ENV_H_
#define _IFC_ENV_H_

#include <ifc_libmqdma.h>
#include <ifc_env_config.h>

#define UIO_PATH_FMT	"/sys/class/uio/uio%u"
#define UIO_DEVICE_PATH_FMT	"/sys/class/uio/uio%u/device"
#define NUM_SEGMENTS		NUM_HUGE_PAGES
#define IFC_CHUNKS_FOR_DESC		(IFC_DESC_MEM/env_ctx.buf_size)
#define IFC_CHUNKS_FOR_CTX		(IFC_CTX_MEM/env_ctx.buf_size)
#define IFC_QDMA_IOMMU_BASE_IOVA	(1<<20)

/* Barriers */
#define ifc_wmb()      __asm volatile("sfence" ::: "memory")
#define ifc_mb()       __asm volatile("mfence" ::: "memory")

/* externs */
extern const struct ifc_pci_id uio_pci_id;

enum {
	IFC_CONFIG_REGION,
	IFC_GCSR_REGION,
	IFC_PIO_REGION,
};


struct ifc_pci_resource {
	__u64 len;
	void *map;
};

struct ifc_pci_device {
	int uio_fd;
	char pci_slot_name[256];
	struct ifc_pci_resource r[PCI_BAR_MAX];
	struct ifc_pci_resource region[MAX_REGIONS];
	char uio_id;
	uint8_t num_bars;
};

struct ifc_pci_id {
	__u16 vend;
	__u16 devid;
	__u16 subved;
	__u16 subdev;
};

struct ifc_hugepage_seg {
	void *virt;
	uint64_t iova; /*'IOVA is valid only for vfio interface */
	uint64_t phys;
	uint64_t size;
};

struct ifc_seg_info {
	int seg_id;
	uint64_t phys;
	uint64_t length;
	void *virt;
	int refcnt;
};

struct ifc_env_ctx {
	int nr_hugepages;
	struct ifc_hugepage_seg hp[NUM_HUGE_PAGES];
	struct ifc_seg_info seg_tab[NUM_SEGMENTS];
	void *hugepage;
	uint64_t hugepage_sz;
	void *alloc_ctx;
	void *ctx_base;
	void *last_page;
	int nr_device;
	int maxfd;
	int seg_tab_len;
	pthread_mutex_t env_lock;
	struct ifc_pci_device uio_devices[UIO_MAX_DEVICE];
	uint32_t buf_size;
};

struct alloc_ele {
	uint8_t len;
	uint8_t dontuse[7];
	char data[0];
};

static inline uint32_t ifc_readl(void *ioaddr)
{
	return *(volatile uint32_t *)ioaddr;
}

static inline void ifc_writel(void *ioaddr, uint32_t val)
{
	*(volatile uint32_t *)ioaddr = val;
}

static inline uint64_t ifc_readq(void *ioaddr)
{
	return *(volatile uint64_t *)ioaddr;
}

static inline void ifc_writeq(void *ioaddr, uint64_t val)
{
	*(volatile uint64_t *)ioaddr = val;
}

void *ifc_dma_malloc(size_t x);

void ifc_dma_free(void *p);

void *ifc_desc_ring_malloc(size_t len);

void ifc_desc_ring_free(void *ptr);

struct ifc_qdma_request *ifc_request_malloc(size_t len);

void ifc_request_free(void *req);

uint64_t mem_virt2phys(const void *virtaddr);

int ifc_env_init(const char *bdf);

void ifc_pci_unmap_resource(struct ifc_pci_device *pdev);

int unlink_hugepage(int i);

int probe_and_map_hugepage(void);

int
ifc_uio_match_device(const char *uio_path, const struct ifc_pci_id *pci_id);

/* Return first configured BAR */
int ifc_mcdma_get_first_bar(struct ifc_pci_device *pdev);

#endif /* _IFC_ENV_H_ */
