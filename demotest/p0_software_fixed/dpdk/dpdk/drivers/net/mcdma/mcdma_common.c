/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#include <stdint.h>
#include <rte_malloc.h>
#include <rte_common.h>
#ifndef DPDK_21_11_RC2
#include <rte_ethdev_pci.h>
#else
#include <ethdev_pci.h>
#endif
#include <rte_cycles.h>
#include <rte_kvargs.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>

#include "mcdma.h"
#include "mcdma_access.h"
#include "qdma_regs_2_registers.h"
#include "mcdma_platform.h"
#include "rte_pmd_mcdma.h"


/**
 * ifc_mcdma_ip_reset - performs IP Reset
 */
int ifc_mcdma_ip_reset(struct ifc_mcdma_device *dev)
{
	void *base = dev->qcsr;

	/*  validate BAR0 base address */
	if (base == 0)
		return -1;

	PMD_DRV_LOG(ERR,"Performing IP_RESET\n");
	/* IP reset */
	usleep(2048);
	ifc_writel(base + QDMA_REGS_2_SOFT_RESET, 0x01);

	/*Giving time for the FPGA reset to complete*/
	sleep(1);
	return 0;
}

/**
 * ifc_mcdma_check_ch_sup - check for max channels
 * Returun values
 * 0 - Success
 * 1 - not valid channel for this device
 */
int ifc_mcdma_check_ch_sup(struct ifc_mcdma_device *dev, uint16_t ch)
{
	uint32_t reg = 0;
	uint16_t max_chcnt;
	uint32_t off = QDMA_REGS_2_PF0_IP_PARAM_2 + ((IFC_MCDMA_CUR_PF - 1) * 16);
	reg = ifc_readl(dev->qcsr + off);
#if IFC_MCDMA_CUR_VF == 0
	/* PF device*/
	max_chcnt = ((reg & QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_MASK) >>
			QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_SHIFT);
#else
	/* VF device*/
	max_chcnt = ((reg & QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_MASK) >>
			QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_SHIFT);
#endif
	if (max_chcnt <= ch) {
		return -1;
	}

	return 0;
}

/**
 * time_before - time a is before time b
 */
static int time_before(struct timeval *a, struct timeval *b)
{
	if (a->tv_sec < b->tv_sec)
		return 1;
	if (a->tv_sec == b->tv_sec && a->tv_usec < b->tv_usec)
		return 1;
	return 0;
}

int ifc_mcdma_reset_queue(struct ifc_mcdma_queue *q)
{
	struct timeval now, then;
	int wait_usec = IFC_MCDMA_RESET_WAIT_COUNT;
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

#ifdef IFC_QDMA_DYN_CHAN
#ifdef IFC_QDMA_TELEMETRY
/*
This API polls the scratch pad register for the vf or PF active status. 
*/
int ifc_mcdma_poll_scratch_reg(struct rte_eth_dev *dev, uint32_t offset, int exp_val)
{
       struct timeval now, then;
       int wait_usec = IFC_MCDMA_SCP_WAIT_COUNT;
        /* deadline */
       int val;
        gettimeofday(&now, NULL);
        then.tv_sec = now.tv_sec;
        then.tv_usec = now.tv_usec + wait_usec;
        if ((double)then.tv_usec <= (double)now.tv_usec)
               then.tv_sec++;
       for (;;) {
               val = ifc_mcdma_reg_read(dev, offset);
               if(val == exp_val){
                       printf("Vf is active\n");
                       return val;
               }
               /* see timeout */
               gettimeofday(&now, NULL);
               if (time_before(&then, &now)){
                       printf("timeout observed\n");
                       break;
               }
       }
       return -1;
}
#endif
#endif

#if 0
int ifc_mcdma_wait_for_queue_comp(struct ifc_mcdma_queue *q)
{
	struct timeval now, then;
	int wait_usec = 65536; //64msec
	uint32_t qhead,qtail;

	/* deadline */
	gettimeofday(&now, NULL);
	then.tv_sec = now.tv_sec;
	then.tv_usec = now.tv_usec + wait_usec;
	if ((double)then.tv_usec <= (double)now.tv_usec)
		then.tv_sec++;

	qtail = ifc_readl(q->qcsr + QDMA_REGS_2_Q_TAIL_POINTER);
	qhead = ifc_readl(q->qcsr + QDMA_REGS_2_Q_HEAD_POINTER);
	/* wait for all completions */
	while (qhead != qtail) {
		gettimeofday(&now, NULL);
		if (time_before(&then, &now)) {
			PMD_DRV_LOG(DEBUG, "head and tail not same head:%u tail:%u\n",
				qhead, qtail);
			break;
		}
		qhead = ifc_readl(q->qcsr + QDMA_REGS_2_Q_HEAD_POINTER);
	}
	PMD_DRV_LOG(DEBUG, "head and tail check completed same. head:%u tail:%u\n",
			qhead, qtail);

	return -1;
}
#endif

int ifc_mcdma_wait_for_comp(struct ifc_mcdma_queue *q)
{
	struct timeval now, then;
	int wait_usec = 65536; //64msec

	/* deadline */
	gettimeofday(&now, NULL);
	then.tv_sec = now.tv_sec;
	then.tv_usec = now.tv_usec + wait_usec;
	if ((double)then.tv_usec <= (double)now.tv_usec)
		then.tv_sec++;

	/* wait for all completions */
	while (q->consumed_head != q->stats.submitted_didx) {
		gettimeofday(&now, NULL);
		if (time_before(&then, &now)) {
			break;
		}
	}

	return -1;
}

int ifc_mcdma_get_hw_version(struct rte_eth_dev *dev)
{
	int ret;
	struct ifc_mcdma_device *dma_priv;

	dma_priv = (struct ifc_mcdma_device *)dev->data->dev_private;
	ret = ifc_mcdma_get_version(dev, &dma_priv->rtl_version);
	if (ret != QDMA_SUCCESS)
		return -1;

	PMD_DRV_LOG(INFO, "MCDMA RTL VERSION : 0x%x",
		    dma_priv->rtl_version);
	return 0;
}

int mcdma_open(const char *file_name, int mode)
{
	struct stat s;
	int fd;

	if (file_name == NULL)
		return -1;

	if (!lstat(file_name, &s)) {
		if (S_ISLNK(s.st_mode))
			return -1;
	}
	fd = open(file_name, mode);
	if (fd < 0)
		return -1;
	return fd;
}

int ifc_get_aligned_payload(int payload)
{
        int new_payload = 0;

/* For 1024 data width, 128 bytes payload alignment is required.
*/
#if (IFC_DATA_WIDTH == 1024)
        int remainder = payload % 128;
#else
        int remainder = payload % 64;
#endif
        if (remainder == 0)
                return payload;
 
#if (IFC_DATA_WIDTH == 1024)
	/* Rounding up to the nearest multiple of a 128 */
        new_payload = payload + 128 - remainder;
#else
        /* Rounding up to the nearest multiple of a 64 */
        new_payload = payload + 64 - remainder;
#endif
        return new_payload;
}

int ifc_mcdma_get_drop_count(int portno, int qid, int dir)
{
	struct ifc_mcdma_queue *q;
	struct rte_eth_dev *dev = &rte_eth_devices[portno];

	if (dir == IFC_QDMA_DIRECTION_TX)
		q = (struct ifc_mcdma_queue *)dev->data->tx_queues[qid];
	else
		q = (struct ifc_mcdma_queue *)dev->data->rx_queues[qid];

	return q->data_drops_cnts;
}

