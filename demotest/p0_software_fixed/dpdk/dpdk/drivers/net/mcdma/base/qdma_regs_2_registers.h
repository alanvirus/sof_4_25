/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#ifndef _QDMA_REGS_2_REGISTERS_H_
#define _QDMA_REGS_2_REGISTERS_H_

/* Control Register */

/*
Definitions:
- Bit positions are numbered from 0 with LSB in position 0.
- Byte ordering is little endian: LSB at lowest address.
- Register width is width of register in bits.
- Access width is width of read/write access to register, in bits. 
  Often access width == register width in which case the register can be 
  read/written in a single access. 
  If access width is less than register width then multiple accesses
  are needed to read/write the register. Access words are numbered from 0 by
  their offset in bytes from the register address. So for access width 32 
  access words are numbered 0, 4, 8, 12 etc. up to the size of the register.
  
Defines for address map:
  map_REGWIDTH              Default width of register in bits
  map_ACCESSWIDTH           Default width of access word in bits
  map_ACCESSTYPE            Default access type. Access types defined below

Defines for register reg:
  map_reg                   Address of register. Only if non-table register.
  map_reg(ix)               Address of table register. Only if table with 1 index
  map_reg(ix1, ix2)         Address of table register. Only if table with 2 indexes
  map_reg_MAX_INDEX         Maximum last (ix/ix2) index in table. Only if table.
  map_reg_ix1_MAX_INDEX     Maximum but-last (ix1) index in table. Only if table.
  map_reg_REGWIDTH          Width of register in bits. 
                            Only if different from map_REGWIDTH
  map_reg_ACCESSWIDTH       Width of access word in bits. 
                            Only if different from map_ACCESSWIDTH
  
Defines for field:
  map_reg_field_ACCESSTYPE  Access type. Only if different from map_ACCESSTYPE
  map_reg_field_WIDTH       Width of field in bits.
                            May be larger than access width
  map_reg_field_SHIFT       Bit position in register of LSB of field.
                            Only if register width == access width
  map_reg_field_SHIFT_n     Bit position, in access word n, of LSB of field.
                            Only if register width != access width
  map_reg_field_MASK        Bit mask of field in register.
                            Only if access width == register width.
                            Only if mask is 64 bits or less
  map_reg_field_MASK_n      Bit mask of field in access word n containing LSB of 
                            field.
                            Only if access width != register width.
                            Only if mask is 64 bits or less
  map_reg_field             map_reg_field_SHIFT. Only if 1-bit field.
                            Only if register width == access width
  map_reg_field_n           map_reg_field_SHIFT_n. Only if 1-bit field
                            Only if register width != access width
*/

#ifndef GENHDR_ACCESSTYPE
#define GENHDR_ACCESSTYPE               
#define GENHDR_ACCESSTYPE_RW            0 /* Read / Write */
#define GENHDR_ACCESSTYPE_RWA           1 /* Read / Write Atomic. Read/write low address first, then high address. */
#define GENHDR_ACCESSTYPE_RO            2 /* Read Only */
#define GENHDR_ACCESSTYPE_ROA           3 /* Read Only Atomic. Read low address first, then high address */
#define GENHDR_ACCESSTYPE_RWE           4 /* Read / Write Exclusive. Read-modify-write not needed for field update. */
#define GENHDR_ACCESSTYPE_W1C           5 /* Write 1 to Clear (on individual bits) */
#define GENHDR_ACCESSTYPE_WO            6 /* Write Only */
#endif /* GENHDR_ACCESSTYPE */

#define QDMA_REGS_2_ACCESSTYPE          GENHDR_ACCESSTYPE_RW /*  Default access type. Access types defined above. */
#define QDMA_REGS_2_REGWIDTH            32 /* Default width of register in bits */
#define QDMA_REGS_2_ACCESSWIDTH         32 /* Default width of access word in bit */


/* Q_CTRL */
#define QDMA_REGS_2_Q_CTRL                      0x00000000U
/*
 * Enable.Once it is enabled, the DMA startsfetching pending descriptors and
 * executing them.
 */
#define QDMA_REGS_2_Q_CTRL_Q_EN_SHIFT           0
#define QDMA_REGS_2_Q_CTRL_Q_EN_WIDTH           1
#define QDMA_REGS_2_Q_CTRL_Q_EN_MASK            (0x1U << QDMA_REGS_2_Q_CTRL_Q_EN_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_Q_CTRL_RSV_ACCESSTYPE       GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_Q_CTRL_RSV_SHIFT            1
#define QDMA_REGS_2_Q_CTRL_RSV_WIDTH            7
#define QDMA_REGS_2_Q_CTRL_RSV_MASK             (0x7FU << QDMA_REGS_2_Q_CTRL_RSV_SHIFT)

/* If set, upon completion, do a write back. */
#define QDMA_REGS_2_Q_CTRL_Q_WB_EN_SHIFT        8
#define QDMA_REGS_2_Q_CTRL_Q_WB_EN_WIDTH        1
#define QDMA_REGS_2_Q_CTRL_Q_WB_EN_MASK         (0x1U << QDMA_REGS_2_Q_CTRL_Q_WB_EN_SHIFT)

/* If set, upon completion generate a MSI-X interrupt. */
#define QDMA_REGS_2_Q_CTRL_Q_INTR_EN_SHIFT      9
#define QDMA_REGS_2_Q_CTRL_Q_INTR_EN_WIDTH      1
#define QDMA_REGS_2_Q_CTRL_Q_INTR_EN_MASK       (0x1U << QDMA_REGS_2_Q_CTRL_Q_INTR_EN_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_Q_CTRL_RSVD_ACCESSTYPE      GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_Q_CTRL_RSVD_SHIFT           10
#define QDMA_REGS_2_Q_CTRL_RSVD_WIDTH           22
#define QDMA_REGS_2_Q_CTRL_RSVD_MASK            (0x3FFFFFU << QDMA_REGS_2_Q_CTRL_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* Q_CTRL */
typedef union {
  struct {
    uint32_t q_en : 1;          /* Enable.Once it is enabled, the DMA startsfetching pending descriptors and
                                   executing them. */
    uint32_t rsv : 7;           /* Reserved. */
    uint32_t q_wb_en : 1;       /* If set, upon completion, do a write back. */
    uint32_t q_intr_en : 1;     /* If set, upon completion generate a MSI-X interrupt. */
    uint32_t rsvd : 22;         /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_Q_CTRL_t;
#endif /* GENHDR_STRUCT */

/* RESERVED */
#define QDMA_REGS_2_RESERVED_1                          0x00000004U
/* Reserved. */
#define QDMA_REGS_2_RESERVED_1_RESERVED_ACCESSTYPE      GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_RESERVED_1_RESERVED_SHIFT           0
#define QDMA_REGS_2_RESERVED_1_RESERVED_WIDTH           32
#define QDMA_REGS_2_RESERVED_1_RESERVED_MASK            (0xFFFFFFFFU << QDMA_REGS_2_RESERVED_1_RESERVED_SHIFT)

#ifdef GENHDR_STRUCT
/* RESERVED */
typedef union {
  struct {
    uint32_t reserved : 32;     /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_RESERVED_1_t;
#endif /* GENHDR_STRUCT */

/* Q_START_ADDR_L */
#define QDMA_REGS_2_Q_START_ADDR_L                              0x00000008U
/*
 * After software allocate the descriptor ring buffer, it writes the lower
 * 32-bit allocated address to this register. The descriptor fetch engine use
 * this address and the pending head/tail pointer to fetch the descriptors.
 */
#define QDMA_REGS_2_Q_START_ADDR_L_Q_STRT_ADDR_L_SHIFT          0
#define QDMA_REGS_2_Q_START_ADDR_L_Q_STRT_ADDR_L_WIDTH          32
#define QDMA_REGS_2_Q_START_ADDR_L_Q_STRT_ADDR_L_MASK           (0xFFFFFFFFU << QDMA_REGS_2_Q_START_ADDR_L_Q_STRT_ADDR_L_SHIFT)

#ifdef GENHDR_STRUCT
/* Q_START_ADDR_L */
typedef union {
  struct {
    uint32_t q_strt_addr_l : 32;        /* After software allocate the descriptor ring buffer, it writes the lower
                                           32-bit allocated address to this register. The descriptor fetch engine use
                                           this address and the pending head/tail pointer to fetch the descriptors. */
  } field;
  uint32_t val;
} QDMA_REGS_2_Q_START_ADDR_L_t;
#endif /* GENHDR_STRUCT */

/* Q_START_ADDR_H */
#define QDMA_REGS_2_Q_START_ADDR_H                              0x0000000CU
/*
 * After software allocate the descriptor ring buffer, it writes the upper
 * 32-bit allocated address to this register. The descriptor fetch engine use
 * this address and the pending head/tail pointer to fetch the descriptors.
 */
#define QDMA_REGS_2_Q_START_ADDR_H_Q_STRT_ADDR_H_SHIFT          0
#define QDMA_REGS_2_Q_START_ADDR_H_Q_STRT_ADDR_H_WIDTH          32
#define QDMA_REGS_2_Q_START_ADDR_H_Q_STRT_ADDR_H_MASK           (0xFFFFFFFFU << QDMA_REGS_2_Q_START_ADDR_H_Q_STRT_ADDR_H_SHIFT)

#ifdef GENHDR_STRUCT
/* Q_START_ADDR_H */
typedef union {
  struct {
    uint32_t q_strt_addr_h : 32;        /* After software allocate the descriptor ring buffer, it writes the upper
                                           32-bit allocated address to this register. The descriptor fetch engine use
                                           this address and the pending head/tail pointer to fetch the descriptors. */
  } field;
  uint32_t val;
} QDMA_REGS_2_Q_START_ADDR_H_t;
#endif /* GENHDR_STRUCT */

/* Q_SIZE */
#define QDMA_REGS_2_Q_SIZE                      0x00000010U
/*
 * Size of the descriptor ring in power of 2 and max value of 16. The unit is
 * number of descriptors. Hardware will default to using a valueof 1 if an
 * illegal value is written. A value of 1 means queue size of 2 (2^1). A value
 * is 16 (0x10) means queue size of 64K (2^16).
 */
#define QDMA_REGS_2_Q_SIZE_Q_SIZE_SHIFT         0
#define QDMA_REGS_2_Q_SIZE_Q_SIZE_WIDTH         5
#define QDMA_REGS_2_Q_SIZE_Q_SIZE_MASK          (0x1FU << QDMA_REGS_2_Q_SIZE_Q_SIZE_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_Q_SIZE_RSVD_ACCESSTYPE      GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_Q_SIZE_RSVD_SHIFT           5
#define QDMA_REGS_2_Q_SIZE_RSVD_WIDTH           27
#define QDMA_REGS_2_Q_SIZE_RSVD_MASK            (0x7FFFFFFU << QDMA_REGS_2_Q_SIZE_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* Q_SIZE */
typedef union {
  struct {
    uint32_t q_size : 5;        /* Size of the descriptor ring in power of 2 and max value of 16. The unit is
                                   number of descriptors. Hardware will default to using a valueof 1 if an
                                   illegal value is written. A value of 1 means queue size of 2 (2^1). A value
                                   is 16 (0x10) means queue size of 64K (2^16). */
    uint32_t rsvd : 27;         /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_Q_SIZE_t;
#endif /* GENHDR_STRUCT */

/* Q_TAIL_POINTER */
#define QDMA_REGS_2_Q_TAIL_POINTER                      0x00000014U
/*
 * After software setups a last valid descriptor in the descriptor buffer, it
 * programs this register with the position of the last (tail) valid descriptor
 * that is ready to be executed. The DMA Descriptor Engine fetches descriptors
 * from the buffer upto this position of the buffer.
 */
#define QDMA_REGS_2_Q_TAIL_POINTER_Q_TL_PTR_SHIFT       0
#define QDMA_REGS_2_Q_TAIL_POINTER_Q_TL_PTR_WIDTH       16
#define QDMA_REGS_2_Q_TAIL_POINTER_Q_TL_PTR_MASK        (0xFFFFU << QDMA_REGS_2_Q_TAIL_POINTER_Q_TL_PTR_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_Q_TAIL_POINTER_RSVD_ACCESSTYPE      GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_Q_TAIL_POINTER_RSVD_SHIFT           16
#define QDMA_REGS_2_Q_TAIL_POINTER_RSVD_WIDTH           16
#define QDMA_REGS_2_Q_TAIL_POINTER_RSVD_MASK            (0xFFFFU << QDMA_REGS_2_Q_TAIL_POINTER_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* Q_TAIL_POINTER */
typedef union {
  struct {
    uint32_t q_tl_ptr : 16;     /* After software setups a last valid descriptor in the descriptor buffer, it
                                   programs this register with the position of the last (tail) valid descriptor
                                   that is ready to be executed. The DMA Descriptor Engine fetches descriptors
                                   from the buffer upto this position of the buffer. */
    uint32_t rsvd : 16;         /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_Q_TAIL_POINTER_t;
#endif /* GENHDR_STRUCT */

/* Q_HEAD_POINTER */
#define QDMA_REGS_2_Q_HEAD_POINTER                      0x00000018U
/*
 * After DMA Descriptor Fetch Engine fetches the descriptors from the descriptor
 * buffer, upto the tail pointer, it updates this register with that last
 * fetched descriptor position. The fetch engine only fetches descriptors if the
 * head and tail pointer is not equal.
 */
#define QDMA_REGS_2_Q_HEAD_POINTER_Q_HD_PTR_SHIFT       0
#define QDMA_REGS_2_Q_HEAD_POINTER_Q_HD_PTR_WIDTH       16
#define QDMA_REGS_2_Q_HEAD_POINTER_Q_HD_PTR_MASK        (0xFFFFU << QDMA_REGS_2_Q_HEAD_POINTER_Q_HD_PTR_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_Q_HEAD_POINTER_RSVD_ACCESSTYPE      GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_Q_HEAD_POINTER_RSVD_SHIFT           16
#define QDMA_REGS_2_Q_HEAD_POINTER_RSVD_WIDTH           16
#define QDMA_REGS_2_Q_HEAD_POINTER_RSVD_MASK            (0xFFFFU << QDMA_REGS_2_Q_HEAD_POINTER_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* Q_HEAD_POINTER */
typedef union {
  struct {
    uint32_t q_hd_ptr : 16;     /* After DMA Descriptor Fetch Engine fetches the descriptors from the descriptor
                                   buffer, upto the tail pointer, it updates this register with that last
                                   fetched descriptor position. The fetch engine only fetches descriptors if the
                                   head and tail pointer is not equal. */
    uint32_t rsvd : 16;         /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_Q_HEAD_POINTER_t;
#endif /* GENHDR_STRUCT */

/* Q_COMPLETED_POINTER */
#define QDMA_REGS_2_Q_COMPLETED_POINTER                         0x0000001CU
/*
 * This register is updated by hardware to store the last descriptor position
 * (pointer) that DMA has completed, that is all data for that descriptor and
 * previous descriptors have arrived at the intended destinations. Software can
 * poll this register to find out the status of the DMA for a specific queue.
 */
#define QDMA_REGS_2_Q_COMPLETED_POINTER_Q_CMPL_PTR_SHIFT        0
#define QDMA_REGS_2_Q_COMPLETED_POINTER_Q_CMPL_PTR_WIDTH        16
#define QDMA_REGS_2_Q_COMPLETED_POINTER_Q_CMPL_PTR_MASK         (0xFFFFU << QDMA_REGS_2_Q_COMPLETED_POINTER_Q_CMPL_PTR_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_Q_COMPLETED_POINTER_RSVD_ACCESSTYPE         GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_Q_COMPLETED_POINTER_RSVD_SHIFT              16
#define QDMA_REGS_2_Q_COMPLETED_POINTER_RSVD_WIDTH              16
#define QDMA_REGS_2_Q_COMPLETED_POINTER_RSVD_MASK               (0xFFFFU << QDMA_REGS_2_Q_COMPLETED_POINTER_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* Q_COMPLETED_POINTER */
typedef union {
  struct {
    uint32_t q_cmpl_ptr : 16;   /* This register is updated by hardware to store the last descriptor position
                                   (pointer) that DMA has completed, that is all data for that descriptor and
                                   previous descriptors have arrived at the intended destinations. Software can
                                   poll this register to find out the status of the DMA for a specific queue. */
    uint32_t rsvd : 16;         /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_Q_COMPLETED_POINTER_t;
#endif /* GENHDR_STRUCT */

/* Q_CONSUMED_HEAD_ADDR_L */
#define QDMA_REGS_2_Q_CONSUMED_HEAD_ADDR_L                              0x00000020U
/*
 * Software programs this register with the lower 32-bit address location where
 * the writeback will target after DMA is completed for a set of descriptors.
 */
#define QDMA_REGS_2_Q_CONSUMED_HEAD_ADDR_L_Q_CNSM_HD_ADDR_L_SHIFT       0
#define QDMA_REGS_2_Q_CONSUMED_HEAD_ADDR_L_Q_CNSM_HD_ADDR_L_WIDTH       32
#define QDMA_REGS_2_Q_CONSUMED_HEAD_ADDR_L_Q_CNSM_HD_ADDR_L_MASK        (0xFFFFFFFFU << QDMA_REGS_2_Q_CONSUMED_HEAD_ADDR_L_Q_CNSM_HD_ADDR_L_SHIFT)

#ifdef GENHDR_STRUCT
/* Q_CONSUMED_HEAD_ADDR_L */
typedef union {
  struct {
    uint32_t q_cnsm_hd_addr_l : 32;     /* Software programs this register with the lower 32-bit address location where
                                           the writeback will target after DMA is completed for a set of descriptors. */
  } field;
  uint32_t val;
} QDMA_REGS_2_Q_CONSUMED_HEAD_ADDR_L_t;
#endif /* GENHDR_STRUCT */

/* Q_CONSUMED_HEAD_ADDR_H */
#define QDMA_REGS_2_Q_CONSUMED_HEAD_ADDR_H                              0x00000024U
/*
 * Software programs this register with the upper 32-bit address location where
 * the writeback will target after DMA is completed for a set of descriptors.
 */
#define QDMA_REGS_2_Q_CONSUMED_HEAD_ADDR_H_Q_CNSM_HD_ADDR_H_SHIFT       0
#define QDMA_REGS_2_Q_CONSUMED_HEAD_ADDR_H_Q_CNSM_HD_ADDR_H_WIDTH       32
#define QDMA_REGS_2_Q_CONSUMED_HEAD_ADDR_H_Q_CNSM_HD_ADDR_H_MASK        (0xFFFFFFFFU << QDMA_REGS_2_Q_CONSUMED_HEAD_ADDR_H_Q_CNSM_HD_ADDR_H_SHIFT)

#ifdef GENHDR_STRUCT
/* Q_CONSUMED_HEAD_ADDR_H */
typedef union {
  struct {
    uint32_t q_cnsm_hd_addr_h : 32;     /* Software programs this register with the upper 32-bit address location where
                                           the writeback will target after DMA is completed for a set of descriptors. */
  } field;
  uint32_t val;
} QDMA_REGS_2_Q_CONSUMED_HEAD_ADDR_H_t;
#endif /* GENHDR_STRUCT */

/* Q_BATCH_DELAY */
#define QDMA_REGS_2_Q_BATCH_DELAY                               0x00000028U
/*
 * Software programs this register with the the amount of time between fetches
 * for descriptors. Each unit is 2ns.
 */
#define QDMA_REGS_2_Q_BATCH_DELAY_Q_BATCH_DSCR_DELAY_SHIFT      0
#define QDMA_REGS_2_Q_BATCH_DELAY_Q_BATCH_DSCR_DELAY_WIDTH      20
#define QDMA_REGS_2_Q_BATCH_DELAY_Q_BATCH_DSCR_DELAY_MASK       (0xFFFFFU << QDMA_REGS_2_Q_BATCH_DELAY_Q_BATCH_DSCR_DELAY_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_Q_BATCH_DELAY_RSVD_ACCESSTYPE               GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_Q_BATCH_DELAY_RSVD_SHIFT                    20
#define QDMA_REGS_2_Q_BATCH_DELAY_RSVD_WIDTH                    12
#define QDMA_REGS_2_Q_BATCH_DELAY_RSVD_MASK                     (0xFFFU << QDMA_REGS_2_Q_BATCH_DELAY_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* Q_BATCH_DELAY */
typedef union {
  struct {
    uint32_t q_batch_dscr_delay : 20;   /* Software programs this register with the the amount of time between fetches
                                           for descriptors. Each unit is 2ns. */
    uint32_t rsvd : 12;                 /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_Q_BATCH_DELAY_t;
#endif /* GENHDR_STRUCT */

/* RESERVED */
#define QDMA_REGS_2_RESERVED_2                          0x0000002CU
/* Reserved. */
#define QDMA_REGS_2_RESERVED_2_RESERVED_ACCESSTYPE      GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_RESERVED_2_RESERVED_SHIFT           0
#define QDMA_REGS_2_RESERVED_2_RESERVED_WIDTH           32
#define QDMA_REGS_2_RESERVED_2_RESERVED_MASK            (0xFFFFFFFFU << QDMA_REGS_2_RESERVED_2_RESERVED_SHIFT)

#ifdef GENHDR_STRUCT
/* RESERVED */
typedef union {
  struct {
    uint32_t reserved : 32;     /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_RESERVED_2_t;
#endif /* GENHDR_STRUCT */

/* RESERVED */
#define QDMA_REGS_2_RESERVED_3                          0x00000030U
/* Reserved. */
#define QDMA_REGS_2_RESERVED_3_RESERVED_ACCESSTYPE      GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_RESERVED_3_RESERVED_SHIFT           0
#define QDMA_REGS_2_RESERVED_3_RESERVED_WIDTH           32
#define QDMA_REGS_2_RESERVED_3_RESERVED_MASK            (0xFFFFFFFFU << QDMA_REGS_2_RESERVED_3_RESERVED_SHIFT)

#ifdef GENHDR_STRUCT
/* RESERVED */
typedef union {
  struct {
    uint32_t reserved : 32;     /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_RESERVED_3_t;
#endif /* GENHDR_STRUCT */

/* RESERVED */
#define QDMA_REGS_2_RESERVED_4                          0x00000034U
/* Reserved. */
#define QDMA_REGS_2_RESERVED_4_RESERVED_ACCESSTYPE      GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_RESERVED_4_RESERVED_SHIFT           0
#define QDMA_REGS_2_RESERVED_4_RESERVED_WIDTH           32
#define QDMA_REGS_2_RESERVED_4_RESERVED_MASK            (0xFFFFFFFFU << QDMA_REGS_2_RESERVED_4_RESERVED_SHIFT)

#ifdef GENHDR_STRUCT
/* RESERVED */
typedef union {
  struct {
    uint32_t reserved : 32;     /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_RESERVED_4_t;
#endif /* GENHDR_STRUCT */

/* Q_DEBUG_STATUS_1 */
#define QDMA_REGS_2_Q_DEBUG_STATUS_1                            0x00000038U
/* Reserved. */
#define QDMA_REGS_2_Q_DEBUG_STATUS_1_Q_DEBUG_STATUS_1_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_Q_DEBUG_STATUS_1_Q_DEBUG_STATUS_1_SHIFT     0
#define QDMA_REGS_2_Q_DEBUG_STATUS_1_Q_DEBUG_STATUS_1_WIDTH     32
#define QDMA_REGS_2_Q_DEBUG_STATUS_1_Q_DEBUG_STATUS_1_MASK      (0xFFFFFFFFU << QDMA_REGS_2_Q_DEBUG_STATUS_1_Q_DEBUG_STATUS_1_SHIFT)

#ifdef GENHDR_STRUCT
/* Q_DEBUG_STATUS_1 */
typedef union {
  struct {
    uint32_t q_debug_status_1 : 32;     /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_Q_DEBUG_STATUS_1_t;
#endif /* GENHDR_STRUCT */

/* Q_DEBUG_STATUS_2 */
#define QDMA_REGS_2_Q_DEBUG_STATUS_2                            0x0000003CU
/* Reserved. */
#define QDMA_REGS_2_Q_DEBUG_STATUS_2_Q_DEBUG_STATUS_2_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_Q_DEBUG_STATUS_2_Q_DEBUG_STATUS_2_SHIFT     0
#define QDMA_REGS_2_Q_DEBUG_STATUS_2_Q_DEBUG_STATUS_2_WIDTH     32
#define QDMA_REGS_2_Q_DEBUG_STATUS_2_Q_DEBUG_STATUS_2_MASK      (0xFFFFFFFFU << QDMA_REGS_2_Q_DEBUG_STATUS_2_Q_DEBUG_STATUS_2_SHIFT)

#ifdef GENHDR_STRUCT
/* Q_DEBUG_STATUS_2 */
typedef union {
  struct {
    uint32_t q_debug_status_2 : 32;     /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_Q_DEBUG_STATUS_2_t;
#endif /* GENHDR_STRUCT */

#define QDMA_REGS_2_Q_DATA_DRP_ERR_CTR  0x00000040U
/* Reserved. */
#define QDMA_REGS_2_Q_DATA_DRP_ERR_CTR_RSV_ACCESSTYPE       GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_Q_DATA_DRP_ERR_CTR_RSV_SHIFT            20
#define QDMA_REGS_2_Q_DATA_DRP_ERR_CTR_RSV_WIDTH            9
#define QDMA_REGS_2_Q_DATA_DRP_ERR_CTR_RSV_MASK             (0x1FFU << QDMA_REGS_2_Q_DATA_DRP_ERR_CTR_RSV_SHIFT)
#ifdef GENHDR_STRUCT
  /* Q_DATA_DRP_ERR_CTR */
typedef union {
  struct {
    uint32_t q_d2h_data_err_st : 16;    /* Data error status for a channel #N  in streaming mode */
    uint32_t q_d2h_data_err_mm : 1;     /* Data error status for a channel #N in AVMM mode */
    uint32_t q_h2d_data_err_st : 1;     /* Data error status for a channel #N  in streaming mode */
    uint32_t q_h2d_data_err_mm : 1;     /* Data error status for a channel #N in AVMM mode */
    uint32_t q_d2h_data_drop_err_st : 1;   /* D2H data drop error status for a channel#N and also read the data drop error */
    uint32_t rsv : 9;                   /* Reserved */
    uint32_t q_err_during_data_fetch : 1;  /* Cmplto error etc */
    uint32_t q_err_during_desc_fetch : 1;  /* Cmplto error etc */
  } field;
  uint32_t val;
} QDMA_REGS_2_Q_DATA_DRP_ERR_CTR_t;
#endif /* GENHDR_STRUCT */

enum error_event {
	TID_ERROR						= (1u << 0),
	COMPLETION_TIME_OUT_ERROR				= (1u << 1),
	DESC_FETCH_EVENT					= (1u << 2),
	DATA_FETCH_EVENT					= (1u << 3),
	OTHER_ERROR						= (1u << 4)
};

#ifndef DPDK_21_11_RC2
enum error_event err_event;
#endif
/* Q_DEBUG_STATUS_3 */
#define QDMA_REGS_2_Q_DEBUG_STATUS_3                            0x00000040U
/* Reserved. */
#define QDMA_REGS_2_Q_DEBUG_STATUS_3_Q_DEBUG_STATUS_3_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_Q_DEBUG_STATUS_3_Q_DEBUG_STATUS_3_SHIFT     0
#define QDMA_REGS_2_Q_DEBUG_STATUS_3_Q_DEBUG_STATUS_3_WIDTH     32
#define QDMA_REGS_2_Q_DEBUG_STATUS_3_Q_DEBUG_STATUS_3_MASK      (0xFFFFFFFFU << QDMA_REGS_2_Q_DEBUG_STATUS_3_Q_DEBUG_STATUS_3_SHIFT)

#ifdef GENHDR_STRUCT
/* Q_DEBUG_STATUS_3 */
typedef union {
  struct {
    uint32_t q_debug_status_3 : 32;     /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_Q_DEBUG_STATUS_3_t;
#endif /* GENHDR_STRUCT */

/* Q_DEBUG_STATUS_4 */
#define QDMA_REGS_2_Q_DEBUG_STATUS_4                            0x00000044U
/* Reserved. */
#define QDMA_REGS_2_Q_DEBUG_STATUS_4_Q_DEBUG_STATUS_4_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_Q_DEBUG_STATUS_4_Q_DEBUG_STATUS_4_SHIFT     0
#define QDMA_REGS_2_Q_DEBUG_STATUS_4_Q_DEBUG_STATUS_4_WIDTH     32
#define QDMA_REGS_2_Q_DEBUG_STATUS_4_Q_DEBUG_STATUS_4_MASK      (0xFFFFFFFFU << QDMA_REGS_2_Q_DEBUG_STATUS_4_Q_DEBUG_STATUS_4_SHIFT)

#ifdef GENHDR_STRUCT
/* Q_DEBUG_STATUS_4 */
typedef union {
  struct {
    uint32_t q_debug_status_4 : 32;     /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_Q_DEBUG_STATUS_4_t;
#endif /* GENHDR_STRUCT */

/* Q_RESET */
#define QDMA_REGS_2_Q_RESET                     0x00000048U
/*
 * Request reset for the queue by writing 1'b1 to this register, and poll for
 * value of 1'b0 when reset has been completed by hardware. Hardware clears this
 * bit after completing the reset of a queue. Similar process occurs for when
 * FLR reset is detected for a VF.
 */
#define QDMA_REGS_2_Q_RESET_Q_RESET_SHIFT       0
#define QDMA_REGS_2_Q_RESET_Q_RESET_WIDTH       1
#define QDMA_REGS_2_Q_RESET_Q_RESET_MASK        (0x1U << QDMA_REGS_2_Q_RESET_Q_RESET_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_Q_RESET_RSVD_ACCESSTYPE     GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_Q_RESET_RSVD_SHIFT          1
#define QDMA_REGS_2_Q_RESET_RSVD_WIDTH          31
#define QDMA_REGS_2_Q_RESET_RSVD_MASK           (0x7FFFFFFFU << QDMA_REGS_2_Q_RESET_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* Q_RESET */
typedef union {
  struct {
    uint32_t q_reset : 1;       /* Request reset for the queue by writing 1'b1 to this register, and poll for
                                   value of 1'b0 when reset has been completed by hardware. Hardware clears this
                                   bit after completing the reset of a queue. Similar process occurs for when
                                   FLR reset is detected for a VF. */
    uint32_t rsvd : 31;         /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_Q_RESET_t;
#endif /* GENHDR_STRUCT */

/* Q_CPL_TIMEOUT */
#define QDMA_REGS_2_Q_CPL_TIMEOUT                               0x0000004CU
/* Completion Time-out flag. */
#define QDMA_REGS_2_Q_CPL_TIMEOUT_Q_CPL_TIMEOUT_SHIFT           0
#define QDMA_REGS_2_Q_CPL_TIMEOUT_Q_CPL_TIMEOUT_WIDTH           32
#define QDMA_REGS_2_Q_CPL_TIMEOUT_Q_CPL_TIMEOUT_MASK            (0xFFFFFFFFU << QDMA_REGS_2_Q_CPL_TIMEOUT_Q_CPL_TIMEOUT_SHIFT)

#ifdef GENHDR_STRUCT
/* Q_CPL_TIMEOUT */
typedef union {
  struct {
    uint32_t q_cpl_timeout : 32;        /* Completion Time-out flag. */
  } field;
  uint32_t val;
} QDMA_REGS_2_Q_CPL_TIMEOUT_t;
#endif /* GENHDR_STRUCT */

/* Message Address for MSI-X Table Entries */
#define QDMA_REGS_2_MSG_ADDR_H2_D_DMA                                   0x00100000U
/*
 * Message_Address_1:Message_Address.
 * 
 * For proper DWORD alignment, software must always write zeroes to these two
 * bits; otherwise the result is undefined. The state of these bits after reset
 * must be 0. These bits are permitted to be read only or read/write. 
 */
#define QDMA_REGS_2_MSG_ADDR_H2_D_DMA_MESSAGE_ADDRESS_1_SHIFT           0
#define QDMA_REGS_2_MSG_ADDR_H2_D_DMA_MESSAGE_ADDRESS_1_WIDTH           2
#define QDMA_REGS_2_MSG_ADDR_H2_D_DMA_MESSAGE_ADDRESS_1_MASK            (0x3U << QDMA_REGS_2_MSG_ADDR_H2_D_DMA_MESSAGE_ADDRESS_1_SHIFT)

/*
 * System-specified message lower address. For MSI-X messages, the contents of
 * this field from an MSI-X Table entry specifies the lower portion of the
 * DWORD-aligned address (AD[31::02]) for the memory write transaction. This
 * field is read/write. 
 */
#define QDMA_REGS_2_MSG_ADDR_H2_D_DMA_MESSAGE_ADDRESS_SHIFT             2
#define QDMA_REGS_2_MSG_ADDR_H2_D_DMA_MESSAGE_ADDRESS_WIDTH             30
#define QDMA_REGS_2_MSG_ADDR_H2_D_DMA_MESSAGE_ADDRESS_MASK              (0x3FFFFFFFU << QDMA_REGS_2_MSG_ADDR_H2_D_DMA_MESSAGE_ADDRESS_SHIFT)

#ifdef GENHDR_STRUCT
/* Message Address for MSI-X Table Entries */
typedef union {
  struct {
    uint32_t message_address_1 : 2;     /* Message_Address_1:Message_Address.
                                           
                                           For proper DWORD alignment, software must always write zeroes to these two
                                           bits; otherwise the result is undefined. The state of these bits after reset
                                           must be 0. These bits are permitted to be read only or read/write. */
    uint32_t message_address : 30;      /* System-specified message lower address. For MSI-X messages, the contents of
                                           this field from an MSI-X Table entry specifies the lower portion of the
                                           DWORD-aligned address (AD[31::02]) for the memory write transaction. This
                                           field is read/write. */
  } field;
  uint32_t val;
} QDMA_REGS_2_MSG_ADDR_H2_D_DMA_t;
#endif /* GENHDR_STRUCT */

/* Message Upper Address for MSI-X Table Entries */
#define QDMA_REGS_2_MSG_UPPER_ADDR_H2_D_DMA                             0x00100004U
/*
 * System-specified message upper address bits. If this field is zero, Single
 * Address Cycle (SAC) messages are used. If this field is non-zero, Dual
 * Address Cycle (DAC) messages are used. This field is read/write. 
 */
#define QDMA_REGS_2_MSG_UPPER_ADDR_H2_D_DMA_MESSAGE_UPPER_ADDRESS_SHIFT 0
#define QDMA_REGS_2_MSG_UPPER_ADDR_H2_D_DMA_MESSAGE_UPPER_ADDRESS_WIDTH 32
#define QDMA_REGS_2_MSG_UPPER_ADDR_H2_D_DMA_MESSAGE_UPPER_ADDRESS_MASK  (0xFFFFFFFFU << QDMA_REGS_2_MSG_UPPER_ADDR_H2_D_DMA_MESSAGE_UPPER_ADDRESS_SHIFT)

#ifdef GENHDR_STRUCT
/* Message Upper Address for MSI-X Table Entries */
typedef union {
  struct {
    uint32_t message_upper_address : 32;        /* System-specified message upper address bits. If this field is zero, Single
                                                   Address Cycle (SAC) messages are used. If this field is non-zero, Dual
                                                   Address Cycle (DAC) messages are used. This field is read/write. */
  } field;
  uint32_t val;
} QDMA_REGS_2_MSG_UPPER_ADDR_H2_D_DMA_t;
#endif /* GENHDR_STRUCT */

/* Message Data for MSI-X Table Entries */
#define QDMA_REGS_2_MSG_DATA_H2_D_DMA                           0x00100008U
/*
 * System-specified message data. For MSI-X messages, the contents of this field
 * from an MSI-X Table entry specifies the data driven on AD[31::00] during the
 * memory write transaction's data phase. C/BE[3::0]# are asserted during the
 * data phase of the memory write transaction. In contrast to message data used
 * for MSI messages, the low-order message data bits in MSI-X messages are not
 * modified by the function. This field is read/write. . 
 */
#define QDMA_REGS_2_MSG_DATA_H2_D_DMA_MESSAGE_DATA_SHIFT        0
#define QDMA_REGS_2_MSG_DATA_H2_D_DMA_MESSAGE_DATA_WIDTH        32
#define QDMA_REGS_2_MSG_DATA_H2_D_DMA_MESSAGE_DATA_MASK         (0xFFFFFFFFU << QDMA_REGS_2_MSG_DATA_H2_D_DMA_MESSAGE_DATA_SHIFT)

#ifdef GENHDR_STRUCT
/* Message Data for MSI-X Table Entries */
typedef union {
  struct {
    uint32_t message_data : 32; /* System-specified message data. For MSI-X messages, the contents of this field
                                   from an MSI-X Table entry specifies the data driven on AD[31::00] during the
                                   memory write transaction's data phase. C/BE[3::0]# are asserted during the
                                   data phase of the memory write transaction. In contrast to message data used
                                   for MSI messages, the low-order message data bits in MSI-X messages are not
                                   modified by the function. This field is read/write. . */
  } field;
  uint32_t val;
} QDMA_REGS_2_MSG_DATA_H2_D_DMA_t;
#endif /* GENHDR_STRUCT */

/* Vector Control for MSI-X Table Entries */
#define QDMA_REGS_2_VECTOR_CONTROL_H2_D_DMA                     0x0010000CU
/*
 * When this bit is set, the function is prohibited from sending a message using
 * this MSI-X Table entry. However, any other MSI-X Table entries programmed
 * with the same vector will still be capable of sending an equivalent message
 * unless they are also masked. This bit's state after reset is 1 (entry is
 * masked). This bit is read/write. 
 */
#define QDMA_REGS_2_VECTOR_CONTROL_H2_D_DMA_MASK_BIT_SHIFT      0
#define QDMA_REGS_2_VECTOR_CONTROL_H2_D_DMA_MASK_BIT_WIDTH      1
#define QDMA_REGS_2_VECTOR_CONTROL_H2_D_DMA_MASK_BIT_MASK       (0x1U << QDMA_REGS_2_VECTOR_CONTROL_H2_D_DMA_MASK_BIT_SHIFT)

/*
 * After reset, the state of these bits must be 0. However, for potential future
 * use, software must preserve the value of these reserved bits when modifying
 * the value of other Vector Control bits. If software modifies the value of
 * these reserved bits, the result is undefined. 
 */
#define QDMA_REGS_2_VECTOR_CONTROL_H2_D_DMA_RESERVED_SHIFT      1
#define QDMA_REGS_2_VECTOR_CONTROL_H2_D_DMA_RESERVED_WIDTH      31
#define QDMA_REGS_2_VECTOR_CONTROL_H2_D_DMA_RESERVED_MASK       (0x7FFFFFFFU << QDMA_REGS_2_VECTOR_CONTROL_H2_D_DMA_RESERVED_SHIFT)

#ifdef GENHDR_STRUCT
/* Vector Control for MSI-X Table Entries */
typedef union {
  struct {
    uint32_t mask_bit : 1;      /* When this bit is set, the function is prohibited from sending a message using
                                   this MSI-X Table entry. However, any other MSI-X Table entries programmed
                                   with the same vector will still be capable of sending an equivalent message
                                   unless they are also masked. This bit's state after reset is 1 (entry is
                                   masked). This bit is read/write. */
    uint32_t reserved : 31;     /* After reset, the state of these bits must be 0. However, for potential future
                                   use, software must preserve the value of these reserved bits when modifying
                                   the value of other Vector Control bits. If software modifies the value of
                                   these reserved bits, the result is undefined. */
  } field;
  uint32_t val;
} QDMA_REGS_2_VECTOR_CONTROL_H2_D_DMA_t;
#endif /* GENHDR_STRUCT */

/* Message Address for MSI-X Table Entries */
#define QDMA_REGS_2_MSG_ADDR_H2_D_EVENT_INTERRUPT                       0x00100010U
/*
 * Message_Address_1:Message_Address.
 * 
 * For proper DWORD alignment, software must always write zeroes to these two
 * bits; otherwise the result is undefined. The state of these bits after reset
 * must be 0. These bits are permitted to be read only or read/write. 
 */
#define QDMA_REGS_2_MSG_ADDR_H2_D_EVENT_INTERRUPT_MESSAGE_ADDRESS_1_SHIFT 0
#define QDMA_REGS_2_MSG_ADDR_H2_D_EVENT_INTERRUPT_MESSAGE_ADDRESS_1_WIDTH 2
#define QDMA_REGS_2_MSG_ADDR_H2_D_EVENT_INTERRUPT_MESSAGE_ADDRESS_1_MASK (0x3U << QDMA_REGS_2_MSG_ADDR_H2_D_EVENT_INTERRUPT_MESSAGE_ADDRESS_1_SHIFT)

/*
 * System-specified message lower address. For MSI-X messages, the contents of
 * this field from an MSI-X Table entry specifies the lower portion of the
 * DWORD-aligned address (AD[31::02]) for the memory write transaction. This
 * field is read/write. 
 */
#define QDMA_REGS_2_MSG_ADDR_H2_D_EVENT_INTERRUPT_MESSAGE_ADDRESS_SHIFT 2
#define QDMA_REGS_2_MSG_ADDR_H2_D_EVENT_INTERRUPT_MESSAGE_ADDRESS_WIDTH 30
#define QDMA_REGS_2_MSG_ADDR_H2_D_EVENT_INTERRUPT_MESSAGE_ADDRESS_MASK  (0x3FFFFFFFU << QDMA_REGS_2_MSG_ADDR_H2_D_EVENT_INTERRUPT_MESSAGE_ADDRESS_SHIFT)

#ifdef GENHDR_STRUCT
/* Message Address for MSI-X Table Entries */
typedef union {
  struct {
    uint32_t message_address_1 : 2;     /* Message_Address_1:Message_Address.
                                           
                                           For proper DWORD alignment, software must always write zeroes to these two
                                           bits; otherwise the result is undefined. The state of these bits after reset
                                           must be 0. These bits are permitted to be read only or read/write. */
    uint32_t message_address : 30;      /* System-specified message lower address. For MSI-X messages, the contents of
                                           this field from an MSI-X Table entry specifies the lower portion of the
                                           DWORD-aligned address (AD[31::02]) for the memory write transaction. This
                                           field is read/write. */
  } field;
  uint32_t val;
} QDMA_REGS_2_MSG_ADDR_H2_D_EVENT_INTERRUPT_t;
#endif /* GENHDR_STRUCT */

/* Message Upper Address for MSI-X Table Entries */
#define QDMA_REGS_2_MSG_UPPER_ADDR_H2_D_EVENT_INTERRUPT                 0x00100014U
/*
 * System-specified message upper address bits. If this field is zero, Single
 * Address Cycle (SAC) messages are used. If this field is non-zero, Dual
 * Address Cycle (DAC) messages are used. This field is read/write. 
 */
#define QDMA_REGS_2_MSG_UPPER_ADDR_H2_D_EVENT_INTERRUPT_MESSAGE_UPPER_ADDRESS_SHIFT 0
#define QDMA_REGS_2_MSG_UPPER_ADDR_H2_D_EVENT_INTERRUPT_MESSAGE_UPPER_ADDRESS_WIDTH 32
#define QDMA_REGS_2_MSG_UPPER_ADDR_H2_D_EVENT_INTERRUPT_MESSAGE_UPPER_ADDRESS_MASK (0xFFFFFFFFU << QDMA_REGS_2_MSG_UPPER_ADDR_H2_D_EVENT_INTERRUPT_MESSAGE_UPPER_ADDRESS_SHIFT)

#ifdef GENHDR_STRUCT
/* Message Upper Address for MSI-X Table Entries */
typedef union {
  struct {
    uint32_t message_upper_address : 32;        /* System-specified message upper address bits. If this field is zero, Single
                                                   Address Cycle (SAC) messages are used. If this field is non-zero, Dual
                                                   Address Cycle (DAC) messages are used. This field is read/write. */
  } field;
  uint32_t val;
} QDMA_REGS_2_MSG_UPPER_ADDR_H2_D_EVENT_INTERRUPT_t;
#endif /* GENHDR_STRUCT */

/* Message Data for MSI-X Table Entries */
#define QDMA_REGS_2_MSG_DATA_H2_D_EVENT_INTERRUPT                       0x00100018U
/*
 * System-specified message data. For MSI-X messages, the contents of this field
 * from an MSI-X Table entry specifies the data driven on AD[31::00] during the
 * memory write transaction's data phase. C/BE[3::0]# are asserted during the
 * data phase of the memory write transaction. In contrast to message data used
 * for MSI messages, the low-order message data bits in MSI-X messages are not
 * modified by the function. This field is read/write. . 
 */
#define QDMA_REGS_2_MSG_DATA_H2_D_EVENT_INTERRUPT_MESSAGE_DATA_SHIFT    0
#define QDMA_REGS_2_MSG_DATA_H2_D_EVENT_INTERRUPT_MESSAGE_DATA_WIDTH    32
#define QDMA_REGS_2_MSG_DATA_H2_D_EVENT_INTERRUPT_MESSAGE_DATA_MASK     (0xFFFFFFFFU << QDMA_REGS_2_MSG_DATA_H2_D_EVENT_INTERRUPT_MESSAGE_DATA_SHIFT)

#ifdef GENHDR_STRUCT
/* Message Data for MSI-X Table Entries */
typedef union {
  struct {
    uint32_t message_data : 32; /* System-specified message data. For MSI-X messages, the contents of this field
                                   from an MSI-X Table entry specifies the data driven on AD[31::00] during the
                                   memory write transaction's data phase. C/BE[3::0]# are asserted during the
                                   data phase of the memory write transaction. In contrast to message data used
                                   for MSI messages, the low-order message data bits in MSI-X messages are not
                                   modified by the function. This field is read/write. . */
  } field;
  uint32_t val;
} QDMA_REGS_2_MSG_DATA_H2_D_EVENT_INTERRUPT_t;
#endif /* GENHDR_STRUCT */

/* Vector Control for MSI-X Table Entries */
#define QDMA_REGS_2_VECTOR_CONTROL_H2_D_EVENT_INTERRUPT                 0x0010001CU
/*
 * When this bit is set, the function is prohibited from sending a message using
 * this MSI-X Table entry. However, any other MSI-X Table entries programmed
 * with the same vector will still be capable of sending an equivalent message
 * unless they are also masked. This bit's state after reset is 1 (entry is
 * masked). This bit is read/write. 
 */
#define QDMA_REGS_2_VECTOR_CONTROL_H2_D_EVENT_INTERRUPT_MASK_BIT_SHIFT  0
#define QDMA_REGS_2_VECTOR_CONTROL_H2_D_EVENT_INTERRUPT_MASK_BIT_WIDTH  1
#define QDMA_REGS_2_VECTOR_CONTROL_H2_D_EVENT_INTERRUPT_MASK_BIT_MASK   (0x1U << QDMA_REGS_2_VECTOR_CONTROL_H2_D_EVENT_INTERRUPT_MASK_BIT_SHIFT)

/*
 * After reset, the state of these bits must be 0. However, for potential future
 * use, software must preserve the value of these reserved bits when modifying
 * the value of other Vector Control bits. If software modifies the value of
 * these reserved bits, the result is undefined. 
 */
#define QDMA_REGS_2_VECTOR_CONTROL_H2_D_EVENT_INTERRUPT_RESERVED_SHIFT  1
#define QDMA_REGS_2_VECTOR_CONTROL_H2_D_EVENT_INTERRUPT_RESERVED_WIDTH  31
#define QDMA_REGS_2_VECTOR_CONTROL_H2_D_EVENT_INTERRUPT_RESERVED_MASK   (0x7FFFFFFFU << QDMA_REGS_2_VECTOR_CONTROL_H2_D_EVENT_INTERRUPT_RESERVED_SHIFT)

#ifdef GENHDR_STRUCT
/* Vector Control for MSI-X Table Entries */
typedef union {
  struct {
    uint32_t mask_bit : 1;      /* When this bit is set, the function is prohibited from sending a message using
                                   this MSI-X Table entry. However, any other MSI-X Table entries programmed
                                   with the same vector will still be capable of sending an equivalent message
                                   unless they are also masked. This bit's state after reset is 1 (entry is
                                   masked). This bit is read/write. */
    uint32_t reserved : 31;     /* After reset, the state of these bits must be 0. However, for potential future
                                   use, software must preserve the value of these reserved bits when modifying
                                   the value of other Vector Control bits. If software modifies the value of
                                   these reserved bits, the result is undefined. */
  } field;
  uint32_t val;
} QDMA_REGS_2_VECTOR_CONTROL_H2_D_EVENT_INTERRUPT_t;
#endif /* GENHDR_STRUCT */

/* Message Address for MSI-X Table Entries */
#define QDMA_REGS_2_MSG_ADDR_D2_H_DMA                                   0x00100020U
/*
 * Message_Address_1:Message_Address.
 * 
 * For proper DWORD alignment, software must always write zeroes to these two
 * bits; otherwise the result is undefined. The state of these bits after reset
 * must be 0. These bits are permitted to be read only or read/write. 
 */
#define QDMA_REGS_2_MSG_ADDR_D2_H_DMA_MESSAGE_ADDRESS_1_SHIFT           0
#define QDMA_REGS_2_MSG_ADDR_D2_H_DMA_MESSAGE_ADDRESS_1_WIDTH           2
#define QDMA_REGS_2_MSG_ADDR_D2_H_DMA_MESSAGE_ADDRESS_1_MASK            (0x3U << QDMA_REGS_2_MSG_ADDR_D2_H_DMA_MESSAGE_ADDRESS_1_SHIFT)

/*
 * System-specified message lower address. For MSI-X messages, the contents of
 * this field from an MSI-X Table entry specifies the lower portion of the
 * DWORD-aligned address (AD[31::02]) for the memory write transaction. This
 * field is read/write. 
 */
#define QDMA_REGS_2_MSG_ADDR_D2_H_DMA_MESSAGE_ADDRESS_SHIFT             2
#define QDMA_REGS_2_MSG_ADDR_D2_H_DMA_MESSAGE_ADDRESS_WIDTH             30
#define QDMA_REGS_2_MSG_ADDR_D2_H_DMA_MESSAGE_ADDRESS_MASK              (0x3FFFFFFFU << QDMA_REGS_2_MSG_ADDR_D2_H_DMA_MESSAGE_ADDRESS_SHIFT)

#ifdef GENHDR_STRUCT
/* Message Address for MSI-X Table Entries */
typedef union {
  struct {
    uint32_t message_address_1 : 2;     /* Message_Address_1:Message_Address.
                                           
                                           For proper DWORD alignment, software must always write zeroes to these two
                                           bits; otherwise the result is undefined. The state of these bits after reset
                                           must be 0. These bits are permitted to be read only or read/write. */
    uint32_t message_address : 30;      /* System-specified message lower address. For MSI-X messages, the contents of
                                           this field from an MSI-X Table entry specifies the lower portion of the
                                           DWORD-aligned address (AD[31::02]) for the memory write transaction. This
                                           field is read/write. */
  } field;
  uint32_t val;
} QDMA_REGS_2_MSG_ADDR_D2_H_DMA_t;
#endif /* GENHDR_STRUCT */

/* Message Upper Address for MSI-X Table Entries */
#define QDMA_REGS_2_MSG_UPPER_ADDR_D2_H_DMA                             0x00100024U
/*
 * System-specified message upper address bits. If this field is zero, Single
 * Address Cycle (SAC) messages are used. If this field is non-zero, Dual
 * Address Cycle (DAC) messages are used. This field is read/write. 
 */
#define QDMA_REGS_2_MSG_UPPER_ADDR_D2_H_DMA_MESSAGE_UPPER_ADDRESS_SHIFT 0
#define QDMA_REGS_2_MSG_UPPER_ADDR_D2_H_DMA_MESSAGE_UPPER_ADDRESS_WIDTH 32
#define QDMA_REGS_2_MSG_UPPER_ADDR_D2_H_DMA_MESSAGE_UPPER_ADDRESS_MASK  (0xFFFFFFFFU << QDMA_REGS_2_MSG_UPPER_ADDR_D2_H_DMA_MESSAGE_UPPER_ADDRESS_SHIFT)

#ifdef GENHDR_STRUCT
/* Message Upper Address for MSI-X Table Entries */
typedef union {
  struct {
    uint32_t message_upper_address : 32;        /* System-specified message upper address bits. If this field is zero, Single
                                                   Address Cycle (SAC) messages are used. If this field is non-zero, Dual
                                                   Address Cycle (DAC) messages are used. This field is read/write. */
  } field;
  uint32_t val;
} QDMA_REGS_2_MSG_UPPER_ADDR_D2_H_DMA_t;
#endif /* GENHDR_STRUCT */

/* Message Data for MSI-X Table Entries */
#define QDMA_REGS_2_MSG_DATA_D2_H_DMA                           0x00100028U
/*
 * System-specified message data. For MSI-X messages, the contents of this field
 * from an MSI-X Table entry specifies the data driven on AD[31::00] during the
 * memory write transaction's data phase. C/BE[3::0]# are asserted during the
 * data phase of the memory write transaction. In contrast to message data used
 * for MSI messages, the low-order message data bits in MSI-X messages are not
 * modified by the function. This field is read/write. . 
 */
#define QDMA_REGS_2_MSG_DATA_D2_H_DMA_MESSAGE_DATA_SHIFT        0
#define QDMA_REGS_2_MSG_DATA_D2_H_DMA_MESSAGE_DATA_WIDTH        32
#define QDMA_REGS_2_MSG_DATA_D2_H_DMA_MESSAGE_DATA_MASK         (0xFFFFFFFFU << QDMA_REGS_2_MSG_DATA_D2_H_DMA_MESSAGE_DATA_SHIFT)

#ifdef GENHDR_STRUCT
/* Message Data for MSI-X Table Entries */
typedef union {
  struct {
    uint32_t message_data : 32; /* System-specified message data. For MSI-X messages, the contents of this field
                                   from an MSI-X Table entry specifies the data driven on AD[31::00] during the
                                   memory write transaction's data phase. C/BE[3::0]# are asserted during the
                                   data phase of the memory write transaction. In contrast to message data used
                                   for MSI messages, the low-order message data bits in MSI-X messages are not
                                   modified by the function. This field is read/write. . */
  } field;
  uint32_t val;
} QDMA_REGS_2_MSG_DATA_D2_H_DMA_t;
#endif /* GENHDR_STRUCT */

/* Vector Control for MSI-X Table Entries */
#define QDMA_REGS_2_VECTOR_CONTROL_D2_H_DMA                     0x0010002CU
/*
 * When this bit is set, the function is prohibited from sending a message using
 * this MSI-X Table entry. However, any other MSI-X Table entries programmed
 * with the same vector will still be capable of sending an equivalent message
 * unless they are also masked. This bit's state after reset is 1 (entry is
 * masked). This bit is read/write. 
 */
#define QDMA_REGS_2_VECTOR_CONTROL_D2_H_DMA_MASK_BIT_SHIFT      0
#define QDMA_REGS_2_VECTOR_CONTROL_D2_H_DMA_MASK_BIT_WIDTH      1
#define QDMA_REGS_2_VECTOR_CONTROL_D2_H_DMA_MASK_BIT_MASK       (0x1U << QDMA_REGS_2_VECTOR_CONTROL_D2_H_DMA_MASK_BIT_SHIFT)

/*
 * After reset, the state of these bits must be 0. However, for potential future
 * use, software must preserve the value of these reserved bits when modifying
 * the value of other Vector Control bits. If software modifies the value of
 * these reserved bits, the result is undefined. 
 */
#define QDMA_REGS_2_VECTOR_CONTROL_D2_H_DMA_RESERVED_SHIFT      1
#define QDMA_REGS_2_VECTOR_CONTROL_D2_H_DMA_RESERVED_WIDTH      31
#define QDMA_REGS_2_VECTOR_CONTROL_D2_H_DMA_RESERVED_MASK       (0x7FFFFFFFU << QDMA_REGS_2_VECTOR_CONTROL_D2_H_DMA_RESERVED_SHIFT)

#ifdef GENHDR_STRUCT
/* Vector Control for MSI-X Table Entries */
typedef union {
  struct {
    uint32_t mask_bit : 1;      /* When this bit is set, the function is prohibited from sending a message using
                                   this MSI-X Table entry. However, any other MSI-X Table entries programmed
                                   with the same vector will still be capable of sending an equivalent message
                                   unless they are also masked. This bit's state after reset is 1 (entry is
                                   masked). This bit is read/write. */
    uint32_t reserved : 31;     /* After reset, the state of these bits must be 0. However, for potential future
                                   use, software must preserve the value of these reserved bits when modifying
                                   the value of other Vector Control bits. If software modifies the value of
                                   these reserved bits, the result is undefined. */
  } field;
  uint32_t val;
} QDMA_REGS_2_VECTOR_CONTROL_D2_H_DMA_t;
#endif /* GENHDR_STRUCT */

/* Message Address for MSI-X Table Entries */
#define QDMA_REGS_2_MSG_ADDR_D2_H_EVENT_INTERRUPT                       0x00100030U
/*
 * Message_Address_1:Message_Address.
 * 
 * For proper DWORD alignment, software must always write zeroes to these two
 * bits; otherwise the result is undefined. The state of these bits after reset
 * must be 0. These bits are permitted to be read only or read/write. 
 */
#define QDMA_REGS_2_MSG_ADDR_D2_H_EVENT_INTERRUPT_MESSAGE_ADDRESS_1_SHIFT 0
#define QDMA_REGS_2_MSG_ADDR_D2_H_EVENT_INTERRUPT_MESSAGE_ADDRESS_1_WIDTH 2
#define QDMA_REGS_2_MSG_ADDR_D2_H_EVENT_INTERRUPT_MESSAGE_ADDRESS_1_MASK (0x3U << QDMA_REGS_2_MSG_ADDR_D2_H_EVENT_INTERRUPT_MESSAGE_ADDRESS_1_SHIFT)

/*
 * System-specified message lower address. For MSI-X messages, the contents of
 * this field from an MSI-X Table entry specifies the lower portion of the
 * DWORD-aligned address (AD[31::02]) for the memory write transaction. This
 * field is read/write. 
 */
#define QDMA_REGS_2_MSG_ADDR_D2_H_EVENT_INTERRUPT_MESSAGE_ADDRESS_SHIFT 2
#define QDMA_REGS_2_MSG_ADDR_D2_H_EVENT_INTERRUPT_MESSAGE_ADDRESS_WIDTH 30
#define QDMA_REGS_2_MSG_ADDR_D2_H_EVENT_INTERRUPT_MESSAGE_ADDRESS_MASK  (0x3FFFFFFFU << QDMA_REGS_2_MSG_ADDR_D2_H_EVENT_INTERRUPT_MESSAGE_ADDRESS_SHIFT)

#ifdef GENHDR_STRUCT
/* Message Address for MSI-X Table Entries */
typedef union {
  struct {
    uint32_t message_address_1 : 2;     /* Message_Address_1:Message_Address.
                                           
                                           For proper DWORD alignment, software must always write zeroes to these two
                                           bits; otherwise the result is undefined. The state of these bits after reset
                                           must be 0. These bits are permitted to be read only or read/write. */
    uint32_t message_address : 30;      /* System-specified message lower address. For MSI-X messages, the contents of
                                           this field from an MSI-X Table entry specifies the lower portion of the
                                           DWORD-aligned address (AD[31::02]) for the memory write transaction. This
                                           field is read/write. */
  } field;
  uint32_t val;
} QDMA_REGS_2_MSG_ADDR_D2_H_EVENT_INTERRUPT_t;
#endif /* GENHDR_STRUCT */

/* Message Upper Address for MSI-X Table Entries */
#define QDMA_REGS_2_MSG_UPPER_ADDR_D2_H_EVENT_INTERRUPT                 0x00100034U
/*
 * System-specified message upper address bits. If this field is zero, Single
 * Address Cycle (SAC) messages are used. If this field is non-zero, Dual
 * Address Cycle (DAC) messages are used. This field is read/write. 
 */
#define QDMA_REGS_2_MSG_UPPER_ADDR_D2_H_EVENT_INTERRUPT_MESSAGE_UPPER_ADDRESS_SHIFT 0
#define QDMA_REGS_2_MSG_UPPER_ADDR_D2_H_EVENT_INTERRUPT_MESSAGE_UPPER_ADDRESS_WIDTH 32
#define QDMA_REGS_2_MSG_UPPER_ADDR_D2_H_EVENT_INTERRUPT_MESSAGE_UPPER_ADDRESS_MASK (0xFFFFFFFFU << QDMA_REGS_2_MSG_UPPER_ADDR_D2_H_EVENT_INTERRUPT_MESSAGE_UPPER_ADDRESS_SHIFT)

#ifdef GENHDR_STRUCT
/* Message Upper Address for MSI-X Table Entries */
typedef union {
  struct {
    uint32_t message_upper_address : 32;        /* System-specified message upper address bits. If this field is zero, Single
                                                   Address Cycle (SAC) messages are used. If this field is non-zero, Dual
                                                   Address Cycle (DAC) messages are used. This field is read/write. */
  } field;
  uint32_t val;
} QDMA_REGS_2_MSG_UPPER_ADDR_D2_H_EVENT_INTERRUPT_t;
#endif /* GENHDR_STRUCT */

/* Message Data for MSI-X Table Entries */
#define QDMA_REGS_2_MSG_DATA_D2_H_EVENT_INTERRUPT                       0x00100038U
/*
 * System-specified message data. For MSI-X messages, the contents of this field
 * from an MSI-X Table entry specifies the data driven on AD[31::00] during the
 * memory write transaction's data phase. C/BE[3::0]# are asserted during the
 * data phase of the memory write transaction. In contrast to message data used
 * for MSI messages, the low-order message data bits in MSI-X messages are not
 * modified by the function. This field is read/write. . 
 */
#define QDMA_REGS_2_MSG_DATA_D2_H_EVENT_INTERRUPT_MESSAGE_DATA_SHIFT    0
#define QDMA_REGS_2_MSG_DATA_D2_H_EVENT_INTERRUPT_MESSAGE_DATA_WIDTH    32
#define QDMA_REGS_2_MSG_DATA_D2_H_EVENT_INTERRUPT_MESSAGE_DATA_MASK     (0xFFFFFFFFU << QDMA_REGS_2_MSG_DATA_D2_H_EVENT_INTERRUPT_MESSAGE_DATA_SHIFT)

#ifdef GENHDR_STRUCT
/* Message Data for MSI-X Table Entries */
typedef union {
  struct {
    uint32_t message_data : 32; /* System-specified message data. For MSI-X messages, the contents of this field
                                   from an MSI-X Table entry specifies the data driven on AD[31::00] during the
                                   memory write transaction's data phase. C/BE[3::0]# are asserted during the
                                   data phase of the memory write transaction. In contrast to message data used
                                   for MSI messages, the low-order message data bits in MSI-X messages are not
                                   modified by the function. This field is read/write. . */
  } field;
  uint32_t val;
} QDMA_REGS_2_MSG_DATA_D2_H_EVENT_INTERRUPT_t;
#endif /* GENHDR_STRUCT */

/* Vector Control for MSI-X Table Entries */
#define QDMA_REGS_2_VECTOR_CONTROL_D2_H_EVENT_INTERRUPT                 0x0010003CU
/*
 * When this bit is set, the function is prohibited from sending a message using
 * this MSI-X Table entry. However, any other MSI-X Table entries programmed
 * with the same vector will still be capable of sending an equivalent message
 * unless they are also masked. This bit's state after reset is 1 (entry is
 * masked). This bit is read/write. 
 */
#define QDMA_REGS_2_VECTOR_CONTROL_D2_H_EVENT_INTERRUPT_MASK_BIT_SHIFT  0
#define QDMA_REGS_2_VECTOR_CONTROL_D2_H_EVENT_INTERRUPT_MASK_BIT_WIDTH  1
#define QDMA_REGS_2_VECTOR_CONTROL_D2_H_EVENT_INTERRUPT_MASK_BIT_MASK   (0x1U << QDMA_REGS_2_VECTOR_CONTROL_D2_H_EVENT_INTERRUPT_MASK_BIT_SHIFT)

/*
 * After reset, the state of these bits must be 0. However, for potential future
 * use, software must preserve the value of these reserved bits when modifying
 * the value of other Vector Control bits. If software modifies the value of
 * these reserved bits, the result is undefined. 
 */
#define QDMA_REGS_2_VECTOR_CONTROL_D2_H_EVENT_INTERRUPT_RESERVED_SHIFT  1
#define QDMA_REGS_2_VECTOR_CONTROL_D2_H_EVENT_INTERRUPT_RESERVED_WIDTH  31
#define QDMA_REGS_2_VECTOR_CONTROL_D2_H_EVENT_INTERRUPT_RESERVED_MASK   (0x7FFFFFFFU << QDMA_REGS_2_VECTOR_CONTROL_D2_H_EVENT_INTERRUPT_RESERVED_SHIFT)

#ifdef GENHDR_STRUCT
/* Vector Control for MSI-X Table Entries */
typedef union {
  struct {
    uint32_t mask_bit : 1;      /* When this bit is set, the function is prohibited from sending a message using
                                   this MSI-X Table entry. However, any other MSI-X Table entries programmed
                                   with the same vector will still be capable of sending an equivalent message
                                   unless they are also masked. This bit's state after reset is 1 (entry is
                                   masked). This bit is read/write. */
    uint32_t reserved : 31;     /* After reset, the state of these bits must be 0. However, for potential future
                                   use, software must preserve the value of these reserved bits when modifying
                                   the value of other Vector Control bits. If software modifies the value of
                                   these reserved bits, the result is undefined. */
  } field;
  uint32_t val;
} QDMA_REGS_2_VECTOR_CONTROL_D2_H_EVENT_INTERRUPT_t;
#endif /* GENHDR_STRUCT */

/* Pending Bits for MSI-X PBA Entries. */
#define QDMA_REGS_2_MSIX_PBA                            0x00180000U
#define QDMA_REGS_2_MSIX_PBA_WIDTH                      64
/*
 * For each Pending Bit that is set, the function has a pending message for the
 * associated MSI-X Table entry. Pending bits that have no associated MSI-X
 * Table entry are reserved. After reset, the state of reserved Pending bits
 * must be 0. Software should never write, and should only read Pending Bits. If
 * software writes to Pending Bits, the result is undefined. Each Pending Bit's
 * state after reset is 0 (no message pending). These bits are permitted to be
 * read only or read/write. 
 */
#define QDMA_REGS_2_MSIX_PBA_PENDING_BITS_ACCESSTYPE    GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_MSIX_PBA_PENDING_BITS_SHIFT         0
#define QDMA_REGS_2_MSIX_PBA_PENDING_BITS_WIDTH         64
#define QDMA_REGS_2_MSIX_PBA_PENDING_BITS_MASK          (0xFFFFFFFFFFFFFFFFULL << QDMA_REGS_2_MSIX_PBA_PENDING_BITS_SHIFT)

#ifdef GENHDR_STRUCT
/* Pending Bits for MSI-X PBA Entries. */
typedef union {
  struct {
    uint32_t pending_bits_0_31 : 32;    /* For each Pending Bit that is set, the function has a pending message for the
                                           associated MSI-X Table entry. Pending bits that have no associated MSI-X
                                           Table entry are reserved. After reset, the state of reserved Pending bits
                                           must be 0. Software should never write, and should only read Pending Bits. If
                                           software writes to Pending Bits, the result is undefined. Each Pending Bit's
                                           state after reset is 0 (no message pending). These bits are permitted to be
                                           read only or read/write. */
    uint32_t pending_bits_32_63 : 32;
  } field;
  uint32_t val[2];
} QDMA_REGS_2_MSIX_PBA_t;
#endif /* GENHDR_STRUCT */

/* CTRL */
#define QDMA_REGS_2_CTRL                0x00200000U
/* Reserved. */
#define QDMA_REGS_2_CTRL_RSVD_ACCESSTYPE GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_CTRL_RSVD_SHIFT     0
#define QDMA_REGS_2_CTRL_RSVD_WIDTH     32
#define QDMA_REGS_2_CTRL_RSVD_MASK      (0xFFFFFFFFU << QDMA_REGS_2_CTRL_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* CTRL */
typedef union {
  struct {
    uint32_t rsvd : 32; /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_CTRL_t;
#endif /* GENHDR_STRUCT */

/* RESERVED */
#define QDMA_REGS_2_RESERVED                    0x00200004U
/* Reserved. */
#define QDMA_REGS_2_RESERVED_RESERVED_ACCESSTYPE GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_RESERVED_RESERVED_SHIFT     0
#define QDMA_REGS_2_RESERVED_RESERVED_WIDTH     32
#define QDMA_REGS_2_RESERVED_RESERVED_MASK      (0xFFFFFFFFU << QDMA_REGS_2_RESERVED_RESERVED_SHIFT)

#ifdef GENHDR_STRUCT
/* RESERVED */
typedef union {
  struct {
    uint32_t reserved : 32;     /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_RESERVED_t;
#endif /* GENHDR_STRUCT */

/* WB_INTR_DELAY */
#define QDMA_REGS_2_WB_INTR_DELAY                               0x00200008U
/*
 * Delay the write-back and/or the MSIX interrupt until the time elapsed from a
 * prior write-back/interrupt exceeds the delay value in this register.Each unit
 * is 2ns.
 */
#define QDMA_REGS_2_WB_INTR_DELAY_WB_INTR_DELAY_SHIFT           0
#define QDMA_REGS_2_WB_INTR_DELAY_WB_INTR_DELAY_WIDTH           20
#define QDMA_REGS_2_WB_INTR_DELAY_WB_INTR_DELAY_MASK            (0xFFFFFU << QDMA_REGS_2_WB_INTR_DELAY_WB_INTR_DELAY_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_WB_INTR_DELAY_RSVD_ACCESSTYPE               GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_WB_INTR_DELAY_RSVD_SHIFT                    20
#define QDMA_REGS_2_WB_INTR_DELAY_RSVD_WIDTH                    12
#define QDMA_REGS_2_WB_INTR_DELAY_RSVD_MASK                     (0xFFFU << QDMA_REGS_2_WB_INTR_DELAY_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* WB_INTR_DELAY */
typedef union {
  struct {
    uint32_t wb_intr_delay : 20;        /* Delay the write-back and/or the MSIX interrupt until the time elapsed from a
                                           prior write-back/interrupt exceeds the delay value in this register.Each unit
                                           is 2ns. */
    uint32_t rsvd : 12;                 /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_WB_INTR_DELAY_t;
#endif /* GENHDR_STRUCT */

/* RSVD */
#define QDMA_REGS_2_RSVD                0x0020000CU
/*
 * Reserved.
 * 
 * It occupies the offset range of 8'h0C - 8'h6F
 */
#define QDMA_REGS_2_RSVD_RSVD_ACCESSTYPE GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_RSVD_RSVD_SHIFT     0
#define QDMA_REGS_2_RSVD_RSVD_WIDTH     1
#define QDMA_REGS_2_RSVD_RSVD_MASK      (0x1U << QDMA_REGS_2_RSVD_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* RSVD */
typedef union {
  struct {
    uint32_t rsvd : 1;  /* Reserved.
                           
                           It occupies the offset range of 8'h0C - 8'h6F */
    uint32_t : 31;
  } field;
  uint32_t val;
} QDMA_REGS_2_RSVD_t;
#endif /* GENHDR_STRUCT */

/* VER_NUM */
#define QDMA_REGS_2_VER_NUM                     0x00200070U
/* Minor version number of QDMA IP. */
#define QDMA_REGS_2_VER_NUM_MIN_VER_ACCESSTYPE  GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_VER_NUM_MIN_VER_SHIFT       0
#define QDMA_REGS_2_VER_NUM_MIN_VER_WIDTH       8
#define QDMA_REGS_2_VER_NUM_MIN_VER_MASK        (0xFFU << QDMA_REGS_2_VER_NUM_MIN_VER_SHIFT)

/* Major version number of QDMA IP. */
#define QDMA_REGS_2_VER_NUM_MAJ_VER_ACCESSTYPE  GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_VER_NUM_MAJ_VER_SHIFT       8
#define QDMA_REGS_2_VER_NUM_MAJ_VER_WIDTH       8
#define QDMA_REGS_2_VER_NUM_MAJ_VER_MASK        (0xFFU << QDMA_REGS_2_VER_NUM_MAJ_VER_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_VER_NUM_RSVD_ACCESSTYPE     GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_VER_NUM_RSVD_SHIFT          16
#define QDMA_REGS_2_VER_NUM_RSVD_WIDTH          16
#define QDMA_REGS_2_VER_NUM_RSVD_MASK           (0xFFFFU << QDMA_REGS_2_VER_NUM_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* VER_NUM */
typedef union {
  struct {
    uint32_t min_ver : 8;       /* Minor version number of QDMA IP. */
    uint32_t maj_ver : 8;       /* Major version number of QDMA IP. */
    uint32_t rsvd : 16;         /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_VER_NUM_t;
#endif /* GENHDR_STRUCT */

/* IP_PARAM_1 */
#define QDMA_REGS_2_IP_PARAM_1                          0x00200074U
/*
 * Data with of AVST interface between HIP and QDMA IP. 1'b0= 256bits,
 * 1'b1=512bits.
 */
#define QDMA_REGS_2_IP_PARAM_1_DW_ACCESSTYPE            GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_IP_PARAM_1_DW_SHIFT                 0
#define QDMA_REGS_2_IP_PARAM_1_DW_WIDTH                 1
#define QDMA_REGS_2_IP_PARAM_1_DW_MASK                  (0x1U << QDMA_REGS_2_IP_PARAM_1_DW_SHIFT)

/*
 * rsvd_2:rsvd.
 * 
 * RESERVED.
 */
#define QDMA_REGS_2_IP_PARAM_1_RSVD_2_ACCESSTYPE        GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_IP_PARAM_1_RSVD_2_SHIFT             1
#define QDMA_REGS_2_IP_PARAM_1_RSVD_2_WIDTH             1
#define QDMA_REGS_2_IP_PARAM_1_RSVD_2_MASK              (0x1U << QDMA_REGS_2_IP_PARAM_1_RSVD_2_SHIFT)

/* Completion re-ordering is enabled in QDMA IP. 1'b0= Disabled, 1'b1=Enabled. */
#define QDMA_REGS_2_IP_PARAM_1_CR_EN_ACCESSTYPE         GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_IP_PARAM_1_CR_EN_SHIFT              2
#define QDMA_REGS_2_IP_PARAM_1_CR_EN_WIDTH              1
#define QDMA_REGS_2_IP_PARAM_1_CR_EN_MASK               (0x1U << QDMA_REGS_2_IP_PARAM_1_CR_EN_SHIFT)

/*
 * rsvd_1:rsvd.
 * 
 * RESERVED.
 */
#define QDMA_REGS_2_IP_PARAM_1_RSVD_1_ACCESSTYPE        GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_IP_PARAM_1_RSVD_1_SHIFT             3
#define QDMA_REGS_2_IP_PARAM_1_RSVD_1_WIDTH             1
#define QDMA_REGS_2_IP_PARAM_1_RSVD_1_MASK              (0x1U << QDMA_REGS_2_IP_PARAM_1_RSVD_1_SHIFT)

/* Number of PF set for QDMA IP.4'd1 - 4'd8. */
#define QDMA_REGS_2_IP_PARAM_1_NUM_PF_ACCESSTYPE        GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_IP_PARAM_1_NUM_PF_SHIFT             4
#define QDMA_REGS_2_IP_PARAM_1_NUM_PF_WIDTH             4
#define QDMA_REGS_2_IP_PARAM_1_NUM_PF_MASK              (0xFU << QDMA_REGS_2_IP_PARAM_1_NUM_PF_SHIFT)

/*
 * Number if user interface data path ports (AVST or AVMM) supported values: 1,
 * 2, 4.
 * 
 * The value indicates the number of source and sink interfaces. Ex: value 1
 * means a source and a sink interface.
 */
#define QDMA_REGS_2_IP_PARAM_1_NUM_UPORT_ACCESSTYPE     GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_IP_PARAM_1_NUM_UPORT_SHIFT          8
#define QDMA_REGS_2_IP_PARAM_1_NUM_UPORT_WIDTH          4
#define QDMA_REGS_2_IP_PARAM_1_NUM_UPORT_MASK           (0xFU << QDMA_REGS_2_IP_PARAM_1_NUM_UPORT_SHIFT)

/* Type of user interface per port. 1'b0=AVMM. 1'b1=AVST. */
#define QDMA_REGS_2_IP_PARAM_1_UPORT_TYPE_ACCESSTYPE    GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_IP_PARAM_1_UPORT_TYPE_SHIFT         12
#define QDMA_REGS_2_IP_PARAM_1_UPORT_TYPE_WIDTH         4
#define QDMA_REGS_2_IP_PARAM_1_UPORT_TYPE_MASK          (0xFU << QDMA_REGS_2_IP_PARAM_1_UPORT_TYPE_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_IP_PARAM_1_RSVD_ACCESSTYPE          GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_IP_PARAM_1_RSVD_SHIFT               16
#define QDMA_REGS_2_IP_PARAM_1_RSVD_WIDTH               16
#define QDMA_REGS_2_IP_PARAM_1_RSVD_MASK                (0xFFFFU << QDMA_REGS_2_IP_PARAM_1_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* IP_PARAM_1 */
typedef union {
  struct {
    uint32_t dw : 1;            /* Data with of AVST interface between HIP and QDMA IP. 1'b0= 256bits,
                                   1'b1=512bits. */
    uint32_t rsvd_2 : 1;        /* rsvd_2:rsvd.
                                   
                                   RESERVED. */
    uint32_t cr_en : 1;         /* Completion re-ordering is enabled in QDMA IP. 1'b0= Disabled, 1'b1=Enabled. */
    uint32_t rsvd_1 : 1;        /* rsvd_1:rsvd.
                                   
                                   RESERVED. */
    uint32_t num_pf : 4;        /* Number of PF set for QDMA IP.4'd1 - 4'd8. */
    uint32_t num_uport : 4;     /* Number if user interface data path ports (AVST or AVMM) supported values: 1,
                                   2, 4.
                                   
                                   The value indicates the number of source and sink interfaces. Ex: value 1
                                   means a source and a sink interface. */
    uint32_t uport_type : 4;    /* Type of user interface per port. 1'b0=AVMM. 1'b1=AVST. */
    uint32_t rsvd : 16;         /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_IP_PARAM_1_t;
#endif /* GENHDR_STRUCT */

/* PF0_IP_PARAM_1 */
#define QDMA_REGS_2_PF0_IP_PARAM_1                              0x00200080U
/* Number of VFs per PF set for QDMA IP. */
#define QDMA_REGS_2_PF0_IP_PARAM_1_NUM_VF_PER_PF_ACCESSTYPE     GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF0_IP_PARAM_1_NUM_VF_PER_PF_SHIFT          0
#define QDMA_REGS_2_PF0_IP_PARAM_1_NUM_VF_PER_PF_WIDTH          16
#define QDMA_REGS_2_PF0_IP_PARAM_1_NUM_VF_PER_PF_MASK           (0xFFFFU << QDMA_REGS_2_PF0_IP_PARAM_1_NUM_VF_PER_PF_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_PF0_IP_PARAM_1_RSVD_ACCESSTYPE              GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF0_IP_PARAM_1_RSVD_SHIFT                   16
#define QDMA_REGS_2_PF0_IP_PARAM_1_RSVD_WIDTH                   15
#define QDMA_REGS_2_PF0_IP_PARAM_1_RSVD_MASK                    (0x7FFFU << QDMA_REGS_2_PF0_IP_PARAM_1_RSVD_SHIFT)

/* sriov is enabled in QDMA IP. 1'b0= Disabled, 1'b1=Enabled. */
#define QDMA_REGS_2_PF0_IP_PARAM_1_SRIOV_ENABLED_ACCESSTYPE     GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF0_IP_PARAM_1_SRIOV_ENABLED_SHIFT          31
#define QDMA_REGS_2_PF0_IP_PARAM_1_SRIOV_ENABLED_WIDTH          1
#define QDMA_REGS_2_PF0_IP_PARAM_1_SRIOV_ENABLED_MASK           (0x1U << QDMA_REGS_2_PF0_IP_PARAM_1_SRIOV_ENABLED_SHIFT)

#ifdef GENHDR_STRUCT
/* PF0_IP_PARAM_1 */
typedef union {
  struct {
    uint32_t num_vf_per_pf : 16;        /* Number of VFs per PF set for QDMA IP. */
    uint32_t rsvd : 15;                 /* Reserved. */
    uint32_t sriov_enabled : 1;         /* sriov is enabled in QDMA IP. 1'b0= Disabled, 1'b1=Enabled. */
  } field;
  uint32_t val;
} QDMA_REGS_2_PF0_IP_PARAM_1_t;
#endif /* GENHDR_STRUCT */

/* PF0_IP_PARAM_2 */
#define QDMA_REGS_2_PF0_IP_PARAM_2                                      0x00200084U
/* Number of DMA channels per VF set for QDMA IP. */
#define QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_ACCESSTYPE       GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_SHIFT            0
#define QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_WIDTH            16
#define QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_MASK             (0xFFFFU << QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_SHIFT)

/* Number of DMA channels per PF set for QDMA IP. */
#define QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_ACCESSTYPE       GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_SHIFT            16
#define QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_WIDTH            16
#define QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_MASK             (0xFFFFU << QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_SHIFT)

#ifdef GENHDR_STRUCT
/* PF0_IP_PARAM_2 */
typedef union {
  struct {
    uint32_t num_dma_chan_per_vf : 16;  /* Number of DMA channels per VF set for QDMA IP. */
    uint32_t num_dma_chan_per_pf : 16;  /* Number of DMA channels per PF set for QDMA IP. */
  } field;
  uint32_t val;
} QDMA_REGS_2_PF0_IP_PARAM_2_t;
#endif /* GENHDR_STRUCT */

/* RSVD */
#define QDMA_REGS_2_RSVD_1                      0x00200088U
#define QDMA_REGS_2_RSVD_1_WIDTH                64
/*
 * Reserved.
 * 
 * offset 8'h88 - 8'h8F
 */
#define QDMA_REGS_2_RSVD_1_RSVD_ACCESSTYPE      GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_RSVD_1_RSVD_SHIFT           0
#define QDMA_REGS_2_RSVD_1_RSVD_WIDTH           64
#define QDMA_REGS_2_RSVD_1_RSVD_MASK            (0xFFFFFFFFFFFFFFFFULL << QDMA_REGS_2_RSVD_1_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* RSVD */
typedef union {
  struct {
    uint32_t rsvd_0_31 : 32;    /* Reserved.
                                   
                                   offset 8'h88 - 8'h8F */
    uint32_t rsvd_32_63 : 32;
  } field;
  uint32_t val[2];
} QDMA_REGS_2_RSVD_1_t;
#endif /* GENHDR_STRUCT */

/* PF1_IP_PARAM_1 */
#define QDMA_REGS_2_PF1_IP_PARAM_1                              0x00200090U
/* Number of VFs per PF set for QDMA IP. */
#define QDMA_REGS_2_PF1_IP_PARAM_1_NUM_VF_PER_PF_ACCESSTYPE     GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF1_IP_PARAM_1_NUM_VF_PER_PF_SHIFT          0
#define QDMA_REGS_2_PF1_IP_PARAM_1_NUM_VF_PER_PF_WIDTH          16
#define QDMA_REGS_2_PF1_IP_PARAM_1_NUM_VF_PER_PF_MASK           (0xFFFFU << QDMA_REGS_2_PF1_IP_PARAM_1_NUM_VF_PER_PF_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_PF1_IP_PARAM_1_RSVD_ACCESSTYPE              GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF1_IP_PARAM_1_RSVD_SHIFT                   16
#define QDMA_REGS_2_PF1_IP_PARAM_1_RSVD_WIDTH                   15
#define QDMA_REGS_2_PF1_IP_PARAM_1_RSVD_MASK                    (0x7FFFU << QDMA_REGS_2_PF1_IP_PARAM_1_RSVD_SHIFT)

/* sriov is enabled in QDMA IP. 1'b0= Disabled, 1'b1=Enabled. */
#define QDMA_REGS_2_PF1_IP_PARAM_1_SRIOV_ENABLED_ACCESSTYPE     GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF1_IP_PARAM_1_SRIOV_ENABLED_SHIFT          31
#define QDMA_REGS_2_PF1_IP_PARAM_1_SRIOV_ENABLED_WIDTH          1
#define QDMA_REGS_2_PF1_IP_PARAM_1_SRIOV_ENABLED_MASK           (0x1U << QDMA_REGS_2_PF1_IP_PARAM_1_SRIOV_ENABLED_SHIFT)

#ifdef GENHDR_STRUCT
/* PF1_IP_PARAM_1 */
typedef union {
  struct {
    uint32_t num_vf_per_pf : 16;        /* Number of VFs per PF set for QDMA IP. */
    uint32_t rsvd : 15;                 /* Reserved. */
    uint32_t sriov_enabled : 1;         /* sriov is enabled in QDMA IP. 1'b0= Disabled, 1'b1=Enabled. */
  } field;
  uint32_t val;
} QDMA_REGS_2_PF1_IP_PARAM_1_t;
#endif /* GENHDR_STRUCT */

/* PF1_IP_PARAM_2 */
#define QDMA_REGS_2_PF1_IP_PARAM_2                                      0x00200094U
/* Number of DMA channels per VF set for QDMA IP. */
#define QDMA_REGS_2_PF1_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_ACCESSTYPE       GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF1_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_SHIFT            0
#define QDMA_REGS_2_PF1_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_WIDTH            16
#define QDMA_REGS_2_PF1_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_MASK             (0xFFFFU << QDMA_REGS_2_PF1_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_SHIFT)

/* Number of DMA channels per PF set for QDMA IP. */
#define QDMA_REGS_2_PF1_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_ACCESSTYPE       GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF1_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_SHIFT            16
#define QDMA_REGS_2_PF1_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_WIDTH            16
#define QDMA_REGS_2_PF1_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_MASK             (0xFFFFU << QDMA_REGS_2_PF1_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_SHIFT)

#ifdef GENHDR_STRUCT
/* PF1_IP_PARAM_2 */
typedef union {
  struct {
    uint32_t num_dma_chan_per_vf : 16;  /* Number of DMA channels per VF set for QDMA IP. */
    uint32_t num_dma_chan_per_pf : 16;  /* Number of DMA channels per PF set for QDMA IP. */
  } field;
  uint32_t val;
} QDMA_REGS_2_PF1_IP_PARAM_2_t;
#endif /* GENHDR_STRUCT */

/* RSVD */
#define QDMA_REGS_2_RSVD_2                      0x00200098U
#define QDMA_REGS_2_RSVD_2_WIDTH                64
/*
 * Reserved.
 * 
 * offset 8'h98 - 8'h9F
 */
#define QDMA_REGS_2_RSVD_2_RSVD_ACCESSTYPE      GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_RSVD_2_RSVD_SHIFT           0
#define QDMA_REGS_2_RSVD_2_RSVD_WIDTH           64
#define QDMA_REGS_2_RSVD_2_RSVD_MASK            (0xFFFFFFFFFFFFFFFFULL << QDMA_REGS_2_RSVD_2_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* RSVD */
typedef union {
  struct {
    uint32_t rsvd_0_31 : 32;    /* Reserved.
                                   
                                   offset 8'h98 - 8'h9F */
    uint32_t rsvd_32_63 : 32;
  } field;
  uint32_t val[2];
} QDMA_REGS_2_RSVD_2_t;
#endif /* GENHDR_STRUCT */

/* PF2_IP_PARAM_1 */
#define QDMA_REGS_2_PF2_IP_PARAM_1                              0x002000A0U
/* Number of VFs per PF set for QDMA IP. */
#define QDMA_REGS_2_PF2_IP_PARAM_1_NUM_VF_PER_PF_ACCESSTYPE     GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF2_IP_PARAM_1_NUM_VF_PER_PF_SHIFT          0
#define QDMA_REGS_2_PF2_IP_PARAM_1_NUM_VF_PER_PF_WIDTH          16
#define QDMA_REGS_2_PF2_IP_PARAM_1_NUM_VF_PER_PF_MASK           (0xFFFFU << QDMA_REGS_2_PF2_IP_PARAM_1_NUM_VF_PER_PF_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_PF2_IP_PARAM_1_RSVD_ACCESSTYPE              GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF2_IP_PARAM_1_RSVD_SHIFT                   16
#define QDMA_REGS_2_PF2_IP_PARAM_1_RSVD_WIDTH                   15
#define QDMA_REGS_2_PF2_IP_PARAM_1_RSVD_MASK                    (0x7FFFU << QDMA_REGS_2_PF2_IP_PARAM_1_RSVD_SHIFT)

/* sriov is enabled in QDMA IP. 1'b0= Disabled, 1'b1=Enabled. */
#define QDMA_REGS_2_PF2_IP_PARAM_1_SRIOV_ENABLED_ACCESSTYPE     GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF2_IP_PARAM_1_SRIOV_ENABLED_SHIFT          31
#define QDMA_REGS_2_PF2_IP_PARAM_1_SRIOV_ENABLED_WIDTH          1
#define QDMA_REGS_2_PF2_IP_PARAM_1_SRIOV_ENABLED_MASK           (0x1U << QDMA_REGS_2_PF2_IP_PARAM_1_SRIOV_ENABLED_SHIFT)

#ifdef GENHDR_STRUCT
/* PF2_IP_PARAM_1 */
typedef union {
  struct {
    uint32_t num_vf_per_pf : 16;        /* Number of VFs per PF set for QDMA IP. */
    uint32_t rsvd : 15;                 /* Reserved. */
    uint32_t sriov_enabled : 1;         /* sriov is enabled in QDMA IP. 1'b0= Disabled, 1'b1=Enabled. */
  } field;
  uint32_t val;
} QDMA_REGS_2_PF2_IP_PARAM_1_t;
#endif /* GENHDR_STRUCT */

/* PF2_IP_PARAM_2 */
#define QDMA_REGS_2_PF2_IP_PARAM_2                                      0x002000A4U
/* Number of DMA channels per VF set for QDMA IP. */
#define QDMA_REGS_2_PF2_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_ACCESSTYPE       GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF2_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_SHIFT            0
#define QDMA_REGS_2_PF2_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_WIDTH            16
#define QDMA_REGS_2_PF2_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_MASK             (0xFFFFU << QDMA_REGS_2_PF2_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_SHIFT)

/* Number of DMA channels per PF set for QDMA IP. */
#define QDMA_REGS_2_PF2_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_ACCESSTYPE       GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF2_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_SHIFT            16
#define QDMA_REGS_2_PF2_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_WIDTH            16
#define QDMA_REGS_2_PF2_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_MASK             (0xFFFFU << QDMA_REGS_2_PF2_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_SHIFT)

#ifdef GENHDR_STRUCT
/* PF2_IP_PARAM_2 */
typedef union {
  struct {
    uint32_t num_dma_chan_per_vf : 16;  /* Number of DMA channels per VF set for QDMA IP. */
    uint32_t num_dma_chan_per_pf : 16;  /* Number of DMA channels per PF set for QDMA IP. */
  } field;
  uint32_t val;
} QDMA_REGS_2_PF2_IP_PARAM_2_t;
#endif /* GENHDR_STRUCT */

/* RSVD */
#define QDMA_REGS_2_RSVD_3                      0x002000A8U
#define QDMA_REGS_2_RSVD_3_WIDTH                64
/*
 * Reserved.
 * 
 * offset 8'hA8 - 8'hAF
 */
#define QDMA_REGS_2_RSVD_3_RSVD_ACCESSTYPE      GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_RSVD_3_RSVD_SHIFT           0
#define QDMA_REGS_2_RSVD_3_RSVD_WIDTH           64
#define QDMA_REGS_2_RSVD_3_RSVD_MASK            (0xFFFFFFFFFFFFFFFFULL << QDMA_REGS_2_RSVD_3_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* RSVD */
typedef union {
  struct {
    uint32_t rsvd_0_31 : 32;    /* Reserved.
                                   
                                   offset 8'hA8 - 8'hAF */
    uint32_t rsvd_32_63 : 32;
  } field;
  uint32_t val[2];
} QDMA_REGS_2_RSVD_3_t;
#endif /* GENHDR_STRUCT */

/* PF3_IP_PARAM_1 */
#define QDMA_REGS_2_PF3_IP_PARAM_1                              0x002000B0U
/* Number of VFs per PF set for QDMA IP. */
#define QDMA_REGS_2_PF3_IP_PARAM_1_NUM_VF_PER_PF_ACCESSTYPE     GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF3_IP_PARAM_1_NUM_VF_PER_PF_SHIFT          0
#define QDMA_REGS_2_PF3_IP_PARAM_1_NUM_VF_PER_PF_WIDTH          16
#define QDMA_REGS_2_PF3_IP_PARAM_1_NUM_VF_PER_PF_MASK           (0xFFFFU << QDMA_REGS_2_PF3_IP_PARAM_1_NUM_VF_PER_PF_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_PF3_IP_PARAM_1_RSVD_ACCESSTYPE              GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF3_IP_PARAM_1_RSVD_SHIFT                   16
#define QDMA_REGS_2_PF3_IP_PARAM_1_RSVD_WIDTH                   15
#define QDMA_REGS_2_PF3_IP_PARAM_1_RSVD_MASK                    (0x7FFFU << QDMA_REGS_2_PF3_IP_PARAM_1_RSVD_SHIFT)

/* sriov is enabled in QDMA IP. 1'b0= Disabled, 1'b1=Enabled. */
#define QDMA_REGS_2_PF3_IP_PARAM_1_SRIOV_ENABLED_ACCESSTYPE     GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF3_IP_PARAM_1_SRIOV_ENABLED_SHIFT          31
#define QDMA_REGS_2_PF3_IP_PARAM_1_SRIOV_ENABLED_WIDTH          1
#define QDMA_REGS_2_PF3_IP_PARAM_1_SRIOV_ENABLED_MASK           (0x1U << QDMA_REGS_2_PF3_IP_PARAM_1_SRIOV_ENABLED_SHIFT)

#ifdef GENHDR_STRUCT
/* PF3_IP_PARAM_1 */
typedef union {
  struct {
    uint32_t num_vf_per_pf : 16;        /* Number of VFs per PF set for QDMA IP. */
    uint32_t rsvd : 15;                 /* Reserved. */
    uint32_t sriov_enabled : 1;         /* sriov is enabled in QDMA IP. 1'b0= Disabled, 1'b1=Enabled. */
  } field;
  uint32_t val;
} QDMA_REGS_2_PF3_IP_PARAM_1_t;
#endif /* GENHDR_STRUCT */

/* PF3_IP_PARAM_2 */
#define QDMA_REGS_2_PF3_IP_PARAM_2                                      0x002000B4U
/* Number of DMA channels per VF set for QDMA IP. */
#define QDMA_REGS_2_PF3_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_ACCESSTYPE       GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF3_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_SHIFT            0
#define QDMA_REGS_2_PF3_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_WIDTH            16
#define QDMA_REGS_2_PF3_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_MASK             (0xFFFFU << QDMA_REGS_2_PF3_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_SHIFT)

/* Number of DMA channels per PF set for QDMA IP. */
#define QDMA_REGS_2_PF3_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_ACCESSTYPE       GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF3_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_SHIFT            16
#define QDMA_REGS_2_PF3_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_WIDTH            16
#define QDMA_REGS_2_PF3_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_MASK             (0xFFFFU << QDMA_REGS_2_PF3_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_SHIFT)

#ifdef GENHDR_STRUCT
/* PF3_IP_PARAM_2 */
typedef union {
  struct {
    uint32_t num_dma_chan_per_vf : 16;  /* Number of DMA channels per VF set for QDMA IP. */
    uint32_t num_dma_chan_per_pf : 16;  /* Number of DMA channels per PF set for QDMA IP. */
  } field;
  uint32_t val;
} QDMA_REGS_2_PF3_IP_PARAM_2_t;
#endif /* GENHDR_STRUCT */

/* RSVD */
#define QDMA_REGS_2_RSVD_4                      0x002000B8U
#define QDMA_REGS_2_RSVD_4_WIDTH                64
/*
 * Reserved.
 * 
 * offset 8'hB8 - 8'hBF
 */
#define QDMA_REGS_2_RSVD_4_RSVD_ACCESSTYPE      GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_RSVD_4_RSVD_SHIFT           0
#define QDMA_REGS_2_RSVD_4_RSVD_WIDTH           64
#define QDMA_REGS_2_RSVD_4_RSVD_MASK            (0xFFFFFFFFFFFFFFFFULL << QDMA_REGS_2_RSVD_4_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* RSVD */
typedef union {
  struct {
    uint32_t rsvd_0_31 : 32;    /* Reserved.
                                   
                                   offset 8'hB8 - 8'hBF */
    uint32_t rsvd_32_63 : 32;
  } field;
  uint32_t val[2];
} QDMA_REGS_2_RSVD_4_t;
#endif /* GENHDR_STRUCT */

/* PF4_IP_PARAM_1 */
#define QDMA_REGS_2_PF4_IP_PARAM_1                              0x002000C0U
/* Number of VFs per PF set for QDMA IP. */
#define QDMA_REGS_2_PF4_IP_PARAM_1_NUM_VF_PER_PF_ACCESSTYPE     GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF4_IP_PARAM_1_NUM_VF_PER_PF_SHIFT          0
#define QDMA_REGS_2_PF4_IP_PARAM_1_NUM_VF_PER_PF_WIDTH          16
#define QDMA_REGS_2_PF4_IP_PARAM_1_NUM_VF_PER_PF_MASK           (0xFFFFU << QDMA_REGS_2_PF4_IP_PARAM_1_NUM_VF_PER_PF_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_PF4_IP_PARAM_1_RSVD_ACCESSTYPE              GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF4_IP_PARAM_1_RSVD_SHIFT                   16
#define QDMA_REGS_2_PF4_IP_PARAM_1_RSVD_WIDTH                   15
#define QDMA_REGS_2_PF4_IP_PARAM_1_RSVD_MASK                    (0x7FFFU << QDMA_REGS_2_PF4_IP_PARAM_1_RSVD_SHIFT)

/* sriov is enabled in QDMA IP. 1'b0= Disabled, 1'b1=Enabled. */
#define QDMA_REGS_2_PF4_IP_PARAM_1_SRIOV_ENABLED_ACCESSTYPE     GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF4_IP_PARAM_1_SRIOV_ENABLED_SHIFT          31
#define QDMA_REGS_2_PF4_IP_PARAM_1_SRIOV_ENABLED_WIDTH          1
#define QDMA_REGS_2_PF4_IP_PARAM_1_SRIOV_ENABLED_MASK           (0x1U << QDMA_REGS_2_PF4_IP_PARAM_1_SRIOV_ENABLED_SHIFT)

#ifdef GENHDR_STRUCT
/* PF4_IP_PARAM_1 */
typedef union {
  struct {
    uint32_t num_vf_per_pf : 16;        /* Number of VFs per PF set for QDMA IP. */
    uint32_t rsvd : 15;                 /* Reserved. */
    uint32_t sriov_enabled : 1;         /* sriov is enabled in QDMA IP. 1'b0= Disabled, 1'b1=Enabled. */
  } field;
  uint32_t val;
} QDMA_REGS_2_PF4_IP_PARAM_1_t;
#endif /* GENHDR_STRUCT */

/* PF4_IP_PARAM_2 */
#define QDMA_REGS_2_PF4_IP_PARAM_2                                      0x002000C4U
/* Number of DMA channels per VF set for QDMA IP. */
#define QDMA_REGS_2_PF4_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_ACCESSTYPE       GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF4_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_SHIFT            0
#define QDMA_REGS_2_PF4_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_WIDTH            16
#define QDMA_REGS_2_PF4_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_MASK             (0xFFFFU << QDMA_REGS_2_PF4_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_SHIFT)

/* Number of DMA channels per PF set for QDMA IP. */
#define QDMA_REGS_2_PF4_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_ACCESSTYPE       GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF4_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_SHIFT            16
#define QDMA_REGS_2_PF4_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_WIDTH            16
#define QDMA_REGS_2_PF4_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_MASK             (0xFFFFU << QDMA_REGS_2_PF4_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_SHIFT)

#ifdef GENHDR_STRUCT
/* PF4_IP_PARAM_2 */
typedef union {
  struct {
    uint32_t num_dma_chan_per_vf : 16;  /* Number of DMA channels per VF set for QDMA IP. */
    uint32_t num_dma_chan_per_pf : 16;  /* Number of DMA channels per PF set for QDMA IP. */
  } field;
  uint32_t val;
} QDMA_REGS_2_PF4_IP_PARAM_2_t;
#endif /* GENHDR_STRUCT */

/* RSVD */
#define QDMA_REGS_2_RSVD_5                      0x002000C8U
#define QDMA_REGS_2_RSVD_5_WIDTH                64
/*
 * Reserved.
 * 
 * offset 8'hC8 - 8'hCF
 */
#define QDMA_REGS_2_RSVD_5_RSVD_ACCESSTYPE      GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_RSVD_5_RSVD_SHIFT           0
#define QDMA_REGS_2_RSVD_5_RSVD_WIDTH           64
#define QDMA_REGS_2_RSVD_5_RSVD_MASK            (0xFFFFFFFFFFFFFFFFULL << QDMA_REGS_2_RSVD_5_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* RSVD */
typedef union {
  struct {
    uint32_t rsvd_0_31 : 32;    /* Reserved.
                                   
                                   offset 8'hC8 - 8'hCF */
    uint32_t rsvd_32_63 : 32;
  } field;
  uint32_t val[2];
} QDMA_REGS_2_RSVD_5_t;
#endif /* GENHDR_STRUCT */

/* PF5_IP_PARAM_1 */
#define QDMA_REGS_2_PF5_IP_PARAM_1                              0x002000D0U
/* Number of VFs per PF set for QDMA IP. */
#define QDMA_REGS_2_PF5_IP_PARAM_1_NUM_VF_PER_PF_ACCESSTYPE     GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF5_IP_PARAM_1_NUM_VF_PER_PF_SHIFT          0
#define QDMA_REGS_2_PF5_IP_PARAM_1_NUM_VF_PER_PF_WIDTH          16
#define QDMA_REGS_2_PF5_IP_PARAM_1_NUM_VF_PER_PF_MASK           (0xFFFFU << QDMA_REGS_2_PF5_IP_PARAM_1_NUM_VF_PER_PF_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_PF5_IP_PARAM_1_RSVD_ACCESSTYPE              GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF5_IP_PARAM_1_RSVD_SHIFT                   16
#define QDMA_REGS_2_PF5_IP_PARAM_1_RSVD_WIDTH                   15
#define QDMA_REGS_2_PF5_IP_PARAM_1_RSVD_MASK                    (0x7FFFU << QDMA_REGS_2_PF5_IP_PARAM_1_RSVD_SHIFT)

/* sriov is enabled in QDMA IP. 1'b0= Disabled, 1'b1=Enabled. */
#define QDMA_REGS_2_PF5_IP_PARAM_1_SRIOV_ENABLED_ACCESSTYPE     GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF5_IP_PARAM_1_SRIOV_ENABLED_SHIFT          31
#define QDMA_REGS_2_PF5_IP_PARAM_1_SRIOV_ENABLED_WIDTH          1
#define QDMA_REGS_2_PF5_IP_PARAM_1_SRIOV_ENABLED_MASK           (0x1U << QDMA_REGS_2_PF5_IP_PARAM_1_SRIOV_ENABLED_SHIFT)

#ifdef GENHDR_STRUCT
/* PF5_IP_PARAM_1 */
typedef union {
  struct {
    uint32_t num_vf_per_pf : 16;        /* Number of VFs per PF set for QDMA IP. */
    uint32_t rsvd : 15;                 /* Reserved. */
    uint32_t sriov_enabled : 1;         /* sriov is enabled in QDMA IP. 1'b0= Disabled, 1'b1=Enabled. */
  } field;
  uint32_t val;
} QDMA_REGS_2_PF5_IP_PARAM_1_t;
#endif /* GENHDR_STRUCT */

/* PF5_IP_PARAM_2 */
#define QDMA_REGS_2_PF5_IP_PARAM_2                                      0x002000D4U
/* Number of DMA channels per VF set for QDMA IP. */
#define QDMA_REGS_2_PF5_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_ACCESSTYPE       GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF5_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_SHIFT            0
#define QDMA_REGS_2_PF5_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_WIDTH            16
#define QDMA_REGS_2_PF5_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_MASK             (0xFFFFU << QDMA_REGS_2_PF5_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_SHIFT)

/* Number of DMA channels per PF set for QDMA IP. */
#define QDMA_REGS_2_PF5_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_ACCESSTYPE       GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF5_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_SHIFT            16
#define QDMA_REGS_2_PF5_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_WIDTH            16
#define QDMA_REGS_2_PF5_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_MASK             (0xFFFFU << QDMA_REGS_2_PF5_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_SHIFT)

#ifdef GENHDR_STRUCT
/* PF5_IP_PARAM_2 */
typedef union {
  struct {
    uint32_t num_dma_chan_per_vf : 16;  /* Number of DMA channels per VF set for QDMA IP. */
    uint32_t num_dma_chan_per_pf : 16;  /* Number of DMA channels per PF set for QDMA IP. */
  } field;
  uint32_t val;
} QDMA_REGS_2_PF5_IP_PARAM_2_t;
#endif /* GENHDR_STRUCT */

/* RSVD */
#define QDMA_REGS_2_RSVD_6                      0x002000D8U
#define QDMA_REGS_2_RSVD_6_WIDTH                64
/*
 * Reserved.
 * 
 * offset 8'hD8 - 8'hDF
 */
#define QDMA_REGS_2_RSVD_6_RSVD_ACCESSTYPE      GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_RSVD_6_RSVD_SHIFT           0
#define QDMA_REGS_2_RSVD_6_RSVD_WIDTH           64
#define QDMA_REGS_2_RSVD_6_RSVD_MASK            (0xFFFFFFFFFFFFFFFFULL << QDMA_REGS_2_RSVD_6_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* RSVD */
typedef union {
  struct {
    uint32_t rsvd_0_31 : 32;    /* Reserved.
                                   
                                   offset 8'hD8 - 8'hDF */
    uint32_t rsvd_32_63 : 32;
  } field;
  uint32_t val[2];
} QDMA_REGS_2_RSVD_6_t;
#endif /* GENHDR_STRUCT */

/* PF6_IP_PARAM_1 */
#define QDMA_REGS_2_PF6_IP_PARAM_1                              0x002000E0U
/* Number of VFs per PF set for QDMA IP. */
#define QDMA_REGS_2_PF6_IP_PARAM_1_NUM_VF_PER_PF_ACCESSTYPE     GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF6_IP_PARAM_1_NUM_VF_PER_PF_SHIFT          0
#define QDMA_REGS_2_PF6_IP_PARAM_1_NUM_VF_PER_PF_WIDTH          16
#define QDMA_REGS_2_PF6_IP_PARAM_1_NUM_VF_PER_PF_MASK           (0xFFFFU << QDMA_REGS_2_PF6_IP_PARAM_1_NUM_VF_PER_PF_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_PF6_IP_PARAM_1_RSVD_ACCESSTYPE              GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF6_IP_PARAM_1_RSVD_SHIFT                   16
#define QDMA_REGS_2_PF6_IP_PARAM_1_RSVD_WIDTH                   15
#define QDMA_REGS_2_PF6_IP_PARAM_1_RSVD_MASK                    (0x7FFFU << QDMA_REGS_2_PF6_IP_PARAM_1_RSVD_SHIFT)

/* sriov is enabled in QDMA IP. 1'b0= Disabled, 1'b1=Enabled. */
#define QDMA_REGS_2_PF6_IP_PARAM_1_SRIOV_ENABLED_ACCESSTYPE     GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF6_IP_PARAM_1_SRIOV_ENABLED_SHIFT          31
#define QDMA_REGS_2_PF6_IP_PARAM_1_SRIOV_ENABLED_WIDTH          1
#define QDMA_REGS_2_PF6_IP_PARAM_1_SRIOV_ENABLED_MASK           (0x1U << QDMA_REGS_2_PF6_IP_PARAM_1_SRIOV_ENABLED_SHIFT)

#ifdef GENHDR_STRUCT
/* PF6_IP_PARAM_1 */
typedef union {
  struct {
    uint32_t num_vf_per_pf : 16;        /* Number of VFs per PF set for QDMA IP. */
    uint32_t rsvd : 15;                 /* Reserved. */
    uint32_t sriov_enabled : 1;         /* sriov is enabled in QDMA IP. 1'b0= Disabled, 1'b1=Enabled. */
  } field;
  uint32_t val;
} QDMA_REGS_2_PF6_IP_PARAM_1_t;
#endif /* GENHDR_STRUCT */

/* PF6_IP_PARAM_2 */
#define QDMA_REGS_2_PF6_IP_PARAM_2                                      0x002000E4U
/* Number of DMA channels per VF set for QDMA IP. */
#define QDMA_REGS_2_PF6_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_ACCESSTYPE       GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF6_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_SHIFT            0
#define QDMA_REGS_2_PF6_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_WIDTH            16
#define QDMA_REGS_2_PF6_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_MASK             (0xFFFFU << QDMA_REGS_2_PF6_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_SHIFT)

/* Number of DMA channels per PF set for QDMA IP. */
#define QDMA_REGS_2_PF6_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_ACCESSTYPE       GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF6_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_SHIFT            16
#define QDMA_REGS_2_PF6_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_WIDTH            16
#define QDMA_REGS_2_PF6_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_MASK             (0xFFFFU << QDMA_REGS_2_PF6_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_SHIFT)

#ifdef GENHDR_STRUCT
/* PF6_IP_PARAM_2 */
typedef union {
  struct {
    uint32_t num_dma_chan_per_vf : 16;  /* Number of DMA channels per VF set for QDMA IP. */
    uint32_t num_dma_chan_per_pf : 16;  /* Number of DMA channels per PF set for QDMA IP. */
  } field;
  uint32_t val;
} QDMA_REGS_2_PF6_IP_PARAM_2_t;
#endif /* GENHDR_STRUCT */

/* RSVD */
#define QDMA_REGS_2_RSVD_7                      0x002000E8U
#define QDMA_REGS_2_RSVD_7_WIDTH                64
/*
 * Reserved.
 * 
 * offset 8'hE8 - 8'hEF
 */
#define QDMA_REGS_2_RSVD_7_RSVD_ACCESSTYPE      GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_RSVD_7_RSVD_SHIFT           0
#define QDMA_REGS_2_RSVD_7_RSVD_WIDTH           64
#define QDMA_REGS_2_RSVD_7_RSVD_MASK            (0xFFFFFFFFFFFFFFFFULL << QDMA_REGS_2_RSVD_7_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* RSVD */
typedef union {
  struct {
    uint32_t rsvd_0_31 : 32;    /* Reserved.
                                   
                                   offset 8'hE8 - 8'hEF */
    uint32_t rsvd_32_63 : 32;
  } field;
  uint32_t val[2];
} QDMA_REGS_2_RSVD_7_t;
#endif /* GENHDR_STRUCT */

/* PF7_IP_PARAM_1 */
#define QDMA_REGS_2_PF7_IP_PARAM_1                              0x002000F0U
/* Number of VFs per PF set for QDMA IP. */
#define QDMA_REGS_2_PF7_IP_PARAM_1_NUM_VF_PER_PF_ACCESSTYPE     GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF7_IP_PARAM_1_NUM_VF_PER_PF_SHIFT          0
#define QDMA_REGS_2_PF7_IP_PARAM_1_NUM_VF_PER_PF_WIDTH          16
#define QDMA_REGS_2_PF7_IP_PARAM_1_NUM_VF_PER_PF_MASK           (0xFFFFU << QDMA_REGS_2_PF7_IP_PARAM_1_NUM_VF_PER_PF_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_PF7_IP_PARAM_1_RSVD_ACCESSTYPE              GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF7_IP_PARAM_1_RSVD_SHIFT                   16
#define QDMA_REGS_2_PF7_IP_PARAM_1_RSVD_WIDTH                   15
#define QDMA_REGS_2_PF7_IP_PARAM_1_RSVD_MASK                    (0x7FFFU << QDMA_REGS_2_PF7_IP_PARAM_1_RSVD_SHIFT)

/* sriov is enabled in QDMA IP. 1'b0= Disabled, 1'b1=Enabled. */
#define QDMA_REGS_2_PF7_IP_PARAM_1_SRIOV_ENABLED_ACCESSTYPE     GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF7_IP_PARAM_1_SRIOV_ENABLED_SHIFT          31
#define QDMA_REGS_2_PF7_IP_PARAM_1_SRIOV_ENABLED_WIDTH          1
#define QDMA_REGS_2_PF7_IP_PARAM_1_SRIOV_ENABLED_MASK           (0x1U << QDMA_REGS_2_PF7_IP_PARAM_1_SRIOV_ENABLED_SHIFT)

#ifdef GENHDR_STRUCT
/* PF7_IP_PARAM_1 */
typedef union {
  struct {
    uint32_t num_vf_per_pf : 16;        /* Number of VFs per PF set for QDMA IP. */
    uint32_t rsvd : 15;                 /* Reserved. */
    uint32_t sriov_enabled : 1;         /* sriov is enabled in QDMA IP. 1'b0= Disabled, 1'b1=Enabled. */
  } field;
  uint32_t val;
} QDMA_REGS_2_PF7_IP_PARAM_1_t;
#endif /* GENHDR_STRUCT */

/* PF7_IP_PARAM_2 */
#define QDMA_REGS_2_PF7_IP_PARAM_2                                      0x002000F4U
/* Number of DMA channels per VF set for QDMA IP. */
#define QDMA_REGS_2_PF7_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_ACCESSTYPE       GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF7_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_SHIFT            0
#define QDMA_REGS_2_PF7_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_WIDTH            16
#define QDMA_REGS_2_PF7_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_MASK             (0xFFFFU << QDMA_REGS_2_PF7_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_SHIFT)

/* Number of DMA channels per PF set for QDMA IP. */
#define QDMA_REGS_2_PF7_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_ACCESSTYPE       GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_PF7_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_SHIFT            16
#define QDMA_REGS_2_PF7_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_WIDTH            16
#define QDMA_REGS_2_PF7_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_MASK             (0xFFFFU << QDMA_REGS_2_PF7_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_SHIFT)

#ifdef GENHDR_STRUCT
/* PF7_IP_PARAM_2 */
typedef union {
  struct {
    uint32_t num_dma_chan_per_vf : 16;  /* Number of DMA channels per VF set for QDMA IP. */
    uint32_t num_dma_chan_per_pf : 16;  /* Number of DMA channels per PF set for QDMA IP. */
  } field;
  uint32_t val;
} QDMA_REGS_2_PF7_IP_PARAM_2_t;
#endif /* GENHDR_STRUCT */

/* RSVD */
#define QDMA_REGS_2_RSVD_8                      0x002000F8U
#define QDMA_REGS_2_RSVD_8_WIDTH                64
/*
 * Reserved.
 * 
 * offset 8'hF8 - 8'hFF
 */
#define QDMA_REGS_2_RSVD_8_RSVD_ACCESSTYPE      GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_RSVD_8_RSVD_SHIFT           0
#define QDMA_REGS_2_RSVD_8_RSVD_WIDTH           64
#define QDMA_REGS_2_RSVD_8_RSVD_MASK            (0xFFFFFFFFFFFFFFFFULL << QDMA_REGS_2_RSVD_8_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* RSVD */
typedef union {
  struct {
    uint32_t rsvd_0_31 : 32;    /* Reserved.
                                   
                                   offset 8'hF8 - 8'hFF */
    uint32_t rsvd_32_63 : 32;
  } field;
  uint32_t val[2];
} QDMA_REGS_2_RSVD_8_t;
#endif /* GENHDR_STRUCT */

/* Space available for number of tail pointer update in H2D transfer in port 0 */
#define QDMA_REGS_2_P0_H2_D_TPTR_AVL                            0x00200100U
/*
 * Default value of NUM_TPTR_AVL = 12'h400 
 * 
 *  Space available for number of tail pointer update in H2D transfer.
 */
#define QDMA_REGS_2_P0_H2_D_TPTR_AVL_NUM_TPTR_AVL_ACCESSTYPE    GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_P0_H2_D_TPTR_AVL_NUM_TPTR_AVL_SHIFT         0
#define QDMA_REGS_2_P0_H2_D_TPTR_AVL_NUM_TPTR_AVL_WIDTH         12
#define QDMA_REGS_2_P0_H2_D_TPTR_AVL_NUM_TPTR_AVL_MASK          (0xFFFU << QDMA_REGS_2_P0_H2_D_TPTR_AVL_NUM_TPTR_AVL_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_P0_H2_D_TPTR_AVL_RSVD_ACCESSTYPE            GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_P0_H2_D_TPTR_AVL_RSVD_SHIFT                 12
#define QDMA_REGS_2_P0_H2_D_TPTR_AVL_RSVD_WIDTH                 20
#define QDMA_REGS_2_P0_H2_D_TPTR_AVL_RSVD_MASK                  (0xFFFFFU << QDMA_REGS_2_P0_H2_D_TPTR_AVL_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* Space available for number of tail pointer update in H2D transfer in port 0 */
typedef union {
  struct {
    uint32_t num_tptr_avl : 12; /* Default value of NUM_TPTR_AVL = 12'h400
                                   
                                   Space available for number of tail pointer update in H2D transfer. */
    uint32_t rsvd : 20;         /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_P0_H2_D_TPTR_AVL_t;
#endif /* GENHDR_STRUCT */

/* Space available for number of tail pointer update in H2D transfer in port 1 */
#define QDMA_REGS_2_P1_H2_D_TPTR_AVL                            0x00200104U
/*
 * Default value of NUM_TPTR_AVL = 12'h400 
 * 
 *  Space available for number of tail pointer update in H2D transfer.
 */
#define QDMA_REGS_2_P1_H2_D_TPTR_AVL_NUM_TPTR_AVL_ACCESSTYPE    GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_P1_H2_D_TPTR_AVL_NUM_TPTR_AVL_SHIFT         0
#define QDMA_REGS_2_P1_H2_D_TPTR_AVL_NUM_TPTR_AVL_WIDTH         12
#define QDMA_REGS_2_P1_H2_D_TPTR_AVL_NUM_TPTR_AVL_MASK          (0xFFFU << QDMA_REGS_2_P1_H2_D_TPTR_AVL_NUM_TPTR_AVL_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_P1_H2_D_TPTR_AVL_RSVD_ACCESSTYPE            GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_P1_H2_D_TPTR_AVL_RSVD_SHIFT                 12
#define QDMA_REGS_2_P1_H2_D_TPTR_AVL_RSVD_WIDTH                 20
#define QDMA_REGS_2_P1_H2_D_TPTR_AVL_RSVD_MASK                  (0xFFFFFU << QDMA_REGS_2_P1_H2_D_TPTR_AVL_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* Space available for number of tail pointer update in H2D transfer in port 1 */
typedef union {
  struct {
    uint32_t num_tptr_avl : 12; /* Default value of NUM_TPTR_AVL = 12'h400
                                   
                                   Space available for number of tail pointer update in H2D transfer. */
    uint32_t rsvd : 20;         /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_P1_H2_D_TPTR_AVL_t;
#endif /* GENHDR_STRUCT */

/* Space available for number of tail pointer update in H2D transfer in port 2 */
#define QDMA_REGS_2_P2_H2_D_TPTR_AVL                            0x00200108U
/*
 * Default value of NUM_TPTR_AVL = 12'h400 
 * 
 *  Space available for number of tail pointer update in H2D transfer.
 */
#define QDMA_REGS_2_P2_H2_D_TPTR_AVL_NUM_TPTR_AVL_ACCESSTYPE    GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_P2_H2_D_TPTR_AVL_NUM_TPTR_AVL_SHIFT         0
#define QDMA_REGS_2_P2_H2_D_TPTR_AVL_NUM_TPTR_AVL_WIDTH         12
#define QDMA_REGS_2_P2_H2_D_TPTR_AVL_NUM_TPTR_AVL_MASK          (0xFFFU << QDMA_REGS_2_P2_H2_D_TPTR_AVL_NUM_TPTR_AVL_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_P2_H2_D_TPTR_AVL_RSVD_ACCESSTYPE            GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_P2_H2_D_TPTR_AVL_RSVD_SHIFT                 12
#define QDMA_REGS_2_P2_H2_D_TPTR_AVL_RSVD_WIDTH                 20
#define QDMA_REGS_2_P2_H2_D_TPTR_AVL_RSVD_MASK                  (0xFFFFFU << QDMA_REGS_2_P2_H2_D_TPTR_AVL_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* Space available for number of tail pointer update in H2D transfer in port 2 */
typedef union {
  struct {
    uint32_t num_tptr_avl : 12; /* Default value of NUM_TPTR_AVL = 12'h400
                                   
                                   Space available for number of tail pointer update in H2D transfer. */
    uint32_t rsvd : 20;         /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_P2_H2_D_TPTR_AVL_t;
#endif /* GENHDR_STRUCT */

/* Space available for number of tail pointer update in H2D transfer in port 3 */
#define QDMA_REGS_2_P3_H2_D_TPTR_AVL                            0x0020010CU
/*
 * Default value of NUM_TPTR_AVL = 12'h400 
 * 
 *  Space available for number of tail pointer update in H2D transfer.
 */
#define QDMA_REGS_2_P3_H2_D_TPTR_AVL_NUM_TPTR_AVL_ACCESSTYPE    GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_P3_H2_D_TPTR_AVL_NUM_TPTR_AVL_SHIFT         0
#define QDMA_REGS_2_P3_H2_D_TPTR_AVL_NUM_TPTR_AVL_WIDTH         12
#define QDMA_REGS_2_P3_H2_D_TPTR_AVL_NUM_TPTR_AVL_MASK          (0xFFFU << QDMA_REGS_2_P3_H2_D_TPTR_AVL_NUM_TPTR_AVL_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_P3_H2_D_TPTR_AVL_RSVD_ACCESSTYPE            GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_P3_H2_D_TPTR_AVL_RSVD_SHIFT                 12
#define QDMA_REGS_2_P3_H2_D_TPTR_AVL_RSVD_WIDTH                 20
#define QDMA_REGS_2_P3_H2_D_TPTR_AVL_RSVD_MASK                  (0xFFFFFU << QDMA_REGS_2_P3_H2_D_TPTR_AVL_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* Space available for number of tail pointer update in H2D transfer in port 3 */
typedef union {
  struct {
    uint32_t num_tptr_avl : 12; /* Default value of NUM_TPTR_AVL = 12'h400
                                   
                                   Space available for number of tail pointer update in H2D transfer. */
    uint32_t rsvd : 20;         /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_P3_H2_D_TPTR_AVL_t;
#endif /* GENHDR_STRUCT */

/* Space available for number of tail pointer update in D2H transfer in port 0 */
#define QDMA_REGS_2_P0_D2_H_TPTR_AVL                            0x00200110U
/*
 * Default value of NUM_TPTR_AVL = 12'h400 
 * 
 *  Space available for number of tail pointer update in D2H transfer.
 */
#define QDMA_REGS_2_P0_D2_H_TPTR_AVL_NUM_TPTR_AVL_ACCESSTYPE    GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_P0_D2_H_TPTR_AVL_NUM_TPTR_AVL_SHIFT         0
#define QDMA_REGS_2_P0_D2_H_TPTR_AVL_NUM_TPTR_AVL_WIDTH         12
#define QDMA_REGS_2_P0_D2_H_TPTR_AVL_NUM_TPTR_AVL_MASK          (0xFFFU << QDMA_REGS_2_P0_D2_H_TPTR_AVL_NUM_TPTR_AVL_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_P0_D2_H_TPTR_AVL_RSVD_ACCESSTYPE            GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_P0_D2_H_TPTR_AVL_RSVD_SHIFT                 12
#define QDMA_REGS_2_P0_D2_H_TPTR_AVL_RSVD_WIDTH                 20
#define QDMA_REGS_2_P0_D2_H_TPTR_AVL_RSVD_MASK                  (0xFFFFFU << QDMA_REGS_2_P0_D2_H_TPTR_AVL_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* Space available for number of tail pointer update in D2H transfer in port 0 */
typedef union {
  struct {
    uint32_t num_tptr_avl : 12; /* Default value of NUM_TPTR_AVL = 12'h400
                                   
                                   Space available for number of tail pointer update in D2H transfer. */
    uint32_t rsvd : 20;         /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_P0_D2_H_TPTR_AVL_t;
#endif /* GENHDR_STRUCT */

/* Space available for number of tail pointer update in D2H transfer in port 1 */
#define QDMA_REGS_2_P1_D2_H_TPTR_AVL                            0x00200114U
/*
 * Default value of NUM_TPTR_AVL = 12'h400 
 * 
 *  Space available for number of tail pointer update in D2H transfer.
 */
#define QDMA_REGS_2_P1_D2_H_TPTR_AVL_NUM_TPTR_AVL_ACCESSTYPE    GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_P1_D2_H_TPTR_AVL_NUM_TPTR_AVL_SHIFT         0
#define QDMA_REGS_2_P1_D2_H_TPTR_AVL_NUM_TPTR_AVL_WIDTH         12
#define QDMA_REGS_2_P1_D2_H_TPTR_AVL_NUM_TPTR_AVL_MASK          (0xFFFU << QDMA_REGS_2_P1_D2_H_TPTR_AVL_NUM_TPTR_AVL_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_P1_D2_H_TPTR_AVL_RSVD_ACCESSTYPE            GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_P1_D2_H_TPTR_AVL_RSVD_SHIFT                 12
#define QDMA_REGS_2_P1_D2_H_TPTR_AVL_RSVD_WIDTH                 20
#define QDMA_REGS_2_P1_D2_H_TPTR_AVL_RSVD_MASK                  (0xFFFFFU << QDMA_REGS_2_P1_D2_H_TPTR_AVL_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* Space available for number of tail pointer update in D2H transfer in port 1 */
typedef union {
  struct {
    uint32_t num_tptr_avl : 12; /* Default value of NUM_TPTR_AVL = 12'h400
                                   
                                   Space available for number of tail pointer update in D2H transfer. */
    uint32_t rsvd : 20;         /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_P1_D2_H_TPTR_AVL_t;
#endif /* GENHDR_STRUCT */

/* Space available for number of tail pointer update in D2H transfer in port 2 */
#define QDMA_REGS_2_P2_D2_H_TPTR_AVL                            0x00200118U
/*
 * Default value of NUM_TPTR_AVL = 12'h400 
 * 
 *  Space available for number of tail pointer update in D2H transfer.
 */
#define QDMA_REGS_2_P2_D2_H_TPTR_AVL_NUM_TPTR_AVL_ACCESSTYPE    GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_P2_D2_H_TPTR_AVL_NUM_TPTR_AVL_SHIFT         0
#define QDMA_REGS_2_P2_D2_H_TPTR_AVL_NUM_TPTR_AVL_WIDTH         12
#define QDMA_REGS_2_P2_D2_H_TPTR_AVL_NUM_TPTR_AVL_MASK          (0xFFFU << QDMA_REGS_2_P2_D2_H_TPTR_AVL_NUM_TPTR_AVL_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_P2_D2_H_TPTR_AVL_RSVD_ACCESSTYPE            GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_P2_D2_H_TPTR_AVL_RSVD_SHIFT                 12
#define QDMA_REGS_2_P2_D2_H_TPTR_AVL_RSVD_WIDTH                 20
#define QDMA_REGS_2_P2_D2_H_TPTR_AVL_RSVD_MASK                  (0xFFFFFU << QDMA_REGS_2_P2_D2_H_TPTR_AVL_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* Space available for number of tail pointer update in D2H transfer in port 2 */
typedef union {
  struct {
    uint32_t num_tptr_avl : 12; /* Default value of NUM_TPTR_AVL = 12'h400
                                   
                                   Space available for number of tail pointer update in D2H transfer. */
    uint32_t rsvd : 20;         /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_P2_D2_H_TPTR_AVL_t;
#endif /* GENHDR_STRUCT */

/* Space available for number of tail pointer update in D2H transfer in port 3 */
#define QDMA_REGS_2_P3_D2_H_TPTR_AVL                            0x0020011CU
/*
 * Default value of NUM_TPTR_AVL = 12'h400 
 * 
 *  Space available for number of tail pointer update in D2H transfer.
 */
#define QDMA_REGS_2_P3_D2_H_TPTR_AVL_NUM_TPTR_AVL_ACCESSTYPE    GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_P3_D2_H_TPTR_AVL_NUM_TPTR_AVL_SHIFT         0
#define QDMA_REGS_2_P3_D2_H_TPTR_AVL_NUM_TPTR_AVL_WIDTH         12
#define QDMA_REGS_2_P3_D2_H_TPTR_AVL_NUM_TPTR_AVL_MASK          (0xFFFU << QDMA_REGS_2_P3_D2_H_TPTR_AVL_NUM_TPTR_AVL_SHIFT)

/* Reserved. */
#define QDMA_REGS_2_P3_D2_H_TPTR_AVL_RSVD_ACCESSTYPE            GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_P3_D2_H_TPTR_AVL_RSVD_SHIFT                 12
#define QDMA_REGS_2_P3_D2_H_TPTR_AVL_RSVD_WIDTH                 20
#define QDMA_REGS_2_P3_D2_H_TPTR_AVL_RSVD_MASK                  (0xFFFFFU << QDMA_REGS_2_P3_D2_H_TPTR_AVL_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* Space available for number of tail pointer update in D2H transfer in port 3 */
typedef union {
  struct {
    uint32_t num_tptr_avl : 12; /* Default value of NUM_TPTR_AVL = 12'h400
                                   
                                   Space available for number of tail pointer update in D2H transfer. */
    uint32_t rsvd : 20;         /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_P3_D2_H_TPTR_AVL_t;
#endif /* GENHDR_STRUCT */

/* SOFT_RESET register */
#define QDMA_REGS_2_SOFT_RESET                          0x00200120U
/* Reserved. */
#define QDMA_REGS_2_SOFT_RESET_RSVD_ACCESSTYPE          GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_SOFT_RESET_RSVD_SHIFT               1
#define QDMA_REGS_2_SOFT_RESET_RSVD_WIDTH               31
#define QDMA_REGS_2_SOFT_RESET_RSVD_MASK                (0x7FFFFFFFU << QDMA_REGS_2_SOFT_RESET_RSVD_SHIFT)

/*
 * Assert soft reset. This will only reset the Data movers. Once set, the soft
 * reset signal generated will be asserted for 5us
 */
#define QDMA_REGS_2_SOFT_RESET_SOFT_RESET_SET_SHIFT     0
#define QDMA_REGS_2_SOFT_RESET_SOFT_RESET_SET_WIDTH     0
#define QDMA_REGS_2_SOFT_RESET_SOFT_RESET_SET_MASK      (0x0U << QDMA_REGS_2_SOFT_RESET_SOFT_RESET_SET_SHIFT)

#ifdef GENHDR_STRUCT
/* SOFT_RESET register */
typedef union {
  struct {
    uint32_t : 1;
    uint32_t rsvd : 31; /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_SOFT_RESET_t;
#endif /* GENHDR_STRUCT */

/* RSVD */
#define QDMA_REGS_2_RSVD_9                      0x00200124U
/*
 * Reserved.
 * 
 * It occupies the offset range of 12'h124 - 12'h14F
 */
#define QDMA_REGS_2_RSVD_9_RSVD_ACCESSTYPE      GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_RSVD_9_RSVD_SHIFT           0
#define QDMA_REGS_2_RSVD_9_RSVD_WIDTH           1
#define QDMA_REGS_2_RSVD_9_RSVD_MASK            (0x1U << QDMA_REGS_2_RSVD_9_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* RSVD */
typedef union {
  struct {
    uint32_t rsvd : 1;  /* Reserved.
                           
                           It occupies the offset range of 12'h124 - 12'h14F */
    uint32_t : 31;
  } field;
  uint32_t val;
} QDMA_REGS_2_RSVD_9_t;
#endif /* GENHDR_STRUCT */

/* H2D debug register 0 */
#define QDMA_REGS_2_DBG1_REG00                  0x00200150U
/* Holds count of total MRd tlp received by RX_RQ. */
#define QDMA_REGS_2_DBG1_REG00_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG1_REG00_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG1_REG00_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG1_REG00_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG1_REG00_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* H2D debug register 0 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total MRd tlp received by RX_RQ. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG1_REG00_t;
#endif /* GENHDR_STRUCT */

/* H2D debug register 1 */
#define QDMA_REGS_2_DBG1_REG01                  0x00200154U
/* Holds count of total completions tlp sent from RX_RQ . */
#define QDMA_REGS_2_DBG1_REG01_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG1_REG01_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG1_REG01_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG1_REG01_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG1_REG01_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* H2D debug register 1 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total completions tlp sent from RX_RQ . */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG1_REG01_t;
#endif /* GENHDR_STRUCT */

/* H2D debug register 2 */
#define QDMA_REGS_2_DBG1_REG02                  0x00200158U
/* Holds count of total RX TLPs received from HIP. */
#define QDMA_REGS_2_DBG1_REG02_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG1_REG02_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG1_REG02_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG1_REG02_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG1_REG02_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* H2D debug register 2 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total RX TLPs received from HIP. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG1_REG02_t;
#endif /* GENHDR_STRUCT */

/* H2D debug register 3 */
#define QDMA_REGS_2_DBG1_REG03                  0x0020015CU
/* Holds count of total H2D MRd tlp sent from H2D. */
#define QDMA_REGS_2_DBG1_REG03_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG1_REG03_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG1_REG03_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG1_REG03_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG1_REG03_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* H2D debug register 3 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total H2D MRd tlp sent from H2D. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG1_REG03_t;
#endif /* GENHDR_STRUCT */

/* H2D debug register 4 */
#define QDMA_REGS_2_DBG1_REG04                  0x00200160U
/* Reserved. */
#define QDMA_REGS_2_DBG1_REG04_RSVD_ACCESSTYPE  GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_DBG1_REG04_RSVD_SHIFT       0
#define QDMA_REGS_2_DBG1_REG04_RSVD_WIDTH       32
#define QDMA_REGS_2_DBG1_REG04_RSVD_MASK        (0xFFFFFFFFU << QDMA_REGS_2_DBG1_REG04_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* H2D debug register 4 */
typedef union {
  struct {
    uint32_t rsvd : 32; /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG1_REG04_t;
#endif /* GENHDR_STRUCT */

/* H2D debug register 5 */
#define QDMA_REGS_2_DBG1_REG05                  0x00200164U
/* Reserved. */
#define QDMA_REGS_2_DBG1_REG05_RSVD_ACCESSTYPE  GENHDR_ACCESSTYPE_
#define QDMA_REGS_2_DBG1_REG05_RSVD_SHIFT       0
#define QDMA_REGS_2_DBG1_REG05_RSVD_WIDTH       32
#define QDMA_REGS_2_DBG1_REG05_RSVD_MASK        (0xFFFFFFFFU << QDMA_REGS_2_DBG1_REG05_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* H2D debug register 5 */
typedef union {
  struct {
    uint32_t rsvd : 32; /* Reserved. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG1_REG05_t;
#endif /* GENHDR_STRUCT */

/* H2D debug register 6 */
#define QDMA_REGS_2_DBG1_REG06                  0x00200168U
/*
 * Holds count of total hardware instruction ack. 8 bits per port H2D aligned in
 * the order of (Port4,Port3,Port2,Port1).Ports 4-2 unused in AVMM
 * configuration.
 */
#define QDMA_REGS_2_DBG1_REG06_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG1_REG06_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG1_REG06_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG1_REG06_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG1_REG06_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* H2D debug register 6 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total hardware instruction ack. 8 bits per port H2D aligned in
                                   the order of (Port4,Port3,Port2,Port1).Ports 4-2 unused in AVMM
                                   configuration. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG1_REG06_t;
#endif /* GENHDR_STRUCT */

/* H2D debug register 7 */
#define QDMA_REGS_2_DBG1_REG07                  0x0020016CU
/*
 * Holds count of total hardware instruction ack. 8 bits per port D2H aligned in
 * the order of (Port4,Port3,Port2,Port1).Ports 4-2 unused in AVMM
 * configuration.
 */
#define QDMA_REGS_2_DBG1_REG07_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG1_REG07_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG1_REG07_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG1_REG07_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG1_REG07_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* H2D debug register 7 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total hardware instruction ack. 8 bits per port D2H aligned in
                                   the order of (Port4,Port3,Port2,Port1).Ports 4-2 unused in AVMM
                                   configuration. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG1_REG07_t;
#endif /* GENHDR_STRUCT */

/* H2D debug register 8 */
#define QDMA_REGS_2_DBG1_REG08                  0x00200170U
/* Holds count of total sof sent on port 1.Unused in AVMM configuration. */
#define QDMA_REGS_2_DBG1_REG08_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG1_REG08_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG1_REG08_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG1_REG08_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG1_REG08_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* H2D debug register 8 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total sof sent on port 1.Unused in AVMM configuration. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG1_REG08_t;
#endif /* GENHDR_STRUCT */

/* H2D debug register 9 */
#define QDMA_REGS_2_DBG1_REG09                  0x00200174U
/* Holds count of total sof sent on port 2.Unused in AVMM configuration. */
#define QDMA_REGS_2_DBG1_REG09_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG1_REG09_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG1_REG09_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG1_REG09_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG1_REG09_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* H2D debug register 9 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total sof sent on port 2.Unused in AVMM configuration. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG1_REG09_t;
#endif /* GENHDR_STRUCT */

/* H2D debug register 10 */
#define QDMA_REGS_2_DBG1_REG10                  0x00200178U
/* Holds count of total sof sent on port 3.Unused in AVMM configuration. */
#define QDMA_REGS_2_DBG1_REG10_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG1_REG10_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG1_REG10_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG1_REG10_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG1_REG10_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* H2D debug register 10 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total sof sent on port 3.Unused in AVMM configuration. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG1_REG10_t;
#endif /* GENHDR_STRUCT */

/* H2D debug register 11 */
#define QDMA_REGS_2_DBG1_REG11                  0x0020017CU
/* Holds count of total sof sent on port 4.Unused in AVMM configuration. */
#define QDMA_REGS_2_DBG1_REG11_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG1_REG11_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG1_REG11_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG1_REG11_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG1_REG11_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* H2D debug register 11 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total sof sent on port 4.Unused in AVMM configuration. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG1_REG11_t;
#endif /* GENHDR_STRUCT */

/* H2D debug register 12 */
#define QDMA_REGS_2_DBG1_REG12                  0x00200180U
/* Holds count of total eof sent on port 1.Unused in AVMM configuration. */
#define QDMA_REGS_2_DBG1_REG12_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG1_REG12_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG1_REG12_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG1_REG12_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG1_REG12_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* H2D debug register 12 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total eof sent on port 1.Unused in AVMM configuration. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG1_REG12_t;
#endif /* GENHDR_STRUCT */

/* H2D debug register 13 */
#define QDMA_REGS_2_DBG1_REG13                  0x00200184U
/* Holds count of total eof sent on port 2.Unused in AVMM configuration. */
#define QDMA_REGS_2_DBG1_REG13_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG1_REG13_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG1_REG13_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG1_REG13_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG1_REG13_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* H2D debug register 13 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total eof sent on port 2.Unused in AVMM configuration. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG1_REG13_t;
#endif /* GENHDR_STRUCT */

/* H2D debug register 14 */
#define QDMA_REGS_2_DBG1_REG14                  0x00200188U
/* Holds count of total eof sent on port 3.Unused in AVMM configuration. */
#define QDMA_REGS_2_DBG1_REG14_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG1_REG14_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG1_REG14_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG1_REG14_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG1_REG14_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* H2D debug register 14 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total eof sent on port 3.Unused in AVMM configuration. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG1_REG14_t;
#endif /* GENHDR_STRUCT */

/* H2D debug register 15 */
#define QDMA_REGS_2_DBG1_REG15                  0x0020018CU
/* Holds count of total eof sent on port 4.Unused in AVMM configuration. */
#define QDMA_REGS_2_DBG1_REG15_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG1_REG15_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG1_REG15_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG1_REG15_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG1_REG15_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* H2D debug register 15 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total eof sent on port 4.Unused in AVMM configuration. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG1_REG15_t;
#endif /* GENHDR_STRUCT */

/* D2H debug register 0 */
#define QDMA_REGS_2_DBG2_REG00                  0x00200190U
/* Holds count of total d2h tlp at tx scheduler input. */
#define QDMA_REGS_2_DBG2_REG00_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG2_REG00_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG2_REG00_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG2_REG00_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG2_REG00_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* D2H debug register 0 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total d2h tlp at tx scheduler input. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG2_REG00_t;
#endif /* GENHDR_STRUCT */

/* D2H debug register 1 */
#define QDMA_REGS_2_DBG2_REG01                  0x00200194U
/* Holds count of total cmpl tlp at tx scheduler input. */
#define QDMA_REGS_2_DBG2_REG01_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG2_REG01_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG2_REG01_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG2_REG01_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG2_REG01_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* D2H debug register 1 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total cmpl tlp at tx scheduler input. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG2_REG01_t;
#endif /* GENHDR_STRUCT */

/* D2H debug register 2 */
#define QDMA_REGS_2_DBG2_REG02                  0x00200198U
/* Holds count of total msi/wb/desc_update tlp at tx scheduler input. */
#define QDMA_REGS_2_DBG2_REG02_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG2_REG02_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG2_REG02_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG2_REG02_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG2_REG02_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* D2H debug register 2 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total msi/wb/desc_update tlp at tx scheduler input. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG2_REG02_t;
#endif /* GENHDR_STRUCT */

/* D2H debug register 3 */
#define QDMA_REGS_2_DBG2_REG03                  0x0020019CU
/* Holds count of total h2d tlp at tx scheduler input. */
#define QDMA_REGS_2_DBG2_REG03_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG2_REG03_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG2_REG03_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG2_REG03_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG2_REG03_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* D2H debug register 3 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total h2d tlp at tx scheduler input. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG2_REG03_t;
#endif /* GENHDR_STRUCT */

/* D2H debug register 4 */
#define QDMA_REGS_2_DBG2_REG04                  0x002001A0U
/*
 * Holds count of total eof received on port 1 AVST. Unused in AVMM
 * configuration.
 */
#define QDMA_REGS_2_DBG2_REG04_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG2_REG04_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG2_REG04_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG2_REG04_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG2_REG04_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* D2H debug register 4 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total eof received on port 1 AVST. Unused in AVMM
                                   configuration. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG2_REG04_t;
#endif /* GENHDR_STRUCT */

/* D2H debug register 5 */
#define QDMA_REGS_2_DBG2_REG05                  0x002001A4U
/*
 * Holds count of total eof received on port 2 AVST.Unused in AVMM
 * configuration.
 */
#define QDMA_REGS_2_DBG2_REG05_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG2_REG05_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG2_REG05_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG2_REG05_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG2_REG05_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* D2H debug register 5 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total eof received on port 2 AVST.Unused in AVMM
                                   configuration. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG2_REG05_t;
#endif /* GENHDR_STRUCT */

/* D2H debug register 6 */
#define QDMA_REGS_2_DBG2_REG06                  0x002001A8U
/*
 * Holds count of total eof received on port 3 AVST.Unused in AVMM
 * configuration.
 */
#define QDMA_REGS_2_DBG2_REG06_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG2_REG06_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG2_REG06_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG2_REG06_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG2_REG06_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* D2H debug register 6 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total eof received on port 3 AVST.Unused in AVMM
                                   configuration. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG2_REG06_t;
#endif /* GENHDR_STRUCT */

/* D2H debug register 7 */
#define QDMA_REGS_2_DBG2_REG07                  0x002001ACU
/*
 * Holds count of total eof received on port 4 AVST.Unused in AVMM
 * configuration.
 */
#define QDMA_REGS_2_DBG2_REG07_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG2_REG07_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG2_REG07_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG2_REG07_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG2_REG07_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* D2H debug register 7 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total eof received on port 4 AVST.Unused in AVMM
                                   configuration. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG2_REG07_t;
#endif /* GENHDR_STRUCT */

/* D2H debug register 8 */
#define QDMA_REGS_2_DBG2_REG08                  0x002001B0U
/* Holds count of total descriptors processed successfully on port 1. */
#define QDMA_REGS_2_DBG2_REG08_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG2_REG08_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG2_REG08_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG2_REG08_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG2_REG08_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* D2H debug register 8 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total descriptors processed successfully on port 1. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG2_REG08_t;
#endif /* GENHDR_STRUCT */

/* D2H debug register 9 */
#define QDMA_REGS_2_DBG2_REG09                  0x002001B4U
/*
 * Holds count of total descriptors processed successfully on port 2.Unused in
 * AVMM configuration.
 */
#define QDMA_REGS_2_DBG2_REG09_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG2_REG09_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG2_REG09_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG2_REG09_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG2_REG09_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* D2H debug register 9 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total descriptors processed successfully on port 2.Unused in
                                   AVMM configuration. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG2_REG09_t;
#endif /* GENHDR_STRUCT */

/* D2H debug register 10 */
#define QDMA_REGS_2_DBG2_REG10                  0x002001B8U
/*
 * Holds count of total descriptors processed successfully on port 3.Unused in
 * AVMM configuration.
 */
#define QDMA_REGS_2_DBG2_REG10_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG2_REG10_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG2_REG10_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG2_REG10_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG2_REG10_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* D2H debug register 10 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total descriptors processed successfully on port 3.Unused in
                                   AVMM configuration. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG2_REG10_t;
#endif /* GENHDR_STRUCT */

/* D2H debug register 11 */
#define QDMA_REGS_2_DBG2_REG11                  0x002001BCU
/*
 * Holds count of total descriptors processed successfully on port 4.Unused in
 * AVMM configuration.
 */
#define QDMA_REGS_2_DBG2_REG11_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG2_REG11_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG2_REG11_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG2_REG11_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG2_REG11_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* D2H debug register 11 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total descriptors processed successfully on port 4.Unused in
                                   AVMM configuration. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG2_REG11_t;
#endif /* GENHDR_STRUCT */

/* D2H debug register 12 */
#define QDMA_REGS_2_DBG2_REG12                  0x002001C0U
/* Holds count of total descriptors received on port 1. */
#define QDMA_REGS_2_DBG2_REG12_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG2_REG12_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG2_REG12_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG2_REG12_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG2_REG12_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* D2H debug register 12 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total descriptors received on port 1. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG2_REG12_t;
#endif /* GENHDR_STRUCT */

/* D2H debug register 13 */
#define QDMA_REGS_2_DBG2_REG13                  0x002001C4U
/*
 * Holds count of total descriptors received on port 2.Unused in AVMM
 * configuration.
 */
#define QDMA_REGS_2_DBG2_REG13_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG2_REG13_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG2_REG13_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG2_REG13_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG2_REG13_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* D2H debug register 13 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total descriptors received on port 2.Unused in AVMM
                                   configuration. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG2_REG13_t;
#endif /* GENHDR_STRUCT */

/* D2H debug register 14 */
#define QDMA_REGS_2_DBG2_REG14                  0x002001C8U
/*
 * Holds count of total descriptors received on port 3.Unused in AVMM
 * configuration.
 */
#define QDMA_REGS_2_DBG2_REG14_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG2_REG14_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG2_REG14_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG2_REG14_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG2_REG14_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* D2H debug register 14 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total descriptors received on port 3.Unused in AVMM
                                   configuration. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG2_REG14_t;
#endif /* GENHDR_STRUCT */

/* D2H debug register 15 */
#define QDMA_REGS_2_DBG2_REG15                  0x002001CCU
/*
 * Holds count of total descriptors received on port 4.Unused in AVMM
 * configuration.
 */
#define QDMA_REGS_2_DBG2_REG15_COUNT_ACCESSTYPE GENHDR_ACCESSTYPE_RO
#define QDMA_REGS_2_DBG2_REG15_COUNT_SHIFT      0
#define QDMA_REGS_2_DBG2_REG15_COUNT_WIDTH      32
#define QDMA_REGS_2_DBG2_REG15_COUNT_MASK       (0xFFFFFFFFU << QDMA_REGS_2_DBG2_REG15_COUNT_SHIFT)

#ifdef GENHDR_STRUCT
/* D2H debug register 15 */
typedef union {
  struct {
    uint32_t count : 32;        /* Holds count of total descriptors received on port 4.Unused in AVMM
                                   configuration. */
  } field;
  uint32_t val;
} QDMA_REGS_2_DBG2_REG15_t;
#endif /* GENHDR_STRUCT */

#endif /* _QDMA_REGS_2_REGISTERS_H_ */

