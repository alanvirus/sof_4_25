/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#ifndef _MCDMA_DCA_H_
#define _MCDMA_DCA_H_

int ifc_mcdma_get_device_info(struct ifc_mcdma_device *mcdma_dev);

int ifc_mcdma_reset_tables( __attribute__((unused)) struct ifc_mcdma_device *mcdma_dev);

int ifc_mcdma_get_avail_channel_count(struct ifc_mcdma_device *mcdma_dev);

int ifc_mcdma_acquire_channel(struct ifc_mcdma_device *mcdma_dev,
			      int lcn);

int ifc_mcdma_queue_stop(struct rte_eth_dev *dev, uint16_t lch);

#ifdef IFC_QDMA_DYN_CHAN
#ifdef IFC_QDMA_TELEMETRY
int ifc_mcdma_release_channel(struct ifc_mcdma_device *mcdma_dev,int lch, int pch);
int ifc_mcdma_get_l2p_vf_base(uint32_t pf, uint32_t vf);
int ifc_mcdma_get_fcoi(struct ifc_mcdma_device *dev, uint32_t pch);
int ifc_mcdma_set_fcoi(struct ifc_mcdma_device *dev, uint32_t ch, uint32_t device_mask);
int ifc_mcdma_get_l2p_pf_base(uint32_t pf);
int ifc_mcdma_acquire_channel_pf(struct ifc_mcdma_device *mcdma_dev,
                              int lcn, uint32_t pch);
int ifc_mcdma_release_channel_pf(struct ifc_mcdma_device *mcdma_dev,
                                int lch, int pch, uint32_t pfn, uint32_t vfn);
int ifc_mcdma_update_l2p_pf(struct ifc_mcdma_device *dev, uint32_t ch,
                                 __attribute__((unused)) uint32_t device_id_mask, uint32_t pfn, uint32_t vfn);
int ifc_mcdma_release_all_channels_pf(uint16_t port_id, uint32_t ph_chn);
int ifc_mcdma_add_channel_vf(struct ifc_mcdma_device *mcdma_dev,
                                int lch, int pch, uint32_t pfn, uint32_t vfn, uint32_t device_mask);
#endif
#endif
#endif
