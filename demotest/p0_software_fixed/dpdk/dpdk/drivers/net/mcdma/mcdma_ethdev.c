/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#include <stdint.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <rte_memzone.h>
#include <rte_string_fns.h>
#ifndef DPDK_21_11_RC2
#include <rte_ethdev_pci.h>
#else
#include <ethdev_pci.h>
#endif
#include <rte_malloc.h>
#include <rte_dev.h>
#include <rte_pci.h>
#include <rte_ethdev.h>
#include <rte_alarm.h>
#include <rte_cycles.h>
#include <unistd.h>
#include <string.h>
#include <rte_ether.h>
#include <net/ethernet.h>
#include <rte_vfio.h>
#include <sys/ioctl.h>
#include <sys/eventfd.h>
#include <mcdma_ip_params.h>
#include <pio_reg_registers.h>
#include "mcdma.h"
#include "rte_pmd_mcdma.h"
#include "mcdma_dca.h"


#ifdef IFC_QDMA_DYN_CHAN
#ifdef IFC_QDMA_TELEMETRY
#include "mcdma_platform.h"
#include "dynamic_channel_params.h"
/* Scratch pad registrs where VF or PF can query  for the resetting the stuck VF.
 * If  VF triggered the queue reset it will make 0 and same for PF */
#define MCDMA_SCRATCHPAD_H2D   0x200308
#define MCDMA_SCRATCHPAD_D2H   0x20030C
#define MCDMA_ERROR_D2H        0x200304
#define MCDMA_ERROR_H2D        0x200300
#define MCDAM_VF_ACTIVE_BIT    0x4000
uint64_t *h2dwb = NULL;
uint64_t *d2hwb = NULL;
#endif
#endif

/* Poll for QDMA errors every 1 second */
#define QDMA_ERROR_POLL_FRQ (1000000)

static struct rte_pci_id mcdma_pci_id_tbl[] = {
#define RTE_PCI_DEV_ID_DECL_XNIC(vend, dev) {RTE_PCI_DEVICE(vend, dev)},
#ifndef PCI_VENDOR_ID_INTEL
#define PCI_VENDOR_ID_INTEL 0x1172
#endif
	RTE_PCI_DEV_ID_DECL_XNIC(PCI_VENDOR_ID_INTEL, 0)
	{ .vendor_id = 0,},
};

#ifdef IFC_QDMA_DYN_CHAN
#ifdef IFC_QDMA_TELEMETRY
/* Poll for any mcdma global write back errors */
void ifc_mcdma_check_errors(void *arg)
{
       struct rte_eth_dev *dev = ((struct rte_eth_dev *)arg);
       int ret ;
       uint32_t reg_data;
       int d2h_map, h2d_map;
       if((*d2hwb & ERROR_DESC_FETCH_MASK) | ( *d2hwb & ERROR_DATA_FETCH_MASK))
       {
               ret = ifc_mcdma_pf_logger_dump(dev);
               if(ret)
               {
                       printf("Successfully dumped\n");
               }
               ifc_mcdma_reg_write(dev, MCDMA_SCRATCHPAD_D2H, 1UL);
               reg_data = ifc_mcdma_poll_scratch_reg(dev, MCDMA_SCRATCHPAD_D2H, 0);
               if(reg_data != 0)
               {
                       printf("D2H : VF is inactive\n");
                       d2h_map = ifc_mcdma_device_err_dca_pf(dev);
                       if(d2h_map < 0)
                       {
                               printf("D2H: VF inactive post operation failed\n");
                       }
                       ifc_mcdma_reg_write(dev, MCDMA_SCRATCHPAD_D2H, 0UL);
               }
               ifc_mcdma_reg_write(dev, MCDMA_ERROR_D2H, 0UL);
               *d2hwb = 0;
       }
       if((*h2dwb & ERROR_DESC_FETCH_MASK) | (*h2dwb & ERROR_DATA_FETCH_MASK))
       {
               ret = ifc_mcdma_pf_logger_dump(dev);
               if(ret)
               {
                       printf("Successfully dumped \n");
               }
               ifc_mcdma_reg_write(dev, MCDMA_SCRATCHPAD_H2D, 1UL);
               reg_data = ifc_mcdma_poll_scratch_reg(dev, MCDMA_SCRATCHPAD_H2D, 0);
               if(reg_data != 0)
               {
                       printf("H2D : VF is inactive\n");
                       h2d_map = ifc_mcdma_host_err_dca_pf(dev);
                       if(h2d_map < 0)
                       {
                               printf("H2D: VF inactive post operation failed\n");
                       }
                               ifc_mcdma_reg_write(dev, MCDMA_SCRATCHPAD_H2D, 0UL);

               }
               ifc_mcdma_reg_write(dev, MCDMA_ERROR_H2D, 0UL);
               *h2dwb = 0;
       }
       rte_eal_alarm_set(QDMA_ERROR_POLL_FRQ, ifc_mcdma_check_errors, arg);
}

int ifc_mcdma_device_err_dca_pf(void *arg)
{
       struct rte_eth_dev *dev = ((struct rte_eth_dev *)arg);
       struct ifc_mcdma_device *mcdma_dev;
       mcdma_dev = (struct ifc_mcdma_device *)dev->data->dev_private;
       uint32_t l2p_base_table_vf = 0;
       uint32_t phy_chnl;
       uint32_t device_mask;
       int i ;
       uint32_t vfnumber;
       int add_to_vf;
       int add_to_pf;
       uint32_t l2p_offb;
       uint32_t regb;
       uint32_t addrb;
       int pflchh = 0;
       uint32_t l2p_base_table_pf;
       uint32_t vflch = 0;
       uint32_t pflch = 0;
       int ret = 0 ;
       int no_remove ;
       int is_vf_active;
       uint32_t pf_l2p_mask;
       struct ifc_mcdma_queue *rxq;
       phy_chnl = ifc_mcdma_reg_read(dev, MCDMA_ERROR_D2H);
       phy_chnl = (phy_chnl & 0x7FF0000) >> 16;
       printf("Physical channel which got stuck = 0x%x\n", phy_chnl);
       uint32_t vf_l2p_mask = 0;
       device_mask = ifc_mcdma_get_fcoi(mcdma_dev, phy_chnl);
       vfnumber = device_mask & 0X00007FF ;
       printf("VF which got stuck = 0x%x\n", vfnumber);
       /* Get the pf from 12 to 14 bit */
       uint32_t pf = (device_mask >> 11) & 0x7;
       if(device_mask & 0x4000)
       {
               is_vf_active = 1;
               printf("VF is active and active set bit value = %d\n", is_vf_active);
       }
       else
       {
               is_vf_active = 0;
               l2p_base_table_pf = ifc_mcdma_get_l2p_pf_base(pf);
               for(i = 0; i < 256/2; i++)
               {
                       pf_l2p_mask = ifc_readl(mcdma_dev->qcsr + l2p_base_table_pf);
                       l2p_base_table_pf += 4;
                       if ((pf_l2p_mask &  0XFFFF) == phy_chnl )
                       {
                               break;
                       }
                       pflch++;
                       if(((vf_l2p_mask &  0XFFFF0000) >> 16) == phy_chnl)
                       {
                               break;
                       }
                       pflch++;
               }
               rxq = (struct ifc_mcdma_queue *)dev->data->rx_queues[pflch];
               ret = ifc_mcdma_reset_queue(rxq);
               if(ret < 0) {
                       PMD_DRV_LOG(ERR, "RX Queue reset failed\n");
                       ret = -1;
               }
               else {
                       PMD_DRV_LOG(ERR, "RX Queue reset success\n");
                       ret = 1;
               }
               return ret;
       }
       l2p_base_table_vf = ifc_mcdma_get_l2p_vf_base(pf, vfnumber);
       for(i = 0; i < 256/2; i++)
       {
               vf_l2p_mask = ifc_readl(mcdma_dev->qcsr + l2p_base_table_vf);
               l2p_base_table_vf += 4;
               if ((vf_l2p_mask &  0XFFFF) == phy_chnl )
               {
                       break;
               }
               vflch++;
               if(((vf_l2p_mask &  0XFFFF0000) >> 16) == phy_chnl)
               {
                       break;
               }
               vflch++;
       }
       /* Check the last lch having pch in L2P PF table */
       l2p_offb = ifc_mcdma_get_l2p_pf_base(0);
       for (i = 0; i < L2P_TABLE_SIZE/4 ; i++) {
               addrb = l2p_offb + (i * sizeof(uint32_t));
               regb = ifc_readll(mcdma_dev->qcsr + addrb);
               if (regb & 0XFFFF)
               {
                       pflchh++;
               }
               if((regb &  0XFFFF0000) >> 16)
               {
                       pflchh++;
               }
       }
       /*Release Physical channel from the VF L2P table */
       if(ifc_mcdma_release_channel_pf(mcdma_dev,vflch, phy_chnl, pf, vfnumber))
       {
               no_remove = -1;
               printf("Channel not removed %d\n", no_remove);
       }
       /* Add the channel to PF */
       pflchh = pflchh + 1;
       add_to_pf = ifc_mcdma_acquire_channel_pf(mcdma_dev, pflchh, phy_chnl);
       if(add_to_pf)
       {
               printf("Channel added to pf\n");
       }

       /* Issue queue reset using logical channel */
       /*Setup Rx details*/
       rxq = rte_zmalloc("QDMA_RxQ", sizeof(struct ifc_mcdma_queue),
                       RTE_CACHE_LINE_SIZE);
       if(!rxq) {
               PMD_DRV_LOG(ERR,
                               "Unable to allocate structure rxq of size %d\n",
                               (int)(sizeof(struct ifc_mcdma_queue)));
               ret = -1;
       }
       rxq->qcsr = mcdma_dev->bar_addr[0] + (pflchh * 256);
       rxq->qid = pflchh;
       dev->data->rx_queues[pflchh] = rxq;
       if (mcdma_dev->channel_context[pflchh].valid == 0)
       {
               rxq->ph_chno = phy_chnl;
       }
       else
       {
               rxq->ph_chno = phy_chnl;
       }

       rxq = (struct ifc_mcdma_queue *)dev->data->rx_queues[pflchh];
       ret = ifc_mcdma_reset_queue(rxq);
       if(ret < 0) {
               PMD_DRV_LOG(ERR, "RX Queue reset failed\n");
               ret = -1;
       }
       /* Release channel from the PF */
       ifc_mcdma_release_all_channels_pf(pf, phy_chnl);
       /* Add the channel back to vf */
       add_to_vf = ifc_mcdma_add_channel_vf(mcdma_dev, vflch, phy_chnl, pf, vfnumber, device_mask);
       if(add_to_vf < 0)
       {
               printf("Channel back to assign to VF failed %d\n", add_to_vf);
               ret  = -1;
       }
       if(rxq) {
               rte_free(rxq);
       }
       return ret ;
}

int ifc_mcdma_host_err_dca_pf(void *arg)
{
       struct rte_eth_dev *dev = ((struct rte_eth_dev *)arg);
       struct ifc_mcdma_device *mcdma_dev;
       mcdma_dev = (struct ifc_mcdma_device *)dev->data->dev_private;
       uint32_t l2p_base_table_vf = 0;
       uint32_t phy_chnl;
       uint32_t device_mask;
       uint32_t pf_l2p_mask;
       int i ;
       uint32_t vfnumber;
       int add_to_vf;
       int add_to_pf;
       uint32_t l2p_base_table_pf;
       uint32_t vflch = 0;
       uint32_t pflch = 0;
       int ret = 0 ;
       struct ifc_mcdma_queue *txq;
       int is_vf_active;
       int no_remove ;
       uint32_t l2p_offb;
       uint32_t regb;
       uint32_t addrb;
       int pflchh = 0;
       phy_chnl = ifc_mcdma_reg_read(dev, MCDMA_ERROR_H2D);
       phy_chnl = (phy_chnl & 0x7FF0000) >> 16;
       printf("Physical channel which got stuck = 0x%x\n", phy_chnl);
       uint32_t vf_l2p_mask = 0;
       device_mask = ifc_mcdma_get_fcoi(mcdma_dev, phy_chnl);
       vfnumber = device_mask & 0X00007FF ;
       printf("VF which got stuck = 0x%x\n", vfnumber);
       /* Get the pf from 12 to 14 bit */
       uint32_t pf = (device_mask >> 11) & 0x7;
       if(device_mask & MCDAM_VF_ACTIVE_BIT)
       {
               is_vf_active = 1;
               printf("VF is active and active set bit value = %d\n", is_vf_active);
       }
       else
       {
               is_vf_active = 0;
               l2p_base_table_pf = ifc_mcdma_get_l2p_pf_base(pf);
	       /* Number of entries in the  table */
               for(i = 0; i < 256/2; i++)
               {
                       pf_l2p_mask = ifc_readl(mcdma_dev->qcsr + l2p_base_table_pf);
                       l2p_base_table_pf += 4;
                       if ((pf_l2p_mask &  0XFFFF) == phy_chnl )
                       {
                               break;
                       }
                       pflch++;
                       if(((vf_l2p_mask &  0XFFFF0000) >> 16) == phy_chnl)
                       {
                               break;
                       }
                       pflch++;
               }
               txq = (struct ifc_mcdma_queue *)dev->data->tx_queues[pflch];
               ret = ifc_mcdma_reset_queue(txq);
               if(ret < 0) {
                       PMD_DRV_LOG(ERR, "RX Queue reset failed\n");
                       ret = -1;
               }
               else {
                       PMD_DRV_LOG(ERR, "RX Queue reset success\n");
                       ret = 1;
               }
               return ret;
       }
       l2p_base_table_vf = ifc_mcdma_get_l2p_vf_base(pf, vfnumber);
       for(i = 0; i < 256/2; i++)
       {
               vf_l2p_mask = ifc_readl(mcdma_dev->qcsr + l2p_base_table_vf);
               l2p_base_table_vf += 4;
               if ((vf_l2p_mask &  0XFFFF) == phy_chnl )
               {
                       break;
               }
               vflch++;
               if(((vf_l2p_mask &  0XFFFF0000) >> 16) == phy_chnl)
               {
                       break;
               }
               vflch++;
       }
       /* Check the last lch having pch in L2P PF table */
       l2p_offb = ifc_mcdma_get_l2p_pf_base(0);
       for (i = 0; i < L2P_TABLE_SIZE/4 ; i++) {
               addrb = l2p_offb + (i * sizeof(uint32_t));
               regb = ifc_readll(mcdma_dev->qcsr + addrb);
               if (regb & 0XFFFF)
               {
                       pflchh++;
               }
               if((regb &  0XFFFF0000) >> 16)
               {
                       pflchh++;
               }
       }
       /*Release Physical channel from the VF L2P table*/
       if(ifc_mcdma_release_channel_pf(mcdma_dev,vflch, phy_chnl, pf, vfnumber))
       {
               no_remove = -1;
               printf("Channel not removed %d\n", no_remove);
       }
       /* Add the channel to PF */
       pflchh = pflchh + 1;
       add_to_pf = ifc_mcdma_acquire_channel_pf(mcdma_dev, pflchh, phy_chnl);
       if(add_to_pf)
       {
               printf("Channel added to pf%d\n", add_to_pf);
       }
       /* Issue queue reset using logical channel */
       /*Setup Rx details*/
       txq = rte_zmalloc("QDMA_TxQ", sizeof(struct ifc_mcdma_queue),
                       RTE_CACHE_LINE_SIZE);
       if(!txq) {
               PMD_DRV_LOG(ERR,
                               "Unable to allocate structure txq of size %d\n",
                               (int)(sizeof(struct ifc_mcdma_queue)));
               ret = -1;

       }
 /* 
 *	Get the qcsr address of the particulat channel the calculation is referenced from the 
 * 	function ifc_mcdma_dev_tx_queue_setup of file mcdma_devops.c 
 */
       txq->qcsr = mcdma_dev->bar_addr[0] + (512 << 10) + (pflchh * 256);
       txq->qid = pflchh;
       dev->data->tx_queues[pflchh] = txq;
       if (mcdma_dev->channel_context[pflchh].valid == 0)
       {
               txq->ph_chno = phy_chnl;
       }
       else
       {
               txq->ph_chno = phy_chnl;
       }
       txq = (struct ifc_mcdma_queue *)dev->data->tx_queues[pflchh];
       ret = ifc_mcdma_reset_queue(txq);
       if(ret < 0) {
               PMD_DRV_LOG(ERR, "TX Queue reset failed\n");
               ret = -1;
       }

       /* Release channel from the PF */
       ifc_mcdma_release_all_channels_pf(pf, phy_chnl);
       /* Assign back the channel to VF */
       add_to_vf = ifc_mcdma_add_channel_vf(mcdma_dev, vflch, phy_chnl, pf, vfnumber, device_mask);
       if(add_to_vf < 0)
       {
               printf("Channel back to assign to VF failed\n");
       }

       if(txq){
               rte_free(txq);
       }
       return ret ;
}

int ifc_mcdma_pf_logger_dump(void *dev_hndl)
{
       int i;
       uint32_t mcdma_error_reg_addr = IFC_MCDMA_ERROR_SPACE;
       uint32_t df_logger_reg_addr = IFC_MCDMA_DF_LOGGER_SPACE;
        uint32_t h2d_logger_reg_addr = IFC_MCDMA_H2D_LOGGER;
        uint32_t d2h_logger_reg_addr = IFC_MCDMA_D2H_LOGGER;
        uint32_t sh_logger_reg_addr = IFC_MCDMA_SH_LOGGER;
        /*MCDMA error space dump*/
        for(i = 0; i < 12; i++)
        {
                printf("mcdma_error_reg_addr : offset = 0x%x value = 0x%x\n",mcdma_error_reg_addr, ifc_mcdma_reg_read(dev_hndl, mcdma_error_reg_addr));
                mcdma_error_reg_addr  = mcdma_error_reg_addr + 0x4;
        }
        /* MCDMA df logger space dump*/
        for(i = 0; i < 24; i++)
        {
                printf("df_logger_reg_addr : offset = 0x%x value = 0x%x\n",df_logger_reg_addr, ifc_mcdma_reg_read(dev_hndl, df_logger_reg_addr));
                df_logger_reg_addr  = df_logger_reg_addr + 0x4;
        }
        /*MCDMA h2d logger space dump */
        for(i = 0; i < 24; i++)
        {
                printf("h2d_logger_reg_addr : offset = 0x%x value = 0x%x\n",h2d_logger_reg_addr, ifc_mcdma_reg_read(dev_hndl, h2d_logger_reg_addr));
                h2d_logger_reg_addr  = h2d_logger_reg_addr + 0x4;
        }
        /* MCDMA d2h logger space dump */
        for(i = 0; i < 64; i++)
        {
                printf("d2h_logger_reg_addr : offset = 0x%x value = 0x%x\n",d2h_logger_reg_addr, ifc_mcdma_reg_read(dev_hndl, d2h_logger_reg_addr));
               d2h_logger_reg_addr = d2h_logger_reg_addr + 0x4;
        }
        /* MCDMA sh logger space dump */
       for(i = 0; i < 24; i++)
        {
                printf("sh_logger_reg_addr : offset = 0x%x value = 0x%x\n",sh_logger_reg_addr, ifc_mcdma_reg_read(dev_hndl, sh_logger_reg_addr));
                sh_logger_reg_addr = sh_logger_reg_addr + 0x4;
        }

        return QDMA_SUCCESS;
}
#endif
#endif


static void ifc_mcdma_get_device_caps(struct rte_eth_dev *dev)
{
	struct ifc_mcdma_device *mcdma_dev;
	struct mcdma_dev_cap mcdma_attrib;

	mcdma_dev = (struct ifc_mcdma_device *)dev->data->dev_private;
	ifc_mcdma_get_device_capabilities(dev, &mcdma_attrib);

	mcdma_dev->ipcap.num_chan = mcdma_attrib.num_chan;
	/* Check DPDK configured queues per port */
	if ((mcdma_dev->ipcap.num_chan * 2) > RTE_MAX_QUEUES_PER_PORT)
		PMD_DRV_LOG(INFO, "Max Supported by DPDK: %u",
			    RTE_MAX_QUEUES_PER_PORT);
}

#ifndef UIO_SUPPORT
#ifndef DPDK_21_11_RC2
static int
ifc_mcdma_intr_efd_enable(struct rte_intr_handle *intr_handle,
			 int vfio_msix_count)
{
	int i;
	int fd;

#ifdef DPDK_21_11_RC2
	intr_handle->efds = rte_zmalloc("msix_FD_info", (sizeof(int) * IFC_MAX_RXTX_INTR_VEC_ID),
                       RTE_CACHE_LINE_SIZE);
#endif
	for (i = 0; i < vfio_msix_count; i++) {
		fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
		if (fd < 0) {
			return -errno;
		}
		intr_handle->efds[i] = fd;
	}
	return 0;
}

static int
ifc_pci_vfio_get_num_intr(int vfio_dev_fd)
{
	int rc;
	struct vfio_irq_info msix_irq_info = { .argsz = sizeof(msix_irq_info) };

	/* populate IRQ vector index */
	msix_irq_info.index = VFIO_PCI_MSIX_IRQ_INDEX;

	/* Get MSIX info */
	rc = ioctl(vfio_dev_fd, VFIO_DEVICE_GET_IRQ_INFO, &msix_irq_info);
	if (rc < 0) {
		return -1;
	}
	return msix_irq_info.count;
}

int
ifc_mcdma_vfio_enable_msix(struct rte_pci_device *pci_dev, uint32_t num_intr)
{
	int len, ret = 0;
	char irq_set_buf[IFC_QDMA_IRQ_SET_BUF_LEN];
	struct vfio_irq_set *irq_set;
	uint32_t vfio_msix_count;
	int *fd_ptr;

	len = sizeof(irq_set_buf);

#ifndef DPDK_21_11_RC2
	vfio_msix_count = ifc_pci_vfio_get_num_intr(
		pci_dev->intr_handle.vfio_dev_fd);
#else
	vfio_msix_count = ifc_pci_vfio_get_num_intr(
		pci_dev->intr_handle->dev_fd);
#endif

	/* create event fds */
	ret = ifc_mcdma_intr_efd_enable(&pci_dev->intr_handle, vfio_msix_count);
	if (ret) {
		PMD_DRV_LOG(ERR, "Error while enablig eventfds ret:%d\n", ret);
		return -1;
	}
	if (vfio_msix_count < num_intr) {
		PMD_DRV_LOG(ERR, "No interrupts support: supported%u req:%u\n",
				vfio_msix_count, num_intr);
		return -1;
	}

	/* populate IRQ info */
	irq_set = (struct vfio_irq_set *) irq_set_buf;
	irq_set->argsz = len;
	irq_set->count = num_intr;
	irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD |
		VFIO_IRQ_SET_ACTION_TRIGGER;
	irq_set->index = VFIO_PCI_MSIX_IRQ_INDEX;
	irq_set->start = 0;

#ifndef DPDK_21_11_RC2
	/* populate event FDs */
	fd_ptr = (int *) &irq_set->data;
	memcpy(fd_ptr + 0, pci_dev->intr_handle.efds,
		sizeof(int) * (IFC_MAX_RXTX_INTR_VEC_ID));

	/* Register interrupts with VFIO */
	ret = ioctl(pci_dev->intr_handle.vfio_dev_fd,
		VFIO_DEVICE_SET_IRQS, irq_set);
#else
	/* populate event FDs */
	fd_ptr = (int *) &irq_set->data;
	memcpy(fd_ptr + 0, pci_dev->intr_handle->efds,
		sizeof(int) * (IFC_MAX_RXTX_INTR_VEC_ID));

	/* Register interrupts with VFIO */
	ret = ioctl(pci_dev->intr_handle->dev_fd,
		VFIO_DEVICE_SET_IRQS, irq_set);
#endif
	if (ret) {
		PMD_DRV_LOG(ERR,"Error enabling interrupts %s %d\n",
			strerror(errno), ret);
		return -1;
	}
	return 0;
}
#else /*MSIX support for ubuntu platform only*/

static int
irq_get_info(struct rte_intr_handle *intr_handle)
{
	struct vfio_irq_info irq = { .argsz = sizeof(irq) };
	int rc, vfio_dev_fd;

	irq.index = VFIO_PCI_MSIX_IRQ_INDEX;

	vfio_dev_fd = rte_intr_dev_fd_get(intr_handle);
	
	if(vfio_dev_fd < 0)
         {
		return vfio_dev_fd;
	 }	

	rc = ioctl(vfio_dev_fd, VFIO_DEVICE_GET_IRQ_INFO, &irq);

	if (rc < 0)
		return rc;

	if (irq.count > RTE_MAX_RXTX_INTR_VEC_ID) {
		if (rte_intr_max_intr_set(intr_handle, RTE_MAX_RXTX_INTR_VEC_ID))
			return -1;
	} else {
		if (rte_intr_max_intr_set(intr_handle, irq.count))
			return -1;
	}

	return 0;
}

static int
irq_init(struct rte_intr_handle *intr_handle)
{
	char irq_set_buf[IFC_QDMA_IRQ_SET_BUF_LEN];
        struct vfio_irq_set *irq_set;
        int len, rc, vfio_dev_fd;
        int32_t *fd_ptr;
        uint32_t i;

        if (rte_intr_max_intr_get(intr_handle) > RTE_MAX_RXTX_INTR_VEC_ID) {
                return -ERANGE;
        }

	len = sizeof(struct vfio_irq_set) +
			sizeof(int32_t) * rte_intr_max_intr_get(intr_handle);

        irq_set = (struct vfio_irq_set *)irq_set_buf;
        irq_set->argsz = len;
        irq_set->start = 0;
        irq_set->count = rte_intr_max_intr_get(intr_handle);
        irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
        irq_set->index = VFIO_PCI_MSIX_IRQ_INDEX;

        fd_ptr = (int32_t *)&irq_set->data[0];
	if(fd_ptr != NULL)
	{	
		for (i = 0; i < irq_set->count; i++){
                	fd_ptr[i] = -1;
		}
        }
	vfio_dev_fd = rte_intr_dev_fd_get(intr_handle);
        if(vfio_dev_fd < 0)
	{
		return vfio_dev_fd;
	}
	rc = ioctl(vfio_dev_fd, VFIO_DEVICE_SET_IRQS, irq_set);

        return rc;
}

static int
irq_config(struct rte_intr_handle *intr_handle, unsigned int vector)
{
        char irq_set_buf[IFC_QDMA_IRQ_SET_BUF_LEN];
        struct vfio_irq_set *irq_set;
        int len, rc, vfio_dev_fd;
        int32_t *fd_ptr;

        len = sizeof(struct vfio_irq_set) + sizeof(int32_t);
        irq_set = (struct vfio_irq_set *)irq_set_buf;
        irq_set->argsz = len;

        irq_set->start = vector;
        irq_set->count = 1;
        irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
        irq_set->index = VFIO_PCI_MSIX_IRQ_INDEX;

	/* Set interrupt vectors from vector fd */
        fd_ptr = (int32_t *)&irq_set->data[0];
	
	if(fd_ptr != NULL ){
        	fd_ptr[0] = rte_intr_efds_index_get(intr_handle, vector);
	}
        vfio_dev_fd = rte_intr_dev_fd_get(intr_handle);
	if( vfio_dev_fd < 0)
	{
		return vfio_dev_fd;
	}
        rc = ioctl(vfio_dev_fd, VFIO_DEVICE_SET_IRQS, irq_set);

        return rc;
}

int
ifc_mcdma_vfio_enable_msix(struct rte_pci_device *pci_dev, uint32_t num_intr)
{
        struct rte_intr_handle *intr_handle = pci_dev->intr_handle;
	uint32_t vfio_msix_max_cnt = 0, vector;
	int ret = 0, fd;

	if(intr_handle == NULL)
	{
		PMD_DRV_LOG(ERR," Invalid rte intr ptr handler\n");
		return -1;
	}
	if (rte_intr_max_intr_get(pci_dev->intr_handle) == 0) {
		irq_get_info(pci_dev->intr_handle);
		ret = irq_init(pci_dev->intr_handle);
	}

	vfio_msix_max_cnt = rte_intr_max_intr_get(intr_handle);

	if (num_intr > vfio_msix_max_cnt) {
		PMD_DRV_LOG(ERR," Invalid interrupt count %d, max allowed %d\n", num_intr,
			    vfio_msix_max_cnt);
                return -ERANGE;
	}

	for (vector = 0; vector < num_intr; vector++) {

		/* Create new eventfd for vector */
		fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
		if (fd == -1)
			return -ENODEV;

		if (rte_intr_fd_set(intr_handle, fd))
			return errno;

		rte_intr_efds_index_set(intr_handle, vector, fd);
		rte_intr_nb_efd_set(intr_handle, vector + 1);

		ret = irq_config(intr_handle, vector);
		if (ret) {
			PMD_DRV_LOG(ERR,"config  %d Error enabling interrupts %s %d\n",
					vector, strerror(errno), ret);
			return -1;
		}
	}

	return 0;
}
#endif /*end of ubuntu platform for MSIX*/
#endif

static int eth_mcdma_dev_init(struct rte_eth_dev *dev)
{
	struct ifc_mcdma_device *dma_priv;
	uint8_t *baseaddr;
	int idx;
	struct rte_pci_device *pci_dev;
#ifdef IFC_QDMA_DYN_CHAN
#ifdef IFC_QDMA_TELEMETRY
	void *gcsr;
	uint64_t wb_d2hcons_head;
	uint64_t wb_h2dcons_head;
#endif
#endif

	/* sanity checks */
	if (dev == NULL)
		return -EINVAL;
	if (dev->data == NULL)
		return -EINVAL;
	if (dev->data->dev_private == NULL)
		return -EINVAL;

	pci_dev = RTE_ETH_DEV_TO_PCI(dev);
	if (pci_dev == NULL)
		return -EINVAL;

	dma_priv = (struct ifc_mcdma_device *)dev->data->dev_private;

	/* DMA memory */
	baseaddr = (uint8_t *)pci_dev->mem_resource[IFC_MCDMA_CONFIG_BAR].addr;
	dma_priv->bar_addr[IFC_MCDMA_CONFIG_BAR] = baseaddr;
	dma_priv->qcsr = baseaddr;

	/* PIO memory */
	baseaddr = (uint8_t *)pci_dev->mem_resource[IFC_MCDMA_PIO_BAR].addr;
	dma_priv->bar_addr[IFC_MCDMA_PIO_BAR] = baseaddr;
	
	/*BAS memory*/
	baseaddr = (uint8_t *)pci_dev->mem_resource[IFC_MCDMA_BAS_BAR].addr;
	dma_priv->bar_addr[IFC_MCDMA_BAS_BAR] = baseaddr;
#ifdef UIO_SUPPORT
#ifndef DPDK_21_11_RC2
	dma_priv->uio_fd = pci_dev->intr_handle.fd;
#else
	dma_priv->uio_fd = pci_dev->intr_handle->fd;
#endif
#else
#ifndef DPDK_21_11_RC2
	dma_priv->uio_fd = pci_dev->intr_handle.vfio_dev_fd;
#else
	dma_priv->uio_fd = pci_dev->intr_handle->dev_fd;
#endif
#endif

#ifdef IFC_QDMA_DYN_CHAN
	/* get PF and VF deatils */
	ifc_mcdma_get_device_info(dma_priv);
#ifdef DEBUG_DCA
	//ifc_mcdma_reset_tables(dma_priv);
#endif
#else
	dma_priv->pf = PCI_DEVFN(pci_dev->addr.devid, pci_dev->addr.function);
	dma_priv->is_vf = 0;
	dma_priv->is_master = 0;
#endif

	idx = ifc_mcdma_get_hw_version(dev);
	if (idx < 0) {
		rte_free(dev->data->mac_addrs);
		return -EINVAL;
	}

	ifc_mcdma_dev_ops_init(dev);

	ifc_mcdma_get_device_caps(dev);

	/* Init mutexes */
	pthread_mutex_init(&(dma_priv->lock), NULL);
	pthread_mutex_init(&(dma_priv->tid_lock), NULL);
#ifdef IFC_QDMA_DYN_CHAN
#ifdef IFC_QDMA_TELEMETRY
	ifc_mcdma_get_device_info(dma_priv);
       	if(dma_priv->is_pf == 1) {
               gcsr = dma_priv->bar_addr[IFC_MCDMA_CONFIG_BAR] + 0x200000;
               d2hwb  = rte_zmalloc(NULL, sizeof(uint64_t), 0);
               if(!d2hwb)
               {
                       rte_free(d2hwb);
                       return -1;
               }
#if defined(UIO_SUPPORT) || defined (NO_IOMMU_MODE)
               wb_d2hcons_head = (uint64_t)rte_mem_virt2phy(d2hwb);
#else
               wb_d2hcons_head = (uint64_t)rte_mem_virt2iova(d2hwb);
#endif
               ifc_writel(gcsr + 0x20, wb_d2hcons_head);
               ifc_writel(gcsr + 0x24, wb_d2hcons_head >> 32);
               ifc_writel(gcsr + 0x28, 1);

               /* H2D PF0 WB Address */
               h2dwb  = rte_zmalloc(NULL, sizeof(uint64_t), 0);
               if(!h2dwb)
               {
                       rte_free(h2dwb);
                       return -1;
               }
#if defined(UIO_SUPPORT) || defined (NO_IOMMU_MODE)
               wb_h2dcons_head = (uint64_t)rte_mem_virt2phy(h2dwb);
#else
               wb_h2dcons_head = (uint64_t)rte_mem_virt2iova(h2dwb);
#endif
               ifc_writel(gcsr + 0x10, wb_h2dcons_head);
               ifc_writel(gcsr + 0x14, wb_h2dcons_head >> 32);
               ifc_writel(gcsr + 0x18, 1);
       }
#endif
#endif

	dev->data->mac_addrs = rte_zmalloc("mcdma", ETHER_ADDR_LEN * 1, 0);
	if (dev->data->mac_addrs == NULL)
		return -ENOMEM;

	return 0;
}

static int eth_mcdma_dev_uninit(struct rte_eth_dev *dev __rte_unused)
{
	return 0;
}

static int eth_mcdma_pci_probe(struct rte_pci_driver *pci_drv __rte_unused,
			      struct rte_pci_device *pci_dev)
{
	return rte_eth_dev_pci_generic_probe(pci_dev,
						sizeof(struct ifc_mcdma_device),
						eth_mcdma_dev_init);
}

static int eth_mcdma_pci_remove(struct rte_pci_device *pci_dev)
{
	return rte_eth_dev_pci_generic_remove(pci_dev, eth_mcdma_dev_uninit);
}

static struct rte_pci_driver rte_mcdma_pmd = {
	.id_table = mcdma_pci_id_tbl,
	.drv_flags = RTE_PCI_DRV_NEED_MAPPING,
	.probe = eth_mcdma_pci_probe,
	.remove = eth_mcdma_pci_remove,
};

RTE_PMD_REGISTER_PCI(net_mcdma, rte_mcdma_pmd);
RTE_PMD_REGISTER_PCI_TABLE(net_mcdma, mcdma_pci_id_tbl);
