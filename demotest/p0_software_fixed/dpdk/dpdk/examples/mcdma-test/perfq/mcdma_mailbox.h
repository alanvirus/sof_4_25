/* SPDX-License-Identifier: BSD-3-Clause
 *  * Copyright(c) 2020-2021 Intel Corporation
 *   */
#ifdef IFC_MCDMA_MAILBOX
/*header files */
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <rte_memzone.h>
#include <rte_string_fns.h>

/* ENUMS */
enum ifc_mcdma_mb_cmd {
        IFC_MB_CMD_PING = 1,
        IFC_MB_CMD_DOWN,
        IFC_MB_CMD_CSR,
        IFC_MB_CMD_CHAN_ADD,
        IFC_MB_CMD_CHAN_DEL,
        IFC_MB_CMD_CSR_RESP = 0x50,
        IFC_MB_CMD_CHAN_ADD_RESP,
        IFC_MB_CMD_CHAN_DEL_RESP
};

enum gcsr_field {
        IFC_GCSR_CTRL = 1,
        IFC_GCSR_VER_NUM,
        IFC_GCSR_NUM_PF,
        IFC_GCSR_SRIOV_ENABLED,
        IFC_GCSR_NUM_VF_PER_PF,         /* Specific to PFs*/
        IFC_GCSR_NUM_DMA_CHAN_PER_PF,
        IFC_GCSR_NUM_DMA_CHAN_PER_VF
};

enum ifc_dev_func {
        IFC_PRIV_PF,
        IFC_CHLD_PF,
        IFC_CHLD_VF
};

/* Structures and Unions */

struct mb_cmd_ping {
        uint8_t cmd;
        uint8_t is_vf;
        uint8_t func_num;
};

struct mb_cmd_down {
        uint8_t cmd;
        uint8_t is_vf;
        uint8_t func_num;
};

struct mb_cmd_csr {
        uint8_t cmd;
        uint8_t is_vf;
        uint8_t gcsr_field;
        uint32_t data;
};
struct mb_cmd_chan_add {
        uint8_t cmd;
        uint8_t is_vf;
        uint8_t func_num;
        uint8_t num_of_chan;
};
struct mb_cmd_chan_del {
        uint8_t cmd;
        uint8_t is_vf;
        uint8_t func_name;
        uint8_t logical_chan_num;
};
struct mb_cmd_chan_add_resp {
        uint8_t cmd;
        uint8_t is_vf;
        uint8_t func_name;
        uint8_t status;
        uint8_t logical_chan_num;
};

struct mb_cmd_chan_del_resp {
        uint8_t cmd;
        uint8_t is_vf;
        uint8_t func_name;
        uint8_t status;
        uint8_t logical_chan_num;
};

/*
 *  * Mail box message of 32 bits size communicated between pf/vf
 *  ---------------------------------------------------------------------------------
 *  |Command | Func Number | VF Flag | target is_vf | Status | Sub-command | Channel |
 *  |8 bits  |    8 bit    | 1 bits  |     1 bit    | 2 bits |   4 bits    | 8 bits  |
 *  ---------------------------------------------------------------------------------
 *  *
 *  */
struct ifc_mailbox_msg {
        uint8_t cmd;
        uint8_t func_num;
        uint8_t is_vf           : 1;
        uint8_t trgt_is_vf      : 1;
        uint8_t status          : 2;
        uint8_t sub_cmd         : 4;
        uint8_t logical_chan_num;
};

union ifc_mcdma_mb_payload {
        struct mb_cmd_ping ping;
        struct mb_cmd_down down;
        struct mb_cmd_csr confsr;
        struct mb_cmd_chan_add chan_add;
        struct mb_cmd_chan_del chan_del;
        struct mb_cmd_chan_add_resp chan_add_resp;
        struct mb_cmd_chan_del_resp chan_del_resp;
};

struct ifc_mb_ctx {
        union ifc_mcdma_mb_payload msg_payload;
        uint8_t is_vf;
        uint8_t dev_func_num;
        uint8_t dev_func_type;
        uint8_t trgt_func_num;
        uint8_t trgt_func_type;
};


/*mailbox library interface functions */

/* Send the message/payload to target PF/VF */
int ifc_mcdma_mb_send(uint16_t portid, struct ifc_mb_ctx *);
/* Receive the message/payload from the PF/VF */
int ifc_mcdma_mb_cmd_recv(uint16_t portid, struct ifc_mailbox_msg *msg_payload);

/*WB method  control */
int ifc_mb_intr_wb_control(uint16_t portid, uint32_t data);
/*WB response status*/
uint64_t ifc_mb_wb_receive_status(uint16_t portid);

#endif /* IFC_MCDMA_MAILBOX */

