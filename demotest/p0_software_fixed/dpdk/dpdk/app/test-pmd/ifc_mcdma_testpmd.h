/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#include <rte_pmd_mcdma.h>
#include <pio_reg_registers.h>
#include <mcdma_ip_params.h>
#include <rte_ethdev.h>

/* To configure ED with Max chnl cnt */
#undef IFC_ENABLE_MAX_CH
/* PF & VF channels */
/* Number of PFs */
#define IFC_MCDMA_PFS            4
/* Channels available per PF */
#define IFC_MCDMA_PER_PF_CHNLS   2
/* Channels available per VF */
#define IFC_MCDMA_PER_VF_CHNLS   7
/* Number of VFs per PF */
#define IFC_MCDMA_PER_PF_VFS     2

#define AVST_MAX_NUM_CHAN       256

#define IFC_DEF_FILE_SIZE       1

void ifc_mcdma_pio_cleanup(uint16_t port);
void ifc_single_port_pio_init(uint16_t port_id);
void ifc_multi_port_pio_init(uint16_t portid);
