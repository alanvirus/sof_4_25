/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#ifndef _DYNAMICCH_MCDMA_IP_PARAMS_H_
#define _DYNAMICCH_MCDMA_IP_PARAMS_H_

#ifdef GENHDR_STRUCT
/* D2H debug register 14 */
typedef union {
  struct {
    uint32_t vf : 16;
    uint32_t pf : 3;
    uint32_t vfactive : 1;
  } field;
  uint32_t val;
} QDMA_REGS_PING;
#endif /* GENHDR_STRUCT */

/* Ping Reg: To retried device ID details */
#define QDMA_PING_REG	0x300000

/* Max Number of channels per VF */
#define MAX_NUM_CHAN_PER_FUNC	256

/* lock bit */
#define QDMA_LOCK_REGS_LOCK_SHIFT       0
#define QDMA_LOCK_REGS_LOCK_SHIFT_WIDTH       1

#define QDMA_LOCK_REGS_VF_SHIFT       1
#define QDMA_LOCK_REGS_VF_SHIFT_WIDTH       11

#define QDMA_LOCK_REGS_PF_SHIFT       12
#define QDMA_LOCK_REGS_PF_SHIFT_WIDTH       3

#define QDMA_LOCK_REGS_VFACTIVE_SHIFT		15
#define QDMA_LOCK_REGS_VFACTIVE_SHIFT_WIDTH       1

#define QDMA_LOCK_REGS_NUM_CHAN_SHIFT		16
#define QDMA_LOCK_REGS_NUM_CHAN_SHIFT_WIDTH     16

/* VF number */
#define QDMA_PING_REGS_VF_SHIFT       0
#define QDMA_PING_REGS_VF_SHIFT_WIDTH       16
#define QDMA_PING_REGS_VF_SHIFT_MASK        (0xFFFFU << QDMA_PING_REGS_VF_SHIFT)

/* PF number */
#define QDMA_PING_REGS_PF_SHIFT       16
#define QDMA_PING_REGS_PF_SHIFT_WIDTH       3
#define QDMA_PING_REGS_PF_SHIFT_MASK        (0x7U << QDMA_PING_REGS_PF_SHIFT)

/* VFACTIVE */
#define QDMA_PING_REGS_VFACTIVE_SHIFT		20
#define QDMA_PING_REGS_VFACTIVE_SHIFT_WIDTH       1
#define QDMA_PING_REGS_VFACTIVE_SHIFT_MASK        (0x1U << QDMA_PING_REGS_VFACTIVE_SHIFT)


/* VF number */
#define QDMA_FCOI_REGS_VF_SHIFT       0
#define QDMA_FCOI_REGS_VF_SHIFT_WIDTH       11
#define QDMA_FCOI_REGS_VF_SHIFT_MASK        (0xFFFFU << QDMA_PING_REGS_VF_SHIFT)

/* PF number */
#define QDMA_FCOI_REGS_PF_SHIFT       11
#define QDMA_FCOI_REGS_PF_SHIFT_WIDTH       3
#define QDMA_FCOI_REGS_PF_SHIFT_MASK        (0x7U << QDMA_PING_REGS_PF_SHIFT)

/* VFACTIVE */
#define QDMA_FCOI_REGS_VFACTIVE_SHIFT		14
#define QDMA_FCOI_REGS_VFACTIVE_SHIFT_WIDTH       1
#define QDMA_FCOI_REGS_VFACTIVE_SHIFT_MASK        (0x1U << QDMA_PING_REGS_VFACTIVE_SHIFT)

/* Allocate */
#define QDMA_FCOI_REGS_ALLOC_SHIFT		15
#define QDMA_FCOI_REGS_ALLOC_SHIFT_WIDTH       1
#define QDMA_FCOI_REGS_ALLOC_SHIFT_MASK        (0x1U << QDMA_PING_REGS_VFACTIVE_SHIFT)

/* To check whether HW is busy or not */
#define QDMA_BUSY_REG	0x300004

/* Acquire lock */
#define QDMA_LOCK_REG	0x300008

/* Specified which device acquired lock */
#define QDMA_DEVID_REG	0x30000C

#define QDMA_PF_VFCOUNT(pf) (VF_CNT#pf)

#define L2P_TABLE_BASE(pf, vf)

/* COI table start address */
#define COI_BASE	0x308000


/* FCOI table start address */
#define FCOI_BASE	0x310000
#define FCOI_TABLE_OFF(ch)  (FCOI_BASE + ((ch/2) * 4))

/* COI table start address */
#define IFC_MCDMA_L2P_PF_BASE	0x318000
#define L2P_PF_TABLE_OFFSET(pf)

/* COI table start address */
#define IFC_MCDMA_L2P_VF_BASE	0x320000

/* L2P table size */
#define L2P_TABLE_SIZE	((MAX_NUM_CHAN_PER_FUNC / 2 ) * 4)

#endif /* _DYNAMICCH_MCDMA_IP_PARAMS_H_*/
