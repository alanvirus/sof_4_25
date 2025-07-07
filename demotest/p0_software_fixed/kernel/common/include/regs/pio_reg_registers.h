/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020-21, Intel Corporation. */

#ifndef _PIO_REG_REGISTERS_H_
#define _PIO_REG_REGISTERS_H_

/* PIO REGISTER FORMAT */

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

#define PIO_REG_ACCESSTYPE              GENHDR_ACCESSTYPE_RW /*  Default access type. Access types defined above. */
#define PIO_REG_REGWIDTH                32 /* Default width of register in bits */
#define PIO_REG_ACCESSWIDTH             32 /* Default width of access word in bit */


/* Start/Stop Packet generator register */
#define PIO_REG_PKT_GEN_EN                              0x00000000U
/* Start/Stop Packet generator for Port 1. */
#define PIO_REG_PKT_GEN_EN_PKT_GEN_EN_P_1_SHIFT         0
#define PIO_REG_PKT_GEN_EN_PKT_GEN_EN_P_1_WIDTH         1
#define PIO_REG_PKT_GEN_EN_PKT_GEN_EN_P_1_MASK          (0x1U << PIO_REG_PKT_GEN_EN_PKT_GEN_EN_P_1_SHIFT)

/* Start/Stop Packet generator for Port 2. */
#define PIO_REG_PKT_GEN_EN_PKT_GEN_EN_P_2_SHIFT         1
#define PIO_REG_PKT_GEN_EN_PKT_GEN_EN_P_2_WIDTH         1
#define PIO_REG_PKT_GEN_EN_PKT_GEN_EN_P_2_MASK          (0x1U << PIO_REG_PKT_GEN_EN_PKT_GEN_EN_P_2_SHIFT)

/* Start/Stop Packet generator for Port 3. */
#define PIO_REG_PKT_GEN_EN_PKT_GEN_EN_P_3_SHIFT         2
#define PIO_REG_PKT_GEN_EN_PKT_GEN_EN_P_3_WIDTH         1
#define PIO_REG_PKT_GEN_EN_PKT_GEN_EN_P_3_MASK          (0x1U << PIO_REG_PKT_GEN_EN_PKT_GEN_EN_P_3_SHIFT)

/* Start/Stop Packet generator for Port 4. */
#define PIO_REG_PKT_GEN_EN_PKT_GEN_EN_P_4_SHIFT         3
#define PIO_REG_PKT_GEN_EN_PKT_GEN_EN_P_4_WIDTH         1
#define PIO_REG_PKT_GEN_EN_PKT_GEN_EN_P_4_MASK          (0x1U << PIO_REG_PKT_GEN_EN_PKT_GEN_EN_P_4_SHIFT)

/* Reserved. */
#define PIO_REG_PKT_GEN_EN_RSVD_ACCESSTYPE              GENHDR_ACCESSTYPE_
#define PIO_REG_PKT_GEN_EN_RSVD_SHIFT                   4
#define PIO_REG_PKT_GEN_EN_RSVD_WIDTH                   28
#define PIO_REG_PKT_GEN_EN_RSVD_MASK                    (0xFFFFFFFU << PIO_REG_PKT_GEN_EN_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* Start/Stop Packet generator register */
typedef union {
  struct {
    uint32_t pkt_gen_en_p_1 : 1;        /* Start/Stop Packet generator for Port 1. */
    uint32_t pkt_gen_en_p_2 : 1;        /* Start/Stop Packet generator for Port 2. */
    uint32_t pkt_gen_en_p_3 : 1;        /* Start/Stop Packet generator for Port 3. */
    uint32_t pkt_gen_en_p_4 : 1;        /* Start/Stop Packet generator for Port 4. */
    uint32_t rsvd : 28;                 /* Reserved. */
  } field;
  uint32_t val;
} PIO_REG_PKT_GEN_EN_t;
#endif /* GENHDR_STRUCT */

/* Port packet generator/checker reset register */
#define PIO_REG_PORT_PKT_RST                            0x00000010U
/* Packet generator/checker reset for Port 1. */
#define PIO_REG_PORT_PKT_RST_PORT_PKT_RST_P_1_SHIFT     0
#define PIO_REG_PORT_PKT_RST_PORT_PKT_RST_P_1_WIDTH     1
#define PIO_REG_PORT_PKT_RST_PORT_PKT_RST_P_1_MASK      (0x1U << PIO_REG_PORT_PKT_RST_PORT_PKT_RST_P_1_SHIFT)

/* Packet generator/checker reset for Port 2. */
#define PIO_REG_PORT_PKT_RST_PORT_PKT_RST_P_2_SHIFT     1
#define PIO_REG_PORT_PKT_RST_PORT_PKT_RST_P_2_WIDTH     1
#define PIO_REG_PORT_PKT_RST_PORT_PKT_RST_P_2_MASK      (0x1U << PIO_REG_PORT_PKT_RST_PORT_PKT_RST_P_2_SHIFT)

/* Packet generator/checker reset for Port 3. */
#define PIO_REG_PORT_PKT_RST_PORT_PKT_RST_P_3_SHIFT     2
#define PIO_REG_PORT_PKT_RST_PORT_PKT_RST_P_3_WIDTH     1
#define PIO_REG_PORT_PKT_RST_PORT_PKT_RST_P_3_MASK      (0x1U << PIO_REG_PORT_PKT_RST_PORT_PKT_RST_P_3_SHIFT)

/* Packet generator/checker reset for Port 4. */
#define PIO_REG_PORT_PKT_RST_PORT_PKT_RST_P_4_SHIFT     3
#define PIO_REG_PORT_PKT_RST_PORT_PKT_RST_P_4_WIDTH     1
#define PIO_REG_PORT_PKT_RST_PORT_PKT_RST_P_4_MASK      (0x1U << PIO_REG_PORT_PKT_RST_PORT_PKT_RST_P_4_SHIFT)

/* Reserved. */
#define PIO_REG_PORT_PKT_RST_RSVD_ACCESSTYPE            GENHDR_ACCESSTYPE_
#define PIO_REG_PORT_PKT_RST_RSVD_SHIFT                 4
#define PIO_REG_PORT_PKT_RST_RSVD_WIDTH                 28
#define PIO_REG_PORT_PKT_RST_RSVD_MASK                  (0xFFFFFFFU << PIO_REG_PORT_PKT_RST_RSVD_SHIFT)

#ifdef GENHDR_STRUCT
/* Port packet generator/checker reset register */
typedef union {
  struct {
    uint32_t port_pkt_rst_p_1 : 1;      /* Packet generator/checker reset for Port 1. */
    uint32_t port_pkt_rst_p_2 : 1;      /* Packet generator/checker reset for Port 2. */
    uint32_t port_pkt_rst_p_3 : 1;      /* Packet generator/checker reset for Port 3. */
    uint32_t port_pkt_rst_p_4 : 1;      /* Packet generator/checker reset for Port 4. */
    uint32_t rsvd : 28;                 /* Reserved. */
  } field;
  uint32_t val;
} PIO_REG_PORT_PKT_RST_t;
#endif /* GENHDR_STRUCT */

/* Transmitted Packet Length register */
#define PIO_REG_PKT_LEN                         0x00000020U
/*
 * Transmitted Packet Length (up to 4GB) 
 * 
 * (Will support packets of any odd length as well).
 */
#define PIO_REG_PKT_LEN_PKT_LEN_SHIFT           0
#define PIO_REG_PKT_LEN_PKT_LEN_WIDTH           32
#define PIO_REG_PKT_LEN_PKT_LEN_MASK            (0xFFFFFFFFU << PIO_REG_PKT_LEN_PKT_LEN_SHIFT)

#ifdef GENHDR_STRUCT
/* Transmitted Packet Length register */
typedef union {
  struct {
    uint32_t pkt_len : 32;      /* Transmitted Packet Length (up to 4GB)
                                   
                                   (Will support packets of any odd length as well). */
  } field;
  uint32_t val;
} PIO_REG_PKT_LEN_t;
#endif /* GENHDR_STRUCT */

/* No. of Idle cycles register */
#define PIO_REG_IDL_CYC                         0x00000030U
/* No of Idle cycles in between two packets (in multiples of 64 bytes). */
#define PIO_REG_IDL_CYC_IDLCYC_SHIFT            0
#define PIO_REG_IDL_CYC_IDLCYC_WIDTH            32
#define PIO_REG_IDL_CYC_IDLCYC_MASK             (0xFFFFFFFFU << PIO_REG_IDL_CYC_IDLCYC_SHIFT)

#ifdef GENHDR_STRUCT
/* No. of Idle cycles register */
typedef union {
  struct {
    uint32_t idlcyc : 32;       /* No of Idle cycles in between two packets (in multiples of 64 bytes). */
  } field;
  uint32_t val;
} PIO_REG_IDL_CYC_t;
#endif /* GENHDR_STRUCT */

/* No of Packets to be sent */
#define PIO_REG_PKT_CNT_2_GEN                   0x00000040U
/*
 * No of Packets to be sent. (when Start bit is set in Address offset 0x0, the
 * configured number of packets in this register will be transmitted by the
 * Generator and then it will stop. User is expected to reset the Start bit and
 * re-assert it to trigger the next set of packets).
 */
#define PIO_REG_PKT_CNT_2_GEN_PKT_CNT_SHIFT     0
#define PIO_REG_PKT_CNT_2_GEN_PKT_CNT_WIDTH     32
#define PIO_REG_PKT_CNT_2_GEN_PKT_CNT_MASK      (0xFFFFFFFFU << PIO_REG_PKT_CNT_2_GEN_PKT_CNT_SHIFT)

#ifdef GENHDR_STRUCT
/* No of Packets to be sent */
typedef union {
  struct {
    uint32_t pkt_cnt : 32;      /* No of Packets to be sent. (when Start bit is set in Address offset 0x0, the
                                   configured number of packets in this register will be transmitted by the
                                   Generator and then it will stop. User is expected to reset the Start bit and
                                   re-assert it to trigger the next set of packets). */
  } field;
  uint32_t val;
} PIO_REG_PKT_CNT_2_GEN_t;
#endif /* GENHDR_STRUCT */

/* Expected Packet Length for Checker module */
#define PIO_REG_EXP_PKT_LEN                             0x00000050U
/*
 * Expected Packet Length for Checker module(up to 4GB)(Will support packets of
 * any odd length as well).
 */
#define PIO_REG_EXP_PKT_LEN_EXP_PKT_LEN_SHIFT           0
#define PIO_REG_EXP_PKT_LEN_EXP_PKT_LEN_WIDTH           32
#define PIO_REG_EXP_PKT_LEN_EXP_PKT_LEN_MASK            (0xFFFFFFFFU << PIO_REG_EXP_PKT_LEN_EXP_PKT_LEN_SHIFT)

#ifdef GENHDR_STRUCT
/* Expected Packet Length for Checker module */
typedef union {
  struct {
    uint32_t exp_pkt_len : 32;  /* Expected Packet Length for Checker module(up to 4GB)(Will support packets of
                                   any odd length as well). */
  } field;
  uint32_t val;
} PIO_REG_EXP_PKT_LEN_t;
#endif /* GENHDR_STRUCT */

/* Good Packet Counter register */
#define PIO_REG_P1_GD_PKT_CNT                           0x00000100U
/*
 * Good Packet Counter
 * 
 * (Register contents will be cleared on read).
 */
#define PIO_REG_P1_GD_PKT_CNT_GD_PKT_CNT_SHIFT          0
#define PIO_REG_P1_GD_PKT_CNT_GD_PKT_CNT_WIDTH          32
#define PIO_REG_P1_GD_PKT_CNT_GD_PKT_CNT_MASK           (0xFFFFFFFFU << PIO_REG_P1_GD_PKT_CNT_GD_PKT_CNT_SHIFT)

#ifdef GENHDR_STRUCT
/* Good Packet Counter register */
typedef union {
  struct {
    uint32_t gd_pkt_cnt : 32;   /* Good Packet Counter
                                   
                                   (Register contents will be cleared on read). */
  } field;
  uint32_t val;
} PIO_REG_P1_GD_PKT_CNT_t;
#endif /* GENHDR_STRUCT */

/* Bad Packet Counter register */
#define PIO_REG_P1_BD_PKT_CNT                           0x00000108U
/*
 * Bad Packet Counter(length error or data pattern error or no EOP)
 * 
 * (Register contents will be cleared on read).
 */
#define PIO_REG_P1_BD_PKT_CNT_BD_PKT_CNT_SHIFT          0
#define PIO_REG_P1_BD_PKT_CNT_BD_PKT_CNT_WIDTH          32
#define PIO_REG_P1_BD_PKT_CNT_BD_PKT_CNT_MASK           (0xFFFFFFFFU << PIO_REG_P1_BD_PKT_CNT_BD_PKT_CNT_SHIFT)

#ifdef GENHDR_STRUCT
/* Bad Packet Counter register */
typedef union {
  struct {
    uint32_t bd_pkt_cnt : 32;   /* Bad Packet Counter(length error or data pattern error or no EOP)
                                   
                                   (Register contents will be cleared on read). */
  } field;
  uint32_t val;
} PIO_REG_P1_BD_PKT_CNT_t;
#endif /* GENHDR_STRUCT */

/* Good Packet Counter register */
#define PIO_REG_P2_GD_PKT_CNT                           0x00000200U
/*
 * Good Packet Counter
 * 
 * (Register contents will be cleared on read).
 */
#define PIO_REG_P2_GD_PKT_CNT_GD_PKT_CNT_SHIFT          0
#define PIO_REG_P2_GD_PKT_CNT_GD_PKT_CNT_WIDTH          32
#define PIO_REG_P2_GD_PKT_CNT_GD_PKT_CNT_MASK           (0xFFFFFFFFU << PIO_REG_P2_GD_PKT_CNT_GD_PKT_CNT_SHIFT)

#ifdef GENHDR_STRUCT
/* Good Packet Counter register */
typedef union {
  struct {
    uint32_t gd_pkt_cnt : 32;   /* Good Packet Counter
                                   
                                   (Register contents will be cleared on read). */
  } field;
  uint32_t val;
} PIO_REG_P2_GD_PKT_CNT_t;
#endif /* GENHDR_STRUCT */

/* Bad Packet Counter register */
#define PIO_REG_P2_BD_PKT_CNT                           0x00000208U
/*
 * Bad Packet Counter(length error or data pattern error or no EOP)
 * 
 * (Register contents will be cleared on read).
 */
#define PIO_REG_P2_BD_PKT_CNT_BD_PKT_CNT_SHIFT          0
#define PIO_REG_P2_BD_PKT_CNT_BD_PKT_CNT_WIDTH          32
#define PIO_REG_P2_BD_PKT_CNT_BD_PKT_CNT_MASK           (0xFFFFFFFFU << PIO_REG_P2_BD_PKT_CNT_BD_PKT_CNT_SHIFT)

#ifdef GENHDR_STRUCT
/* Bad Packet Counter register */
typedef union {
  struct {
    uint32_t bd_pkt_cnt : 32;   /* Bad Packet Counter(length error or data pattern error or no EOP)
                                   
                                   (Register contents will be cleared on read). */
  } field;
  uint32_t val;
} PIO_REG_P2_BD_PKT_CNT_t;
#endif /* GENHDR_STRUCT */

/* Good Packet Counter register */
#define PIO_REG_P3_GD_PKT_CNT                           0x00000300U
/*
 * Good Packet Counter
 * 
 * (Register contents will be cleared on read).
 */
#define PIO_REG_P3_GD_PKT_CNT_GD_PKT_CNT_SHIFT          0
#define PIO_REG_P3_GD_PKT_CNT_GD_PKT_CNT_WIDTH          32
#define PIO_REG_P3_GD_PKT_CNT_GD_PKT_CNT_MASK           (0xFFFFFFFFU << PIO_REG_P3_GD_PKT_CNT_GD_PKT_CNT_SHIFT)

#ifdef GENHDR_STRUCT
/* Good Packet Counter register */
typedef union {
  struct {
    uint32_t gd_pkt_cnt : 32;   /* Good Packet Counter
                                   
                                   (Register contents will be cleared on read). */
  } field;
  uint32_t val;
} PIO_REG_P3_GD_PKT_CNT_t;
#endif /* GENHDR_STRUCT */

/* Bad Packet Counter register */
#define PIO_REG_P3_BD_PKT_CNT                           0x00000308U
/*
 * Bad Packet Counter(length error or data pattern error or no EOP)
 * 
 * (Register contents will be cleared on read).
 */
#define PIO_REG_P3_BD_PKT_CNT_BD_PKT_CNT_SHIFT          0
#define PIO_REG_P3_BD_PKT_CNT_BD_PKT_CNT_WIDTH          32
#define PIO_REG_P3_BD_PKT_CNT_BD_PKT_CNT_MASK           (0xFFFFFFFFU << PIO_REG_P3_BD_PKT_CNT_BD_PKT_CNT_SHIFT)

#ifdef GENHDR_STRUCT
/* Bad Packet Counter register */
typedef union {
  struct {
    uint32_t bd_pkt_cnt : 32;   /* Bad Packet Counter(length error or data pattern error or no EOP)
                                   
                                   (Register contents will be cleared on read). */
  } field;
  uint32_t val;
} PIO_REG_P3_BD_PKT_CNT_t;
#endif /* GENHDR_STRUCT */

/* Good Packet Counter register */
#define PIO_REG_P4_GD_PKT_CNT                           0x00000400U
/*
 * Good Packet Counter
 * 
 * (Register contents will be cleared on read).
 */
#define PIO_REG_P4_GD_PKT_CNT_GD_PKT_CNT_SHIFT          0
#define PIO_REG_P4_GD_PKT_CNT_GD_PKT_CNT_WIDTH          32
#define PIO_REG_P4_GD_PKT_CNT_GD_PKT_CNT_MASK           (0xFFFFFFFFU << PIO_REG_P4_GD_PKT_CNT_GD_PKT_CNT_SHIFT)

#ifdef GENHDR_STRUCT
/* Good Packet Counter register */
typedef union {
  struct {
    uint32_t gd_pkt_cnt : 32;   /* Good Packet Counter
                                   
                                   (Register contents will be cleared on read). */
  } field;
  uint32_t val;
} PIO_REG_P4_GD_PKT_CNT_t;
#endif /* GENHDR_STRUCT */

/* Bad Packet Counter register */
#define PIO_REG_P4_BD_PKT_CNT                           0x00000408U
/*
 * Bad Packet Counter(length error or data pattern error or no EOP)
 * 
 * (Register contents will be cleared on read).
 */
#define PIO_REG_P4_BD_PKT_CNT_BD_PKT_CNT_SHIFT          0
#define PIO_REG_P4_BD_PKT_CNT_BD_PKT_CNT_WIDTH          32
#define PIO_REG_P4_BD_PKT_CNT_BD_PKT_CNT_MASK           (0xFFFFFFFFU << PIO_REG_P4_BD_PKT_CNT_BD_PKT_CNT_SHIFT)

#ifdef GENHDR_STRUCT
/* Bad Packet Counter register */
typedef union {
  struct {
    uint32_t bd_pkt_cnt : 32;   /* Bad Packet Counter(length error or data pattern error or no EOP)
                                   
                                   (Register contents will be cleared on read). */
  } field;
  uint32_t val;
} PIO_REG_P4_BD_PKT_CNT_t;
#endif /* GENHDR_STRUCT */

#endif /* _PIO_REG_REGISTERS_H_ */

