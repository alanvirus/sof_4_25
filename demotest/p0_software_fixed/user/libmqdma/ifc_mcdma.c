// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <fnmatch.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/sysmacros.h>
#include <sys/file.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <linux/pci_regs.h>
#include <ifc_reglib_osdep.h>
#include <ifc_mqdma.h>
#include <ifc_mcdma_config.h>
#include <ifc_env.h>
#include <ifc_vfio.h>
#include <ifc_qdma_utils.h>
#include <ifc_libmqdma.h>
#include <ifc_mcdma_debug.h>
#include <time.h>
#include <mcdma_ip_params.h>
#include <regs/dynamic_channel_params.h>
#include <immintrin.h>
#include <string.h>
#ifndef IFC_MCDMA_EXTERNL_DESC
#include <regs/qdma_regs_2_registers.h>
#else
#include <regs/qdma_ext_regs_2_registers.h>
#endif

#ifdef IFC_DEBUG_STATS
const char *intel_mcdma_dbg_message = "WARNING: this is a DEBUG driver";
#define __INTEL__USE_DBG_CHK() asm("" :: "m"(intel_mcdma_dbg_message))
#else /* __INTEL__DEBUG_CHK */
const char *intel_mcdma_release_message = "Intel PRODUCTION driver";
#define __INTEL__USE_DBG_CHK() asm("" :: "m"(intel_mcdma_release_message))
#endif

uint64_t hugepage_req_alloc_mask[IFC_HUGEPAGE_REQ_NR / 64];
uint32_t ifc_qdma_log_level = IFC_QDMA_DEFAULT_LOG_LEVEL;
uint32_t ifc_qdma_log_area = IFC_QDMA_DEFAULT_LOG_AREA;
static int ifc_qdma_get_poll_ctx(struct ifc_qdma_device *qdev);
#ifdef IFC_QDMA_DYN_CHAN
static int qdma_acquire_lock(struct ifc_qdma_device *qdev, int num);
static int qdma_release_lock(struct ifc_qdma_device *qdev);
static int qdma_get_l2p_pf_base(uint32_t pf);
static int qdma_get_l2p_vf_base(uint32_t pf, uint32_t vf);
#ifdef DEBUG_DCA
static int qdma_reset_tables(struct ifc_qdma_device *qdev);
#endif
#endif

extern struct ifc_intr_handle ifc_intr_handler;
struct ifc_env_ctx env_ctx;
struct ifc_qdma_device *qdev_g;
struct ifc_qdma_stats ifc_qdma_stats_g;
#ifndef UIO_SUPPORT
static int num_channels = 0;
#endif

#ifdef IFC_QDMA_INTF_ST
static int ifc_check_payload(int payload)
{
	int new_payload = 0;
/*This macro refers to data width of AVST interface between HIP and QDMA IP.
 *For 1024 data width required 128 bytes payload alignment.
 */
#if     (IFC_DATA_WIDTH == 1024)
	int remainder = payload % 128;
#else
	int remainder = payload % 64;
#endif
	if (remainder == 0)
		return payload;

#if     (IFC_DATA_WIDTH == 1024)
	/* Rounding up to the nearest multiple of a 128 */
	new_payload = payload + 128 - remainder;
#else
	/* Rounding up to the nearest multiple of a 64 */
        new_payload = payload + 64 - remainder;        
#endif
	return new_payload;
}
#endif
static uint32_t ifc_get_descq_len(struct ifc_qdma_queue *q)
{
#ifndef IFC_MCDMA_EXTERNL_DESC
	return ceil(log2(q->qlen));
#else
	return q->qlen;
#endif
}

static uint32_t ifc_get_channel_base(struct ifc_qdma_channel *c, int dir)
{
	uint32_t chnl_base = 0;
#ifndef IFC_MCDMA_EXTERNL_DESC
	if (dir == IFC_QDMA_DIRECTION_RX)
		chnl_base = c->channel_id * 256;
	else
		chnl_base = (512 << 10) + (c->channel_id * 256);
#else
		chnl_base = 0x10000 + (c->channel_id * 256) + (dir * 128);
#endif
	return chnl_base;
}

#ifdef IFC_QDMA_IP_RESET
static int ifc_qdma_ip_reset(void)
{
	void *base = env_ctx.uio_devices[0].region[IFC_GCSR_REGION].map;

	/*  validate BAR0 base address */
	if (base == 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			"BAR Register mapping not yet done\n");
		return -1;
	}
	if (IFC_GCSR_OFFSET(QDMA_REGS_2_SOFT_RESET) > env_ctx.uio_devices[0].region[IFC_GCSR_REGION].len) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			"Length of BAR Register is less\n");
		return -1;
	}

	/* IP reset */
	ifc_writel(base + IFC_GCSR_OFFSET(QDMA_REGS_2_SOFT_RESET), 0x01);

	/*Giving time for the FPGA reset to complete*/
	usleep(100000);
	return 0;
}
#endif

#ifdef IFC_QDMA_DYN_CHAN
static int ifc_qdma_get_free_logical_chno(struct ifc_qdma_device *qdev)
{
	int i;

	for (i = 0; i <IFC_QDMA_CHANNEL_MAX; i++) {
		if (qdev->channel_context[i].valid == 0)
			return i;
	}
	return -1;
}

static int ifc_qdma_get_lch(struct ifc_qdma_device *qdev, int pch)
{
	int i;

	if (pch >= IFC_QDMA_CHANNEL_MAX) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			"Invalid physical channel number\n");
		return -1;
	}

	for (i = 0; i <IFC_QDMA_CHANNEL_MAX; i++) {
		if (qdev->channel_context[i].valid == 1 &&
		    qdev->channel_context[i].ph_chno == pch)
			return i;
	}
	return -1;
}

static int ifc_qdma_get_device_info(struct ifc_qdma_device *qdev)
{
	void *base = qdev->pdev->r[0].map;
	void *gcsr = qdev->gcsr;
	uint32_t reg;

	/*  validate BAR0 base address */
	if (base == 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			"BAR Register mapping not yet done\n");
		return -1;
	}

	/* Read Device ID */
	reg = ifc_readl(gcsr + IFC_GCSR_OFFSET(QDMA_PING_REG));

	/* check if it is PF or VF based on VFACTIVE bit*/
	if (reg & QDMA_PING_REGS_VFACTIVE_SHIFT_MASK)
		qdev->is_pf = 0;
	else
		qdev->is_pf = 1;

	/* Retrieve  PF number */
	qdev->pf = IFC_QDMA_RD_FIELD(reg, QDMA_PING_REGS_PF_SHIFT, QDMA_PING_REGS_PF_SHIFT_WIDTH);

	/* retrieve VF number */
	if (qdev->is_pf == 0)
		qdev->vf = IFC_QDMA_RD_FIELD(reg, QDMA_PING_REGS_VF_SHIFT, QDMA_PING_REGS_VF_SHIFT_WIDTH);

	return 0;
}
#endif

#if 0
struct ifc_qdma_desc {
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
#endif

#ifdef GCSR_ENABLED
static void ifc_qdma_set_avoid_hol(struct ifc_qdma_device *qdev)
{
	/* avoid hol */
#ifdef LOOPBACK
	ifc_writel(qdev->gcsr + IFC_GCSR_OFFSET(QDMA_REGS_2_RSVD_9), 0);
#else
	ifc_writel(qdev->gcsr + IFC_GCSR_OFFSET(QDMA_REGS_2_RSVD_9), 0xff);
#endif
}
#endif

static void ifc_qdma_hw_init(struct ifc_qdma_device *qdev)
{
	int i = 0;

	/* extract num of channels from PARAM_2 */
	qdev->nchannel = NUM_MAX_CHANNEL;

	for (i = 0; i < (NUM_MAX_CHANNEL / 32); i++) {
		qdev->tx_bitmap[i] = 0;
		qdev->rx_bitmap[i] = 0;
	}

	for (i = 0; i <IFC_QDMA_CHANNEL_MAX; i++) {
		qdev->channel_context[i].valid = 0;
		qdev->channel_context[i].ctx = NULL;
		qdev->channel_context[i].ph_chno = 0;
	}

	for (i = 0; i < (IFC_QDMA_CHANNEL_MAX * 2); i++)
		qdev->que_context[i] = NULL;

}

void ifc_smp_barrier()
{
	__asm volatile("mfence" ::: "memory");
}

int get_first_available_port(void)
{
	if (!env_ctx.nr_device) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    "No device bounded to driver\n");
		return -1;
	}
	printf("use first device as default: %s\n", 
	   env_ctx.uio_devices[0].pci_slot_name);
	return env_ctx.uio_devices[0].uio_id;
}

int
ifc_qdma_device_get(int port, struct ifc_qdma_device **qdev,
		    uint32_t page_desc, int completion_mode)
{
	struct ifc_pci_device *pdev;
	struct ifc_qdma_device *qd;
#ifdef UIO_SUPPORT
	char     dev_file_name[64] = {0};
#endif
	int i = 0;

	if (!env_ctx.nr_device) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    "No device bounded to driver\n");
		return -1;
	}

	for (i = 0; i < env_ctx.nr_device; i++) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			    "i:%u pci_slot_name:%s\n",
			    i, env_ctx.uio_devices[i].pci_slot_name);
		if (env_ctx.uio_devices[i].uio_id == port) {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
		    		"device matched idx:%u\n", i);
			break;
		}
	}
	if (i == env_ctx.nr_device) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    "invalid port:%u\n", port);
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    "valid bdf are:\n");
		for (i = 0; i < env_ctx.nr_device; i++)
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    "%s\n", env_ctx.uio_devices[i].pci_slot_name);
		return -1;
	}

	pdev = &env_ctx.uio_devices[i];
	qd = (struct ifc_qdma_device *)malloc(sizeof(*qd));
	if (qd == NULL)
		return -1;
	qd->pdev = pdev;
	qd->qcsr = pdev->r[0].map;
	qd->gcsr = pdev->region[IFC_GCSR_REGION].map;
	qd->desc_per_page = page_desc;
	qd->active_ch_cnt = 0;
	qd->completion_mode = completion_mode;
	if (pdev->num_bars > 1)
		qd->pio_bar = PIO_BAR;
	else {
		i = ifc_mcdma_get_first_bar(pdev);
		if (i < 0) {
			printf("No BAR registers are enabled\n");
			free(qd);
			return -1;
		}
		qd->pio_bar = i;
	}

	memcpy(qd->pci_slot_name, pdev->pci_slot_name, 256);

#ifdef UIO_SUPPORT
	/* open device files */
	snprintf(dev_file_name, sizeof(dev_file_name), "/dev/uio%u",i);

	env_ctx.uio_devices[i].uio_fd = ifc_qdma_open(dev_file_name, O_RDWR | O_EXCL);
	if (env_ctx.uio_devices[i].uio_fd < 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
				"Failed while opening device file dev:%s errno:%d\n",
				dev_file_name, errno);
	}
#endif

	if (pthread_mutex_init(&qd->lock, NULL) != 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
				"mutex init failed \n");
		free(qd);
		return -1;
	}
	if (pthread_mutex_init(&qd->tid_lock, NULL) != 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "mutex init failed\n");
		if (pthread_mutex_destroy(&qd->lock) != 0) {
			IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR,
			     "lock destroy failed\n");
		}
		free(qd);
		return -1;
	}
	ifc_qdma_dump_config(qd);

	*qdev = qd;

	/* initialize and enable DMA device */
	ifc_qdma_hw_init(qd);

#ifdef IFC_QDMA_DYN_CHAN
	/* get PF and VF deatils */
	if(ifc_qdma_get_device_info(qd) < 0)
		printf("Unable to get device info\n");
#ifdef DEBUG_DCA
	qdma_reset_tables(qd);
#endif
#endif

	return 0;
}

static void ifc_mcdma_dev_stop(struct ifc_qdma_device  *qdev __attribute__((unused)))
{
#ifdef IFC_QDMA_DYN_CHAN
#ifdef UIO_SUPPORT
        int err, command = -1;

        command = MSIX_DISABLE_INTR;
	command |= (qdev->active_ch_cnt << 20);
        err = pwrite(qdev->pdev->uio_fd,
			&command, 4, 0);
        if (err == 0)
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Not able to enable interrupts\n");
#endif
#endif
}


void ifc_qdma_device_put(struct ifc_qdma_device *qdev)
{
	int i = 0;

	if (unlikely(qdev == NULL || qdev->pdev->uio_id > UIO_MAX_DEVICE)) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Invalid Device Context\n");
		return;
	}

	if (pthread_mutex_destroy(&qdev->lock) != 0) {
		IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR,
			     "mutex destroy failed\n");
	}
	for (i = 0; i < MAX_POLL_CTX; i++) {
		if (qdev->poll_ctx[i].valid == 1)
			close(qdev->poll_ctx[i].epollfd);
	}
	ifc_mcdma_dev_stop(qdev);
	free(qdev);
}

int ifc_mcdma_dev_start(struct ifc_qdma_device  *qdev __attribute__((unused)))
{
#ifdef IFC_QDMA_DYN_CHAN
#ifdef UIO_SUPPORT
        int err, command = -1;
        command = MSIX_IRQFD_MASK;
	command |= (qdev->active_ch_cnt << 20);
        err = pwrite(qdev->pdev->uio_fd,
			&command, 4, 0);
        if (err == 0)
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Not able to enable interrupts\n");
#endif
#endif
        return 0;
}

#ifndef UIO_SUPPORT
int  set_num_channels(int channel_count) 
{
	num_channels = channel_count;
        return 0;
}
#endif
/**
 * ifc_app_start - start application and prepare environment
 */
int ifc_app_start(const char* bdf, uint32_t buf)
{
	int ret = 0;

	if (buf == 0) {
		printf("Failed to initialize buffer: buffer size 0\n");
		return -1;
	}
	env_ctx.buf_size = buf;
#ifdef UIO_SUPPORT
	/* init UIO environment */
	ret = ifc_env_init(bdf);
	if (ret) {
		printf("Failed to initialize UIO environment\n");
		return ret;
	}

#else
	/* init VFIO environment */
	ret = ifc_vfio_init(bdf, num_channels);
	if (ret) {
		printf("Failed to initialize VFIO environment\n");
		return ret;
	}
#endif

	/* Perform IP Reset */
#ifdef IFC_QDMA_IP_RESET
	ret = ifc_qdma_ip_reset();
	if (ret) {
		printf("Failed to reset IP\n");
		return ret;
	}
#endif
	return ret;
}

/**
 * ifc_mcdma_port_by_name - Return the port number corresponing
 * BDF
 */
int ifc_mcdma_port_by_name(const char* bdf)
{
	int i = 0;

	for (i = 0; i < env_ctx.nr_device; i++) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			    "i:%u pci_slot_name:%s\n",
			    i, env_ctx.uio_devices[i].pci_slot_name);
		if ((strncmp(env_ctx.uio_devices[i].pci_slot_name, bdf, 256) == 0)) {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
		    		"device matched idx:%u\n", i);
			return i;
		}
	}
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
		    "invalid bdf:%s\n", bdf);
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
		    "valid bdf are:\n");
	for (i = 0; i < env_ctx.nr_device; i++)
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
		    "%s\n", env_ctx.uio_devices[i].pci_slot_name);
	return -1;
}

/**
 * ifc_app_stop - stop application and release resources
 */
void ifc_app_stop(void)
{
	int i;

	for (i = 0; i < env_ctx.nr_device; i++) {
#ifdef UIO_SUPPORT
		ifc_pci_unmap_resource(&env_ctx.uio_devices[i]);
#else
		ifc_vfio_release_device(env_ctx.uio_devices[i].pci_slot_name,
					&env_ctx.uio_devices[i]);
#endif
	}

	if (env_ctx.hugepage == NULL)
		return;

	for (i = 0; i < NUM_HUGE_PAGES; i++) {
#ifdef IFC_32BIT_SUPPORT
		if (madvise(env_ctx.hp[i].virt, env_ctx.hp[i].size, MADV_REMOVE) == -1) {
			perror("madvise");
			exit(EXIT_FAILURE);
		}
#endif
		munmap(env_ctx.hp[i].virt, env_ctx.hp[i].size);

		unlink_hugepage(i);
	}
}

/**
 * ifc_num_channels_get - get number of channels supported
 */
int ifc_num_channels_get(struct ifc_qdma_device *qdev)
{
	if (unlikely(qdev == NULL || qdev->pdev->uio_id > UIO_MAX_DEVICE)) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Invalid Device Context\n");
		return 0;
	}
	return qdev->nchannel;
}

/**
 * ifc_qdma_descq_init - internal, allocate and initialize descr ring
 */
static int ifc_qdma_descq_init(struct ifc_qdma_queue *q)
{
#ifndef IFC_MCDMA_EXTERNL_DESC
	QDMA_REGS_2_Q_CTRL_t q_ctrl = { { 0 } };
#endif
	struct ifc_qdma_desc *desc;
	struct ifc_qdma_desc_sw *sw;
	uint64_t cons_head;
	uint32_t desc_per_page;
	uint32_t descq_ring_len;
	uint32_t i;
	uint32_t len;
	uint32_t ch_id;
	uint32_t dir;

	if (((q->num_desc_pages) & (q->num_desc_pages - 1)) || (q->num_desc_pages > 64)) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Please configure number of pages"
			     "  as power of 2 and <= 16 \n");
		return -1;
	}

	desc_per_page = q->qchnl->qdev->desc_per_page;
	q->qlen = desc_per_page * q->num_desc_pages;
	len = q->qlen * sizeof(*desc);
	ch_id = q->qchnl->channel_id;
	dir = q->dir;

	/* Allocate memory for descriptors
 	 * if already allocated, reuse it
	 */
	if (q->qchnl->qdev->que_context[IFC_QBUF_CTX_INDEX(ch_id, dir)]) {
		q->qbuf =
		   q->qchnl->qdev->que_context[IFC_QBUF_CTX_INDEX(ch_id, dir)];
	}
	else {
		q->qbuf = ifc_desc_ring_malloc(len);
		q->qchnl->qdev->que_context[IFC_QBUF_CTX_INDEX(ch_id, dir)] =
			q->qbuf;
	}

	if (q->qbuf == NULL) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			     "qbuf allocation failed\n");
		return -1;
	}

	descq_ring_len = ifc_get_descq_len(q);
	q->num_desc_bits = descq_ring_len;
	
	if(q->ctx == NULL){
		q->ctx = (struct ifc_qdma_desc_sw *)malloc(sizeof(*sw) * q->qlen);
		if (q->ctx == NULL) {
			ifc_desc_ring_free(q->qbuf);
			return -1;
		}
	}

	memset(q->qbuf, 0, sizeof(*q));
	q->qbuf_dma = mem_virt2phys(q->qbuf);
	if (q->qbuf_dma == 0)
		return -1;
	desc = (struct ifc_qdma_desc *)q->qbuf;

	for (i = 0; i < q->qlen; i++)
		desc[i].link = 0;

	/* let last one point back to first with link = 1 */
	desc[i - 1].src = q->qbuf_dma;
#ifndef IFC_MCDMA_EXTERNL_DESC
	desc[i - 1].link = 1;

        for (i = desc_per_page; i <= q->qlen; i += desc_per_page) {
               desc[i-1].link = 1;
               if (i == q->qlen) {
                       desc[i-1].src = mem_virt2phys(q->qbuf);
               }
               else {
                      desc[i-1].src = mem_virt2phys(desc + i);
              }
        }
#endif
	/* program regs */
	ifc_writel(q->qcsr + QDMA_REGS_2_Q_START_ADDR_L, q->qbuf_dma);
#ifndef IFC_32BIT_SUPPORT
	ifc_writel(q->qcsr + QDMA_REGS_2_Q_START_ADDR_H, q->qbuf_dma >> 32);
#endif
	ifc_writel(q->qcsr + QDMA_REGS_2_Q_SIZE, descq_ring_len);
	ifc_writel(q->qcsr + QDMA_REGS_2_Q_TAIL_POINTER, 0);

	/* configure head address to write back */
	cons_head = (uint64_t)mem_virt2phys(&(q->consumed_head));
	ifc_writel(q->qcsr + QDMA_REGS_2_Q_CONSUMED_HEAD_ADDR_L, cons_head);
#ifndef IFC_32BIT_SUPPORT
	ifc_writel(q->qcsr + QDMA_REGS_2_Q_CONSUMED_HEAD_ADDR_H, (cons_head >> 32));
#endif

#ifndef IFC_MCDMA_EXTERNL_DESC
	/* configure batch delay */
	ifc_writel(q->qcsr + QDMA_REGS_2_Q_BATCH_DELAY, IFC_QDMA_Q_BATCH_DELAY);
#endif

#ifdef IFC_QDMA_INTF_ST
	/* configure payload */
	ifc_writel(q->qcsr + QDMA_REGS_2_Q_DEBUG_STATUS_4,
		   ifc_check_payload(env_ctx.buf_size));
#endif

#ifndef IFC_MCDMA_EXTERNL_DESC
	/* configure the control register*/
	q_ctrl.field.q_en = 1;
#endif
	/* set descriptor completion mechanism */
	q->wbe = 0;
	switch (q->qchnl->qdev->completion_mode) {
	case CONFIG_QDMA_QUEUE_WB:
#ifndef IFC_MCDMA_EXTERNL_DESC
		q_ctrl.field.q_wb_en = 1;
#endif
		q->wbe = 1;
		break;
	case CONFIG_QDMA_QUEUE_REG:
		break;
	case CONFIG_QDMA_QUEUE_MSIX:
#ifndef IFC_MCDMA_EXTERNL_DESC
		q_ctrl.field.q_intr_en = 1;
		q_ctrl.field.q_wb_en = 1;
#endif
		q->qie = 1;
		break;
	default:
		return -2;
	}

#ifndef IFC_MCDMA_EXTERNL_DESC
	if (q->dir == IFC_QDMA_DIRECTION_RX)
		q_ctrl.field.q_wb_en = 1;

	/* write in QCSR  register*/
	ifc_writel(q->qcsr + QDMA_REGS_2_Q_CTRL, q_ctrl.val);
#endif
	/* set defaults */
	q->head = 0;
	q->consumed_head = 0;
#ifdef WB_CHK_TID_UPDATE
	q->batch_done = 1;
#endif

	return 0;
}

#ifndef IFC_MCDMA_EXTERNL_DESC
/**
 * time_before - time a is before time b
 */
static int time_before(struct timeval *a, struct timeval *b)
{
	if (a->tv_sec < b->tv_sec)
		return 1;
	if ((a->tv_sec == b->tv_sec) && (a->tv_usec < b->tv_usec))
		return 1;
	return 0;
}

static int ifc_qdma_reset_queue(struct ifc_qdma_queue *q)
{
	struct timeval now, then;
	int wait_usec = IFC_QDMA_RESET_WAIT_COUNT;
	int val;

	/* assert reset */
	ifc_writel(q->qcsr + QDMA_REGS_2_Q_RESET, 1);

	/* deadline */
	gettimeofday(&now, NULL);
	then.tv_sec = now.tv_sec;
	then.tv_usec = now.tv_usec + wait_usec;
	if ((double)then.tv_usec <= (double)now.tv_usec)
		then.tv_sec++;

	/* wait for reset to deassert */
	for (;;) {
		val = ifc_readl(q->qcsr + QDMA_REGS_2_Q_RESET);
		if (!val)
			return 0;

		/* see timeout */
		gettimeofday(&now, NULL);
		if (time_before(&then, &now))
			break;
	}
	return -1;
}
#endif

static int ifc_qdma_queue_init(struct ifc_qdma_queue *q)
{
#ifndef IFC_MCDMA_EXTERNL_DESC
	int ret;
	int drop_cnt;

	ret = ifc_qdma_reset_queue(q);
	if (ret < 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Queue reset failed\n");
		return ret;
	}
	/* TODO: Change the address wiht data drop register */
	ifc_writel(q->qcsr + QDMA_REGS_2_Q_DEBUG_STATUS_3, 0);
	drop_cnt = ifc_readl(q->qcsr + QDMA_REGS_2_Q_DEBUG_STATUS_3);
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
		     "drop_cnt:%u\n", drop_cnt);
#endif
	return ifc_qdma_descq_init(q);
}

int ifc_qdma_queue_init_msix(struct ifc_qdma_channel *chnl, int dir)
{
	int efd, err;
	struct ifc_qdma_queue *que;
	int msix_base;
	int command = 0;
	int uioid;

	if (dir == IFC_QDMA_DIRECTION_TX) {
		que = &chnl->tx;
		msix_base = ((chnl->channel_id) * 4);
	} else {
		que = &chnl->rx;
		msix_base = ((chnl->channel_id) * 4 + 2);
	}
	uioid = chnl->qdev->pdev->uio_id;

	/* Register DMA interrupt */
	efd = eventfd(0,0);
	if ((efd == -1) || (efd >= MAX_IRQ_FD)){
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "eventfd creation failed or max reached. Exiting...\n");
		return -1;
	}
	que->dma_irqfd = efd;
	command = ((msix_base << IRQ_FD_BITS) | efd);
	err = pwrite(env_ctx.uio_devices[uioid].uio_fd, &command, 4, 0);
	if (err < 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "MSIX Registration Failed with errno:%u\n", errno);
		return -1;
	}

#ifdef IFC_USER_MSIX
	/* Register Events interrpt */
	command = 0;
	efd = eventfd(0,0);
	if ((efd == -1) || (efd >= MAX_IRQ_FD)){
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "eventfd creation failed or max reached. Exiting...\n");
		return -1;
	}
	que->event_irqfd = efd;
	command = (((msix_base + 1) << IRQ_FD_BITS) | efd);
	err = pwrite(env_ctx.uio_devices[uioid].uio_fd, &command, 4, 0);
	if (err < 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "error while writing %u\n", errno);
		return -1;
	}
#endif
	return 0;
}

static int ifc_qdma_get_poll_ctx(struct ifc_qdma_device *qdev)
{
	int i;

	for (i = 0; i < MAX_POLL_CTX; i++) {
		if (qdev->poll_ctx[i].valid == 0)
			return i;
	}
	return -1;
}

void* ifc_qdma_poll_init(struct ifc_qdma_device *qdev)
{
	uint32_t i;

	if (unlikely(qdev == NULL || qdev->pdev->uio_id > UIO_MAX_DEVICE)) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Invalid Device Context\n");
		return NULL;
	}

	/* get availale polling context*/
	i = ifc_qdma_get_poll_ctx(qdev);
	if (i >= MAX_POLL_CTX) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Getting polling context failed\n");
		return NULL;
	}
	qdev->poll_ctx[i].valid = 1;

	/* create epoll context */
	qdev->poll_ctx[i].epollfd = epoll_create1(0);
	if (qdev->poll_ctx[i].epollfd == -1) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "creating epollfd failed %s\n",
			      strerror(errno));
		return NULL;
	}
	return &(qdev->poll_ctx[i]);
}

int ifc_qdma_poll_add(struct ifc_qdma_channel *chnl, int dir, void *ctx)
{
	struct ifc_qdma_queue *que;
	struct epoll_event ev;
	struct fd_info_s *data_ptr;
	struct ifc_poll_ctx *poll_ctx = (struct ifc_poll_ctx *)ctx;
	int msix_base;

	if (unlikely(chnl == NULL || chnl->channel_id > NUM_MAX_CHANNEL)) {
		IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR,
			     "Invalid channel context\n");
		return -1;
	}

	if (dir == IFC_QDMA_DIRECTION_TX) {
		que = &(chnl->tx);
		msix_base = ((chnl->channel_id) * 4);
	} else {
		que = &(chnl->rx);
		msix_base = ((chnl->channel_id) * 4 + 2);
	}

	data_ptr = (struct fd_info_s*)malloc(sizeof(struct fd_info_s));
	if (data_ptr == NULL)
		return -1;
	data_ptr->chnl = chnl;
	data_ptr->dir = dir;
	data_ptr->umsix = 0;
#ifdef UIO_SUPPORT
	data_ptr->eventfd = que->dma_irqfd;
	(void)msix_base;
#else
	que->dma_irqfd = ifc_intr_handler.efds[msix_base];
	data_ptr->eventfd = ifc_intr_handler.efds[msix_base];
#endif
        ev.events = EPOLLIN;
        ev.data.ptr = data_ptr;
        if (epoll_ctl(poll_ctx->epollfd, EPOLL_CTL_ADD, que->dma_irqfd, &ev) == -1) {
		free(data_ptr);
		return -1;
        }

#ifdef IFC_USER_MSIX
	data_ptr = (struct fd_info_s*)malloc(sizeof(struct fd_info_s));
	if (data_ptr == NULL)
		return -1;
	data_ptr->chnl = chnl;
	data_ptr->dir = dir;
	data_ptr->umsix = 0xAA;
#ifdef UIO_SUPPORT
	data_ptr->eventfd = que->event_irqfd;
#else
	que->event_irqfd = ifc_intr_handler.efds[msix_base + 1];
	data_ptr->eventfd = ifc_intr_handler.efds[msix_base + 1];
#endif
        ev.events = EPOLLIN;
        ev.data.ptr = data_ptr;
        if (epoll_ctl(poll_ctx->epollfd, EPOLL_CTL_ADD, que->event_irqfd, &ev) == -1) {
		free(data_ptr);
		return -1;
        }
#endif
	return 0;
}

int ifc_mcdma_poll_add_event(struct ifc_qdma_device *qdev, int dir, void *ctx)
{
	struct epoll_event ev;
	struct fd_info_s *data_ptr;
	struct ifc_poll_ctx *poll_ctx = (struct ifc_poll_ctx *)ctx;
	uint32_t command;
	int vec, err, uioid;

	uioid = qdev->pdev->uio_id;

	if (dir == IFC_QDMA_DIRECTION_RX) {
		vec = 0;
	} else {
		vec = 1;
	}

	data_ptr = (struct fd_info_s*)malloc(sizeof(struct fd_info_s));
	if (data_ptr == NULL)
		return -1;
	data_ptr->dir = dir;
	/* Register DMA interrupt */
	data_ptr->eventfd = eventfd(0,0);
	if ((data_ptr->eventfd  == -1) || (data_ptr->eventfd >= MAX_IRQ_FD)){
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "eventfd creation failed or max reached. Exiting...\n");
                free(data_ptr);
                data_ptr = NULL;
		return -1;
	}

	command = ((vec << MSI_IRQFD_BITS) | data_ptr->eventfd);
	err = pwrite(env_ctx.uio_devices[uioid].uio_fd, &command, 4, 0);
	if (err < 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "MSIX Registration Failed with errno:%u\n", errno);
                free(data_ptr);
                data_ptr = NULL;
		return -1;
	}

        ev.events = EPOLLIN;
        ev.data.ptr = data_ptr;
        if (epoll_ctl(poll_ctx->epollfd, EPOLL_CTL_ADD, data_ptr->eventfd, &ev) == -1) {
		free(data_ptr);
                data_ptr = NULL;
		return -1;
        }
	return 0;
}

static uint32_t handle_desc_data_fetch_err(struct ifc_qdma_queue *q)
{
	uint32_t q_cpl_timeout;
	uint32_t q_err_during_desc_fetch, q_err_during_data_fetch;

	q_cpl_timeout =  ifc_readl(q->qcsr + QDMA_REGS_2_Q_CPL_TIMEOUT);

	q_err_during_desc_fetch = q->consumed_head & 0x80000000;
	q_err_during_data_fetch = q->consumed_head & 0x40000000;

	if (q_err_during_desc_fetch || q_err_during_data_fetch || q_cpl_timeout)
	{
		if(q_cpl_timeout) {
			q_cpl_timeout &= 0xfffffffe;
			ifc_writel(q->qcsr + QDMA_REGS_2_Q_CPL_TIMEOUT, q_cpl_timeout);
#ifdef IFC_DEBUG_STATS
			q->stats.cto_count++;
#endif
            //printf("handle_desc_data_fetch_err: CTO_EVENT\n");
			return CTO_EVENT;
		}
		else {
			if (q_err_during_desc_fetch){
				//printf("handle_desc_data_fetch_err: DESC_FETCH_EVENT\n");
				return DESC_FETCH_EVENT;
			} else if (q_err_during_data_fetch){
				//printf("handle_desc_data_fetch_err: DATA_FETCH_EVENT\n");
				return DATA_FETCH_EVENT;
			}
		}
	}

	return 0;
}

/**
 * qdma_get_error - read error bits
 */
static uint32_t ifc_qdma_read_error(struct ifc_qdma_channel *c,
				    int dir)
{
	struct ifc_qdma_queue *q;
	volatile uint32_t head;
	volatile uint32_t  total_drops_count = 0;
	int err;
	if (dir == IFC_QDMA_DIRECTION_RX)
		q = &c->rx;
	else
		q = &c->tx;

	head =  ifc_readl(q->qcsr + QDMA_REGS_2_Q_DATA_DRP_ERR_CTR);
	if (head & (1 << 20))
	{	
		total_drops_count = head & 0xFFFF;
		q->data_drops_cnts =  q->data_drops_cnts + total_drops_count;
		head &= ~0x10FFFF ;
		ifc_writel(q->qcsr + QDMA_REGS_2_Q_DATA_DRP_ERR_CTR, head);
		q->tid_drops++;
		err = TID_ERROR;
		return err;
	}
	else {
	/* Need to use mask once chdr file generated */
	return (q->consumed_head & 0xf0000000);
	}
}

int ifc_qdma_poll_wait(void *ctx,
		       int timeout,
		       struct ifc_qdma_channel **chnl,
		       int *dir)
{
	struct epoll_event events[MAX_EVENTS];
	int nfds, n;
	struct ifc_poll_ctx *poll_ctx = (struct ifc_poll_ctx *)ctx;
	int errinfo;

	nfds = epoll_wait(poll_ctx->epollfd, events, MAX_EVENTS, timeout);
	if (nfds == -1) {
		return -1;
	}

	for (n = 0; n < nfds; ++n) {
		if (events[n].data.ptr) {
			struct fd_info_s *dataptr = (struct fd_info_s*) events[n].data.ptr;
			*chnl = dataptr->chnl;
			*dir = dataptr->dir;
			if (dataptr->umsix == 0xAA) {
				errinfo = ifc_qdma_read_error(*chnl, *dir);
				/* call the user MSIx handler */
				if(*dir == IFC_QDMA_DIRECTION_RX)
					dataptr->chnl->rx.irq_handler(
						(*chnl)->qdev,
						*chnl,*dir,
						&dataptr->chnl->rx.irqdata,
						&errinfo);
				else
					dataptr->chnl->tx.irq_handler(
						(*chnl)->qdev,
						*chnl,
						*dir,
						&dataptr->chnl->tx.irqdata,
						&errinfo);
			}
#ifdef IFC_DEBUG_STATS
			if (dataptr->dir) {
				dataptr->chnl->tx.stats.last_intr_cons_head =
					dataptr->chnl->tx.consumed_head;
				dataptr->chnl->tx.stats.last_intr_reg = ifc_readl(
					dataptr->chnl->tx.qcsr + QDMA_REGS_2_Q_COMPLETED_POINTER);
			}
			else {
				dataptr->chnl->rx.stats.last_intr_cons_head =
					dataptr->chnl->rx.consumed_head;
				dataptr->chnl->rx.stats.last_intr_reg =
					ifc_readl(dataptr->chnl->rx.qcsr + QDMA_REGS_2_Q_COMPLETED_POINTER);
			}
#endif
		}
	}
	return nfds;
}

void ifc_qdma_add_irq_handler( struct ifc_qdma_channel *chnl,
		      	      int dir,
		     	      umsix_irq_handler irq_handler,
		      	      void *data){
	
	if(dir == IFC_QDMA_DIRECTION_TX){
		chnl->tx.irq_handler = irq_handler;
		chnl->tx.irqdata = data;
	} else {
		chnl->rx.irq_handler = irq_handler;
		chnl->rx.irqdata = data;
	}

}

int ifc_mcdma_poll_wait_for_intr(void *ctx,
		       int timeout,
		       int *dir)
{
	struct epoll_event events[MAX_EVENTS];
	int nfds, n;
	struct ifc_poll_ctx *poll_ctx = (struct ifc_poll_ctx *)ctx;

	nfds = epoll_wait(poll_ctx->epollfd, events, MAX_EVENTS, timeout);
	if (nfds == -1) {
		return -1;
	}

	for (n = 0; n < nfds; ++n) {
		if (events[n].data.ptr) {
			struct fd_info_s *dataptr = (struct fd_info_s*) events[n].data.ptr;
			*dir = dataptr->dir;
		}
	}
	return nfds;
}


static int ifc_qdma_chnl_init(struct ifc_qdma_device *qdev,
			      struct ifc_qdma_channel *c,
			      int dir)
{
	struct ifc_qdma_queue *q;
	int ret;
	uint32_t channel_base;

	/* Set QCSR address */
	if (dir == IFC_QDMA_DIRECTION_RX) {
		q = &c->rx;
		channel_base = ifc_get_channel_base(c, dir);
		q->qcsr = qdev->qcsr + channel_base;
	} else {
		q = &c->tx;
		channel_base = ifc_get_channel_base(c, dir);
		q->qcsr = qdev->qcsr + channel_base;
	}

	/* Set Queue Parameters */
	q->num_desc_pages = IFC_CONFIG_QDMA_NUM_DESC_PAGES_PER_QUEUE;
	q->qchnl = c;
	q->dir = dir;

	/* Init descriptors and QCSR */
	ret = ifc_qdma_queue_init(q);
	if (ret)
		return ret;

#ifdef UIO_SUPPORT
	/* Init RX MSIX context */
	if (q->qie) {
		ret = ifc_qdma_queue_init_msix(c, dir);
		if (ret)
			return ret;
	}
#endif
	return 0;
}

#ifndef IFC_QDMA_DYN_CHAN
#ifdef IFC_MCDMA_ERR_CHAN
static int ifc_qdma_check_for_max(struct ifc_qdma_device *qdev)
{
	uint32_t reg = 0;
	uint32_t max_chcnt;
	reg = ifc_readl(qdev->gcsr + IFC_GCSR_OFFSET(QDMA_REGS_2_PF0_IP_PARAM_2));
#ifdef IFC_MCDMA_IS_PF
	max_chcnt = ((reg & QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_MASK) >>
			QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_SHIFT);
#else
	max_chcnt = ((reg & QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_MASK) >>
			QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_SHIFT);
#endif
	if (max_chcnt <= qdev->active_ch_cnt)
		return -1;

	return 0;
}
#endif //IFC_MCDMA_ERR_CHAN

static int is_qdma_channel_avail(struct ifc_qdma_device *qdev,
			         int  cid,
			         int dir)
{
	int w = cid / 32;
	int i = cid % 32;

	switch(dir) {
	case IFC_QDMA_DIRECTION_TX:
		if (qdev->tx_bitmap[w] & BIT(i))
			return -1;
		break;
	case IFC_QDMA_DIRECTION_RX:
		if (qdev->rx_bitmap[w] & BIT(i))
			return -1;
		break;
	case IFC_QDMA_DIRECTION_BOTH:
		if ((qdev->tx_bitmap[w] & BIT(i)) ||
		    (qdev->rx_bitmap[w] & BIT(i)))
			return -1;
		break;
	default:
		return -1;
	}
	return 0;
}

static void qdma_channel_reserve(struct ifc_qdma_device *qdev,
			         int  cid,
			         int dir)
{
	int w = cid / 32;
	int i = cid % 32;

	switch(dir) {
	case IFC_QDMA_DIRECTION_TX:
		qdev->tx_bitmap[w] |= BIT(i);
		break;
	case IFC_QDMA_DIRECTION_RX:
		qdev->rx_bitmap[w] |= BIT(i);
		break;
	case IFC_QDMA_DIRECTION_BOTH:
		qdev->tx_bitmap[w] |= BIT(i);
		qdev->rx_bitmap[w] |= BIT(i);
		break;
	default:
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Invalid direction \n");
	}
}
#endif

static int qdma_channel_free(struct ifc_qdma_device *qdev,
			         int  cid,
			         int dir)
{
	int w = cid / 32;
	int i = cid % 32;

	switch(dir) {
	case IFC_QDMA_DIRECTION_TX:
		qdev->tx_bitmap[w] &= ~BIT(i);
		break;
	case IFC_QDMA_DIRECTION_RX:
		qdev->rx_bitmap[w] &= ~BIT(i);
		break;
	case IFC_QDMA_DIRECTION_BOTH:
		qdev->tx_bitmap[w] &= ~BIT(i);
		qdev->rx_bitmap[w] &= ~BIT(i);
		break;
	default:
		return -1;
	}
	return 0;
}

#ifdef IFC_QDMA_DYN_CHAN
#ifdef DEBUG_DCA
static int qdma_reset_tables( __attribute__((unused)) struct ifc_qdma_device *qdev)
{
	uint32_t addr;
	uint32_t l2p_off;
	void *gcsr = qdev->gcsr;

	int coi_len = NUM_MAX_CHANNEL/32;
	int i = 0;


	if (qdma_acquire_lock(qdev, 0)) {
		/* dump of FCOI table for debug */
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "could not acquire lock to reset tables\n");
		return 0;
	}

	/* dump of FCOI table for debug */
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			     "Resetting FCOI table\n");
	for (i= 0; i < 64; i++) {
		/* Read COI table */
		addr = FCOI_BASE + (i * sizeof(uint32_t));
		ifc_writel(gcsr + IFC_GCSR_OFFSET(addr), 0);
	}

	/* dump of COI table for debug */
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			     "Resetting COI table\n");
	for (i= 0; i < coi_len; i++) {
		/* Read COI table */
		addr = COI_BASE + (i * sizeof(uint32_t));
		ifc_writel(gcsr + IFC_GCSR_OFFSET(addr), 0);
	}

	/* Get L2P table offset of the device */
	if (qdev->is_pf)
		l2p_off = qdma_get_l2p_pf_base(qdev->pf);
	else
		l2p_off = qdma_get_l2p_vf_base(qdev->pf, qdev->vf);

	/* dump of COI table for debug */
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			     "Resetting L2P table\n");
	for (i= 0; i < L2P_TABLE_SIZE/4 ; i++) {
		/* Read COI table */
		addr = l2p_off + (i * sizeof(uint32_t));
		ifc_writel(gcsr + IFC_GCSR_OFFSET(addr), 0);
	}

	/* Release lock */
	qdma_release_lock(qdev);
	return 0;
}

static void qdma_dump_tables(struct ifc_qdma_device *qdev)
{
	uint32_t reg;
	uint32_t addr;
	uint32_t l2p_off;

	int coi_len = NUM_MAX_CHANNEL/32;
	void *gcsr = qdev->gcsr;
	int i = 0;

	if (qdma_acquire_lock(qdev, 0)) {
		/* dump of FCOI table for debug */
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "could not acquire lock to reset tables\n");
		return;
	}

	/* dump of FCOI table for debug */
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "FCOI table dump\n");
	for (i= 0; i < 64; i++) {
		/* Read COI table */
		addr = FCOI_BASE + (i * sizeof(uint32_t));
		reg = ifc_readq(gcsr + IFC_GCSR_OFFSET(addr));
		if (reg)
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     	     "0x%x %u:0x%x\n", addr, i, reg);
	}

	/* dump of COI table for debug */
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "COI table dump\n");
	for (i= 0; i < coi_len; i++) {
		/* Read COI table */
		addr = COI_BASE + (i * sizeof(uint32_t));
		reg = ifc_readq(gcsr + IFC_GCSR_OFFSET(addr));
		if (reg)
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
		     	     "0x%x %u:0x%x\n", addr, i, reg);
	}

	/* Get L2P table offset of the device */
	if (qdev->is_pf)
		l2p_off = qdma_get_l2p_pf_base(qdev->pf);
	else
		l2p_off = qdma_get_l2p_vf_base(qdev->pf, qdev->vf);

	/* dump of COI table for debug */
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "L2P table dump\n");
	for (i= 0; i < L2P_TABLE_SIZE/4 ; i++) {
		/* Read COI table */
		addr = l2p_off + (i * sizeof(uint32_t));
		reg = ifc_readq(gcsr + IFC_GCSR_OFFSET(addr));
		if (reg)
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
		     	     "0x%x %u:0x%x\n", addr, i, reg);
	}

	/* Release lock */
	qdma_release_lock(qdev);
}
#else
#define qdma_dump_tables(a)			do {} while (0)
#define qdma_reset_tables(a)			do {} while (0)
#endif

static int qdma_get_fcoi_device_mask(struct ifc_qdma_device *qdev)
{
	int pf_mask = 0;
	int vf_mask = 0;
	int device_mask = 0;
	int vf_active_mask = 0;
	int vf_alloc_mask = 0;

	/* create PF mask */
	pf_mask = IFC_QDMA_WR_FIELD(qdev->pf, QDMA_FCOI_REGS_PF_SHIFT, QDMA_FCOI_REGS_PF_SHIFT_WIDTH);

	/* create VF mask */
	vf_mask = IFC_QDMA_WR_FIELD(qdev->vf, QDMA_FCOI_REGS_VF_SHIFT, QDMA_FCOI_REGS_VF_SHIFT_WIDTH);

	/* create VFACTIVE mask */
	if (qdev->is_pf == 0)
		vf_active_mask = IFC_QDMA_WR_FIELD(1, QDMA_FCOI_REGS_VFACTIVE_SHIFT, QDMA_FCOI_REGS_VFACTIVE_SHIFT_WIDTH);

	vf_alloc_mask = IFC_QDMA_WR_FIELD(1, QDMA_FCOI_REGS_ALLOC_SHIFT, QDMA_FCOI_REGS_ALLOC_SHIFT_WIDTH);

	/* create device mask */
	device_mask = (pf_mask | vf_mask | vf_active_mask | vf_alloc_mask);

	return device_mask;
}

static int qdma_get_lock_mask(struct ifc_qdma_device *qdev)
{
	int pf_mask = 0;
	int vf_mask = 0;
	int device_mask = 0;
	int lock_mask = 0;
	int vf_active_mask = 0;

	/* create PF mask */
	lock_mask = IFC_QDMA_WR_FIELD(1, QDMA_LOCK_REGS_LOCK_SHIFT, QDMA_LOCK_REGS_LOCK_SHIFT_WIDTH);

	/* create PF mask */
	pf_mask = IFC_QDMA_WR_FIELD(qdev->pf, QDMA_LOCK_REGS_PF_SHIFT, QDMA_LOCK_REGS_PF_SHIFT_WIDTH);

	/* create VF mask */
	vf_mask = IFC_QDMA_WR_FIELD(qdev->vf, QDMA_LOCK_REGS_VF_SHIFT, QDMA_LOCK_REGS_VF_SHIFT_WIDTH);

	/* create VFACTIVE mask */
	if (qdev->is_pf == 0)
		vf_active_mask = IFC_QDMA_WR_FIELD(1, QDMA_LOCK_REGS_VFACTIVE_SHIFT, QDMA_LOCK_REGS_VFACTIVE_SHIFT_WIDTH);

	/* create device mask */
	device_mask = (lock_mask | pf_mask | vf_mask | vf_active_mask);

	return device_mask;
}

static int qdma_get_device_mask(struct ifc_qdma_device *qdev)
{
	int pf_mask = 0;
	int vf_mask = 0;
	int device_mask = 0;
	int lock_mask = 0;
	int vf_active_mask = 0;

	/* create PF mask */
	pf_mask = IFC_QDMA_WR_FIELD(qdev->pf, QDMA_PING_REGS_PF_SHIFT, QDMA_PING_REGS_PF_SHIFT_WIDTH);

	/* create VF mask */
	vf_mask = IFC_QDMA_WR_FIELD(qdev->vf, QDMA_PING_REGS_VF_SHIFT, QDMA_PING_REGS_VF_SHIFT_WIDTH);

	/* create VFACTIVE mask */
	if (qdev->is_pf == 0)
		vf_active_mask = IFC_QDMA_WR_FIELD(1, QDMA_PING_REGS_VFACTIVE_SHIFT, QDMA_PING_REGS_VFACTIVE_SHIFT_WIDTH);

	/* create device mask */
	device_mask = (lock_mask | pf_mask | vf_mask | vf_active_mask);

	return device_mask;
}

static int qdma_get_l2p_vf_base(uint32_t pf, uint32_t vf)
{
	uint32_t off = 0;
	uint32_t i = 0;

	for (i = 0; i <pf; i++) {
		off += 32; // every PF contains 32 VFs
	}

	off += vf;
	off = IFC_MCDMA_L2P_VF_BASE + (off * L2P_TABLE_SIZE);
	return off;
}

static int qdma_get_l2p_pf_base(uint32_t pf)
{
	uint32_t off = 0;
	uint32_t i = 0;

	for (i = 0; i <pf; i++) {
		off += 32; // every VF contains 32 VFs
	}

	off = IFC_MCDMA_L2P_PF_BASE + (off * NUM_MAX_CHANNEL);
	return off;
}

static int qdma_update_l2p(struct ifc_qdma_device *dev, uint32_t ch,
			    __attribute__((unused)) uint32_t device_id_mask)
{
	uint32_t l2p_off, reg;
	uint32_t widx, pos;
	void *gcsr = dev->gcsr;

	/* Get L2P table offset of the device */
	if (dev->is_pf)
		l2p_off = qdma_get_l2p_pf_base(dev->pf);
	else
		l2p_off = qdma_get_l2p_vf_base(dev->pf, dev->vf);

	widx = ch / 2;
	pos = ch % 2;

	/* get l2p table offset of the channel */
	l2p_off += (widx * sizeof(uint32_t));

	/* read entry */
	reg = ifc_readl(gcsr + IFC_GCSR_OFFSET(l2p_off));

	/* create mask to update */
	if (pos == 0) {
		reg &= ~0xFFFF;
		reg |= device_id_mask;
	} else {
		reg &= ~0xFFFF0000;
		reg |= (device_id_mask << 16);
	}

	/* Update L2P Table */
	ifc_writel(gcsr + IFC_GCSR_OFFSET(l2p_off), reg);

	return 0;
}

static int qdma_set_fcoi(struct ifc_qdma_device *dev, uint32_t ch, uint32_t device_mask)
{
	uint32_t fcoi_addr, reg;
	uint32_t pos;
	void *gcsr = dev->gcsr;

	/* get FCOI offset of the channel */
	fcoi_addr = FCOI_TABLE_OFF(ch);
	pos = ch % 2;

	/* read entry */
	reg = ifc_readl(gcsr + IFC_GCSR_OFFSET(fcoi_addr));

	/* create mask to update */
	if (pos == 0) {
		reg &= ~0xFFFF;
		reg |= device_mask;
	} else {
		reg &= ~0xFFFF0000;
		reg |= (device_mask << 16);
	}

	/* Update FCOI Table */
	ifc_writel(gcsr + IFC_GCSR_OFFSET(fcoi_addr),
		    reg);

	return 0;
}

static int qdma_release_channel(struct ifc_qdma_device *qdev, int lch, int pch)
{
	uint32_t  reg, addr = 0;
	int widx, bidx;
	int ret;

	void *gcsr = qdev->gcsr;

	/* get coi table  offset and index */
	widx = pch / 32;
	bidx = pch % 32;

	if (qdma_acquire_lock(qdev, 0))
		return -1;

	/* update COI table */
	addr = COI_BASE + (widx * sizeof(uint32_t));
	reg  = ifc_readl(gcsr + IFC_GCSR_OFFSET(addr));
	reg  = (reg & ~(1 << (bidx)));
	ifc_writel(gcsr + IFC_GCSR_OFFSET(addr), reg);

	/* update FCOI table */
	ret = qdma_set_fcoi(qdev, pch, 0);
	if (ret < 0)
		return -1;

	/* update L2P table */
	ret = qdma_update_l2p(qdev, lch, 0);
	if (ret < 0)
		return -1;

	/* Release lock */
	qdma_release_lock(qdev);

	/* dump tables for debugging purpose */
	qdma_dump_tables(qdev);

	return 0;
}

int ifc_qdma_get_avail_channel_count(struct ifc_qdma_device *qdev)
{
	uint32_t reg, addr;
	int i = 0, j = 0, count = 0;;
	int coi_len = NUM_MAX_CHANNEL/32;
	void *gcsr = qdev->gcsr;

	/* Acquire lock */
	if (qdma_acquire_lock(qdev, 0)) {
		return -1;
	}

	/* Iterate COI table and find free channel */
	for (i= 0; i < coi_len; i++) {
		/* Read COI table */
		addr = COI_BASE + (i * sizeof(uint32_t));
		reg = ifc_readl(gcsr + IFC_GCSR_OFFSET(addr));
		for (j= 0; j < 32; j++) {
			/* Got the available channel. update COI table */
			if ((reg & (1 << j)) == 0) {
				count++;
			}
		}
	}

	/* Release lock */
	qdma_release_lock(qdev);

	return count;
}

static int qdma_get_available_channel(struct ifc_qdma_device *qdev)
{
	uint32_t  reg, addr = 0;
	int coi_len = NUM_MAX_CHANNEL/32;
	int i, j, done = 0;
	void *gcsr = qdev->gcsr;

	/* Iterate COI table and find free channel */
	for (i= 0; i < coi_len; i++) {
		/* Read COI table */
		addr = COI_BASE + (i * sizeof(uint32_t));
		reg = ifc_readl(gcsr + IFC_GCSR_OFFSET(addr));
		done = 0;
		for (j= 0; j < 32; j++) {
			/* Got the available channel. update COI table */
			if ((reg & (1 << j)) == 0) {
				reg |= (1 << j);
				ifc_writel(gcsr + IFC_GCSR_OFFSET(addr),
					    reg);
				done = 1;
				break;
			}
		}
		if (done)
			break;
	}

	/* Check for no channel condition */
	if ((i == coi_len) && (j == 32))
		return -1;

	/* Return available channel ID */
	return (i * 32 + j);
}

static int qdma_release_lock(struct ifc_qdma_device *qdev)
{
	void *gcsr = qdev->gcsr;

	/* update lock register */
	ifc_writeq(gcsr + IFC_GCSR_OFFSET(QDMA_LOCK_REG), 0x0);

	return 0;
}

static int qdma_acquire_lock(struct ifc_qdma_device *qdev, int num)
{
	int busy_reg_off = QDMA_BUSY_REG;
	void *gcsr;
	int num_chan_mask = 0;
	int device_mask = 0;
	int lock_mask = 0;
	int reg = 0;

	gcsr = qdev->gcsr;

	/* prepare device ID mask */
	device_mask = qdma_get_device_mask(qdev);

	/* prepare device ID mask */
	lock_mask = qdma_get_lock_mask(qdev);

	num_chan_mask = IFC_QDMA_WR_FIELD(num,
				QDMA_LOCK_REGS_NUM_CHAN_SHIFT,
				QDMA_LOCK_REGS_NUM_CHAN_SHIFT_WIDTH);

	/* check whether HW is busy */
	reg = ifc_readl(gcsr + IFC_GCSR_OFFSET(busy_reg_off));
	if (reg == 1) {
		return -1;
	}

	/* update lock register */
	ifc_writel(gcsr + IFC_GCSR_OFFSET(QDMA_LOCK_REG), (lock_mask | num_chan_mask));

	/* read device ID */
	reg = ifc_readl(gcsr + IFC_GCSR_OFFSET(QDMA_DEVID_REG));
	if (reg == device_mask) {
		return 0;
	}

	return -1;
}

static int qdma_acquire_channel_from_hw(struct ifc_qdma_device *qdev, uint32_t lch)
{
	uint32_t device_id_mask;
	int chno;
	int ret;

	/* Get free channel */
	chno = qdma_get_available_channel(qdev);
	if (chno < 0)
		return -1;

	/* prepare device ID mask */
	device_id_mask = qdma_get_fcoi_device_mask(qdev);

	/* update FCOI table */
	ret = qdma_set_fcoi(qdev, chno, device_id_mask);
	if (ret < 0)
		return -1;

	/* update L2P table */
	ret = qdma_update_l2p(qdev, lch, chno);
	if (ret < 0)
		return -1;

	/* Return usable channel number */
	return chno;
}
#endif

static int qdma_acquire_channel(struct ifc_qdma_device *qdev,
				__attribute__((unused)) int chno,
				__attribute__((unused)) int dir,
				struct ifc_qdma_channel **chnl)
{
	int i = 0;
	*chnl = NULL;

#ifdef IFC_QDMA_DYN_CHAN
	*chnl = qdev->channel_context[chno].ctx;
	if (qdev->channel_context[chno].valid &&
	    qdev->channel_context[chno].ctx == NULL) {
		/* Need to allocate  */
		i = chno;
	} else {
		/* channel number allocated */
		return -2;
	}
#else
#ifdef IFC_MCDMA_ERR_CHAN
	/* check for max channel count */
	if (ifc_qdma_check_for_max(qdev)) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Out of channel\n");
		return -1;
	}
#endif

	int done = 0;
	if (chno == IFC_QDMA_AVAIL_CHNL_ARG) {
		for (i = 0; i < IFC_QDMA_CHANNEL_MAX; i++) {
			if (is_qdma_channel_avail(qdev, i, dir) == 0) {
				chnl = qdev->channel_context[i].ctx;
				qdma_channel_reserve(qdev, i, dir);
				done = 1;
				break;
			}
		}
	} else {
		if (is_qdma_channel_avail(qdev, chno, dir) == 0) {
			qdma_channel_reserve(qdev, chno, dir);
			*chnl = qdev->channel_context[chno].ctx;
			i = chno;
			done = 1;
		}
		else {
			*chnl = qdev->channel_context[chno].ctx;
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_INFO,
				     "channel already used %u\n", chno);
			return -2;
		}
	}

	if (!done) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Out of channel\n");
		return -1;
	}
#endif
	return i;
}

#ifdef IFC_QDMA_DYN_CHAN
/**
 * ifc_qdma_acquire_channels - get channel context for pushing request
 * @qdev: QDMA device context
 * @chnl: location to store channel address
 *
 * @return 0 for success, negavite otherwise
 */
int ifc_qdma_acquire_channels(struct ifc_qdma_device *qdev,
			      int num_chnls)
{
	int i = 0;
	int chno,lcn, reg, avail;
	int busy_reg_off = QDMA_BUSY_REG;
	void *gcsr = qdev->gcsr;

	avail = ifc_qdma_get_avail_channel_count(qdev);
	if (avail < num_chnls) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_INFO,
				"Available channels:%u\n", avail);
		return -1;
	}

	if (pthread_mutex_lock(&qdev->lock) != 0) {
		printf("Acquiring mutex got failed \n");
		return -1;
	}

	/* Acquire lock */
	if (qdma_acquire_lock(qdev, num_chnls))
		return -1;

	reg = ifc_readl(gcsr + IFC_GCSR_OFFSET(busy_reg_off));
	if (reg == 0) {
		return -1;
	}

	for (i = 0; i < num_chnls; i++) {
		lcn = ifc_qdma_get_free_logical_chno(qdev);
		chno = qdma_acquire_channel_from_hw(qdev, lcn);
		if (chno < 0) {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
					"Failed to acquire channel\n");
			continue;
		}
		qdev->channel_context[lcn].valid = 1;
		qdev->channel_context[lcn].ctx = NULL;
		qdev->channel_context[lcn].ph_chno = chno;
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
				"Channel Allocated lch:0x%x pch:0x%x\n", lcn, chno);
	}

	/* Release lock */
	qdma_release_lock(qdev);

	/* Dump debug stats if enabled */
	qdma_dump_tables(qdev);

	if (pthread_mutex_unlock(&qdev->lock) != 0) {
		printf("Releasing mutex got failed \n");
		return -1;
	}
	return num_chnls;
}
#endif

/**
 * ifc_qdma_channel_get - get channel context for pushing request
 * @qdev: QDMA device context
 * @chnl: location to store channel address
 *
 * @return 0 for success, negavite otherwise
 */
int ifc_qdma_channel_get(struct ifc_qdma_device *qdev,
			 struct ifc_qdma_channel **chnl,
			 int chno, int dir)
{
	struct ifc_qdma_channel *c = NULL;
	int ret = 0;
	int i;

	if (unlikely(qdev == NULL || qdev->pdev->uio_id > UIO_MAX_DEVICE)) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Invalid Device Context\n");
		return -1;
	}

	if (pthread_mutex_lock(&qdev->lock) != 0) {
		printf("Acquiring mutex got failed \n");
		return -1;
	}

	i = qdma_acquire_channel(qdev, chno, dir, &c);
	if (i < 0) {
		*chnl = c;
		if (pthread_mutex_unlock(&qdev->lock) != 0) {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
					"Releasing mutex failed\n");
		}
		return i;
	}

	if (c == NULL) {
		/* allocate data on hugepage */
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			     "allocating memory for channel context %u \n", i);
		c = (struct ifc_qdma_channel*)ifc_dma_malloc(sizeof(struct ifc_qdma_channel));
		if (c == NULL) {
			/* release */
			qdma_channel_free(qdev, i, dir);
			if (pthread_mutex_unlock(&qdev->lock) != 0) {
				IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
						"Releasing mutex failed\n");
			}
			return -1;
		}

		memset(c, 0, sizeof(*c));
		c->channel_id = i;
		c->qdev = qdev;
		qdev->active_ch_cnt++;
	}

	switch(dir) {
		case IFC_QDMA_DIRECTION_TX:
		case IFC_QDMA_DIRECTION_RX:
			ret = ifc_qdma_chnl_init(qdev, c, dir);
			break;
		case IFC_QDMA_DIRECTION_BOTH:
			ret = ifc_qdma_chnl_init(qdev, c, IFC_QDMA_DIRECTION_RX);
			if (ret)
				break;
			ret = ifc_qdma_chnl_init(qdev, c, IFC_QDMA_DIRECTION_TX);
			if (ret)
				break;
			break;
		default:
			ret = -1;
			break;
	}
	if (ret) {
		if (pthread_mutex_unlock(&qdev->lock) != 0) {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
					"Releasing mutex failed\n");
		}
		qdma_channel_free(qdev, i, dir);
		return ret;
	}

	/* update channel context */
#ifdef IFC_QDMA_DYN_CHAN
	qdev->channel_context[chno].ctx = c;
#else
	qdev->channel_context[c->channel_id].ctx = c;
#endif
	if (pthread_mutex_unlock(&qdev->lock) != 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
				"Releasing mutex failed\n");
	}

#ifdef GCSR_ENABLED
	ifc_qdma_set_avoid_hol(qdev);
#endif
	*chnl = c;

#ifdef IFC_QDMA_DYN_CHAN
	return qdev->channel_context[c->channel_id].ph_chno;
#else
	return i;
#endif

}

/**
 * qdma_descq_nb_free - number of free
 * place available for queuing new request
 */
static uint32_t qdma_descq_nb_free(struct ifc_qdma_queue *q)
{
	volatile uint32_t head;

	if ((q->wbe) || (q->qie))
		head = (volatile uint32_t)q->consumed_head;
	else
		head = ifc_readl(q->qcsr + QDMA_REGS_2_Q_COMPLETED_POINTER);

	return (head <= q->tail) ?
		(q->qlen - q->tail + head) : (head - q->tail);
}

/**
 * qdma_descq_nb_used - number of descriptor
 * consumed by H/W since last sampling
 */
static uint32_t qdma_descq_nb_used(struct ifc_qdma_queue *q, uint32_t *id)
{
	uint32_t cur_head;
	volatile uint32_t head;

	if ((q->wbe) || (q->qie))
		head = (volatile uint32_t)q->consumed_head;
	else
		head = ifc_readl(q->qcsr + QDMA_REGS_2_Q_COMPLETED_POINTER);

	cur_head = head;
	head = (head  % q->qlen);

	ifc_mb();
	if (id)
		*id = head;

	if ((q->head == head) && (q->processed_head != cur_head)) {
		q->processed_head = cur_head;
		return q->qlen;
	}
	q->processed_head = cur_head;

	return (head >= q->head) ?
		(head - q->head) : (q->qlen - q->head + head);
}

/**
 * ifc_qdma_complete_requests - complete pending requests, if processed
 * @q: channel descriptor ring
 * @pkts: address where completed requests to be copied
 * @quota: maximum number of requests to search
 *
 * Complete processing all the outstanding requests, if processed
 * by DMA engine.
 *
 * @return number of requests completed in this iteration
 */
static int ifc_qdma_complete_requests(struct ifc_qdma_queue *q,
				      void **pkts,
				      uint32_t quota, int dir)
{
	struct ifc_qdma_request *r;
	struct ifc_qdma_desc_sw *sw;
	struct ifc_qdma_desc *desc;
	uint32_t head;
	uint32_t i = 0;
	uint32_t t;
	uint32_t nr;
#ifdef IFC_QDMA_INTF_ST
	int sof = 0, eof = 0;
#endif
	/*  if anything consumed */
	nr = qdma_descq_nb_used(q, &head);
	desc = (struct ifc_qdma_desc *)(q->qbuf + sizeof(*desc) * q->head);
	for (t = q->head; i < nr ; t = (t + 1) % q->qlen) {
		if (desc->link || desc->desc_invalid) {
			if (desc->src == (uint64_t)q->qbuf_dma)
#ifdef IFC_32BIT_SUPPORT
				desc = (struct ifc_qdma_desc *)(uintptr_t)((uint64_t)(uintptr_t)q->qbuf);
#else
				desc = (struct ifc_qdma_desc *)((uint64_t)q->qbuf);
#endif
			else
				desc++;
			--nr;
			continue;
		}
		sw = (struct ifc_qdma_desc_sw *)&q->ctx[t];
		r = sw->ctx_data;
		if (r == NULL) {
			ifc_qdma_stats_g.ifc_qdma_bad_ds++;
			IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR,
				     "something bad with datastructures %u %d \n",
				     t, head);
			break;
		}
#ifdef IFC_QDMA_INTF_ST
		if ( dir == IFC_QDMA_DIRECTION_RX) {
			r->flags = 0;
			eof = desc->eof;
			sof = desc->sof;
			if (eof) {
				r->flags |= IFC_QDMA_EOF_MASK;
				r->pyld_cnt = desc->rx_pyld_cnt;
				/* pyld_cnt 0 is the special value. Represents 1MB*/
				if (desc->rx_pyld_cnt == 0)
					r->pyld_cnt = IFC_QDMA_MB;
			}

			if (sof)
				r->flags |= IFC_QDMA_SOF_MASK;

#ifdef IFC_QDMA_META_DATA
			/*In case of D2H src address conatian metadata*/
			r->metadata = desc->src;
#endif
		}
#else
		if(dir == IFC_QDMA_DIRECTION_RX) {
			r->flags = 0;
		}
#endif
                /*get the current processed descriptor to application for debug purposes*/
                r->cur_desc = (void*) desc;
		
		/* return requests to applications for further processing */
		pkts[i++] = (void *)r;
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

/*
 * ifc_qdma_completion_poll - poll for request processing completions
 * @qchnl: channel context
 * @dir: DMA direction, one of IFC_QDMA_DIRECTION_*
 * @pkts: address where completed requests to be copied
 * @quota: maximum number of requests to search
 *
 * Poll and check if there is any previously queued and pending
 * request got completed. If completed, the requester would be
 * be notified by callback registered.
 */
int ifc_qdma_completion_poll(struct ifc_qdma_channel *qchnl,
			     int dir,
			     void **pkts,
			     uint32_t quota)
{
	int err = 0;
	uint32_t nr;
	struct ifc_qdma_queue *q;
	volatile uint32_t head;
	volatile uint32_t total_drops_count = 0;
	/* validate channel context */
	if (unlikely(qchnl == NULL || qchnl->channel_id > NUM_MAX_CHANNEL)) {
		IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR,
			     "Invalid channel context\n");
		return -1;
	}
	
	

	switch (dir) {
	case IFC_QDMA_DIRECTION_RX:
		/* on RX queue, if anything consumed */
		nr = ifc_qdma_complete_requests(&qchnl->rx, pkts, quota, dir);
		if(nr && 0)
		  printf("debug: rx %d blocks\n", nr);
		q = &qchnl->rx;
		head =  ifc_readl(q->qcsr + QDMA_REGS_2_Q_DATA_DRP_ERR_CTR);
          	if (head & (1 << 20)) {
			total_drops_count = head & 0xFFFF;
			q->data_drops_cnts =  q->data_drops_cnts + total_drops_count;
                  	head &= ~0x10FFFF ;
                  	ifc_writel(q->qcsr + QDMA_REGS_2_Q_DATA_DRP_ERR_CTR, head);
			q->tid_drops++;
			//printf("debug: rx drop error\n");
              	}	
		if ((err = handle_desc_data_fetch_err(&(qchnl->rx)))) {
			//printf("debug: rx fetch error\n");
			qchnl->rx.irq_handler(qchnl->qdev, qchnl, dir, qchnl->rx.irqdata, &err);
			return -1;
		}
		break;
	case IFC_QDMA_DIRECTION_TX:
		/* on TX queue, if anything consumed */
		nr = ifc_qdma_complete_requests(&qchnl->tx, pkts, quota, dir);
		if ((err = handle_desc_data_fetch_err(&(qchnl->tx)))) {
			qchnl->tx.irq_handler(qchnl->qdev, qchnl, dir, qchnl->tx.irqdata, &err);
			return -1;
		}
		break;
	default:
		/* TODO: handle both cases */
		nr = 0;
		break;
	}

	return nr;
}

#define IFC_QDMA_DUMMY_AVMM	0x0

/**
 * ifc_qdma_read_pio - Read the value from BAR2 address
 */
uint32_t ifc_qdma_pio_read32(struct ifc_qdma_device *qdev, uint32_t addr)
{
	uint8_t pio_bar;
	if (qdev == NULL)
		return -1;
	if (qdev->pdev == NULL)
		return -1;

	pio_bar = qdev->pio_bar;

	if (unlikely(qdev->pdev->uio_id > UIO_MAX_DEVICE)) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Invalid Device Context\n");
		return -1;
	}

	return ifc_readl(qdev->pdev->r[pio_bar].map + addr);
}
uint64_t ifc_qdma_pio_read64(struct ifc_qdma_device *qdev, uint64_t addr)
{
	uint8_t pio_bar;
	if (qdev == NULL)
		return -1;
	if (qdev->pdev == NULL)
		return -1;

	pio_bar = qdev->pio_bar;

	if (unlikely(qdev->pdev->uio_id > UIO_MAX_DEVICE)) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Invalid Device Context\n");
		return -1;
	}

	return ifc_readq(qdev->pdev->r[pio_bar].map + addr);
}

/**
 * ifc_qdma_read_pio - Read the value from BAR2 address
 */
uint32_t ifc_qdma_read32(struct ifc_qdma_device *qdev,
			 uint32_t addr, int bar_num)
{
	if (qdev == NULL)
		return -1;
	if (qdev->pdev == NULL)
		return -1;

	if (unlikely(qdev->pdev->uio_id > UIO_MAX_DEVICE)) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Invalid Device Context\n");
		return -1;
	}

	return ifc_readl(qdev->pdev->r[bar_num].map + addr);
}

void ifc_qdma_write32(struct ifc_qdma_device *qdev, uint32_t addr,
		      uint32_t value, int bar_num)
{
	if (qdev == NULL)
		return;
	if (qdev->pdev == NULL)
		return;

	if (unlikely(qdev->pdev->uio_id > UIO_MAX_DEVICE)) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Invalid Device Context\n");
		return;
	}

	ifc_writel(qdev->pdev->r[bar_num].map + addr, value);
}

void ifc_qdma_pio_write32(struct ifc_qdma_device *qdev, uint32_t addr,
			 uint32_t value)
{
	uint8_t pio_bar;
	if (qdev == NULL)
		return;
	if (qdev->pdev == NULL)
		return;

	pio_bar = qdev->pio_bar;

	if (unlikely(qdev->pdev->uio_id > UIO_MAX_DEVICE)) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Invalid Device Context\n");
		return;
	}

	ifc_writel(qdev->pdev->r[pio_bar].map + addr, value);
}
void ifc_qdma_pio_write64(struct ifc_qdma_device *qdev, uint64_t addr,
			 uint64_t value)
{
	uint8_t pio_bar;
	if (qdev == NULL)
		return;
	if (qdev->pdev == NULL)
		return;

	pio_bar = qdev->pio_bar;

	if (unlikely(qdev->pdev->uio_id > UIO_MAX_DEVICE)) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Invalid Device Context\n");
		return;
	}

	ifc_writeq(qdev->pdev->r[pio_bar].map + addr, value);
}

uint64_t ifc_qdma_read64(struct ifc_qdma_device *qdev,
			 uint64_t addr, int bar_num)
{
	if (qdev == NULL)
		return -1;
	if (qdev->pdev == NULL)
		return -1;

	if (unlikely(qdev->pdev->uio_id > UIO_MAX_DEVICE)) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Invalid Device Context\n");
		return -1;
	}

	return ifc_readq(qdev->pdev->r[bar_num].map + addr);
}

void ifc_qdma_write64(struct ifc_qdma_device *qdev, uint64_t addr,
		      uint64_t value, int bar_num)
{
	if (qdev == NULL)
		return;
	if (qdev->pdev == NULL)
		return;

	if (unlikely(qdev->pdev->uio_id > UIO_MAX_DEVICE)) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Invalid Device Context\n");
		return;
	}

	ifc_writeq(qdev->pdev->r[bar_num].map + addr, value);
}
#ifndef IFC_32BIT_SUPPORT
int
ifc_qdma_pio_read256(struct ifc_qdma_device *qdev, uint64_t offset,
		     uint64_t *buf, int bar_num)
{
	uint64_t base;
	uint64_t addr;
	__m256d a;

         if (qdev == NULL)
                return -1;
        if (qdev->pdev == NULL)
                return -1;
        if (unlikely(qdev->pdev->uio_id > UIO_MAX_DEVICE)) {
                IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
                             "Invalid Device Context\n");
                return -1;
        }
	base = (uint64_t)qdev->pdev->r[bar_num].map;
	addr = base + offset;

	/* read = load the data from the addr */
	a = _mm256_loadu_pd ((double *)addr);
	_mm256_storeu_pd ((double *)buf, a);

	return 0;
}

int ifc_qdma_pio_write256(struct ifc_qdma_device *qdev, uint64_t offset,
                          uint64_t *value, int bar_num)
{
	uint64_t addr;
	uint64_t base;
	__m256d a;

        if (qdev == NULL)
                return -1;
        if (qdev->pdev == NULL)
                return -1;

        if (unlikely(qdev->pdev->uio_id > UIO_MAX_DEVICE)) {
                IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
                             "Invalid Device Context\n");
                return -1;
        }

	base = (uint64_t)qdev->pdev->r[bar_num].map;
	addr = base + offset;

	a =  _mm256_loadu_pd ((double *)value);
	_mm256_storeu_pd ((double *)addr, a);

	return 0;
}
__uint128_t ifc_qdma_pio_read128(struct ifc_qdma_device *qdev, uint64_t addr,
				 int bar_num)
{
	__uint128_t temp;
	if (qdev == NULL)
		return -1;
	if (qdev->pdev == NULL)
		return -1;
	if (unlikely(qdev->pdev->uio_id > UIO_MAX_DEVICE)) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Invalid Device Context\n");
		return -1;
	}

	__uint128_t *data;
	data = qdev->pdev->r[bar_num].map + addr;
	__uint128_t *out;
	out = &temp;
#ifdef __SSE2__
	asm volatile("movdqu %1,%%xmm1;"
			"movdqu %%xmm1,%0;"
                           :"=m"(*out)
                           :"m"(*data)
                            :"memory");

#endif
	return temp;
}

void ifc_qdma_pio_write128(struct ifc_qdma_device *qdev, uint64_t addr,
			 __uint128_t value, int bar_num)
{
	if (qdev == NULL)
		return;
	if (qdev->pdev == NULL)
		return;

	if (unlikely(qdev->pdev->uio_id > UIO_MAX_DEVICE)) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Invalid Device Context\n");
		return;
	}
	__uint128_t *data;
	__uint128_t *data1;

	data1 = qdev->pdev->r[bar_num].map + addr;
	data = &value;
#ifdef __SSE2__
	asm volatile("movdqu %1,%%xmm2;"
		     "movdqu %%xmm2,%0;"
                           :"=m"(*data1)
                         :"m"(*data)
                           :"memory");
#endif
}
#endif
#ifdef HW_FIFO_ENABLED
/**
 * ifc_qdma_check_hw_fifo - internal, check for HW register to know
 * 			    available space
 */
static int ifc_qdma_check_hw_fifo(struct ifc_qdma_queue *q)
{
	int off, regoff = 0;
	uint32_t space = 32;
#ifdef IFC_QDMA_ST_MULTI_PORT
	space = 0;
	regoff = (q->qchnl->channel_id * 4);
#endif
#ifdef IFC_QDMA_INTF_ST
	/*
	 * check for fifo_len. In case if it reaches 0, read latest
	 * values from DMQ CSR
 	 */
	if (q->dir)
		off = IFC_GCSR_OFFSET(QDMA_REGS_2_P0_H2_D_TPTR_AVL) +
			regoff;
	else
		off = IFC_GCSR_OFFSET(QDMA_REGS_2_P0_D2_H_TPTR_AVL) +
			regoff;

	q->fifo_len = ifc_readl((char *)q->qchnl->qdev->gcsr + off);
	if (q->fifo_len <=  space) {
		ifc_qdma_stats_g.ifc_qdma_zero_fifo_len++;
		return -1;
	}
#else // AVST
#ifdef IFC_QDMA_ST_MULTI_PORT
	if (pthread_mutex_lock(&q->qchnl->qdev->tid_lock) != 0) {
		printf("Acquiring mutex got failed \n");
		return -1;
	}
#endif
	if (q->dir)
		off = IFC_GCSR_OFFSET(QDMA_REGS_2_P0_H2_D_TPTR_AVL) +
			regoff;
	else
		off = IFC_GCSR_OFFSET(QDMA_REGS_2_P0_D2_H_TPTR_AVL) +
			regoff;

	q->fifo_len = ifc_readl((char *)q->qchnl->qdev->gcsr + off);
	if (q->fifo_len <= space) {
		ifc_qdma_stats_g.ifc_qdma_zero_fifo_len++;
		if (pthread_mutex_unlock(&q->qchnl->qdev->tid_lock) != 0) {
			printf("Releasing mutex got failed \n");
			return -1;
		}
		return -1;
	}
#ifdef IFC_QDMA_ST_MULTI_PORT
	if (pthread_mutex_unlock(&q->qchnl->qdev->tid_lock) != 0) {
		printf("Releasing mutex got failed \n");
		return -1;
	}
#endif
#endif
	return 0;
}
#endif

#ifdef WB_CHK_TID_UPDATE
static int ifc_mcdma_check_wb(struct ifc_qdma_queue *q)
{
	if ((q->tid_updates) &&
	   ((q->tid_updates % IFC_TID_CNT_TO_CHEK_FOR_HOL) == 0)) {
		if (q->consumed_head < q->expecting_wb) {
			/* HOL hit */
#ifdef IFC_DEBUG_STATS
			q->stats.tid_skip++;
#endif
			/* No space available */
			return -1;
		}
		//q->last_head = qhead;
		q->tid_updates = 0;
	}
	/* space available */
	return 0;
}
#endif

#ifdef TID_FIFO_ENABLED
/**
 * ifc_qdma_check_fifo_len - internal, check for fifo space
 */
static int ifc_qdma_check_fifo_len(struct ifc_qdma_queue *q)
{
#ifdef HW_FIFO_ENABLED
	return ifc_qdma_check_hw_fifo(q);
#else
	/* HW fifo disabled */
	if ((q->tid_updates) &&
	   ((q->tid_updates % IFC_TID_CNT_TO_CHEK_FOR_HOL) == 0)) {
		volatile uint32_t qhead = ifc_readl(q->qcsr + QDMA_REGS_2_Q_HEAD_POINTER);
		//if (q->last_head == qhead) {
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
 * ifc_qdma_desc_queue_submit - internal, submits request for prcessing
 */
static int ifc_qdma_descq_queue_submit(struct ifc_qdma_queue *q)
{
#ifdef TID_LATENCY_STATS
	static struct timespec last;
	struct timespec timediff;
	struct timespec timediff1;
#endif

	struct ifc_qdma_desc *d;
	uint32_t qhead;
	int didx;
	int count = 0;

#ifdef TID_FIFO_ENABLED
	if (ifc_qdma_check_fifo_len(q) < 0)
		return -1;
#endif

#ifdef WB_CHK_TID_UPDATE
	if (ifc_mcdma_check_wb(q) < 0)
		return -1;
#endif

	/* update completion mechanism */
	if (q->tail != 0)
		didx = q->tail -1;
	else
		didx = q->qlen -1;
#ifdef IFC_32BIT_SUPPORT
	d = (struct ifc_qdma_desc *)(uintptr_t)((uint64_t)(uintptr_t)q->qbuf + sizeof(*d) * didx);
#else
	d = (struct ifc_qdma_desc *)((uint64_t)q->qbuf + sizeof(*d) * didx);
#endif
	if (q->wbe || q->qie)
		d->wb_en = 1;
	d->msix_en = q->qie;

#ifdef IFC_64B_DESC_FETCH
	if((q->count_desc % 2) && (q->tail != q->qlen -1)){
		q->tail = (q->tail + 1) % q->qlen;
		q->didx = ((q->didx + 1) % IFC_NUM_DESC_INDEXES);
		didx = q->tail -1;
#ifdef IFC_32BIT_SUPPORT
	d = (struct ifc_qdma_desc *)(uintptr_t)((uint64_t)(uintptr_t)q->qbuf + sizeof(*d) * didx);
#else
	d = (struct ifc_qdma_desc *)((uint64_t)q->qbuf + sizeof(*d) * didx);
#endif
		d->didx = q->didx;
		if(d->link == 0)
			d->desc_invalid = 1;
	}
	q->count_desc = 0;
#endif
	/* address same TID update */
	if ((q->tail == q->processed_tail) && (q->processed_tail == q->qlen -1)) {
		ifc_writel(q->qcsr + QDMA_REGS_2_Q_TAIL_POINTER, (q->qlen));
		while (count < IFC_CNT_HEAD_MOVE) {
			qhead = ifc_readl(q->qcsr + QDMA_REGS_2_Q_HEAD_POINTER);
			if ((qhead % 128) == 0)
				break;
			count++;
		}
	}

#ifdef IFC_DEBUG_STATS
	uint32_t cnt = (q->tail <= q->processed_tail) ?
		(q->qlen - 1 - q->processed_tail + q->tail) :
		(q->tail - q->processed_tail);
	q->stats.tid_update += cnt;
	q->stats.submitted_didx = q->didx;
#endif
	ifc_wmb();

	/* update tail pointer */
	ifc_writel(q->qcsr + QDMA_REGS_2_Q_TAIL_POINTER, q->tail);
#ifdef WB_CHK_TID_UPDATE
	q->batch_done = 1;
	q->expecting_wb = q->first_didx;
#endif
#ifdef TID_LATENCY_STATS
	clock_gettime(CLOCK_MONOTONIC, &q->cur_time);
	time_diff(&q->cur_time, &q->last, &timediff);
	time_diff(&q->cur_time, &last, &timediff1);
	q->last = q->cur_time;
	fprintf(stderr, "TEST%u%u: %u,%lu,%lu ns: %lu %lu\n",
			q->qchnl->channel_id, q->dir, cnt,
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

/**
 * ifc_qdma_desc_queue - internal, prepare request for prcessing
 */
static int ifc_qdma_descq_queue_prepare(struct ifc_qdma_queue *q,
				struct ifc_qdma_request *r,
				int rx_dir)
{
	struct ifc_qdma_desc *d;
	uint64_t dma_buf;
	uint32_t tail;
#ifdef IFC_QDMA_DW_LEN
	uint32_t pad_len;
	uint32_t dlen;
#endif

	if (qdma_descq_nb_free(q) <= 0) {
		ifc_qdma_stats_g.ifc_qdma_no_queue_space++;
		return -1;
	}

	tail = q->tail;
#ifdef IFC_32BIT_SUPPORT
	d = (struct ifc_qdma_desc *)(uintptr_t)((uint64_t)(uintptr_t)q->qbuf + sizeof(*d) * tail);
#else
	d = (struct ifc_qdma_desc *)((uint64_t)q->qbuf + sizeof(*d) * tail);
#endif
	if (d->link) {	/* skip the link */
		tail = (tail + 1) % q->qlen;
		q->didx = ((q->didx + 1) % IFC_NUM_DESC_INDEXES);
		d->didx = q->didx;
#ifdef IFC_32BIT_SUPPORT
		d = (struct ifc_qdma_desc *)(uintptr_t)((uint64_t)(uintptr_t)q->qbuf + sizeof(*d) * tail);
#else
		d = (struct ifc_qdma_desc *)((uint64_t)q->qbuf + sizeof(*d) * tail);
#endif
	}
	memset(d, 0, sizeof(struct ifc_qdma_desc));

	dma_buf = mem_virt2phys(r->buf);
	switch (rx_dir) {
	case IFC_QDMA_DIRECTION_RX:
		d->src = r->src;
		d->dest = (uint64_t)dma_buf;
		break;
	case IFC_QDMA_DIRECTION_TX:
		d->src = (uint64_t)dma_buf;
		d->dest = r->dest;
#ifdef IFC_QDMA_INTF_ST
#ifdef IFC_QDMA_META_DATA
		d->dest = r->metadata;
#endif
		if (IFC_QDMA_SOF_MASK & r->flags)  {
			d->sof = 1;
			q->sof_rcvd = 1;
		}
		if (IFC_QDMA_EOF_MASK & r->flags) {
			if (q->sof_rcvd == 0) {
				IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR,
					"EOF Received without SOF\n");
				return -1;
			}
			d->eof = 1;
			q->sof_rcvd = 0;
		}
#endif
		break;
	default:
		break;
	}

	d->len  = r->len;
	d->pad_len = 0;

#ifdef IFC_QDMA_INTF_ST
	/* For Rx, we don't know the start and end of the file */
	if (rx_dir == IFC_QDMA_DIRECTION_TX) {
		if  ((d->len != IFC_MTU_LEN) &&  ((d->len % 64) != 0) &&
		     ((IFC_QDMA_EOF_MASK & r->flags) == 0)) {
			IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR,
				     "Invalid length for Non-EOF descriptor\n");
			ifc_qdma_stats_g.ifc_qdma_inv_len++;
			return -2;
		}
	}
#endif
	/* add mem barrier */
	ifc_wmb();

	q->ctx[tail].ctx_data = (void *)r;
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
#ifdef WB_CHK_TID_UPDATE
	if (q->batch_done == 1) {
       		d->wb_en = q->wbe;
        	d->msix_en = q->qie;
		q->batch_done = 0;
		q->first_didx = q->didx;
	}
#endif

#ifdef IFC_SET_WB_ALL
       d->wb_en = q->wbe;
       d->msix_en = q->qie;
#endif
	return 0;
}

int ifc_qdma_descq_queue_batch_load(struct ifc_qdma_channel *qchnl,
					   void *req_buf,
					   int dir,
					   int n)
{
	struct ifc_qdma_request **rbuf;
	struct ifc_qdma_queue *q;
	int i = 0;
	int ret;

	if (req_buf == NULL || n <= 0)
		return -1;

	if (dir == IFC_QDMA_DIRECTION_RX)
		q = &qchnl->rx;
	else
		q = &qchnl->tx;

	rbuf = (struct ifc_qdma_request **)req_buf;

	for (i = 0; i < n; i++) {
		ret = ifc_qdma_descq_queue_prepare(q, rbuf[i], dir);
		if (ret < 0)
			return -1;
	}
	return 0;
}

/*
 * ifc_qdma_request_start - queue a DMA request for processing
 * @qchnl: channel context received on ifc_qchannel_get()
 * @dir: DMA direction, one of IFC_QDMA_DIRECTION_*
 * @r: request struct that needs to be processed
 *
 * Depending on data direction, one of the descriptor ring pushes
 * the request to DMA engine to be processed.
 * Please note, this is not blocking request. Completion of processing
 * to be notified by callback.
 *
 * @returns 0, on success. negative otherwise.
 */
int ifc_qdma_request_start(struct ifc_qdma_channel *qchnl, int dir,
			   struct ifc_qdma_request *r)
{
	int ret = 0;

	if (unlikely(qchnl == NULL || qchnl->channel_id > NUM_MAX_CHANNEL
	    || r == NULL)) {
		IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR,
			     "Invalid channel/request context\n");
		return -1;
	}

	if (dir == IFC_QDMA_DIRECTION_BOTH) {
		IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR,
			     "currently BOTH direction not supported\n");
		return -1;
	}

	switch (dir) {
	case IFC_QDMA_DIRECTION_RX:
		ret = ifc_qdma_descq_queue_prepare(&qchnl->rx, r, dir);
		if (ret < 0) {
			ifc_qdma_stats_g.ifc_qdma_rx_load_fail++;
			break;
		}
		ret = ifc_qdma_descq_queue_submit(&qchnl->rx);
		break;
	case IFC_QDMA_DIRECTION_TX:
		ret = ifc_qdma_descq_queue_prepare(&qchnl->tx, r, dir);
		if (ret < 0) {
			ifc_qdma_stats_g.ifc_qdma_tx_load_fail++;
			break;
		}
		ret = ifc_qdma_descq_queue_submit(&qchnl->tx);
		break;
	default:
		break;
	}
	ifc_qdma_dump_chnl_qcsr(NULL, qchnl, dir);

	return ret;
}

/*
 * ifc_qdma_request_prepare - prepare a DMA request for processing
 * @qchnl: channel context received on ifc_qchannel_get()
 * @dir: DMA direction, one of IFC_QDMA_DIRECTION_*
 * @r: request struct that needs to be processed
 *
 * Depending on data direction, one of the descriptor ring loads
 * the request to DMA engine to be processed.
 * Please note, this do not submits the transactions. It populates
 * descriptor and update the tail in queue context
 * Application must use ifc_qdma_request_submit API To submit this
 * loaded transaction
 *
 * @returns 0, on success. negative otherwise.
 */
int ifc_qdma_request_prepare(struct ifc_qdma_channel *qchnl, int dir,
			   struct ifc_qdma_request *r)
{
	int ret = 0;

	if (unlikely(qchnl == NULL || qchnl->channel_id > NUM_MAX_CHANNEL
	    || r == NULL)) {
		IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR,
			     "Invalid channel/request context\n");
		return -1;
	}

	if (dir == IFC_QDMA_DIRECTION_BOTH) {
		IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR,
			     "currently BOTH direction not supported\n");
		return -1;
	}
	switch (dir) {
	case IFC_QDMA_DIRECTION_RX:
		ret = ifc_qdma_descq_queue_prepare(&qchnl->rx, r, dir);
		break;
	case IFC_QDMA_DIRECTION_TX:
		ret = ifc_qdma_descq_queue_prepare(&qchnl->tx, r, dir);
		break;
	default:
		break;
	}
	ifc_qdma_dump_chnl_qcsr(NULL, qchnl, dir);

	return ret;
}

/*
 * ifc_qdma_request_submit - submit a DMA request for processing
 * @qchnl: channel context received on ifc_qchannel_get()
 * @dir: DMA direction, one of IFC_QDMA_DIRECTION_*
 *
 * Depending on data direction, DMA transactions would get submitted
 * to DMA engine. Application must use ifc_qdma_request_prepare to load
 * the descriptor before calling this function
 * Please note, this is not blocking request. Completion of processing
 * to be notified by callback.
 *
 * @returns 0, on success. negative otherwise.
 */
int ifc_qdma_request_submit(struct ifc_qdma_channel *qchnl, int dir)
{
	int ret;

	if (unlikely(qchnl == NULL || qchnl->channel_id > NUM_MAX_CHANNEL)) {
		IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR,
			     "Invalid channel context\n");
		return -1;
	}

	if (dir == IFC_QDMA_DIRECTION_BOTH) {
		IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR,
			     "currently BOTH direction not supported\n");
		return -1;
	}

	switch (dir) {
	case IFC_QDMA_DIRECTION_RX:
		ret = ifc_qdma_descq_queue_submit(&qchnl->rx);
		break;
	case IFC_QDMA_DIRECTION_TX:
		ret = ifc_qdma_descq_queue_submit(&qchnl->tx);
		break;
	default:
		ret = -1;
		break;
	}
	return ret;
}

int ifc_mcdma_get_drop_count(struct ifc_qdma_channel *c, int dir){

	struct ifc_qdma_queue *q;

	if (dir == IFC_QDMA_DIRECTION_RX)
		q = &c->rx;
	else
		q = &c->tx;

	return q->data_drops_cnts;

}

#ifdef IFC_DEBUG_STATS
/**
 * ifc_qdma_print_stats - display statistics
 */
void ifc_qdma_print_stats(struct ifc_qdma_channel *c, int dir) {
	struct ifc_qdma_queue *q;

	if (unlikely(c == NULL || c->channel_id > NUM_MAX_CHANNEL)) {
		IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR,
			     "Invalid channel context\n");
		return;
	}

	if (dir == IFC_QDMA_DIRECTION_RX)
		q = &c->rx;
	else
		q = &c->tx;

	printf("SW Stats: chnl:%u dir:%u,bdf:%s,",
		q->qchnl->channel_id, q->dir,q->qchnl->qdev->pdev->pci_slot_name);
#ifdef IFC_32BIT_SUPPORT
			printf("TID updates :%llu,",q->stats.tid_update);
	printf("processed :%llu,",q->stats.processed);
#else
		
	printf("TID updates :%lu,",q->stats.tid_update);
	printf("processed :%lu,",q->stats.processed);
#endif
	printf("phead:%u,",q->processed_head);
	printf("ptail:%u,",q->processed_tail);
	printf("tid skips:%u,",q->stats.tid_skip);
	printf("lhead :%u\n", (volatile uint32_t)q->consumed_head);
	printf("\tshadow head :%u,",q->head);
	printf("proc tail :%u,",q->tail);
	printf("submitted didx :%u,",q->stats.submitted_didx);
	printf("didx :%u,",q->didx);
	printf("head:%u,", ifc_readl(q->qcsr + QDMA_REGS_2_Q_HEAD_POINTER));
	printf("compreg:%u,", ifc_readl(q->qcsr + QDMA_REGS_2_Q_COMPLETED_POINTER));
	printf("lintr head:%u,", q->stats.last_intr_cons_head);
	printf("lintr reg:%u ", q->stats.last_intr_reg);
	printf("d2h max:%u ", ifc_readl(q->qcsr + QDMA_REGS_2_Q_DEBUG_STATUS_4));
	printf("Total CTO occured:%u \n ", q->stats.cto_count);
	if(dir == IFC_QDMA_DIRECTION_RX)
        {
	    	printf("drops:%u\n", ifc_readl(q->qcsr + QDMA_REGS_2_Q_DEBUG_STATUS_3));
#ifdef IFC_32BIT_SUPPORT
						printf("Cumulative data drops :%llu\n", q->data_drops_cnts);
#else
		printf("Cumulative data drops :%lu\n", q->data_drops_cnts);
#endif
	}
}
#endif

/**
 * ifc_qdma_descq_disable - disable/abort the queue desc processing
 */
static void ifc_qdma_descq_disable(struct ifc_qdma_queue *q)
{
	/* configure the control register*/
	ifc_writel(q->qcsr + QDMA_REGS_2_Q_CTRL, 0);
}

static void ifc_qdma_queue_put(struct ifc_qdma_channel *qchnl,
			int dir)
{
	struct ifc_qdma_queue *que;
#ifndef IFC_MCDMA_EXTERNL_DESC
	int qreset ;
#endif
	if (dir == IFC_QDMA_DIRECTION_RX)
		que = &qchnl->rx;
	else
		que = &qchnl->tx;

	ifc_qdma_descq_disable(que);
#ifndef IFC_MCDMA_EXTERNL_DESC
        qreset = ifc_qdma_reset_queue(que);
        if (qreset < 0) {
                IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
                                "Queue reset failed\n");
        }
#endif
	ifc_desc_ring_free(que->qbuf);
}

/*
 * ifc_qdma_channel_put - release a qdma channel
 * @qchnl: channel received by ifc_qdma_channel_get()
 *
 * Once usage of a channel is over release the channel.
 */
void ifc_qdma_channel_put(struct ifc_qdma_channel *qchnl, int dir)
{
	struct ifc_qdma_device *qdev;
	uint32_t cid;

	if (unlikely(qchnl == NULL || qchnl->channel_id > NUM_MAX_CHANNEL)) {
		IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR,
			     "Invalid channel context\n");
		return;
	}
	cid = qchnl->channel_id;
	if (cid >= IFC_QDMA_CHANNEL_MAX)
		return;

	qdev = qchnl->qdev;
	if (pthread_mutex_lock(&qdev->lock) != 0 ) {
		IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR,
			"Acquiring lock failed\n");
		return;
	}
	qdma_channel_free(qdev, cid, dir);
	if (pthread_mutex_unlock(&qdev->lock) != 0) {
		IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR,
			"Releasing lock failed\n");
		return;
	}

	switch(dir) {
		case IFC_QDMA_DIRECTION_TX:
		case IFC_QDMA_DIRECTION_RX:
			ifc_qdma_queue_put(qchnl, dir);
			break;
		case IFC_QDMA_DIRECTION_BOTH:
			ifc_qdma_queue_put(qchnl, IFC_QDMA_DIRECTION_RX);
			ifc_qdma_queue_put(qchnl, IFC_QDMA_DIRECTION_TX);
			break;
		default:
			break;
	}
	ifc_dma_free(qchnl);
}

/*
 * ifc_qdma_channel_reset - reset a qdma channel
 * @qdev: QDMA device context
 * @qchnl: channel received by ifc_qdma_channel_get()
 * @dir: direction to be reset
 *
 * Once usage of a channel is over release the channel.
 */
int ifc_qdma_channel_reset(struct ifc_qdma_device *qdev,
			 struct ifc_qdma_channel *qchnl,
			 int dir)
{
	uint32_t cid;

	if (qchnl == NULL)
		return -1;

	cid = qchnl->channel_id;
	if (cid >= IFC_QDMA_CHANNEL_MAX)
		return -1;

	qdev = qchnl->qdev;
	switch(dir) {
		case IFC_QDMA_DIRECTION_TX:
		case IFC_QDMA_DIRECTION_RX:
			ifc_qdma_queue_put(qchnl, dir);
			ifc_qdma_chnl_init(qdev, qchnl, dir);
			break;
		case IFC_QDMA_DIRECTION_BOTH:
			ifc_qdma_queue_put(qchnl, IFC_QDMA_DIRECTION_RX);
			ifc_qdma_queue_put(qchnl, IFC_QDMA_DIRECTION_TX);
			ifc_qdma_chnl_init(qdev, qchnl, IFC_QDMA_DIRECTION_RX);
			ifc_qdma_chnl_init(qdev, qchnl, IFC_QDMA_DIRECTION_TX);
			break;
		default:
			break;
	}
	return 0;
}

#ifdef IFC_QDMA_DYN_CHAN
/**
 * ifc_qdma_release_all_channels - Release all channels
 * @qdev: QDMA device context
 *
 * @return 0 for success, negavite otherwise
 */
int ifc_qdma_release_all_channels(struct ifc_qdma_device *qdev)
{
	int reg,l2p_off, ph_chno, lch;
	uint32_t addr;
	void *gcsr = qdev->gcsr;
	int i = 0;

	if (pthread_mutex_lock(&qdev->lock) != 0) {
		printf("Acquiring mutex got failed \n");
		return -1;
	}

	qdma_dump_tables(qdev);

	/* Get L2P table offset of the device */
	if (qdev->is_pf)
		l2p_off = qdma_get_l2p_pf_base(qdev->pf);
	else
		l2p_off = qdma_get_l2p_vf_base(qdev->pf, qdev->vf);

	for (i = 0; i < L2P_TABLE_SIZE/4; i++) {

		/* get l2p table offset of the channel */
		addr = l2p_off + (i * sizeof(uint32_t));

		if (qdma_acquire_lock(qdev, 0))
			return -1;

		/* release 1st entry entry */
		reg = ifc_readl(gcsr + IFC_GCSR_OFFSET(addr));
		if (reg == 0) {
			if (i != 0) {
				/* Release lock */
				qdma_release_lock(qdev);
				continue;
			}
		}

		/* Release lock */
		qdma_release_lock(qdev);

		ph_chno = reg & 0xFFFF;
		lch = ifc_qdma_get_lch(qdev, ph_chno);
		if (lch < 0) {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
				"Invalid logical ch num pch:%u reg:0x%x i:%u\n",
				ph_chno, reg, i);
			continue;
		}
		qdma_release_channel(qdev, lch, ph_chno);
		qdev->channel_context[lch].valid = 0;
		qdev->channel_context[lch].ctx = NULL;
		qdev->channel_context[lch].ph_chno = 0;

		/* release 2nd entry entry */
		ph_chno = ((reg & 0xFFFF0000) >> 16);
		lch = ifc_qdma_get_lch(qdev, ph_chno);
		if (lch < 0) {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
				"Invalid logical ch num pch:%u reg:0x%x i:%u\n",
				ph_chno, reg, i);
			continue;
		}
		if (lch < 0) {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
				"Invalid logical ch num pch:%u reg:0x%x i:%u\n",
				ph_chno, reg, i);
			continue;
		}
		qdma_release_channel(qdev, lch, ph_chno);
		qdev->channel_context[lch].valid = 0;
		qdev->channel_context[lch].ctx = NULL;
		qdev->channel_context[lch].ph_chno = 0;
	}

	/* Dump debug stats if enabled */
	qdma_dump_tables(qdev);

	if (pthread_mutex_unlock(&qdev->lock) != 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			    "Releasing mutex got failed \n");
		return -1;
	}
	return 0;
}
#endif
