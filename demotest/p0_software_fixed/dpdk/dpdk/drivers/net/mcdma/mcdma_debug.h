/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#ifndef _MCDMA_DEBUG_H_
#define _MCDMA_DEBUG_H_

int ifc_mcdma_dump_config(struct ifc_mcdma_device *qd);

int ifc_mcdma_dump_chnl_qcsr(int portno, int qid, int dir);
#endif /* _MCDMA_DEBUG_H_ */
