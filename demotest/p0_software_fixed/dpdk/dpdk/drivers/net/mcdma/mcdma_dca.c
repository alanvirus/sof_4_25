/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */

#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include "rte_pmd_mcdma.h"
#include "dynamic_channel_params.h"
#include "mcdma_access.h"
#include "mcdma_platform.h"
#include "mcdma.h"
#include "mcdma_dca.h"

#ifdef IFC_QDMA_DYN_CHAN
#ifdef DEBUG_DCA
static void ifc_mcdma_dump_tables(struct ifc_mcdma_device *mcdma_dev);
#else
#define ifc_mcdma_dump_tables(a)                     do {} while (0)
#define ifc_mcdma_reset_tables(a)                    do {} while (0)
#endif
#endif

#ifdef IFC_QDMA_DYN_CHAN
#ifdef IFC_QDMA_TELEMETRY
int ifc_mcdma_get_l2p_vf_base(uint32_t pf, uint32_t vf)
#else 
static int ifc_mcdma_get_l2p_vf_base(uint32_t pf, uint32_t vf)
#endif
{
	uint32_t off = 0;
	uint32_t i = 0;

	for (i = 0; i < pf; i++) {
		off += 32; // every PF contains 32 VFs
	}

	off += vf;
	off = IFC_MCDMA_L2P_VF_BASE + (off * L2P_TABLE_SIZE);
	return off;
}
#ifdef IFC_QDMA_TELEMETRY
int ifc_mcdma_get_l2p_pf_base(uint32_t pf)
#else
static int ifc_mcdma_get_l2p_pf_base(uint32_t pf)
#endif
{
	uint32_t off = 0;
	uint32_t i = 0;

	for (i = 0; i < pf; i++) {
		off += 32; // every VF contains 32 VFs
	}

	off = IFC_MCDMA_L2P_PF_BASE + (off * NUM_MAX_CHANNEL);
	return off;
}

static int ifc_mcdma_get_lock_mask(struct ifc_mcdma_device *mcdma_dev)
{
	int pf_mask = 0;
	int vf_mask = 0;
	int device_mask = 0;
	int lock_mask = 0;
	int vf_active_mask = 0;

	/* create PF mask */
	lock_mask = IFC_QDMA_WR_FIELD(1, QDMA_LOCK_REGS_LOCK_SHIFT,
		QDMA_LOCK_REGS_LOCK_SHIFT_WIDTH);

	/* create PF mask */
	pf_mask = IFC_QDMA_WR_FIELD(mcdma_dev->pf, QDMA_LOCK_REGS_PF_SHIFT,
		QDMA_LOCK_REGS_PF_SHIFT_WIDTH);

	/* create VF mask */
	vf_mask = IFC_QDMA_WR_FIELD(mcdma_dev->vf, QDMA_LOCK_REGS_VF_SHIFT,
		QDMA_LOCK_REGS_VF_SHIFT_WIDTH);

	/* create VFACTIVE mask */
	if (mcdma_dev->is_pf == 0)
		vf_active_mask = IFC_QDMA_WR_FIELD(1,
			QDMA_LOCK_REGS_VFACTIVE_SHIFT,
			QDMA_LOCK_REGS_VFACTIVE_SHIFT_WIDTH);

	/* create device mask */
	device_mask = (lock_mask | pf_mask | vf_mask | vf_active_mask);

	return device_mask;
}

static int ifc_mcdma_get_device_mask(struct ifc_mcdma_device *mcdma_dev)
{
	int pf_mask = 0;
	int vf_mask = 0;
	int device_mask = 0;
	int lock_mask = 0;
	int vf_active_mask = 0;

	/* create PF mask */
	pf_mask = IFC_QDMA_WR_FIELD(mcdma_dev->pf, QDMA_PING_REGS_PF_SHIFT,
		QDMA_PING_REGS_PF_SHIFT_WIDTH);

	/* create VF mask */
	vf_mask = IFC_QDMA_WR_FIELD(mcdma_dev->vf, QDMA_PING_REGS_VF_SHIFT,
		QDMA_PING_REGS_VF_SHIFT_WIDTH);

	/* create VFACTIVE mask */
	if (mcdma_dev->is_pf == 0)
		vf_active_mask = IFC_QDMA_WR_FIELD(1,
			QDMA_PING_REGS_VFACTIVE_SHIFT,
			QDMA_PING_REGS_VFACTIVE_SHIFT_WIDTH);

	/* create device mask */
	device_mask = (lock_mask | pf_mask | vf_mask | vf_active_mask);

	return device_mask;
}

static int ifc_mcdma_acquire_lock(struct ifc_mcdma_device *mcdma_dev, int num)
{
	int busy_reg_off = QDMA_BUSY_REG;
	int num_chan_mask = 0;
	int device_mask = 0;
	int lock_mask = 0;
	int reg = 0;

	/* prepare device ID mask */
	device_mask = ifc_mcdma_get_device_mask(mcdma_dev);

	/* prepare device ID mask */
	lock_mask = ifc_mcdma_get_lock_mask(mcdma_dev);

	num_chan_mask = IFC_QDMA_WR_FIELD(num,
				QDMA_LOCK_REGS_NUM_CHAN_SHIFT,
				QDMA_LOCK_REGS_NUM_CHAN_SHIFT_WIDTH);

	/* check whether HW is busy */
	reg = ifc_readl(mcdma_dev->qcsr + busy_reg_off);
	if (reg == 1) {
		return -1;
	}

	/* update lock register */
	ifc_writel(mcdma_dev->qcsr + QDMA_LOCK_REG,
		   (lock_mask | num_chan_mask));

	/* read device ID */
	reg = ifc_readl(mcdma_dev->qcsr + QDMA_DEVID_REG);
	if (reg == device_mask) {
		return 0;
	}

	return -1;
}

static int ifc_mcdma_release_lock(struct ifc_mcdma_device *mcdma_dev)
{
	/* update lock register */
	ifc_writell(mcdma_dev->qcsr + QDMA_LOCK_REG, 0x0);

	return 0;
}

#ifdef IFC_QDMA_TELEMETRY
int ifc_mcdma_update_l2p_pf(struct ifc_mcdma_device *dev, uint32_t ch,
			   __attribute__((unused)) uint32_t device_id_mask, uint32_t pfn, uint32_t vfn)
{
	uint32_t l2p_off, reg;
	uint32_t widx, pos;
	l2p_off = ifc_mcdma_get_l2p_vf_base(pfn, vfn);
	widx = ch / 2;
	pos = ch % 2;

	/* get l2p table offset of the channel */
	l2p_off += (widx * sizeof(uint32_t));

	/* read entry */
	reg = ifc_readl(dev->qcsr + l2p_off);

	/* create mask to update */
	if (pos == 0) {
		reg &= ~0xFFFF;
		reg |= device_id_mask;
	} else {
		reg &= ~0xFFFF0000;
		reg |= (device_id_mask << 16);
	}

	/* Update L2P Table */
	ifc_writel(dev->qcsr + l2p_off, reg);

	return 0;
}
#endif

static int ifc_mcdma_update_l2p(struct ifc_mcdma_device *dev, uint32_t ch,
			   __attribute__((unused)) uint32_t device_id_mask)
{
	uint32_t l2p_off, reg;
	uint32_t widx, pos;

	/* Get L2P table offset of the device */
	if (dev->is_pf)
		l2p_off = ifc_mcdma_get_l2p_pf_base(dev->pf);
	else
		l2p_off = ifc_mcdma_get_l2p_vf_base(dev->pf, dev->vf);

	widx = ch / 2;
	pos = ch % 2;

	/* get l2p table offset of the channel */
	l2p_off += (widx * sizeof(uint32_t));

	/* read entry */
	reg = ifc_readl(dev->qcsr + l2p_off);

	/* create mask to update */
	if (pos == 0) {
		reg &= ~0xFFFF;
		reg |= device_id_mask;
	} else {
		reg &= ~0xFFFF0000;
		reg |= (device_id_mask << 16);
	}

	/* Update L2P Table */
	ifc_writel(dev->qcsr + l2p_off, reg);

	return 0;
}

#ifdef IFC_QDMA_TELEMETRY 
int ifc_mcdma_get_fcoi(struct ifc_mcdma_device *dev, uint32_t pch)
#else
static int ifc_mcdma_get_fcoi(struct ifc_mcdma_device *dev, uint32_t pch)
#endif
{
	uint32_t fcoi_addr, reg;
	uint32_t pos;

	/* get FCOI offset of the channel */
	fcoi_addr = FCOI_TABLE_OFF(pch);
	pos = pch % 2;

	/* read entry */
	reg = ifc_readl(dev->qcsr + fcoi_addr);

	/* create mask to update */
	if (pos == 0) {
		reg = reg & 0xFFFF;
	} else {
		reg = reg & 0xFFFF0000;
		reg = (reg >> 16);
	}

	return reg;
}

#ifdef IFC_QDMA_TELEMETRY
int ifc_mcdma_set_fcoi(struct ifc_mcdma_device *dev, uint32_t ch,
 				uint32_t device_mask)
#else
static int ifc_mcdma_set_fcoi(struct ifc_mcdma_device *dev, uint32_t ch,
			      uint32_t device_mask)
#endif
{
	uint32_t fcoi_addr, reg;
	uint32_t pos;

	/* get FCOI offset of the channel */
	fcoi_addr = FCOI_TABLE_OFF(ch);
	pos = ch % 2;

	reg = ifc_readl(dev->qcsr + fcoi_addr);

	/* create mask to update */
	if (pos == 0) {
		reg &= ~0xFFFF;
		reg |= device_mask;
	} else {
		reg &= ~0xFFFF0000;
		reg |= (device_mask << 16);
	}

	/* Update FCOI Table */
	ifc_writel(dev->qcsr + fcoi_addr, reg);

	return 0;
}

static int ifc_mcdma_get_fcoi_device_mask(struct ifc_mcdma_device *mcdma_dev)
{
	int pf_mask = 0;
	int vf_mask = 0;
	int device_mask = 0;
	int vf_active_mask = 0;
	int vf_alloc_mask = 0;

	/* create PF mask */
	pf_mask = IFC_QDMA_WR_FIELD(mcdma_dev->pf, QDMA_FCOI_REGS_PF_SHIFT,
		QDMA_FCOI_REGS_PF_SHIFT_WIDTH);

	/* create VF mask */
	vf_mask = IFC_QDMA_WR_FIELD(mcdma_dev->vf, QDMA_FCOI_REGS_VF_SHIFT,
		QDMA_FCOI_REGS_VF_SHIFT_WIDTH);

	/* create VFACTIVE mask */
	if (mcdma_dev->is_pf == 0)
		vf_active_mask = IFC_QDMA_WR_FIELD(1,
					QDMA_FCOI_REGS_VFACTIVE_SHIFT,
					QDMA_FCOI_REGS_VFACTIVE_SHIFT_WIDTH);

	vf_alloc_mask = IFC_QDMA_WR_FIELD(1, QDMA_FCOI_REGS_ALLOC_SHIFT,
			QDMA_FCOI_REGS_ALLOC_SHIFT_WIDTH);

	/* create device mask */
	device_mask = (pf_mask | vf_mask | vf_active_mask | vf_alloc_mask);

	return device_mask;
}

int ifc_mcdma_get_device_info(struct ifc_mcdma_device *mcdma_dev)
{
        void *base = mcdma_dev->qcsr;
        uint32_t reg;

        /*  validate BAR0 base address */
        if (base == 0) {
                PMD_DRV_LOG(ERR, "BAR Register mapping not yet done\n");
                return -1;
        }

        /* Read Device ID */
        reg = ifc_readl((char *)base + QDMA_PING_REG);

        /* check if it is PF or VF based on VFACTIVE bit*/
        if (reg & QDMA_PING_REGS_VFACTIVE_SHIFT_MASK)
                mcdma_dev->is_pf = 0;
        else
                mcdma_dev->is_pf = 1;

        /* Retrieve  PF number */
        mcdma_dev->pf = IFC_QDMA_RD_FIELD(reg, QDMA_PING_REGS_PF_SHIFT,
			QDMA_PING_REGS_PF_SHIFT_WIDTH);

        /* retrieve VF number */
        if (mcdma_dev->is_pf == 0)
                mcdma_dev->vf = IFC_QDMA_RD_FIELD(reg,
				QDMA_PING_REGS_VF_SHIFT,
				QDMA_PING_REGS_VF_SHIFT_WIDTH);

        return 0;
}

static int ifc_mcdma_get_available_channel(struct ifc_mcdma_device *mcdma_dev)
{
        uint32_t reg, addr = 0;
        int coi_len = NUM_MAX_CHANNEL/32;
        int i, j, done = 0;

        /* Iterate COI table and find free channel */
        for (i = 0; i < coi_len; i++) {
                /* Read COI table */
                addr = COI_BASE + (i * sizeof(uint32_t));
                reg = ifc_readl(mcdma_dev->qcsr + addr);
                done = 0;
                for (j = 0; j < 32; j++) {
                        /* Got the available channel. update COI table */
                        if ((reg & (1 << j)) == 0) {
                                reg |= (1 << j);
                                ifc_writel(mcdma_dev->qcsr + addr, reg);
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

static int ifc_mcdma_acquire_channel_from_hw(struct ifc_mcdma_device *mcdma_dev,
					uint32_t lch)
{
        uint32_t device_id_mask;
        int chno;
        int ret;

        /* Get free channel */
        chno = ifc_mcdma_get_available_channel(mcdma_dev);
        if (chno < 0)
                return -1;

        /* prepare device ID mask */
        device_id_mask = ifc_mcdma_get_fcoi_device_mask(mcdma_dev);

        /* update FCOI table */
        ret = ifc_mcdma_set_fcoi(mcdma_dev, chno, device_id_mask);
        if (ret < 0)
                return -1;

        /* update L2P table */
        ret = ifc_mcdma_update_l2p(mcdma_dev, lch, chno);
        if (ret < 0)
                return -1;

        /* Return usable channel number */
        return chno;
}

#ifdef IFC_QDMA_TELEMETRY
static int ifc_mcdma_acquire_channel_from_hw_pf(struct ifc_mcdma_device *mcdma_dev,
					uint32_t lch, uint32_t pch)
{
        uint32_t device_id_mask;
        int chno;
        int ret;

        /* Get free channel */
        chno = pch;
        if (chno < 0)
                return -1;

        /* prepare device ID mask */
        device_id_mask = ifc_mcdma_get_fcoi_device_mask(mcdma_dev);

        /* update FCOI table */
        ret = ifc_mcdma_set_fcoi(mcdma_dev, chno, device_id_mask);
        if (ret < 0)
                return -1;

        /* update L2P table */
        ret = ifc_mcdma_update_l2p(mcdma_dev, lch, chno);
        if (ret < 0)
                return -1;

        /* Return usable channel number */
        return chno;
}
#endif

int ifc_mcdma_get_avail_channel_count(struct ifc_mcdma_device *mcdma_dev)
{
        uint32_t reg, addr;
        int i = 0, j = 0, count = 0;;
        int coi_len = NUM_MAX_CHANNEL/32;

        /* Acquire lock */
        if (ifc_mcdma_acquire_lock(mcdma_dev, 0)) {
                return -1;
        }

        /* Iterate COI table and find free channel */
        for (i = 0; i < coi_len; i++) {
                /* Read COI table */
                addr = COI_BASE + (i * sizeof(uint32_t));
                reg = ifc_readl(mcdma_dev->qcsr + addr);
                for (j= 0; j < 32; j++) {
                        /* Got the available channel. update COI table */
                        if ((reg & (1 << j)) == 0) {
                                count++;
                        }
                }
        }

        /* Release lock */
        ifc_mcdma_release_lock(mcdma_dev);

        return count;
}

#ifdef IFC_QDMA_TELEMETRY 
/*
 *   ifc_mcdma_acquire_channel_pf - get channel context for pushing request
 *   @mcdma_dev: QDMA device context
 *   @lch: logical channel ID
 *     
 *   @return 0 for success, negavite otherwise
 */
int ifc_mcdma_acquire_channel_pf(struct ifc_mcdma_device *mcdma_dev,
		int lch, uint32_t pch)
{
	int chno, reg;
	int busy_reg_off = QDMA_BUSY_REG;

	if (mcdma_dev->channel_context[lch].valid == 1) {
		PMD_DRV_LOG(ERR,
			"Channel already Acquired for %u\n", lch);
		return lch;
	}


        if (pthread_mutex_lock(&mcdma_dev->lock) != 0) {
                PMD_DRV_LOG(ERR, "Acquiring mutex got failed \n");
                return -1;
        }

acquire:
        /* Acquire lock */
        if (ifc_mcdma_acquire_lock(mcdma_dev, lch)) {
		goto acquire;
	}

        reg = ifc_readl(mcdma_dev->qcsr + busy_reg_off);
        if (reg == 0) {
                return -1;
        }

	chno = ifc_mcdma_acquire_channel_from_hw_pf(mcdma_dev, lch, pch);
        if (chno < 0) {
        	PMD_DRV_LOG(ERR,
                	"Failed to acquire channel\n");
        }
        mcdma_dev->channel_context[lch].valid = 1;
        mcdma_dev->channel_context[lch].ctx = NULL;
        mcdma_dev->channel_context[lch].ph_chno = chno;
        PMD_DRV_LOG(ERR,
		"Channel Allocated lch:0x%x pch:0x%x", lch, chno);

        /* Release lock */
        ifc_mcdma_release_lock(mcdma_dev);

        if (pthread_mutex_unlock(&mcdma_dev->lock) != 0) {
                PMD_DRV_LOG(ERR, "Releasing mutex got failed \n");
                return -1;
        }
        return lch;
}
#endif
/**
 * ifc_mcdma_acquire_channel - get channel context for pushing request
 * @mcdma_dev: QDMA device context
 * @lch: logical channel ID
 *
 * @return 0 for success, negavite otherwise
 */
int ifc_mcdma_acquire_channel(struct ifc_mcdma_device *mcdma_dev,
			      int lch)
{
        int chno, reg, avail;
        int busy_reg_off = QDMA_BUSY_REG;

	if (mcdma_dev->channel_context[lch].valid == 1) {
                PMD_DRV_LOG(ERR,
			"Channel already Acquired for %u\n", lch);
		return lch;
	}

        avail = ifc_mcdma_get_avail_channel_count(mcdma_dev);
        if (avail < 1) {
                PMD_DRV_LOG(ERR,
                                "Available channels:%u\n", avail);
                return -1;
        }

        if (pthread_mutex_lock(&mcdma_dev->lock) != 0) {
                PMD_DRV_LOG(ERR, "Acquiring mutex got failed \n");
                return -1;
        }

acquire:
        /* Acquire lock */
        if (ifc_mcdma_acquire_lock(mcdma_dev, lch)) {
		goto acquire;
	}

        reg = ifc_readl(mcdma_dev->qcsr + busy_reg_off);
        if (reg == 0) {
                return -1;
        }

	chno = ifc_mcdma_acquire_channel_from_hw(mcdma_dev, lch);
        if (chno < 0) {
        	PMD_DRV_LOG(ERR,
                	"Failed to acquire channel\n");
        }
        mcdma_dev->channel_context[lch].valid = 1;
        mcdma_dev->channel_context[lch].ctx = NULL;
        mcdma_dev->channel_context[lch].ph_chno = chno;
        PMD_DRV_LOG(ERR,
		"Channel Allocated lch:0x%x pch:0x%x", lch, chno);

        /* Release lock */
        ifc_mcdma_release_lock(mcdma_dev);

        /* Dump debug stats if enabled */
        ifc_mcdma_dump_tables(mcdma_dev);

        if (pthread_mutex_unlock(&mcdma_dev->lock) != 0) {
                PMD_DRV_LOG(ERR, "Releasing mutex got failed \n");
                return -1;
        }
        return lch;
}

#ifdef IFC_QDMA_TELEMETRY
int ifc_mcdma_add_channel_vf(struct ifc_mcdma_device *mcdma_dev,
				     int lch, int pch, uint32_t pfn, uint32_t vfn, uint32_t device_mask)
{
        uint32_t reg, addr = 0;
        int widx, bidx;
        int ret;

        /* get coi table  offset and index */
        widx = pch / 32;
        bidx = pch % 32;

        if (pthread_mutex_lock(&mcdma_dev->lock) != 0) {
                PMD_DRV_LOG(ERR, "Acquiring mutex got failed \n");
                return -1;
        }

release:
        if (ifc_mcdma_acquire_lock(mcdma_dev, 0)) {
		goto release;
	}

        /* update COI table */
        addr = COI_BASE + (widx * sizeof(uint32_t));
        reg = ifc_readl(mcdma_dev->qcsr + addr);
        reg = (reg & ~(1 << (bidx)));
        ifc_writel(mcdma_dev->qcsr + addr, reg);

        /* update FCOI table */
        ret = ifc_mcdma_set_fcoi(mcdma_dev, pch, device_mask);
        if (ret < 0)
                return -1;

        /* update L2P table */
        ret = ifc_mcdma_update_l2p_pf(mcdma_dev, lch, pch, pfn, vfn);
        if (ret < 0)
                return -1;

        /* Release lock */
        ifc_mcdma_release_lock(mcdma_dev);

        if (pthread_mutex_unlock(&mcdma_dev->lock) != 0) {
                PMD_DRV_LOG(ERR, "Releasing mutex got failed \n");
                return -1;
        }
        return 0;
}

int ifc_mcdma_release_channel_pf(struct ifc_mcdma_device *mcdma_dev,
				     int lch, int pch, uint32_t pfn, uint32_t vfn)
{
        uint32_t reg, addr = 0;
        int widx, bidx;
        int ret;
        /* get coi table  offset and index */
        widx = pch / 32;
        bidx = pch % 32;
        if (pthread_mutex_lock(&mcdma_dev->lock) != 0) {
                PMD_DRV_LOG(ERR, "Acquiring mutex got failed \n");
                return -1;
        }

release:
        if (ifc_mcdma_acquire_lock(mcdma_dev, 0)) {
		goto release;
	}

        /* update COI table */
        addr = COI_BASE + (widx * sizeof(uint32_t));
        reg = ifc_readl(mcdma_dev->qcsr + addr);
        reg = (reg & ~(1 << (bidx)));
        ifc_writel(mcdma_dev->qcsr + addr, reg);

        /* update FCOI table */
        ret = ifc_mcdma_set_fcoi(mcdma_dev, pch, 0);
        if (ret < 0)
                return -1;

        /* update L2P table */
        ret = ifc_mcdma_update_l2p_pf(mcdma_dev, lch, 0, pfn, vfn);
        if (ret < 0)
                return -1;

        /* Release lock */
        ifc_mcdma_release_lock(mcdma_dev);

        if (pthread_mutex_unlock(&mcdma_dev->lock) != 0) {
                PMD_DRV_LOG(ERR, "Releasing mutex got failed \n");
                return -1;
        }

        return 0;
}
#endif

#ifdef IFC_QDMA_TELEMETRY
int ifc_mcdma_release_channel(struct ifc_mcdma_device *mcdma_dev,
				     int lch, int pch)
#else
static int ifc_mcdma_release_channel(struct ifc_mcdma_device *mcdma_dev,
				     int lch, int pch)
#endif
{
        uint32_t reg, addr = 0;
        int widx, bidx;
        int ret;

        /* get coi table  offset and index */
        widx = pch / 32;
        bidx = pch % 32;

        if (pthread_mutex_lock(&mcdma_dev->lock) != 0) {
                PMD_DRV_LOG(ERR, "Acquiring mutex got failed \n");
                return -1;
        }

release:
        if (ifc_mcdma_acquire_lock(mcdma_dev, 0)) {
		goto release;
	}

        /* update COI table */
        addr = COI_BASE + (widx * sizeof(uint32_t));
        reg = ifc_readl(mcdma_dev->qcsr + addr);
        reg = (reg & ~(1 << (bidx)));
        ifc_writel(mcdma_dev->qcsr + addr, reg);

        /* update FCOI table */
        ret = ifc_mcdma_set_fcoi(mcdma_dev, pch, 0);
        if (ret < 0)
                return -1;

        /* update L2P table */
        ret = ifc_mcdma_update_l2p(mcdma_dev, lch, 0);
        if (ret < 0)
                return -1;

        /* Release lock */
        ifc_mcdma_release_lock(mcdma_dev);

        /* dump tables for debugging purpose */
        ifc_mcdma_dump_tables(mcdma_dev);

        if (pthread_mutex_unlock(&mcdma_dev->lock) != 0) {
                PMD_DRV_LOG(ERR, "Releasing mutex got failed \n");
                return -1;
        }

        return 0;
}

int ifc_mcdma_queue_stop(struct rte_eth_dev *dev,
			 uint16_t lch)
{
        struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;
        int ph_chno = mcdma_dev->channel_context[lch].ph_chno;

	if (!mcdma_dev->channel_context[lch].valid) {
        	PMD_DRV_LOG(ERR,
        		"lch:0x%x Channel already released\n",lch);
		return 0;
	}

        ifc_mcdma_release_channel(mcdma_dev, lch, ph_chno);
        mcdma_dev->channel_context[lch].valid = 0;
        mcdma_dev->channel_context[lch].ctx = NULL;
        mcdma_dev->channel_context[lch].ph_chno = 0;

	return 0;
}

#ifdef IFC_QDMA_TELEMETRY 
/**
 * ifc_mcdma_release_all_channels_pf - Release all channels
 * @qdev: QDMA device context
 *
 * @return 0 for success, negavite otherwise
 */
int ifc_mcdma_release_all_channels_pf(uint16_t port_id, uint32_t ph_chn)
{
	uint32_t reg,l2p_off, ph_chno, lch;
	uint32_t addr;
	uint32_t device_mask, fcoi_dev_mask;
	struct rte_eth_dev *dev = &rte_eth_devices[port_id];
	struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;
	int i = 0;
	/* Get L2P table offset of the device */

	if (mcdma_dev->is_pf){
		l2p_off = ifc_mcdma_get_l2p_pf_base(mcdma_dev->pf);
	}
	else {
		l2p_off = ifc_mcdma_get_l2p_vf_base(mcdma_dev->pf, mcdma_dev->vf);

	}

        /* prepare device ID mask */
        fcoi_dev_mask = ifc_mcdma_get_fcoi_device_mask(mcdma_dev);

	for (i = 0; i < L2P_TABLE_SIZE/4; i++) {

		/* get l2p table offset of the channel */
		addr = l2p_off + (i * sizeof(uint32_t));

		if (ifc_mcdma_acquire_lock(mcdma_dev, 0))
			return -1;

		/* release 1st entry entry */
		reg = ifc_readl(mcdma_dev->qcsr + addr);

		/* Release lock */
		ifc_mcdma_release_lock(mcdma_dev);

		ph_chno = reg & 0xFFFF;
		lch = i * 2;
		device_mask = ifc_mcdma_get_fcoi(mcdma_dev, ph_chno);
		if ((device_mask == fcoi_dev_mask) && (ph_chno == ph_chn) ){
        		ifc_mcdma_release_channel(mcdma_dev, lch, ph_chno);
                	PMD_DRV_LOG(ERR, "Released lch:%u pch:%u", lch, ph_chno);
		} else {
                	PMD_DRV_LOG(DEBUG, "Skipping Release lch:%u pch:%u devicemask:0x%x focimask:0x%x",
				lch, ph_chno, device_mask, fcoi_dev_mask);
		}

		/* release 2nd entry entry */
		ph_chno = ((reg & 0xFFFF0000) >> 16);
		lch = (i * 2) + 1;
		device_mask = ifc_mcdma_get_fcoi(mcdma_dev, ph_chno);
		if( (device_mask == fcoi_dev_mask) && (ph_chno == ph_chn) ) {
        		ifc_mcdma_release_channel(mcdma_dev, lch, ph_chno);
                	PMD_DRV_LOG(ERR, "Released lch:%u pch:%u", lch, ph_chno);
		} else {
                	PMD_DRV_LOG(DEBUG, "Skipping Release lch:%u pch:%u devicemask:0x%x focimask:0x%x",
				lch, ph_chno, device_mask, fcoi_dev_mask);
		}
	}
	return 0;
}
#endif

/**
 * ifc_mcdma_release_all_channels - Release all channels
 * @qdev: QDMA device context
 *
 * @return 0 for success, negavite otherwise
 */
int ifc_mcdma_release_all_channels(uint16_t port_id)
{
	int reg,l2p_off, ph_chno, lch;
	uint32_t addr;
	uint32_t device_mask, fcoi_dev_mask;
	struct rte_eth_dev *dev = &rte_eth_devices[port_id];
	struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;
	int i = 0;

	ifc_mcdma_dump_tables(mcdma_dev);

	/* Get L2P table offset of the device */
	if (mcdma_dev->is_pf)

		l2p_off = ifc_mcdma_get_l2p_pf_base(mcdma_dev->pf);
	else
		l2p_off = ifc_mcdma_get_l2p_vf_base(mcdma_dev->pf, mcdma_dev->vf);

        /* prepare device ID mask */
        fcoi_dev_mask = ifc_mcdma_get_fcoi_device_mask(mcdma_dev);

	for (i = 0; i < L2P_TABLE_SIZE/4; i++) {

		/* get l2p table offset of the channel */
		addr = l2p_off + (i * sizeof(uint32_t));

		if (ifc_mcdma_acquire_lock(mcdma_dev, 0))
			return -1;

		/* release 1st entry entry */
		reg = ifc_readl(mcdma_dev->qcsr + addr);

		/* Release lock */
		ifc_mcdma_release_lock(mcdma_dev);

		ph_chno = reg & 0xFFFF;
		lch = i * 2;
		device_mask = ifc_mcdma_get_fcoi(mcdma_dev, ph_chno);
		if (device_mask == fcoi_dev_mask) {
        		ifc_mcdma_release_channel(mcdma_dev, lch, ph_chno);
                	PMD_DRV_LOG(ERR, "Released lch:%u pch:%u", lch, ph_chno);
		} else {
                	PMD_DRV_LOG(DEBUG, "Skipping Release lch:%u pch:%u devicemask:0x%x focimask:0x%x",
				lch, ph_chno, device_mask, fcoi_dev_mask);
		}

		/* release 2nd entry entry */
		ph_chno = ((reg & 0xFFFF0000) >> 16);
		lch = (i * 2) + 1;
		device_mask = ifc_mcdma_get_fcoi(mcdma_dev, ph_chno);
		if (device_mask == fcoi_dev_mask) {
        		ifc_mcdma_release_channel(mcdma_dev, lch, ph_chno);
                	PMD_DRV_LOG(ERR, "Released lch:%u pch:%u", lch, ph_chno);
		} else {
                	PMD_DRV_LOG(DEBUG, "Skipping Release lch:%u pch:%u devicemask:0x%x focimask:0x%x",
				lch, ph_chno, device_mask, fcoi_dev_mask);
		}
	}

	/* Dump debug stats if enabled */
	ifc_mcdma_dump_tables(mcdma_dev);

	return 0;
}

#ifdef DEBUG_DCA
int ifc_mcdma_reset_tables( __attribute__((unused)) struct ifc_mcdma_device *mcdma_dev)
{
        uint32_t addr;
        uint32_t l2p_off;

        int coi_len = NUM_MAX_CHANNEL/32;
        int i = 0;


        if (ifc_mcdma_acquire_lock(mcdma_dev, 0)) {
                /* dump of FCOI table for debug */
                PMD_DRV_LOG(ERR,
                             "could not acquire lock to reset tables\n");
                return 0;
        }

        /* dump of FCOI table for debug */
        PMD_DRV_LOG(DEBUG, "Resetting FCOI table\n");
        for (i = 0; i < 64; i++) {
                /* Read COI table */
                addr = FCOI_BASE + (i * sizeof(uint32_t));
                ifc_writel(mcdma_dev->qcsr + addr, 0);
        }

        /* dump of COI table for debug */
        PMD_DRV_LOG(DEBUG, "Resetting COI table\n");
        for (i = 0; i < coi_len; i++) {
                /* Read COI table */
                addr = COI_BASE + (i * sizeof(uint32_t));
                ifc_writel(mcdma_dev->qcsr + addr, 0);
        }

        /* Get L2P table offset of the device */
        if (mcdma_dev->is_pf)
                l2p_off = ifc_mcdma_get_l2p_pf_base(mcdma_dev->pf);
        else
                l2p_off = ifc_mcdma_get_l2p_vf_base(mcdma_dev->pf,
						    mcdma_dev->vf);

        /* dump of COI table for debug */
        PMD_DRV_LOG(DEBUG, "Resetting L2P table\n");
        for (i = 0; i < L2P_TABLE_SIZE/4 ; i++) {
                /* Read COI table */
                addr = l2p_off + (i * sizeof(uint32_t));
                ifc_writel(mcdma_dev->qcsr + addr, 0);
        }

        /* Release lock */
        ifc_mcdma_release_lock(mcdma_dev);
        return 0;
}

static void ifc_mcdma_dump_tables(struct ifc_mcdma_device *mcdma_dev)
{
        uint32_t reg;
        uint32_t addr;
        uint32_t l2p_off;
        int coi_len = NUM_MAX_CHANNEL/32;
        int i = 0;

        if (ifc_mcdma_acquire_lock(mcdma_dev, 0)) {
                /* dump of FCOI table for debug */
                PMD_DRV_LOG(ERR,
                             "could not acquire lock to reset tables\n");
                return;
        }

        /* dump of FCOI table for debug */
        PMD_DRV_LOG(ERR, "FCOI table dump\n");
        for (i = 0; i < 64; i++) {
                /* Read COI table */
                addr = FCOI_BASE + (i * sizeof(uint32_t));
                reg = ifc_readll(mcdma_dev->qcsr + addr);
                if (reg)
                        PMD_DRV_LOG(ERR,
                                     "0x%x %u:0x%x\n", addr, i, reg);
        }

        /* dump of COI table for debug */
        PMD_DRV_LOG(ERR, "COI table dump\n");
        for (i = 0; i < coi_len; i++) {
                /* Read COI table */
                addr = COI_BASE + (i * sizeof(uint32_t));
                reg = ifc_readll(mcdma_dev->qcsr + addr);
                if (reg)
                        PMD_DRV_LOG(ERR,
                             "0x%x %u:0x%x\n", addr, i, reg);
        }

        /* Get L2P table offset of the device */
        if (mcdma_dev->is_pf)
                l2p_off = ifc_mcdma_get_l2p_pf_base(mcdma_dev->pf);
        else
                l2p_off = ifc_mcdma_get_l2p_vf_base(mcdma_dev->pf,
						    mcdma_dev->vf);

        /* dump of COI table for debug */
        PMD_DRV_LOG(ERR, "L2P table dump\n");
        for (i = 0; i < L2P_TABLE_SIZE/4 ; i++) {
                /* Read COI table */
                addr = l2p_off + (i * sizeof(uint32_t));
                reg = ifc_readll(mcdma_dev->qcsr + addr);
                if (reg)
                        PMD_DRV_LOG(ERR,
                             "0x%x %u:0x%x\n", addr, i, reg);
        }

        /* Release lock */
        ifc_mcdma_release_lock(mcdma_dev);
}
#endif
#endif
