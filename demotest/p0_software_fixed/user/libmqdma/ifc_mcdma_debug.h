// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _IFC_MCDMA_DEBUG_H_
#define _IFC_MCDMA_DEBUG_H_

#include <mcdma_ip_params.h>
#include <ifc_libmqdma.h>
#include <ifc_env_config.h>
#include <ifc_env.h>
#ifndef IFC_MCDMA_EXTERNL_DESC
#include <regs/qdma_regs_2_registers.h>
#else
#include <regs/qdma_ext_regs_2_registers.h>
#endif

void qdma_hexdump(FILE *f, unsigned char *base, int len);

#ifdef DEBUG
int ifc_qdma_dump_config(struct ifc_qdma_device *qd);
int ifc_qdma_dump_chnl_qcsr(struct ifc_qdma_device *dev,
			    struct ifc_qdma_channel *chnl,
			    int dir);
#else
#define ifc_qdma_dump_config(a)			do {} while (0)
#define ifc_qdma_dump_chnl_qcsr(a, b, c)	do {} while (0)
#endif

#endif /* _IFC_MCDMA_DEBUG_H_ */
