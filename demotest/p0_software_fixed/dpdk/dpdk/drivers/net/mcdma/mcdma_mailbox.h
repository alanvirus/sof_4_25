/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#undef IFC_MCDMA_MAILBOX
#ifdef IFC_MCDMA_MAILBOX

#include <stdint.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <rte_memzone.h>
#include <rte_string_fns.h>
#include "mcdma.h"
#include "qdma_regs_2_registers.h"
#include "rte_pmd_mcdma.h"
#include "mcdma_platform.h"

/* MACROS */

#define IFC_GCSR_OFFSET		0x200000
#define IFC_MB_OFFSET           0x300000
#define IFC_MB_CONTROL		0x00
#define IFC_MB_STATUS		0x04
#define IFC_MB_OUT_MSG_REG(x)	(0x200) + ((x) * 4 )
#define IFC_MB_IN_MSG_REG(x)    (0x100) + ((x) * 4 ) /*Inbox message register*/
#define IFC_MB_READ_CONTROL     0x01 /*Message read control for inbox*/
#define IFC_MB_INTR_WB_CTRL     0x08
#define IFC_MB_WB_ADDR_LOW      0x0C
#define IFC_MB_WB_ADDR_HIGH     0x10

#define IFC_MB_TARGET_PF_EN_BIT	16
/*TBD : 3x20 micro second*/
#define IFC_MCDMA_MAILBOX_WAIT_COUNT   60
/*DCA with mailbox */
#define IFC_DCA_MAILBOX_OFFSET      0x38000000
#define IFC_DCA_L2P_CH_MAP_CFG      0x0000000C /*Logical to physical channel mapping details*/
#define IFC_DCA_L2P_CH_MAP_CFG_CTRL 0x00000010 /*Logical to physical channel mapping -control*/
#define IFC_DCA_L2P_CH_MAP_CFG_STS  0x00000014 /*Logical to physical channel mapping -status*/


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
	IFC_GCSR_NUM_VF_PER_PF,		/* Specific to PFs*/
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
 * Mail box message of 32 bits size communicated between pf/vf 
---------------------------------------------------------------------------------
|Command | Func Number | VF Flag | target is_vf | Status | Sub-command | Channel |
|8 bits	 |    8 bit    | 1 bits  |     1 bit    | 2 bits |   4 bits    | 8 bits  |
---------------------------------------------------------------------------------
*
*/
struct ifc_mailbox_msg {
	uint8_t cmd;
	uint8_t func_num;
	uint8_t is_vf 		: 1;
	uint8_t trgt_is_vf	: 1;
	uint8_t status		: 2; 
	uint8_t sub_cmd 	: 4;
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

/*L2P channel map table*/
struct l2p_ch_map_table {
        uint32_t pf_num:3;
        uint32_t vf_num:8;
        uint32_t is_vf :1;
        uint32_t logical_channel_num: 8;
        uint32_t physical_channel_num: 12;
};

union l2p_ch_map {
        struct l2p_ch_map_table mtable;
        uint32_t map_table;
};


/* Functions */

/* Send the message/payload to target PF/VF. */
int ifc_mcdma_mb_send(uint16_t portid, struct ifc_mb_ctx *);

/* Internal APIs:*/

/* Check the outbox status.*/
int ifc_mb_check_ob_status( struct ifc_mcdma_device *mcdma_dev);

int ifc_mcdma_mb_vf2pf_send( struct ifc_mcdma_device *mcdma_dev, struct ifc_mailbox_msg *msg_payload );

int ifc_mcdma_mb_pf2pfvf_send(struct ifc_mcdma_device *mcdma_dev, struct ifc_mailbox_msg *msg_payload);

/* Send the message/payload to target PF/VF. */
int ifc_mcdma_mb_cmd_send(struct ifc_mcdma_device *mcdma_dev, struct ifc_mailbox_msg *msg_payload);

/* function for all commands */
int ifc_mcdma_send_mb_cmd_ping( struct ifc_mcdma_device *mcdma_dev, struct ifc_mb_ctx *mb_cxt);

/* Check the inbox status */
int ifc_mb_check_ib_status(struct ifc_mcdma_device *mcdma_dev);
/* Receive the message/payload from the PF/VF*/
int ifc_mcdma_mb_cmd_recv(uint16_t portid, struct ifc_mailbox_msg *msg_payload);
/*Intr or WB  enable or disble*/
int ifc_mb_intr_wb_control(uint16_t portid, uint32_t data);
/*WB response status*/
uint64_t ifc_mb_wb_receive_status(uint16_t portid);
/*This function is used to configure the channel allocation in L2P channel map table*/
int ifc_mb_channel_allocate(struct ifc_mcdma_device *mcdma_dev, struct ifc_mb_ctx* mb_ctx);
/*This function optionally can check for mapping table update complete of channel allocation*/
int ifc_mb_channel_add_respond(struct ifc_mcdma_device *mcdma_dev);
/*This function is used to configure the channel de-allocation in L2P channel map table*/
int ifc_mb_channel_deallocate(struct ifc_mcdma_device *mcdma_dev, struct ifc_mb_ctx* mb_ctx);
/*This function optionally can check for mapping table update complete of channel deallocation*/
int ifc_mb_channel_delete_respond(struct ifc_mcdma_device *mcdma_dev);


#endif /* IFC_MCDMA_MAILBOX */

