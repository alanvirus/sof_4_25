/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <rte_memzone.h>
#include <rte_string_fns.h>
#include"mcdma_mailbox.h"
#include "mcdma.h"
#include "qdma_regs_2_registers.h"
#include "rte_pmd_mcdma.h"
#include "mcdma_platform.h"

#ifdef IFC_MCDMA_MAILBOX

/*Map the WB memory for host and HW access for WB status*/
uint64_t mb_wb_status;

/* Functions */
int ifc_mcdma_mb_send(uint16_t portid, struct ifc_mb_ctx * mb_ctx){

	uint8_t mb_cmd;
	struct rte_eth_dev *dev = &rte_eth_devices[portid];
        struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;

	union ifc_mcdma_mb_payload msg_payload = mb_ctx->msg_payload;
	union ifc_mcdma_mb_payload *ptr_msg_payload = &msg_payload;
	struct mb_cmd_ping *msg_cmd = (struct mb_cmd_ping *)ptr_msg_payload;
	
	mb_cmd = msg_cmd->cmd;

	switch(mb_cmd){

	case IFC_MB_CMD_PING :
		ifc_mcdma_send_mb_cmd_ping(mcdma_dev, (struct mb_cmd_ping *)ptr_msg_payload);
		break;

	case IFC_MB_CMD_DOWN :
		break;
	case IFC_MB_CMD_CSR :
		break;
	case IFC_MB_CMD_CHAN_ADD :
		break;
	case IFC_MB_CMD_CHAN_DEL :
		break;
	case IFC_MB_CMD_CHAN_ADD_RESP :
		break;
	case IFC_MB_CMD_CHAN_DEL_RESP:
		break;
	case IFC_MB_CMD_CSR_RESP :
		break;
	default: 
		/*invalid command*/
	}

	return 0;
}


/* Send the message/payload to target PF/VF. */
int ifc_mcdma_mb_cmd_send(struct ifc_mcdma_device *mcdma_dev, struct ifc_mailbox_msg *msg_payload){


	if(!ifc_mb_check_ob_status(mcdma_dev)){
		printf("fail due to ob status is not free\n");
		return -1;
	}

	if(mcdma_dev->is_vf){
		ifc_mcdma_mb_vf2pf_send( mcdma_dev, msg_payload);

	}
		ifc_mcdma_mb_pf2pfvf_send( mcdma_dev, msg_payload);

	return 0 ;
}

int ifc_mcdma_send_mb_cmd_ping( struct ifc_mcdma_device *mcdma_dev, struct ifc_mb_ctx *mb_ctx){

	union ifc_mcdma_mb_payload msg_payload = mb_ctx->msg_payload;
	union ifc_mcdma_mb_payload *ptr_msg_payload = &msg_payload;
	struct mb_cmd_ping *mb_ping = (struct mb_cmd_ping *)ptr_msg_payload;
	
	struct ifc_mailbox_msg mb_msg;

	memset((void*)&mb_msg, 0 , sizeof(struct ifc_mailbox_msg));

	mb_msg.cmd = mb_ping->cmd;
	mb_msg.is_vf = mb_ping->is_vf;
	mb_msg.func_num = mb_ping->func_num;
	
	ifc_mcdma_mb_cmd_send(mcdma_dev, &mb_msg);

	return 0;
}



/*Internal APIs:*/
/* Check the outbox status.*/
int ifc_mb_check_ob_status( struct ifc_mcdma_device *mcdma_dev){

	void *mb_csr;
	uint32_t status = 0;

	mb_csr = mcdma_dev->bar_addr[IFC_MCDMA_CONFIG_BAR] + IFC_MB_OFFSET;

	/* read STATUS.O_MSG_PEND_STS */
	status = ifc_readl(mb_csr + IFC_MB_STATUS);

	if(status & (1 << 1)){
		return 0;
	}
	else 
		return -1;

/*TBD*/
	/* Keep reading untill it get reset.	*/
	/*return -1 ? if it is not free in 3x20 micro second.*/

}



int ifc_mcdma_mb_vf2pf_send(struct ifc_mcdma_device *mcdma_dev, struct ifc_mailbox_msg *msg_payload){

	void *mb_csr;
	uint16_t vf_func_num = mcdma_dev->vf;
	uint32_t addr = IFC_MB_OUT_MSG_REG(vf_func_num);
	uint32_t data;
	uint32_t mb_msg_payload;

	memcpy(&mb_msg_payload, msg_payload, sizeof(struct ifc_mailbox_msg));

	mb_csr = mcdma_dev->bar_addr[IFC_MCDMA_CONFIG_BAR] + IFC_MB_OFFSET;

	/*  write command to OUT_MSG_REG [OUT_MSG_DATA - 0: 7] */
	ifc_writel(mb_csr + addr, (uint32_t)mb_msg_payload);

	data = 0x1;
	/* Write to CONTROL.WRITE_MSG */
	ifc_writel(mb_csr + IFC_MB_CONTROL, data);
	return 0;
}


int ifc_mcdma_mb_pf2pfvf_send(struct ifc_mcdma_device *mcdma_dev, struct ifc_mailbox_msg *msg_payload){

	void *mb_csr;
	uint16_t pf_func_num = mcdma_dev->pf;
	uint32_t addr = IFC_MB_OUT_MSG_REG(pf_func_num);
	uint32_t data = 0;
	uint8_t target_func = msg_payload->func_num;
	uint32_t mb_msg_payload;

	memcpy(&mb_msg_payload, msg_payload, sizeof(struct ifc_mailbox_msg));
	
	mb_csr = mcdma_dev->bar_addr[IFC_MCDMA_CONFIG_BAR] + IFC_MB_OFFSET;

	/* write command to OUT_MSG_REG [OUT_MSG_DATA - 0: 7]; */
	ifc_writel(mb_csr + addr, (uint32_t)mb_msg_payload);

	/* Write to CONTROL.WRITE_MSG */
	data = data | 0x1; 

	/*Configure the VF enable*/
	if(msg_payload->trgt_is_vf)	
		/* write CONTROL.Target_PF_en */
		data = data & ~(1 << 16);
	else
		/* write CONTROL.Target_PF_en */
		data = data | (1 << 16);

	/* write CONTROL.Target_func_num */
	data = data | (target_func << 17);

	ifc_writel(mb_csr + IFC_MB_CONTROL, data);	
	return 0;
}

/*Enable or disable the WB method or interrupt method for mailbox feature*/
int ifc_mb_intr_wb_control(uint16_t portid, uint32_t data)
{
        void *mb_csr = NULL;
	uint64_t wb_status;

        struct rte_eth_dev *dev = &rte_eth_devices[portid];
        struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;

        /*Get the GCSR base address and its offset */
        mb_csr = mcdma_dev->bar_addr[IFC_MCDMA_CONFIG_BAR] + IFC_MB_OFFSET;

        /*Validate gcsr base addrss*/
        if (mb_csr == NULL) {
                printf("Invalid GCSR base address\n");
                return -1;
        }

	/*Mapped the WB status b/w host and HW for WB status*/
        wb_status = (uint64_t)rte_mem_virt2phy(&mb_wb_status);
        ifc_writel(mb_csr + IFC_MB_WB_ADDR_LOW, wb_status);
        ifc_writel(mb_csr + IFC_MB_WB_ADDR_HIGH, (wb_status >> 32));

        /*send completion write back instead of constant polling by driver*/
        ifc_writel(mb_csr + IFC_MB_INTR_WB_CTRL, data);
        
	return 0;
}

/*WB response status*/
uint64_t ifc_mb_wb_receive_status(uint16_t portid)
{
        void* mb_csr = NULL;
	uint32_t status = 0;

        /*Get the mcdma device context using port id*/
        struct rte_eth_dev *dev = &rte_eth_devices[portid];
        struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;

        /*Get the GCSR. base address and its offset */
        mb_csr = mcdma_dev->bar_addr[IFC_MCDMA_CONFIG_BAR] + IFC_MB_OFFSET;

        /*Validate gcsr base addrss*/
        if (mb_csr == NULL) {
                printf("Invalid GCSR base address\n");
                return -1;
        }
	/* Read  the inbox status from STATUS.I_MSG_PEND_ST reg as alternatvie for WB status completion */
        status = ifc_readl(mb_csr + IFC_MB_STATUS);
        /* TBD */
        /*Keep reading untill it get reset.
 	return -1 ? if it is not free in 3x20 micro second.
 	Check the inbox message pending status bit i.e zero position*/
        if (status & 0x1) {
                return 1;
         } else {
                return 0;
        }
        /*TBD: WB purpose*/
        /*if (mb_wb_status != 0)
 	{
           return 1;
        } else {
           return 0;
        }*/

}

/* Check the inbox status */
int ifc_mb_check_ib_status(struct ifc_mcdma_device *mcdma_dev)
{
        void *mb_csr = NULL;
        uint32_t status = 0;

        /*find base GCSR address */
        mb_csr = mcdma_dev->bar_addr[IFC_MCDMA_CONFIG_BAR] + IFC_MB_OFFSET;

        /*Validate gcsr base addrss*/
        if (mb_csr == NULL) {
                printf("Invalid GCSR base address\n");
                return -1;
        }

        /* Read  the inbox status from STATUS.I_MSG_PEND_ST reg*/
 	status = ifc_readl(mb_csr + IFC_MB_STATUS);
        /* TBD */ 
	/*Keep reading untill it get reset.    
        return -1 ? if it is not free in 3x20 micro second.
        Check the inbox message pending status bit i.e zero position*/
        if (status & 0x1) {
         	return 1;
        } else {
                return 0;
	}
}
/* Receive the message/payload from the PF/VF*/
int ifc_mcdma_mb_cmd_recv(uint16_t portid, struct ifc_mailbox_msg* msg_payload)
{
	uint8_t recv_func_type;
        void* mb_csr = NULL;
        uint32_t addr;
        uint32_t mb_msg_payload;
        uint32_t data;
        uint32_t pf_vf_num;

        struct rte_eth_dev *dev = &rte_eth_devices[portid];
        struct ifc_mcdma_device *mcdma_dev = dev->data->dev_private;

        /*Check the inbox pending bit status*/
        if (!ifc_mb_check_ib_status(mcdma_dev)){
                printf("fail due to IB status is not available\n");
                return -1;
        }
        /*Validate receive msg payload addrss*/
        if (msg_payload == NULL) {
                printf("Invalid recv msg payload address\n");
                return -1;
        }
        /* read STATUS.PF_CURRENT_SRC_VLD*/
        /* read STATUS.VF_PF_CURRENT_SRC*/
        /* read IN_MSG_DATA[pfnum/vf/num]*/
        /*Get the GCSR base address and its offset */
        mb_csr = mcdma_dev->bar_addr[IFC_MCDMA_CONFIG_BAR] + IFC_MB_OFFSET;
        /*Validate gcsr base addrss*/
        if (mb_csr == NULL) {
                printf("Invalid GCSR base address\n");
                return -1;
        }
        /*Check function type is vf or pf and its number*/
        pf_vf_num = ifc_readl(mb_csr + IFC_MB_STATUS);
        recv_func_type = ((pf_vf_num >> 11u) & 0x1 );

        /*Check the function type is pf*/
        if (recv_func_type == 1) {
                /*Get the pf inbox address space for pf number*/
                addr = IFC_MB_IN_MSG_REG(mcdma_dev->pf);
                /* Read response data from IN_MSG_REG [IN_MSG_DATA - 0: 7] */
                mb_msg_payload = ifc_readl(mb_csr + addr);

                /*copy the read ib msg data from HW*/
                memcpy(msg_payload, &mb_msg_payload, sizeof(mb_msg_payload));

                /*VF_PF_CURRENT_SRC’s value is PF number or VF number.
 *                 *For PF to PF message, this bit will be set [11]*/
                msg_payload->trgt_is_vf = recv_func_type;

                /*Source PF/VF number for the message in inbox[10:3]*/
                msg_payload->func_num = ((pf_vf_num >> 3u) & 0xFF);

        } else { /*function type is vf and its number*/

                /*Get the vf inbox address space based on vf number*/
                addr = IFC_MB_IN_MSG_REG(mcdma_dev->vf);
                /* Read response data from IN_MSG_REG [IN_MSG_DATA - 0: 7] */
                mb_msg_payload = ifc_readl(mb_csr + addr);

                /*copy the read ib msg data from HW*/
                memcpy(msg_payload, &mb_msg_payload, sizeof(mb_msg_payload));

        }

        /* Write to CONTROL.READ_MSG bit to confirm an inbox read completion*/
        data = 0x2;
        ifc_writel(mb_csr + IFC_MB_CONTROL, data);

        return 0;

}
/*This function is used for configuration of Channel allocation in L2P channel map table*/
int ifc_mb_channel_allocate(struct ifc_mcdma_device *mcdma_dev, struct ifc_mb_ctx* mb_ctx)
{
        uint32_t data;
        uint32_t func_num;
        uint32_t is_vf;
        uint32_t num_of_chan;
        union    l2p_ch_map map_table;
        void*    dca_mb = NULL;

        /*Validate the mcdma dev address*/
        if (mcdma_dev == NULL) {
                printf("Invalid MCDMA dev address\n");
                return -1;
        }

        /*Get the dca mailbox address from device context which is mapped to PCIe BAR0*/
        dca_mb = mcdma_dev->bar_addr[IFC_MCDMA_CONFIG_BAR] + IFC_DCA_MAILBOX_OFFSET;

        /*Validate the DCA MAILBOX base address*/
        if (dca_mb == NULL) {
                printf("Invalid DCA MAILBOX base address\n");
                return -1;
        }
        /*Validate the mailbox context address*/
        if (mb_ctx == NULL) {
                printf("Invalid MAILBOX CONTEXT address\n");
                return -1;
        }
	/*Configure the l2p channel map table based on cmd request*/
        is_vf       = mb_ctx->msg_payload.chan_add.is_vf;
        func_num    = mb_ctx->msg_payload.chan_add.func_num;
        num_of_chan = mb_ctx->msg_payload.chan_add.num_of_chan;

        /*Program the pf/vf function details for which allocation/deallocation is happening DCA_L2P_CH_MAP_CFG*/             /*Check the VF function type*/
        if (is_vf == 1) {
                map_table.mtable.is_vf                = is_vf;
                map_table.mtable.vf_num               = func_num;
                map_table.mtable.logical_channel_num  = num_of_chan;

	}
        else {/*It is for PF function type and its configuration PF number[2:0]*/
                map_table.mtable.is_vf                = is_vf;
                map_table.mtable.pf_num               = func_num;
                map_table.mtable.logical_channel_num  = num_of_chan;
	}
	/*Configure L2P channel map*/
        ifc_writel(dca_mb + IFC_DCA_L2P_CH_MAP_CFG,  map_table.map_table);

        /*Configure the DCA_L2P_CH_MAP_CFG_CTRL register after channel added to L2P table.
    	 Mapping table update enable and channel alloc enable*/
        data = 0x3U;
        ifc_writel(dca_mb + IFC_DCA_L2P_CH_MAP_CFG_CTRL, data);

        return 0;
}
/*This function optionally can check for mapping table update complete of channel allocation*/
int ifc_mb_channel_add_respond(struct ifc_mcdma_device *mcdma_dev)
{
        uint32_t data;
        void* dca_mb = NULL;

        /*Validate the mcdma dev address*/
        if (mcdma_dev == NULL) {
                printf("Invalid MCDMA dev address\n");
                return -1;
        }

        /*Get the dca mailbox address from device context which is mapped to PCIe BAR0*/
        dca_mb = mcdma_dev->bar_addr[IFC_MCDMA_CONFIG_BAR] + IFC_DCA_MAILBOX_OFFSET;

        /*Validate the DCA MAILBOX base address*/
        if (dca_mb == NULL) {
                printf("Invalid DCA MAILBOX base address\n");
                return -1;
        }

        /*Read the channel allocate status*/
        data = ifc_readl(dca_mb + IFC_DCA_L2P_CH_MAP_CFG_STS);

        /*Check the channel add status when access to mapping table is completed*/
        if (data & 0x1) {
                return 0;
        }
        else {
                return -1;
        }
}
/*This function is used for configuration of  channel de-allocation in L2P channel map table*/
int ifc_mb_channel_deallocate(struct ifc_mcdma_device *mcdma_dev, struct ifc_mb_ctx* mb_ctx)
{
        uint32_t data;
        uint32_t func_num;
        uint32_t is_vf;
        uint32_t num_of_chan;
        void*    dca_mb = NULL;
        union    l2p_ch_map map_table;

        /*Validate the mcdma dev address*/
        if (mcdma_dev == NULL) {
                printf("Invalid MCDMA dev address\n");
                return -1;
        }

        /*Get the dca mailbox address from device context which is mapped to PCIe BAR0*/
        dca_mb = mcdma_dev->bar_addr[IFC_MCDMA_CONFIG_BAR] + IFC_DCA_MAILBOX_OFFSET;

        /*Validate the DCA MAILBOX base address*/
        if (dca_mb == NULL) {
                printf("Invalid DCA MAILBOX base address\n");
                return -1;
        }
        /*Validate the MAILBOX CONTEXT address*/
        if (mb_ctx == NULL) {
                printf("Invalid MAILBOX CONTEXT address\n");
                return -1;
        }
	/*Configure the l2p channel map table based on cmd request*/
        is_vf       = mb_ctx->msg_payload.chan_add.is_vf;
        func_num    = mb_ctx->msg_payload.chan_add.func_num;
        num_of_chan = mb_ctx->msg_payload.chan_add.num_of_chan;

        /*Program the pf/vf function details for which deallocation is happening DCA_L2P_CH_MAP_CFG*/
        /*Check the VF function type*/
        if (is_vf == 1) {
                map_table.mtable.is_vf                = is_vf;
                map_table.mtable.vf_num               = func_num;
                map_table.mtable.logical_channel_num  = num_of_chan;
        }
        else {/*It is for PF function type and its configuration*/
                map_table.mtable.is_vf                = is_vf;
                map_table.mtable.pf_num               = func_num;
                map_table.mtable.logical_channel_num  = num_of_chan;
        }
        /*Configure L2P channel map*/
        ifc_writel(dca_mb + IFC_DCA_L2P_CH_MAP_CFG, map_table.map_table);

        /*Configure the DCA_L2P_CH_MAP_CFG_CTRL register after channel deleted from L2P channel map table.
 	Mapping table update enable and channel alloc disable*/
        data = 0x1U;
        /*Configure the channel alloc bit and update mapping table*/
        ifc_writel(dca_mb + IFC_DCA_L2P_CH_MAP_CFG_CTRL, data);

        return 0;
}
/*This function optionally can check for mapping table update complete of channel deallocation*/
int ifc_mb_channel_delete_respond(struct ifc_mcdma_device *mcdma_dev)
{
        uint32_t data;
        void* dca_mb = NULL;

        /*Validate the mcdma dev address*/
        if (mcdma_dev == NULL) {
                printf("Invalid MCDMA dev address\n");
                return -1;
        }

        /*Get the dca mailbox address from device context which is mapped to PCIe BAR0*/
        dca_mb = mcdma_dev->bar_addr[IFC_MCDMA_CONFIG_BAR] + IFC_DCA_MAILBOX_OFFSET;

        /*Validate the DCA MAILBOX base address*/
        if (dca_mb == NULL) {
                printf("Invalid DCA MAILBOX base address\n");
                return -1;
        }
        /*Read the channel delete status*/
        data = ifc_readl(dca_mb + IFC_DCA_L2P_CH_MAP_CFG_STS);

        /*Check the channel delete status when access to mapping table is completed*/
        if (data & 0x1) {
                return 0;
        }
        else {
                return -1;
        }
}




#endif /* IFC_MCDMA_MAILBOX */
