/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#include "perfq_app.h"
#include "rte_atomic.h"
#include "rte_mbuf.h"
#include "rte_ethdev.h"
#include "rte_mbuf_core.h"
#include "ifc_qdma_utils.h"
#ifdef IFC_MCDMA_RANDOMIZATION
#include "perfq_random.h"
#endif

#ifdef DUMP_FAIL_DATA
#include <rte_hexdump.h>
#endif

extern struct struct_flags *global_flags;
extern pthread_mutex_t dev_lock;
extern struct thread_context *global_tctx;
extern bool force_exit;
static void pkt_gen_config(struct queue_context *tctx __attribute__((unused)));
static int queue_cleanup(struct queue_context *tctx);
static enum
ifc_config_mcdma_cmpl_proc config_mcdma_cmpl_proc = IFC_CONFIG_QDMA_COMPL_PROC;

#ifdef IFC_QDMA_DYN_CHAN
extern uint16_t mcdma_ph_chno[AVST_MAX_NUM_CHAN];
#endif

uint32_t per_channel_ed_config[AVST_CHANNELS];

#if 0//def IFC_MCDMA_RANDOMIZATION
static uint32_t ifc_mcdma_get_random(struct queue_context *tctx)
{
    //uint32_t num = ((rand() % (u - l + 1)) + l) & 0xFFFFFFFC;
	//((tctx->flags->rand_param_list[tctx->rand_idx].hifun_p % (1024 - 64 + 1)) + 64) & 0xFFFFFFFC;
	uint32_t num = (tctx->flags->rand_param_list[tctx->rand_f_idx].hifun_p > 0) ? tctx->flags->rand_param_list[tctx->rand_f_idx].hifun_p:64;
	
    return num;
}
#endif

static void perfq_smp_barrier(void){
	rte_wmb();
//	rte_mb();
	rte_smp_wmb();
//	rte_rmb();
//	rte_smp_mb();
	rte_smp_rmb();
}

void signal_mask(void)
{
	sigset_t thread_mask;

	sigemptyset(&thread_mask);
	sigaddset(&thread_mask, SIGINT);
	pthread_sigmask(SIG_BLOCK, &thread_mask, NULL);

}

int pio_bit_writer(uint16_t portid,
		   uint64_t offset, int n, int set)
{
	uint64_t val;
	uint64_t addr = PIO_REG_PKT_GEN_EN + offset;

	val = ifc_mcdma_pio_read64(portid, addr);
	if (set)
		val |= (1ULL << n);
	else
		val &= ~(1ULL << n);

	ifc_mcdma_pio_write64(portid, addr, val);
	return 0;
}

struct queue_context *ifc_get_que_ctx(int qid, int dir)
{
	struct queue_context *qctx = NULL;
	int threads_count;
	int i, q;

	threads_count = global_flags->num_threads;

	for (i = 0; i < threads_count; i++) {
		for(q = 0; q < global_tctx[i].qcnt; q++) {
			if (global_tctx[i].qctx[q].channel_id == qid &&
			    global_tctx[i].qctx[q].direction == dir){
				qctx = &(global_tctx[i].qctx[q]);
				break;
			}
		}
	}
	return qctx;
}

int ifc_mcdma_queue_reset(int port_id, int qid, int dir)
{
        struct rte_eth_rxconf rx_conf;
        struct rte_eth_txconf tx_conf;
        struct rte_mempool *rx_mbuf_pool = NULL;
        char rx_pool_name[64];
	int ret;

        rx_conf.offloads = 0;
        rx_conf.reserved_ptrs[0] = ifc_mcdma_umsix_irq_handler;
        tx_conf.offloads = 0;
        tx_conf.reserved_ptrs[0] = ifc_mcdma_umsix_irq_handler;


	if (dir) {
		rte_eth_dev_tx_queue_stop(port_id, qid);
		ret = rte_eth_tx_queue_setup(port_id, qid, QUEUE_SIZE,
			rte_eth_dev_socket_id(port_id), &tx_conf);
		if (ret < 0)
			return ret;
		rte_eth_dev_tx_queue_start(port_id, qid);
	} else {
		rte_eth_dev_rx_queue_stop(port_id, qid);

		snprintf(rx_pool_name, sizeof(rx_pool_name),
			"RX MBUF POOL %u", qid);

		rx_mbuf_pool = rte_pktmbuf_pool_create(rx_pool_name,
			IFC_QDMA_NUM_CHUNKS_IN_EACH_POOL,
			MBUF_CACHE_SIZE, MBUF_PRIVATE_DATA_SIZE,
			global_flags->packet_size, rte_socket_id());
		if (rx_mbuf_pool == NULL) {
			printf("Cannot create RX mbuf pool"
                                " qid: %u\n", qid);
			global_flags->mbuf_pool_fail = 1;
			return -1;
		}

		ret = rte_eth_rx_queue_setup(port_id, qid, QUEUE_SIZE,
				rte_eth_dev_socket_id(port_id),&rx_conf,
				rx_mbuf_pool);
		if (ret < 0)
			return ret;
#ifdef IFC_QDMA_DYN_CHAN
		/* Get the physical channel number */
		mcdma_ph_chno[qid] = ret;
#endif
		rte_eth_dev_rx_queue_start(port_id, qid);
	}

	return 0;
}

int ifc_mcdma_umsix_irq_handler(int port_id __rte_unused,
				int qid,
				int dir, int *err_info)
{
	struct queue_context *qctx;

	qctx = ifc_get_que_ctx(qid, dir);
	if (qctx == NULL){
		printf("Invalid Queue Context");
		return -1;
	}

	qctx->umsix_err_cnt++;
	if(*err_info & TID_ERROR){
		qctx->tid_err++;
#ifndef IFC_QDMA_DYN_CHAN
		if (ifc_mcdma_queue_reset(port_id, qid, dir))
			printf("%s: Error while reseting queue\n",__func__);
#endif
	} else if ((*err_info & DESC_FETCH_EVENT) || (*err_info & DESC_FETCH_EVENT) ||(*err_info & COMPLETION_TIME_OUT_ERROR)){
		if (*err_info & DESC_FETCH_EVENT )
			printf("Descriptor fetch error occured\n");
		else if (*err_info & DATA_FETCH_EVENT)
			printf("Data fetch error occured\n");

		if(*err_info & COMPLETION_TIME_OUT_ERROR){
			qctx->comp_err++;
			printf("Completion timeout occured\n");
		}
		printf("INFO: port id = %d, channel id = %d pf = %d vf = %d\n", qctx->port_id, qctx->channel_id, qctx->flags->pf, qctx->flags->vf);

		if (ifc_mcdma_queue_reset(port_id, qid, dir))
			printf("%s: Error while reseting queue\n",__func__);
		ifc_mcdma_print_stats(port_id, qid, dir);

	} else {
		qctx->unknown_err++;
	}

        return 0;
}

int msix_init(struct queue_context *tctx)
{
	int ret;
	int size = (4 + (QDEPTH) * sizeof(struct rte_mbuf *));

	tctx->poll_ctx = ifc_mcdma_poll_init(tctx->port_id);
	if (tctx->poll_ctx == NULL)
		return PERFQ_CMPLTN_NTFN_FAILURE;

	tctx->dma_buffer = rte_zmalloc_socket("tx_buffer",
				size, 0,
				rte_eth_dev_socket_id(tctx->port_id));
	if (tctx->dma_buffer == NULL)
		return PERFQ_CMPLTN_NTFN_FAILURE;

	tctx->dma_buffer->size = tctx->flags->batch_size;

	ret = ifc_mcdma_poll_add(tctx->port_id, tctx->channel_id, tctx->direction,
				tctx->poll_ctx);
	if (ret < 0){
		printf("%s%u Failed to add polling ctx\n",__func__,__LINE__);
		return PERFQ_CMPLTN_NTFN_FAILURE;
	}

	ret = ifc_mcdma_usmix_poll_add(tctx->port_id, tctx->channel_id,
				      tctx->direction, tctx->poll_ctx);
	if (ret < 0){
		printf("%s%u Failed to add polling ctx for user msix intr \n",
			__func__,__LINE__);
		return PERFQ_CMPLTN_NTFN_FAILURE;
	}

	return PERFQ_SUCCESS;
}

int non_msi_init(struct queue_context *tctx)
{
	int size = (4 + (QDEPTH) * sizeof(struct rte_mbuf *));

	tctx->dma_buffer = rte_zmalloc_socket("tx_buffer",
				size, 0,
				rte_eth_dev_socket_id(tctx->port_id));
	if (tctx->dma_buffer == NULL)
		return PERFQ_CMPLTN_NTFN_FAILURE;

	tctx->dma_buffer->size = tctx->flags->batch_size;

	return PERFQ_SUCCESS;
}

#ifndef IFC_QDMA_ST_MULTI_PORT
#ifdef IFC_QDMA_ST_PACKETGEN
uint64_t pio_queue_config_offset(struct queue_context *tctx)
{
#ifndef IFC_QDMA_DYN_CHAN
	uint64_t pf_chnl_offset;
	uint64_t vf_chnl_offset;
#endif
	uint64_t offset;

#ifdef IFC_QDMA_DYN_CHAN
	offset = mcdma_ph_chno[tctx->channel_id];
	return (8 * offset);
#else
	if (tctx->flags->vf == 0) {
		pf_chnl_offset = (tctx->flags->pf - 1) * IFC_QDMA_PER_PF_CHNLS;
		vf_chnl_offset = 0;
	} else {
		pf_chnl_offset = IFC_QDMA_PFS * IFC_QDMA_PER_PF_CHNLS;
		vf_chnl_offset = (tctx->flags->pf - 1) *
			(IFC_QDMA_PER_PF_VFS * IFC_QDMA_PER_VF_CHNLS);
		vf_chnl_offset +=
			(tctx->flags->vf - 1) * IFC_QDMA_PER_VF_CHNLS;
	}
	offset = 8 * (pf_chnl_offset + vf_chnl_offset);
#endif
#ifdef IFC_MCDMA_SINGLE_FUNC
	return offset + (8 * tctx->channel_id);
#else
	return offset + (8 * tctx->phy_channel_id);
#endif
}

#ifdef IFC_ED_CONFIG_TID_UPDATE
int pio_update_files(uint16_t portid, struct queue_context *tctx,
			    uint32_t fcount)
{
	uint64_t base;
	uint64_t offset;
	uint64_t ed_config = 0ULL;
	unsigned long file_size;
	uint64_t files = 0ULL;

	file_size = get_file_size(tctx);
	offset = pio_queue_config_offset(tctx);

	if (tctx->direction == REQUEST_TRANSMIT) {
		base = PIO_REG_PORT_PKT_CONFIG_H2D_BASE;
		ed_config = tctx->flags->packet_size * file_size;
	}
	if (tctx->direction == REQUEST_RECEIVE) {
		base = PIO_REG_PORT_PKT_CONFIG_D2H_BASE;
		ed_config = tctx->flags->packet_size * file_size;
		if (tctx->flags->flimit == REQUEST_BY_SIZE)
			files = tctx->flags->rx_packets / file_size;
		else
			files = fcount;

		/* Build ED Configuration */
		ed_config |= (files << PIO_REG_PORT_PKT_CONFIG_FILES_SHIFT);
		ed_config |= (1UL << PIO_REG_PORT_PKT_CONFIG_ENABLE_SHIFT);
		ed_config |= (1UL << PIO_REG_PORT_PKT_CONFIG_IDLE_CYCLES_SHIFT);
	}
	ifc_mcdma_pio_write64(portid, base + offset, ed_config);
	return 0;
}
#else

static int pio_init(uint16_t portid, struct queue_context *tctx,
			uint32_t pkt_gen_file_size __attribute__((unused)))
{
	uint64_t base = 0ULL;
	uint64_t offset = 0ULL;
	uint64_t ed_config = 0ULL;
	unsigned long file_size = 0ULL;
	uint64_t files = 0ULL;

	file_size = get_file_size(tctx);
	offset = pio_queue_config_offset(tctx);

	if (tctx->direction == REQUEST_TRANSMIT) {
		base = PIO_REG_PORT_PKT_CONFIG_H2D_BASE;
		ed_config = tctx->flags->packet_size * file_size;
	}
	if (tctx->direction == REQUEST_RECEIVE) {
		base = PIO_REG_PORT_PKT_CONFIG_D2H_BASE;
		ed_config = tctx->flags->packet_size * file_size;
#if 0//def IFC_MCDMA_RANDOMIZATION
		ed_config = pkt_gen_file_size;
#endif
		if (tctx->flags->flimit == REQUEST_BY_SIZE)
			files = tctx->flags->rx_packets / file_size;
		else
			files = tctx->flags->pkt_gen_files;

		/* Build ED Configuration */
		ed_config |= (files << PIO_REG_PORT_PKT_CONFIG_FILES_SHIFT);
		ed_config |= (1UL << PIO_REG_PORT_PKT_CONFIG_ENABLE_SHIFT);
#ifdef ED_VER0
		ed_config |= (1UL << PIO_REG_PORT_PKT_CONFIG_IDLE_CYCLES_SHIFT);
#endif
	}
	ifc_mcdma_pio_write64(portid, base + offset, ed_config);
	return 0;
}
#endif //ED_CONFIG_AT_TID_UPDATE
#endif
#endif

void set_avst_pattern_data(struct queue_context *tctx)
{
/*
 * Function: 16 bit, vfnum, vf active, pfnum
 * Channel : 11 Bit
 * data :Remaining.
 * switch off : old test case (Increamental DW  data pattern),
 * switch on  : programmable DW data pattern
 * default switch state  is off (0);
 * */
        int channel_index_offset = 0;
        uint32_t data = AVST_PATTERN_DATA;
        channel_index_offset = tctx->channel_id*PATTERN_CHANNEL_MULTIPLIER;
        tctx->data_tag = tctx->flags->portid << PORT_TOTAL_WIDTH | tctx->channel_id << CHANNEL_TOTAL_WIDTH | data;
        ifc_mcdma_pio_write64(tctx->flags->portid, PATTERN_CHANNEL_BASE + channel_index_offset, tctx->data_tag);
        tctx->expected_pattern = tctx->data_tag | data;
}

enum perfq_status queue_init(struct queue_context *tctx)
{
	signal_mask();
#ifdef IFC_QDMA_INTF_ST
	struct struct_flags *flags = tctx->flags;
#endif
	int ret;

	/* Initiailize completion notificate policy*/
	if (config_mcdma_cmpl_proc == CONFIG_QDMA_QUEUE_MSIX)
		ret = msix_init(tctx);
	else
		ret = non_msi_init(tctx);

	if (ret) {
		printf("Channel%d: Direction%d: Channel Initializaiton failed "
			"ret:%d\n", tctx->channel_id, tctx->direction, ret);
		return PERFQ_CMPLTN_NTFN_FAILURE;
	}

	/* allocate memory for completion_buf */
	tctx->completion_buf = rte_zmalloc("Comp_buf", QDEPTH *
				   sizeof(struct rte_mbuf *), 0);
	if (!(tctx->completion_buf)) {
		printf("Channel%d: Direction%d: Failed to allocate memory for "
			"completion buffer\n",
			tctx->channel_id, tctx->direction);
		return PERFQ_MALLOC_FAILURE;
	}
#ifdef IFC_QDMA_INTF_ST
	/* init AVST specific things */
	/* Talk to the Hardware if required */
	if ((flags->direction != REQUEST_LOOPBACK) && (tctx->direction ==
	    REQUEST_RECEIVE))
		hw_eof_handler(tctx);
#endif
	tctx->extra_bytes = 0;
#ifdef CID_PAT
	/* 0-16 => Data, 16-24 -> cid, 24-31 => portid */
#ifdef IFC_QDMA_DYN_CHAN
#ifdef IFC_QDMA_ST_PACKETGEN
#ifdef IFC_PROG_DATA_EN
	set_avst_pattern_data(tctx);
#else
	/*  Correction needed for static channel allocation */
	tctx->data_tag = mcdma_ph_chno[tctx->channel_id];
	tctx->data_tag |= (tctx->data_tag << 8);
	tctx->data_tag |= (tctx->data_tag << 16);
#endif
#else
	tctx->data_tag = (tctx->channel_id | (tctx->flags->portid << 8));
#endif
#endif

#ifdef IFC_PROG_DATA_EN
	set_avst_pattern_data(tctx);
#else
	tctx->expected_pattern = (0x00000000 | tctx->data_tag << 16);
#endif
#endif

/*Applied to non-static channel configuration*/
#ifdef CID_PFVF_PAT
        /* 0-16 => Data, 16-24 -> cid, 24-29 => portid , 30-31 => PF or VF*/
#ifdef IFC_QDMA_ST_PACKETGEN
        /*  Correction needed for static channel allocation */
        tctx->data_tag = tctx->channel_id;
        tctx->data_tag |= (tctx->data_tag << 8);
        tctx->data_tag |= (tctx->data_tag << 16);
#else//AVST loop back
        /* 0-15 => Data, 16-23 -> cid, 24-29 => portid , 30=> PF channel, 31=>VF channel*/
        tctx->data_tag = (tctx->channel_id | (tctx->flags->portid << 8)| (tctx->flags->is_pf << 14) |(tctx->flags->is_vf << 15));
#endif
#endif
   //     tctx->expected_pattern = (0x00000000 | tctx->data_tag << 16);

	return PERFQ_SUCCESS;
}

#pragma GCC push_options
#pragma GCC optimize("O0")
uint32_t load_data(void *buf, size_t size, uint32_t pattern, struct queue_context *qctx __attribute__((unused)) )
{
	unsigned int i, j;
	uint32_t data;

	for (i = 0; i < (size / sizeof(uint32_t)); i++) {
#ifndef IFC_MCDMA_RANDOMIZATION
		// normal case
		data = pattern++;
#ifdef CID_PAT
		data = (data | (qctx->data_tag << 16));
#endif
#ifdef CID_PFVF_PAT 
		data = (data | (qctx->data_tag << 16));
#endif
#else
		/* Randomization */
		data = qctx->data_tag;
#endif
		*(((uint32_t *)buf) + i) = data;
	}

	for (j = 0; j < (size % sizeof(uint32_t)); j++) {
		data = 0xff;
		*(((uint8_t *)buf) + (i * 4) + j) = data;
	}

	return pattern;
}
#pragma GCC pop_options

int append_to_file(char *file_name, char *append_data)
{
	FILE *f;
	int ret;

	f = ifc_mcdma_fopen(file_name, "a+");
	if (f == NULL)
		return -1;
	ret = fputs(append_data, f);
	fclose(f);
	if (ret)
		return -1;
	return 0;
}

enum perfq_status tag_checker(void *buf, size_t size,
				   uint32_t expected_pattern __attribute__((unused)),
				   uint32_t *extra_bytes __attribute__((unused)),
				   struct queue_context *qctx __attribute__((unused)) )

{
	uint32_t actual_pattern;
	int i;
#if defined(DUMP_DATA) || defined(DUMP_FAIL_DATA)
	char data_to_write[128];
#endif
#ifdef DUMP_DATA
	char file_name[128] = DUMP_FILE;
#endif
	for (i = 0; i < (int)(size / sizeof(uint32_t)); i++) {
		actual_pattern = ((uint32_t *)buf)[i];
		rte_smp_wmb();
		if (actual_pattern != qctx->data_tag) {
#ifdef DUMP_FAIL_DATA
			snprintf(data_to_write, sizeof(data_to_write),
					"TEST%u: Pattern Validation Failed:"
					"tag mismatch act:0x%x datatag:0x%x i:%u\n",
					qctx->channel_id, actual_pattern, qctx->data_tag, i);
			append_to_file(global_flags->dump_file_name, data_to_write);
#endif
			return PERFQ_DATA_VALDN_FAILURE;
		} else
			continue;
	}
	return PERFQ_SUCCESS;
}


enum perfq_status pattern_checker(void *buf, size_t size,
				   uint32_t expected_pattern, uint32_t *extra_bytes,
				   struct queue_context *qctx __attribute__((unused)) )
{
	uint32_t actual_pattern;
	int i;
	//unsigned int j;
	uint32_t num = 0;
#ifdef CID_PAT
#ifdef DUMP_FAIL_DATA
	uint32_t portid;
	int cid;
#endif
//	uint32_t magic_word = (0xff | (qctx->data_tag << 16));
#else
//	uint32_t magic_word = 0xff;
#endif
	uint32_t magic_word = 0xff;
#ifdef CID_PFVF_PAT
#ifdef DUMP_FAIL_DATA
	uint32_t portid;
	int cid;
        int pf_chnl, vf_chnl;
#endif
#endif

#if defined(DUMP_DATA) || defined(DUMP_FAIL_DATA)
	char data_to_write[128];
#endif
#ifdef DUMP_DATA
	char file_name[128] = DUMP_FILE;
#endif
	for (i = 0; i < (int)(size / sizeof(uint32_t)); i++) {
		actual_pattern = ((uint32_t *)buf)[i];
#ifdef DUMP_DATA
		memset(data_to_write, 0, 50);
		snprintf(data_to_write, sizeof(data_to_write), "0x%x,0x%x\n",
			 actual_pattern, expected_pattern);
		append_to_file(file_name, data_to_write);
#endif
		perfq_smp_barrier();
		if (actual_pattern != expected_pattern) {
			/* skip special training data */
			num = 0;
			uint32_t off = i * 4;
			size -= off;
			while ((((uint8_t *)buf)[off] == magic_word) && (num < 3)) {
				off++;
				size--;
				num++;
				(*extra_bytes)++;
			}
			if (num ) {
				i = -1;
				buf = ((uint8_t*)buf) + off;
				continue;
			}
			if (size <= 8192) {
				usleep(1);
				actual_pattern = ((uint32_t *)buf)[i];
				if (actual_pattern == expected_pattern) {
					expected_pattern++;
					continue;
				}
			}
#ifdef DUMP_FAIL_DATA
#ifdef CID_PAT
			memset(data_to_write, 0, 50);
			if ((qctx->data_tag) != (actual_pattern >> 16)) {
				portid = (actual_pattern >> 24);
				cid = ((actual_pattern & 0x00FFFFFF) >> 16);
				if (qctx->channel_id != cid) {
					snprintf(data_to_write, sizeof(data_to_write),
						"TEST%u: Pattern Validation Failed: cid mismatch  act:0x%x exp:0x%x tag:0x%x cid:0x%x port:0x%x\n",
						qctx->channel_id, actual_pattern, expected_pattern, actual_pattern>>16, cid, portid);
				} else {
					snprintf(data_to_write, sizeof(data_to_write),
						"TEST%u: Pattern Validation Failed: port mismatch  act:0x%x exp:0x%x tag:0x%x cid:0x%x port:0x%x\n",
						qctx->channel_id, actual_pattern, expected_pattern, actual_pattern>>16, cid, portid);
				}
			} else   {
				snprintf(data_to_write, sizeof(data_to_write),
						"TEST%u: Pattern Validation Failed: data mismatch act:0x%x exp:0x%x\n",
						qctx->channel_id, actual_pattern, expected_pattern);
			}

//For static channel configuration
#elif defined(CID_PFVF_PAT)
             memset(data_to_write, 0, 50);
             if ((qctx->data_tag) != (actual_pattern >> 16)) {
                   portid = (actual_pattern >> 24);
                   pf_chnl     = ((portid >> 6) & 0x1U);
                   vf_chnl     = (portid >> 7);
                   cid = ((actual_pattern & 0x00FFFFFF) >> 16);
	           if (qctx->channel_id != cid) {
                          snprintf(data_to_write, sizeof(data_to_write),
                           "TEST%u: Pattern Validation Failed: cid mismatch  act:0x%x exp:0x%x tag:0x%x cid:0x%x port:0x%x\n",
                                                qctx->channel_id, actual_pattern, expected_pattern, actual_pattern>>16, cid, portid);
                    }
                    else if(qctx->is_pf == pf_chnl)
                    {
                          snprintf(data_to_write, sizeof(data_to_write),
                          "TEST%u: Pattern Validation Failed: PF chnl type mismatch  act:0x%x exp:0x%x tag:0x%x cid:0x%x pf_chnl:0x%x\n",
                           qctx->channel_id, actual_pattern, expected_pattern, actual_pattern>>16, cid, pf_chnl);
                     }
                     else if(qctx->is_vf == vf_chnl)
                     {
                           snprintf(data_to_write, sizeof(data_to_write),
                            "TEST%u: Pattern Validation Failed: VF chnl type mismatch  act:0x%x exp:0x%x tag:0x%x cid:0x%x vf_chnl:0x%x\n",
                           qctx->channel_id, actual_pattern, expected_pattern, actual_pattern>>16, cid, vf_chnl);
                     }
                     else {
                           snprintf(data_to_write, sizeof(data_to_write),
                           "TEST%u: Pattern Validation Failed: port mismatch  act:0x%x exp:0x%x tag:0x%x cid:0x%x port:0x%x\n",
                            qctx->channel_id, actual_pattern, expected_pattern, actual_pattern>>16, cid, portid);
                     }
                 } else{
                           snprintf(data_to_write, sizeof(data_to_write),
                            "TEST%u: Pattern Validation Failed: data mismatch act:0x%x exp:0x%x\n",
                             qctx->channel_id, actual_pattern, expected_pattern);
                 }
#else
		snprintf(data_to_write, sizeof(data_to_write),
					"TEST%u: Pattern Validation Failed: data mismatch act:0x%x exp:0x%x\n",
					qctx->channel_id, actual_pattern, expected_pattern);
#endif
			append_to_file(global_flags->dump_file_name, data_to_write);
#endif
			return PERFQ_DATA_VALDN_FAILURE;
		}
		perfq_smp_barrier();
#ifndef IFC_PROG_DATA_EN 
		expected_pattern++;
#endif
	}
#if 0
	for (j = 0; j < (size % sizeof(uint32_t)); j++) {
		if (((uint8_t *)buf)[(i*4) + j] != 0xff) {
#ifdef DUMP_DATA
			memset(data_to_write, 0, 50);
			snprintf(data_to_write, sizeof(data_to_write),
				 "Pattern Validation Failed for Packet act:0x%x exp:0x%x\n",
				 actual_pattern, expected_pattern);
			append_to_file(global_flags->dump_file_name, data_to_write);
#endif
			return PERFQ_DATA_VALDN_FAILURE;
		}
	}
#endif
#ifdef DUMP_DATA
	memset(data_to_write, 0, 50);
	snprintf(data_to_write, sizeof(data_to_write),
		 "Pattern Validation Successful for Packet\n");
	append_to_file(file_name, data_to_write);
#endif
/*Due to some latency issue at the device side ,Application faces some RACE condition in data validity mode.
Adding delay before next submition of descriptors to recive the data resolve the issue .  */
#ifndef PERFQ_PERF
usleep(1);
#endif

	return PERFQ_SUCCESS;
}

inline int should_thread_stop(struct queue_context *tctx)
{
	struct timespec cur_time, diff;
	long timediff_sec, timediff_msec;
	int timeout_sec = tctx->flags->time_limit;
	int epoch_done = false;
	uint32_t packets;

	if (tctx->direction == 0)
		packets = tctx->flags->rx_packets;
	else
		packets = tctx->flags->packets;

	if (force_exit == true)
		return true;

	if (tctx->direction == 0)
		timeout_sec += 1;

	if (tctx->fstart == false && tctx->flags->flimit == REQUEST_BY_TIME)
		return false;

	/* does the main thread want me to stop? */
	if (tctx->status == THREAD_STOP)
		return true;

	clock_gettime(CLOCK_MONOTONIC, &cur_time);

	/* is the transfer by time or request */
	if (tctx->flags->flimit == REQUEST_BY_TIME) {
		timediff_sec = difftime(cur_time.tv_sec,
				(tctx->start_time).tv_sec);
#ifdef IFC_MCDMA_RANDOMIZATION
		if (timediff_sec > tctx->prev_timediff_sec) {
			pkt_gen_config(tctx);
			tctx->prev_timediff_sec = timediff_sec;
		}
#endif	
		if (timediff_sec >= timeout_sec) {
			clock_gettime(CLOCK_MONOTONIC, &(tctx->end_time));
			tctx->epoch_done = 0;
			return true;
		}
	} else {
		if (packets <= tctx->request_counter +
		    tctx->prep_counter) {
			clock_gettime(CLOCK_MONOTONIC, &(tctx->end_time));
			tctx->epoch_done = 0;
			return true;
		}
	}

	/* Logic for epoch done detection */

	/* For -s flag, we don't consider TDM, we only consider tid updates
	 * For data validation in case of loopback strict ordering will be
	 * maintained */
	if (tctx->flags->flimit == REQUEST_BY_SIZE ||
	    (tctx->flags->direction == REQUEST_LOOPBACK &&
	    tctx->flags->fvalidation == true)) {
		/* For non-batch mode, 64 tid updates must be done
		 * by a queue in one iteration, in batch mode, one batch update
		 * should be done */
		if (tctx->tid_count > 0 || tctx->nonb_tid_count >= 64U)
			epoch_done = true;

	/* For -l flag, we also consider TDM */
	} else {
		time_diff(&cur_time, &(tctx->start_time_epoch), &diff);
		timediff_msec = (diff.tv_sec * 1000) +
			((double)diff.tv_nsec / 1e6);
		if (timediff_msec >= THR_EXEC_TIME_MSEC ||
		    tctx->tid_count > 0 || tctx->nonb_tid_count >= 64U)
			epoch_done = true;
	}

	if (tctx->flags->flimit == REQUEST_BY_SIZE) {
		/* if continuously hang for 20 intervals, exit from thread */
		if (tctx->stuck_count == 20)
			return true;
	}

	if (epoch_done) {
		tctx->epoch_done = 1;
		return true;
	}

	return false;
}

/* req_counter starts from 0 */
enum perfq_status set_sof_eof(struct struct_flags *flags, struct rte_mbuf *req,
			      unsigned long req_counter)
{
	unsigned long file_size = flags->file_size;
	int ret = PERFQ_SUCCESS;
#if 0
	uint32_t last = QDEPTH - (QDEPTH % file_size);

	/* Last few descriptors, which do not fit to complete file
         * set file size as 1 */
	if ((req_counter >= last) && (req_counter < QDEPTH)) {
		file_size = 1;
		fprintf(stderr, "TEST: setting req:%lu setting file size 1\n", req_counter);
	}
	fprintf(stderr, "TEST: req:%lu setting file size %lu\n", req_counter, file_size);
#endif


	req->dynfield1[2] = 0;
	/* first packet of a file */
	if ((req_counter % file_size) == 0) {
		req->dynfield1[2] |= IFC_QDMA_SOF_MASK;
		ret = PERFQ_SOF;
	} else {
		req->dynfield1[2] &= ~IFC_QDMA_SOF_MASK;
	}

	/* last packet of a file */
	if ((req_counter % file_size) == file_size - 1) {
		req->dynfield1[2] |= IFC_QDMA_EOF_MASK;
		/* Does it also contain SOF */
		ret = ret ? PERFQ_BOTH : PERFQ_EOF;
	} else {
		req->dynfield1[2] &= ~IFC_QDMA_EOF_MASK;
	}
	return ret;
}

/* request_number starts from 1 */
/* TODO: move to request_number starting from 0 */
enum perfq_status check_sof_eof(struct queue_context *tctx,
				struct rte_mbuf *req,
				unsigned long request_number)
{
	unsigned long file_size = get_file_size(tctx);
	//unsigned long payload = get_payload_size(tctx, request_number - 1);
	unsigned long payload = rte_pktmbuf_data_len(req);
	int ret = PERFQ_SUCCESS;
#ifdef DUMP_FAIL_DATA
	char data_to_write[128] = {0};
#endif
	if (tctx->cur_file_status == PERFQ_DATA_VALDN_FAILURE) {
		tctx->cur_file_status = PERFQ_FILE_VALDN_FAILURE;
		tctx->bfile_counter++;
	}

	/* if data is already invalidated */
	if (tctx->cur_file_status == PERFQ_FILE_VALDN_FAILURE) {
		/* If EOF is expected */
		if (!(request_number % file_size)) {
			ret = PERFQ_EOF;
			/* reset for the next file */
			tctx->cur_file_status = PERFQ_SUCCESS;
		}
		return ret;
	}

	if (file_size == 1) {
		if (check_payload(payload, req->data_len)) {
			tctx->bfile_counter++;
			return PERFQ_DATA_PAYLD_FAILURE;
		}
		if ((req->dynfield1[2] & IFC_QDMA_SOF_MASK) &&
		    (req->dynfield1[2] & IFC_QDMA_EOF_MASK)) {
			/* good file */
			tctx->gfile_counter++;
#ifdef VERIFY_FUNC
		printf("Channel%d: Direction%d: File Transfer Successful\n",
			tctx->channel_id, tctx->direction);
#endif
		} else {
			tctx->bfile_counter++;
#ifdef VERIFY_FUNC
		printf("Channel%d: Direction%d: File Transfer Unsuccessful\n",
			tctx->channel_id, tctx->direction);
#endif
		}
		/* reset status for the next file */
		tctx->cur_file_status = PERFQ_SUCCESS;
		return PERFQ_EOF;

	}

	/* Expected SOF & received SOF */
	if ((req->dynfield1[2] & IFC_QDMA_SOF_MASK) &&
	    ((request_number % file_size) == 1)) {
		tctx->cur_file_status = PERFQ_SOF;
#ifdef VERIFY_FUNC
		printf("Channel%d: Direction%d: File Transfer Started\n",
			tctx->channel_id, tctx->direction);
#endif
	}

	/* Didn't expect SOF but received SOF */
	else if ((req->dynfield1[2] & IFC_QDMA_SOF_MASK) && !((request_number %
		 file_size) == 1)) {
		tctx->cur_file_status = PERFQ_FILE_VALDN_FAILURE;

		/* bad file */
		tctx->bfile_counter++;
#ifdef VERIFY_FUNC
		printf("Channel%d: Direction%d: Unexpected SOF received --> "
			"File data invalidated\n",
			tctx->channel_id, tctx->direction);
#endif
	}

	/* Expected SOF but didn't received SOF*/
	else if (!(req->dynfield1[2] & IFC_QDMA_SOF_MASK) && ((request_number %
		 file_size) == 1)) {
		tctx->cur_file_status = PERFQ_FILE_VALDN_FAILURE;

		/* bad file */
		tctx->bfile_counter++;
#ifdef VERIFY_FUNC
		printf("Channel%d: Direction%d: Failed to received SOF --> "
			"File data invalidated\n",
			tctx->channel_id, tctx->direction);
#endif
	}

	/* Expected EOF & received EOF */
	if ((req->dynfield1[2] & IFC_QDMA_EOF_MASK) &&
	    !(request_number % file_size)) {

		/* Have we seen SOF? */
		if (tctx->cur_file_status != PERFQ_SOF) {
			tctx->cur_file_status = PERFQ_FILE_VALDN_FAILURE;
			tctx->bfile_counter++;
#ifdef VERIFY_FUNC
			printf("Channel%d: Direction%d: Unexpected EOF received"
				" --> File data invalidated\n",
				tctx->channel_id, tctx->direction);
#endif
		} else {
			/* check for payload */
			if (check_payload(payload, req->data_len)) {
#ifdef VERIFY_FUNC
				printf("Channel%d: Direction%d: EOF expected & "
					"received but payload check failed --> "
					"File data invalidated\n",
					tctx->channel_id, tctx->direction);
#endif
				tctx->bfile_counter++;
				/* reset status for the next file */
				tctx->cur_file_status = PERFQ_SUCCESS;
				return PERFQ_DATA_PAYLD_FAILURE;
			}
			/* good file */
			tctx->gfile_counter++;
#ifdef VERIFY_FUNC
			printf("Channel%d: Direction%d: File Transfer Completed"
				"\n", tctx->channel_id, tctx->direction);
#endif
		}

		/* reset status for the next file */
		tctx->cur_file_status = PERFQ_SUCCESS;
		ret = PERFQ_EOF;
	}

	/* Didn't expect EOF but received EOF */
	else if ((req->dynfield1[2] & IFC_QDMA_EOF_MASK) && (request_number %
		 file_size)) {
		tctx->cur_file_status = PERFQ_FILE_VALDN_FAILURE;
		tctx->bfile_counter++;
#ifdef VERIFY_FUNC
		printf("Channel%d: Direction%d: Unexpected EOF received --> "
			"File data invalidated\n",
			tctx->channel_id, tctx->direction);
#endif
	}

	/* Expected EOF but didn't received EOF */
	else if (!(req->dynfield1[2] & IFC_QDMA_EOF_MASK) && !(request_number %
		 file_size)) {
		tctx->cur_file_status = PERFQ_FILE_VALDN_FAILURE;
		tctx->bfile_counter++;

		/* reset for the next file */
		tctx->cur_file_status = PERFQ_SUCCESS;
		ret = PERFQ_EOF;
#ifdef VERIFY_FUNC
		printf("Channel%d: Direction%d: Failed to received EOF --> "
			"File data invalidated\n",
			tctx->channel_id, tctx->direction);
#endif
	}

#ifdef DUMP_FAIL_DATA
	if(ret !=PERFQ_SUCCESS && tctx->bfile_counter != 0 )
	{
		snprintf(data_to_write, sizeof(data_to_write), "Bad File %lu, Channel_no %d\n", request_number, tctx->channel_id);
		append_to_file(global_flags->dump_file_name, data_to_write);
	}
#endif
	return ret;
}

int hw_eof_handler(struct queue_context *tctx)
{
	/* For Rx: clear & set the PIO flag */
	pio_bit_writer(tctx->port_id, 0x00, tctx->channel_id, false);
	pio_bit_writer(tctx->port_id, 0x00, tctx->channel_id, true);
	return 0;
}

enum perfq_status post_processing(struct queue_context *tctx,
				  struct rte_mbuf **buf)
{
	uint32_t bkp_ex_pttrn;
	int ret = PERFQ_SUCCESS;
	unsigned long i;
#ifdef IFC_QDMA_INTF_ST
	unsigned long req_number;
#endif
	unsigned long payload;
	uint32_t extra_bytes = 0;
#ifdef DUMP_FAIL_DATA
	//char dump_data[512] = {0};
#endif
#ifdef IFC_QDMA_INTF_ST
#ifdef IFC_QDMA_META_DATA
	uint32_t bkp_mdata_ex_pttrn;
#endif
#endif

	for (i = 0; i < tctx->cur_comp_counter; i++) {
#ifdef IFC_QDMA_INTF_ST
		req_number = (tctx->completion_counter - tctx->cur_comp_counter)
				+ 1 + i;
#endif
		payload = rte_pktmbuf_data_len(buf[i]);

		bkp_ex_pttrn = tctx->expected_pattern;
#ifdef IFC_QDMA_INTF_ST
#ifdef IFC_QDMA_META_DATA
		if (IFC_QDMA_SOF_MASK & buf[i]->dynfield1[2]) {

			bkp_mdata_ex_pttrn = tctx->mdata_expected_pattern;
			/*Verify the metat data*/
			tctx->extra_bytes = 0;
			ret = pattern_checker(&buf[i]->dynfield1[0],
				METADATA_SIZE, tctx->mdata_expected_pattern, &(tctx->extra_bytes), tctx);
			if (ret == PERFQ_DATA_VALDN_FAILURE) {
				/* Update the bad metapacket counter*/
				tctx->mdata_bpckt_counter++;
#ifdef VERIFY_FUNC
				printf("Channel%d: Direction%d: metadata Packet Data "
					"Validation Failed\n", tctx->channel_id,
					tctx->direction);
#endif
			} else {
				/* Update the good metadata packet counter*/
				tctx->mdata_gpckt_counter++;
			}

			tctx->mdata_expected_pattern = bkp_mdata_ex_pttrn +
				(METADATA_SIZE / sizeof(uint32_t));
		}
#endif // IFC_QDMA_META_DATA
		perfq_smp_barrier();
		/* consider the previous extra bytes */
		extra_bytes = tctx->extra_bytes;
		tctx->extra_bytes = 0;
#ifndef IFC_MCDMA_RANDOMIZATION
		/* Raw data validation */
		ret = pattern_checker( (char * )buf[i]->buf_addr + extra_bytes,
				      payload - extra_bytes,
				      tctx->expected_pattern, &(tctx->extra_bytes), tctx);

#else
#ifdef IFC_QDMA_ST_LOOPBACK		
		ret = tag_checker( (char * )buf[i]->buf_addr + extra_bytes,
				      payload - extra_bytes,
				      tctx->expected_pattern, &(tctx->extra_bytes), tctx);
#else
		/* Raw data validation */
		ret = pattern_checker( (char * )buf[i]->buf_addr + extra_bytes,
				      payload - extra_bytes,
				      tctx->expected_pattern, &(tctx->extra_bytes), tctx);
#endif
#endif

#else // else block for AVMM
 
		perfq_smp_barrier();
		tctx->extra_bytes = 0;
		ret = pattern_checker(buf[i]->buf_addr, payload, 0x00, &(tctx->extra_bytes), tctx);
#endif
		if (ret == PERFQ_DATA_VALDN_FAILURE) {
			/* Update the bad packet counter*/
			tctx->bpckt_counter++;

			/* current file data is invalidated */
			if (tctx->cur_file_status != PERFQ_FILE_VALDN_FAILURE)
				tctx->cur_file_status =
						       PERFQ_DATA_VALDN_FAILURE;
#ifdef VERIFY_FUNC
			printf("Channel%d: Direction%d: Packet Data Validation "
				"Failed\n", tctx->channel_id, tctx->direction);
#endif
		} else {
			/* Update the good packet counter*/
			tctx->gpckt_counter++;
		}
		perfq_smp_barrier();
		tctx->expected_pattern = bkp_ex_pttrn +
					 ((payload - extra_bytes) / sizeof(uint32_t));
		//tctx->expected_pattern -= tctx->extra_bytes/4;

#ifdef IFC_QDMA_INTF_ST
		perfq_smp_barrier();
		ret = check_sof_eof(tctx, buf[i], req_number);
		if (ret == PERFQ_EOF || ret == PERFQ_DATA_PAYLD_FAILURE) {
			/* reset the expected pattern */
			if (ret == PERFQ_EOF)
				tctx->extra_bytes = 0;
#ifdef CID_PAT
#ifdef IFC_PROG_DATA_EN
			tctx->expected_pattern = tctx->data_tag | AVST_PATTERN_DATA;
#else
			tctx->expected_pattern = (0x00 | tctx->data_tag << 16);
#endif
#elif defined(CID_PFVF_PAT)
			tctx->expected_pattern = (0x00 | tctx->data_tag << 16);
#else
			tctx->expected_pattern = 0x00;
#endif
			tctx->mdata_expected_pattern = 0x00;
			perfq_smp_barrier();
		}
#endif
		tctx->vpckt_counter++;
	}
	return ret;
}

/* req_no starts from 0 */
unsigned long get_file_size(struct queue_context *qctx)
{
	if (qctx->direction == IFC_QDMA_DIRECTION_RX)
		return qctx->flags->rx_file_size;
	else
		return qctx->flags->file_size;
}

static unsigned long get_packet_size(struct queue_context *qctx, uint32_t req_no)
{
	uint32_t desc_no;
	uint32_t file_idx;
	if (qctx->direction == IFC_QDMA_DIRECTION_RX) {
		return qctx->flags->rx_packet_size;
	} else {
		file_idx = req_no % qctx->file_size;
		desc_no = file_idx % qctx->flags->num_pylds;
		return qctx->flags->desc_pyld[desc_no];
	}
}

/* req_no starts from 0 */
unsigned long get_payload_size(struct queue_context *tctx,
			       unsigned long req_no)
{
	if (tctx->flags->flimit == REQUEST_BY_TIME)
		return get_packet_size(tctx, req_no);

	if (tctx->flags->h2d_pyld_sum == tctx->flags->request_size)
		return get_packet_size(tctx, req_no);

	return RTE_MIN(get_packet_size(tctx, req_no), tctx->flags->request_size -
		   (get_packet_size(tctx, req_no) * req_no));
}

int ifc_mcdma_request_buffer_flush(struct queue_context *tctx)
{
	int len = 0;
	int ret = 0;
	ret = ifc_mcdma_request_submit(tctx, tctx->direction,
				       tctx->dma_buffer->pkts,
				       tctx->dma_buffer->length);

	if (ret < 0) {
		tctx->status = THREAD_WAITING;
		return -1;
	}
	len = tctx->dma_buffer->length;
	tctx->dma_buffer->length = 0;
	tctx->ovrall_tid++;

	return len;
}

int ifc_mcdma_request_buffer(struct queue_context *tctx, struct rte_mbuf *req)
{
	int length;
	int ret;

	/* There is buffer to transmit */
	tctx->dma_buffer->pkts[tctx->dma_buffer->length++] = req;
#if defined(TRACK_DF_HEAD) && defined(IFC_QDMA_INTF_ST)
	/* Do single TID update for entire QDEPTH incase of pre-filling */
	if (!tctx->init_done && (tctx->dma_buffer->length < QDEPTH))
		return 0;
	else if (tctx->dma_buffer->length < tctx->dma_buffer->size)
#else
	if (tctx->dma_buffer->length < tctx->dma_buffer->size)
#endif
		return 0;

	ret = ifc_mcdma_request_submit(tctx, tctx->direction,
				       tctx->dma_buffer->pkts,
				       tctx->dma_buffer->length);
	if (ret < 0) {
		tctx->status = THREAD_WAITING;
		return -1;
	}
	length = tctx->dma_buffer->length;
	tctx->dma_buffer->length = 0;
	tctx->tid_count++;
	tctx->ovrall_tid++;

	return length;
}

int ifc_mcdma_request_submit(struct queue_context *tctx, int dir,
			struct rte_mbuf **bufs, uint32_t nr)
{
#ifdef BUFFER_FORWARD_D2H_TO_H2D
	uint16_t j, cid;
	struct rte_mbuf *buf_ptrs[QDEPTH];
#else 
	uint16_t i, j, cid;
	struct rte_mbuf *buf_ptrs[QDEPTH];
#endif 

	if (bufs == NULL)
		return 0;

#ifdef IFC_ED_CONFIG_TID_UPDATE
	if ((tctx->desc_requested  + nr) >= tctx->file_size) {
		uint32_t num_files = (tctx->desc_requested  + nr) / tctx->file_size;
		pio_update_files(tctx->portid, tctx, num_files);
		tctx->files_requested++;
		tctx->desc_requested =  (tctx->desc_requested + nr) % tctx->file_size;
	} else {
		tctx->desc_requested += nr;
	}
#endif

	if (dir == IFC_QDMA_DIRECTION_RX) {
		j = rte_eth_rx_burst(tctx->port_id, tctx->channel_id, bufs, nr);
#if 0
		if(j == TID_ERROR) {
			printf("TIDE register set for channel %d\n", tctx->channel_id);
		}
		if (j == DF_HEAD_NOT_MOVED)
			return -1;
#endif

			tctx->completion_counter += j;
			tctx->cur_comp_counter = j;
			if ((tctx->cur_comp_counter > 0) && (tctx->flags->fvalidation))
				post_processing(tctx, bufs);

#ifndef BUFFER_FORWARD_D2H_TO_H2D
			for (i = 0; i < j; i++)
				rte_pktmbuf_free(bufs[i]);
#endif
	} else {
		memcpy(buf_ptrs, bufs, nr * sizeof(struct rte_mbuf *));
		j = rte_eth_tx_burst(tctx->port_id, tctx->channel_id, buf_ptrs, nr);
		if (j == DF_HEAD_NOT_MOVED)
			return -1;

		tctx->completion_counter += j;
		for (cid = 0; cid < j; cid++) {
			if (cid >= QDEPTH)
				break;
			((struct rte_mbuf **)(tctx->completion_buf))
				[tctx->cur_comp_counter + cid] = buf_ptrs[cid];
		}
		tctx->cur_comp_counter = j;
	}
	return j;
}

enum perfq_status enqueue_request(struct queue_context *tctx,
				  struct rte_mbuf **req_buf,
				  int nr, int do_submit __rte_unused,
				  int force, bool check_thread_status)
{
	struct rte_mbuf *req = NULL;
	int do_load_data;
	unsigned long payload;
	unsigned long req_number;
	int ret = 0;
	int i = 0;
#ifdef NO_ALIGN
	static int align_off;
#endif
#if (IFC_DATA_WIDTH == 1024)
	uint64_t *dma_buf;
#endif

	while (i < nr) {
		/* check stopping condition */
		if (check_thread_status && !force &&
		    should_thread_stop(tctx)) {
			if (tctx->epoch_done == 0) {
				/* time limit teached */
				tctx->status = THREAD_STOP;
			}

			if (tctx->flags->flimit == REQUEST_BY_SIZE)
				return PERFQ_SUCCESS;

			/* Return by packets limit reached. and not epoch */
			if ((tctx->flags->flimit == REQUEST_BY_SIZE) &&
				(tctx->epoch_done == 0))
				return PERFQ_SUCCESS;
		}

#ifdef IFC_MCDMA_RANDOMIZATION
		if(tctx->flags->frand & IFC_RAND_HIFUN_P) {
			//payload =  ((ifc_rand_get_next_entry(IFC_RAND_HIFUN_P, tctx)% (1024 - 64 + 1)) + 64) & 0xFFFFFFFC;
			payload =  ifc_rand_get_next_entry(IFC_RAND_HIFUN_P, tctx);
		}
		else {
			req_number = tctx->request_counter + tctx->prep_counter;
			payload = get_payload_size(tctx, req_number);
		}
#else
		req_number = tctx->request_counter + tctx->prep_counter;
		payload = get_payload_size(tctx, req_number);
#endif

#ifdef PERFQ_LOAD_DATA
		do_load_data = true;
#else
		do_load_data = false;
#endif
		if (tctx->direction == REQUEST_TRANSMIT) {
			if (!ret) {
				if (req_buf) {
					req = req_buf[i];
				} else {
					req = rte_pktmbuf_alloc(tctx->tx_mbuf_pool);
					if (!req) {
						tctx->failed_attempts++;
						continue;
					}
					do_load_data = true;
					/*Check the src address is aligned to 128 bytes*/
					#if (IFC_DATA_WIDTH == 1024)
						#ifndef DPDK_21_11_RC2
							dma_buf = &(req->buf_physaddr);
						#else
							dma_buf = &(req->buf_iova);
						#endif
						if (*dma_buf % 128 != 0){
							req->buf_addr = ((char *)req->buf_addr + (((*dma_buf + 127) & ~127)- *dma_buf )) ;
							*dma_buf = (*dma_buf + 127) & ~127;
						}
					#endif
#ifdef NO_ALIGN
					//fprintf(stderr, "TEST: before bufaddr: %lx off:%u\n", (long unsigned int)req->buf_addr, align_off);
					align_off = (align_off + PERFQ_ALIGN_OFF) % 8;
					req->buf_addr = ((char *)req->buf_addr)  + align_off;
					req->buf_physaddr = (req->buf_physaddr)  + align_off;
					//fprintf(stderr, "TEST: after bufaddr: %lx off:%u\n", (long unsigned int)req->buf_addr, align_off);
#endif
					rte_pktmbuf_data_len(req) = payload;
					rte_pktmbuf_pkt_len(req) = payload;
				}
			}


			/* Load data if not already done */
			if (do_load_data) {
#ifdef IFC_QDMA_INTF_ST
				tctx->cur_pattern = load_data(req->buf_addr,
					payload, tctx->cur_pattern, tctx);
#else
				load_data(req->buf_addr, payload, 0x00, tctx);
#endif
				tctx->vpckt_counter++;
			}

#ifdef IFC_QDMA_INTF_ST
			/* check if SOP or EOP is to be set */
			ret = set_sof_eof(tctx->flags, req,
				tctx->request_counter + tctx->prep_counter);
			/* have you seen and EOF then reset the cur_pattern */
			if (ret == PERFQ_EOF || ret == PERFQ_BOTH)
				tctx->cur_pattern = 0x00;
#ifdef IFC_QDMA_META_DATA
			if (do_load_data && (IFC_QDMA_SOF_MASK & req->dynfield1[2])) {
				tctx->mdata_cur_pattern = load_data(&req->dynfield1[0],
					METADATA_SIZE, tctx->mdata_cur_pattern, tctx);
			}
#endif//IFC_QDMA_META_DATA
			tctx->mdata_cur_pattern = 0x00;
#endif
		}


		ret = ifc_mcdma_request_buffer(tctx, req);
		if (ret < 0) {
			return -1;
		} else if (ret == 0) {
			tctx->prep_counter++;
			tctx->pr_count++;
		} else {
			tctx->prep_counter = 0;
			tctx->request_counter += ret;
		}
		i++;
	}

	return PERFQ_SUCCESS;
}

int batch_enqueue_wrapper(struct queue_context *tctx, struct rte_mbuf
			  **buf, unsigned long nr, bool check_thread_status)
{
	unsigned long next_push_size;
	unsigned long batch_size = tctx->flags->batch_size;
	int buf_offset = 0;
	int rc = 0;

	/* no submit required, one chunk */
	if (batch_size - tctx->prep_counter > nr) {
		rc = enqueue_request(tctx, buf, nr, false, NO_FORCE_ENQUEUE,
				check_thread_status);
		return 0;
	}

	while ((nr + tctx->prep_counter) >= batch_size) {
		/* commands with -s option, may exit from here */
		if (check_thread_status && should_thread_stop(tctx)) {
			if (tctx->epoch_done == 0)
				tctx->status = THREAD_STOP;

			tctx->tid_count = 0;
			tctx->nonb_tid_count = 0;
#if 0
		if (tctx->flags->flimit == REQUEST_BY_TIME)
			return -1;
#endif
			/* Return by packets limit reached. and not epoch */
			if ((tctx->flags->flimit == REQUEST_BY_SIZE) &&
				(tctx->epoch_done == 0))
				return -1;
		}
		next_push_size = batch_size - tctx->prep_counter;
		rc = enqueue_request(tctx, buf ? buf + buf_offset : buf,
				next_push_size, true, NO_FORCE_ENQUEUE,
				check_thread_status);
		if (rc < 0) {
			/* next_push_size + prep_cunter => in buffer,
			* which we submit next time */
			tctx->backlog = nr - tctx->dma_buffer->length;
			/* This batch might have submitted next time
			 * So, start processing from next batch */
		        buf_offset += next_push_size;
		        tctx->comp_buf_offset = buf_offset;
		        return rc;
		}
		nr -= next_push_size;
		buf_offset += next_push_size;
	}
	if (nr > 0)
		rc = enqueue_request(tctx, buf ? buf + buf_offset : buf, nr, false,
				NO_FORCE_ENQUEUE, check_thread_status);
	tctx->backlog = 0;
	return 0;
}

static enum perfq_status batch_load_request(struct queue_context *tctx,
		    __attribute__((unused)) struct rte_mbuf **req_buf,
		    int nr, __attribute__((unused)) int do_submit,
		    __attribute__((unused)) int force)
{
	int ret = 0, n = 0;
	for (n = 0; n < nr; n++) {
		if (tctx->flags->flimit == REQUEST_BY_SIZE) {
			/* commands with -s option, may exit from here */
			if (should_thread_stop(tctx)) {
				if (tctx->epoch_done == 0)
					tctx->status = THREAD_STOP;

				tctx->tid_count = 0;
				tctx->nonb_tid_count = 0;
				/* Return by packets limit reached. and not epoch */
				if ((tctx->flags->flimit == REQUEST_BY_SIZE) &&
					(tctx->epoch_done == 0))
					return -1;
			}
		}
		ret = ifc_mcdma_request_buffer(tctx, req_buf[n]);
		if (ret < 0) {
			return -1;
		} else if (ret == 0) {
			tctx->prep_counter++;
			tctx->pr_count++;
		} else {
			tctx->prep_counter = 0;
			tctx->request_counter += ret;
		}
	}
	return PERFQ_SUCCESS;
}

int batch_load_wrapper(struct queue_context *tctx, struct rte_mbuf
			  **buf, unsigned long nr)
{
	unsigned long next_push_size;
	unsigned long batch_size = global_flags->batch_size;
	int buf_offset = 0;
	int rc = 0;

	/* no submit required, one chunk */
	if (batch_size - tctx->prep_counter > nr) {
		rc = batch_load_request(tctx, buf, nr, false, NO_FORCE_ENQUEUE);
		return rc;
	}

	while ((nr + tctx->prep_counter) >= batch_size) {
		next_push_size = batch_size - tctx->prep_counter;
		rc = batch_load_request(tctx, buf ? buf + buf_offset : buf,
				next_push_size, true, NO_FORCE_ENQUEUE);
		if (rc < 0) {
			/* next_push_size + prep_cunter => in buffer,
			* which we submit next time */
			tctx->backlog = nr - tctx->dma_buffer->length;
			/* This batch might have submitted next time
			 * So, start processing from next batch */
		        buf_offset += next_push_size;
		        tctx->comp_buf_offset = buf_offset;
		        return rc;
		}
		nr -= next_push_size;
		buf_offset += next_push_size;
	}
	if (nr > 0) {
		rc = batch_load_request(tctx, buf ? buf + buf_offset : buf, nr,
				false, NO_FORCE_ENQUEUE);
	}
	tctx->backlog = 0;
	return rc;
}

static uint32_t check_for_completions(struct queue_context *qctx)
{
	uint32_t count = 0;
	uint32_t ret = 0;
	uint32_t i = 0;
	uint32_t pending = qctx->accumulator_index;
	struct rte_mbuf *accumulator[QDEPTH] = {0};

	while (qctx->request_counter != qctx->completion_counter) {
		if (should_thread_stop(qctx)) {
			if ((qctx->prep_counter)  &&
			    (qctx->flags->flimit == REQUEST_BY_SIZE) &&
			    (qctx->epoch_done == 0)) {
				ret = ifc_mcdma_request_buffer_flush(qctx);
				if (ret) {
					qctx->request_counter +=
						qctx->prep_counter;
					qctx->prep_counter = 0ULL;
					qctx->tid_count++;
				}
			}
			qctx->cur_comp_counter = pending + count;
			return 1;
		}

		/* In case of MSIX, we busy wait on the signal */
		request_completion_poll(qctx);
		count += qctx->cur_comp_counter;
		/* accumulate */
		for (i = 0; i < qctx->cur_comp_counter; i++)
			accumulator[qctx->accumulator_index++] =
				((struct rte_mbuf **)
				(qctx->completion_buf))[i];
	}
	qctx->cur_comp_counter = pending + count;
	/* copy them back */
	for (i = 0; i < qctx->accumulator_index; i++)
		((struct rte_mbuf **)
			(qctx->completion_buf))[i] =
				accumulator[i];

	/* reset accumulator index for next time */
	qctx->accumulator_index = 0;

	return 0;
}

#ifndef IFC_QDMA_ST_MULTI_PORT
#ifdef IFC_QDMA_ST_PACKETGEN
#ifndef IFC_ED_CONFIG_TID_UPDATE
static int checkpoint(struct queue_context *qctx)
{
	uint64_t base;
	uint64_t offset;
	uint64_t ed_config = 0ULL;

	/* Get the offset */
	base = PIO_REG_PORT_PKT_CONFIG_D2H_BASE;
	offset = pio_queue_config_offset(qctx);

	/* Disable the channel in ED logic */
	ed_config = ifc_mcdma_pio_read64(qctx->port_id, base + offset);
	ed_config &= ~(1UL << PIO_REG_PORT_PKT_CONFIG_ENABLE_SHIFT);
	ifc_mcdma_pio_write64(qctx->port_id, base + offset, ed_config);
	return 0;
}
#endif // IFC_EDCONFIG_TID_UPDATE
#endif
#endif

static int submit_pending_reqs(struct queue_context *qctx)
{
        int ret;

        /* Submit already prepared requests */
        if (qctx->prep_counter) {
		ret = ifc_mcdma_request_buffer_flush(qctx);
                if (ret < 0)
			return -1;
		qctx->request_counter += qctx->prep_counter;
		qctx->prep_counter = 0ULL;
		qctx->tid_count++;
        }

        return 0;
}

static void pkt_gen_config(struct queue_context *tctx __attribute__((unused)))
{
#ifndef IFC_QDMA_ST_MULTI_PORT
#ifdef IFC_QDMA_ST_PACKETGEN
#ifndef IFC_ED_CONFIG_TID_UPDATE
               uint16_t portno;
               rte_eth_dev_get_port_by_name(tctx->flags->bdf, &portno);

#if 0//def IFC_MCDMA_RANDOMIZATION
		if ((tctx->channel_id % 2) == 0)
			per_channel_ed_config[tctx->channel_id] =  ifc_mcdma_get_random(1024, 4096);
		else
			per_channel_ed_config[tctx->channel_id] =  ifc_mcdma_get_random(0, 512);
#endif

               /* Initialize ED */
		pio_init(portno, tctx, per_channel_ed_config[tctx->channel_id]);
#endif
#endif
#endif
        return;
}

void *transfer_handler_loopback(void *ptr)
{
	struct queue_context *tctx = (struct queue_context *)ptr;
	int ret;
	sem_t *rx_lock = &((tctx->flags->loop_locks[tctx->channel_id]).rx_lock);
	sem_t *tx_lock = &((tctx->flags->loop_locks[tctx->channel_id]).tx_lock);
	sem_t *prefill_lock;

	if (tctx->init_done == 0) {

		/* We prefill the requests in case of transfer by time case */
		if (tctx->flags->flimit == REQUEST_BY_TIME) {
			prefill_lock = tctx->direction == REQUEST_RECEIVE ?
					rx_lock : tx_lock;
			if (sem_trywait(prefill_lock) != 0) {
				tctx->epoch_done = true;
				return NULL;
			}
			/* Initilalize things */
			ret = queue_init(tctx);
			if (ret != PERFQ_SUCCESS) {
				goto last;
			}
			if (tctx->batch_size) {
				if (!tctx->direction)
					usleep(10000);
				batch_enqueue_wrapper(tctx, NULL,
						PREFILL_QDEPTH, true);
			} else
				enqueue_request(tctx, NULL,
						PREFILL_QDEPTH, true,
						FORCE_ENQUEUE, true);
			prefill_lock = tctx->direction == REQUEST_RECEIVE ?
					tx_lock : rx_lock;
			sem_post(prefill_lock);
		} else {
			/* Initilalize things */
			ret = queue_init(tctx);
			if (ret != PERFQ_SUCCESS) {
				goto last;
			}
			tctx->cur_comp_counter = PREFILL_QDEPTH;
		}
		tctx->init_done = 1;
		tctx->prefill_done = true;
		tctx->status = THREAD_READY;
		return NULL;
	} else {
		/* wait for all queues to get ready */
		if (!tctx->flags->ready) {
			tctx->epoch_done = true;
			return NULL;
		}

		/* let's start the clock */
		if (tctx->fstart == 0) {
			clock_gettime(CLOCK_MONOTONIC, &(tctx->start_time));
			tctx->fstart = true;
		}

		/* let's start epoch */
		clock_gettime(CLOCK_MONOTONIC, &(tctx->start_time_epoch));
		tctx->tid_count = 0;
		tctx->nonb_tid_count = 0;
	}

	tctx->status = THREAD_RUNNING;

	/* try to keep the ring filled */
	while (true) {
		/* check stopping condition */
		if (should_thread_stop(tctx)) {
			/* For epoch_done, not much to do */
			if (tctx->epoch_done == 1)
				return NULL;

			/* For -s, submit pending requests */
			if (tctx->flags->flimit == REQUEST_BY_SIZE)
				submit_pending_reqs(tctx);

			tctx->status = THREAD_DEAD;
			return NULL;
		}
		if (tctx->request_counter != tctx->completion_counter) {
			ret = check_for_completions(tctx);
			if (ret) {
				/* All completions not received */
				if (tctx->epoch_done == 1) {
					tctx->state =
						IFC_QDMA_QUE_WAIT_FOR_COMP;
					return NULL;
				}
				tctx->status = THREAD_DEAD;
				return NULL;
			}
			tctx->state = IFC_QDMA_QUE_INIT;
		}
		if (tctx->direction == REQUEST_RECEIVE) {
			if (sem_trywait(rx_lock) != 0) {
				tctx->epoch_done = true;
				return NULL;
			}

			if (tctx->batch_size)
				batch_enqueue_wrapper(tctx,
					tctx->request_counter ?
					tctx->completion_buf :
					NULL, tctx->cur_comp_counter, true);
			else
				enqueue_request(tctx, tctx->request_counter ?
					tctx->completion_buf : NULL,
					tctx->cur_comp_counter, true,
					NO_FORCE_ENQUEUE, true);

			sem_post(tx_lock);

			ret = check_for_completions(tctx);
			if (ret) {
				/* All completions not received */
				if (tctx->epoch_done == 1) {
					tctx->state = IFC_QDMA_QUE_WAIT_FOR_COMP;
					return NULL;
				}
				tctx->status = THREAD_DEAD;
				return NULL;
			}
		}
		if (tctx->direction == REQUEST_TRANSMIT) {
			if (sem_trywait(tx_lock) != 0) {
				tctx->epoch_done = true;
				return NULL;
			}

			if (tctx->batch_size)
				batch_enqueue_wrapper(tctx,
					tctx->request_counter ?
					tctx->completion_buf : NULL,
					tctx->cur_comp_counter, true);
			else
				enqueue_request(tctx, tctx->request_counter ?
					tctx->completion_buf : NULL,
					tctx->cur_comp_counter, true,
					NO_FORCE_ENQUEUE, true);

			sem_post(rx_lock);

			ret = check_for_completions(tctx);
			if (ret) {
				/* All completions not received */
				if (tctx->epoch_done == 1) {
					tctx->state =
						IFC_QDMA_QUE_WAIT_FOR_COMP;
					return NULL;
				}
				tctx->status = THREAD_DEAD;
				return NULL;
			}
		}
	}

last:
	tctx->status = THREAD_ERROR_STATE;
	return NULL;
}

/*Transfer handler for buffer forward from D2H to H2D*/
#ifdef BUFFER_FORWARD_D2H_TO_H2D
void *transfer_handler_util(void *ptr)
{
	struct queue_context *tctx = (struct queue_context *)ptr;
	struct queue_context *rxq = NULL;
	struct queue_context *txq = NULL;
	int ret;
	int rxcount= 0;
	

	if (tctx->init_done == 0) {
		// channel initilalization and memory allocation for completion buffer.  
		ret = queue_init(tctx);
		if (ret != PERFQ_SUCCESS) {
			goto last;
		}
#ifndef IFC_QDMA_ST_MULTI_PORT
#ifdef IFC_QDMA_ST_PACKETGEN
#ifndef IFC_ED_CONFIG_TID_UPDATE
		// Initialize ED 
		pio_init(tctx->port_id, tctx, per_channel_ed_config[tctx->channel_id]);
#endif
#endif
#endif
		if (tctx->flags->flimit == REQUEST_BY_SIZE &&
			tctx->fstart == false) {
			clock_gettime(CLOCK_MONOTONIC, &(tctx->start_time));
			tctx->fstart = true;
		}
#if 0
		// fill up the ring first 
		if (tctx->batch_size)
			batch_enqueue_wrapper(tctx, NULL,
					      PREFILL_QDEPTH, true);
		else
#endif
		enqueue_request(tctx, NULL,
				PREFILL_QDEPTH, true, NO_FORCE_ENQUEUE,
				true);
		if (tctx->status == THREAD_STOP) {
			if (tctx->prep_counter) {
				ret = ifc_mcdma_request_buffer_flush(tctx);
				if (ret) {
					tctx->request_counter +=
						tctx->prep_counter;
					tctx->prep_counter = 0ULL;
					tctx->tid_count++;
				}
			}
			pkt_gen_config(tctx);
			tctx->status = THREAD_DEAD;
			return NULL;
		}

		tctx->status = THREAD_READY;
		tctx->init_done = 1;
	        //Initialize the pkt gen ED	
               pkt_gen_config(tctx);
		return NULL;
	} else { // prefill 

		// wait for all queues to get ready 
		if (!tctx->flags->ready) {
			tctx->epoch_done = true;
			return NULL;
		}

		// let's start the clock 
		if (tctx->fstart == false) {
			clock_gettime(CLOCK_MONOTONIC, &(tctx->start_time));
			tctx->fstart = true;
		}

		// let's start the clock 
		clock_gettime(CLOCK_MONOTONIC, &(tctx->start_time_epoch));
		tctx->tid_count = 0;

		if (should_thread_stop(tctx)) {
			queue_cleanup(tctx);
			return NULL;
		}
	}
	/*ret = check_for_backlog(tctx);
	if (ret < 0) {
		tctx->epoch_done = true;
		return NULL;
	}*/
	tctx->status = THREAD_RUNNING;
        if(tctx->direction ==  REQUEST_RECEIVE)
        {
        	rxq = (struct queue_context *)ptr; 
	}
        else
	{
     		txq = (struct queue_context *)ptr;
	}

	//Poll for both Rx and Tx completion for remaining buffer.  
	while(true) {
		// check stopping condition
		if(should_thread_stop(tctx)) {
			queue_cleanup(tctx);
			return NULL;
		}
		// check for RX queue completions 
		rxcount = request_completion_poll(rxq);

		// Submit all descriptors in TX 
		if (tctx->direction ==  REQUEST_TRANSMIT && txq->cur_comp_counter > 0) {
			while (rxcount > 0) {	
				// check how many are there in TX 
				request_completion_poll(txq);
				// submit data to TX 
				ret = batch_load_wrapper(txq, rxq->completion_buf,
						txq->cur_comp_counter);
                                if (ret < 0) {
					tctx->epoch_done = 1;
					return NULL;
				}
				// Fill that many in RX also 
				ret = batch_load_wrapper(rxq, txq->completion_buf,
						rxq->cur_comp_counter);
				rxcount -= txq->cur_comp_counter;

				if (ret < 0) {
					tctx->epoch_done = 1;
					return NULL;
				}
			}
		}
                else
		{
			tctx->epoch_done = 1;
			return NULL;
		}

       }
last:
	tctx->status = THREAD_ERROR_STATE;
	return NULL;
}
#endif

static int queue_cleanup(struct queue_context *tctx)
//int queue_cleanup(struct queue_context *tctx)
{
	int ret = 0;
        if (tctx->epoch_done == 1)
                return 0;

        /* For -s, submit pending requests */
        if (tctx->flags->flimit == REQUEST_BY_SIZE){
                ret = submit_pending_reqs(tctx);
		if (ret < 0)
			return -1;
	}
#ifndef IFC_QDMA_ST_MULTI_PORT
#ifdef IFC_QDMA_ST_PACKETGEN
#ifndef IFC_ED_CONFIG_TID_UPDATE
        /* For -z or -r, Rx should stop the packet gen module */
        else if (tctx->direction == REQUEST_RECEIVE)
                checkpoint(tctx);
#endif //IFC_ED_CONFIG_TID_UPDATE
#endif
#endif

        tctx->status = THREAD_DEAD;
        return 0;
}

static int check_for_backlog(struct queue_context *tctx)
{
	int ret;
	struct rte_mbuf **buf = tctx->completion_buf;

	if (tctx->status == THREAD_WAITING) {
		if (tctx->backlog <= 0)
			return 0;

		/* backlog */
		ret = ifc_mcdma_request_buffer_flush(tctx);
		if (ret < 0)
			return -1;
		tctx->request_counter += ret;
		tctx->backlog -= ret;
		tctx->prep_counter = 0ULL;
		tctx->tid_count++;

		if (tctx->backlog > 0) {
#ifdef PERFQ_PERF
			ret = batch_load_wrapper(tctx, buf + tctx->comp_buf_offset,
						 tctx->backlog);
#else
			ret = batch_enqueue_wrapper(tctx, buf + tctx->comp_buf_offset,
						    tctx->backlog, true);
#endif
			if (ret < 0)
				return -1;
		}
	}
	return 0;
}

void *transfer_handler(void *ptr)
{
	struct queue_context *tctx = (struct queue_context *)ptr;
	int ret;

	if (tctx->init_done == 0) {
		/* Initilalize things */
		ret = queue_init(tctx);
		if (ret != PERFQ_SUCCESS) {
			goto last;
		}
#if 0
#ifndef IFC_QDMA_ST_MULTI_PORT
#ifdef IFC_QDMA_ST_PACKETGEN
#ifndef IFC_ED_CONFIG_TID_UPDATE
		/* Initialize ED */
		pio_init(tctx->port_id, tctx);
#endif
#endif
#endif
#endif
		if (tctx->flags->flimit == REQUEST_BY_SIZE &&
			tctx->fstart == false) {
			clock_gettime(CLOCK_MONOTONIC, &(tctx->start_time));
			tctx->fstart = true;
		}

		/* fill up the ring first */
		if (tctx->batch_size)
			batch_enqueue_wrapper(tctx, NULL,
					      PREFILL_QDEPTH, true);
		else
			enqueue_request(tctx, NULL,
					PREFILL_QDEPTH, true, NO_FORCE_ENQUEUE,
					true);

		if (tctx->status == THREAD_STOP) {
			if (tctx->prep_counter) {
				ret = ifc_mcdma_request_buffer_flush(tctx);
				if (ret) {
					tctx->request_counter +=
						tctx->prep_counter;
					tctx->prep_counter = 0ULL;
					tctx->tid_count++;
				}
			}
			pkt_gen_config(tctx);
			tctx->status = THREAD_DEAD;
			return NULL;
		}

		tctx->status = THREAD_READY;
		tctx->init_done = 1;
		pkt_gen_config(tctx);
		return NULL;
	} else { /* prefill */

		/* wait for all queues to get ready */
		if (!tctx->flags->ready) {
			tctx->epoch_done = true;
			return NULL;
		}

		/* let's start the clock */
		if (tctx->fstart == false) {
			clock_gettime(CLOCK_MONOTONIC, &(tctx->start_time));
			tctx->fstart = true;
		}

		/* let's start the clock */
		clock_gettime(CLOCK_MONOTONIC, &(tctx->start_time_epoch));
		tctx->tid_count = 0;

		if (should_thread_stop(tctx)) {
			queue_cleanup(tctx);
			return NULL;
		}
	}
	ret = check_for_backlog(tctx);
	if (ret < 0) {
		tctx->epoch_done = true;
		return NULL;
	}
	tctx->status = THREAD_RUNNING;

	/* try to keep the ring filled */
	while (true) {
		/* check stopping condition */
		if (should_thread_stop(tctx)) {
			queue_cleanup(tctx);
			return NULL;
		}

		request_completion_poll(tctx);
		if (tctx->cur_comp_counter > 0) {
#ifdef PERFQ_PERF
			if (tctx->init_done)
				ret = batch_load_wrapper(tctx, tctx->completion_buf,
						tctx->cur_comp_counter);
#else
			if (tctx->batch_size)
				ret = batch_enqueue_wrapper(tctx,
						      tctx->completion_buf,
						      tctx->cur_comp_counter,
						      true);
			else
				ret = enqueue_request(tctx, tctx->completion_buf,
						tctx->cur_comp_counter, true,
						NO_FORCE_ENQUEUE, true);
#endif
			pkt_gen_config(tctx);

			if (ret < 0) {
				tctx->epoch_done = 1;
				return NULL;
			}
		} else {
			tctx->epoch_done = 1;
			return NULL;
		}
	}
last:
	tctx->status = THREAD_ERROR_STATE;
	return NULL;
}

enum perfq_status request_completion_poll(struct queue_context *tctx)
{
	struct rte_mbuf *bufs[QDEPTH];
	int qid, dir, ret;
#ifdef BUFFER_FORWARD_D2H_TO_H2D
        int rxcount;
	if(tctx == NULL)
	{
                return -1;
	}
#endif
	if (config_mcdma_cmpl_proc == CONFIG_QDMA_QUEUE_MSIX) {
		ret = ifc_mcdma_poll_wait(tctx->port_id, tctx->poll_ctx, 1, &qid, &dir);
		if (ret <= 0) {
			tctx->cur_comp_counter = 0;
			return PERFQ_CMPLTN_NTFN_FAILURE;
		}
	}
#ifdef BUFFER_FORWARD_D2H_TO_H2D
	rxcount = ifc_mcdma_request_submit(tctx, tctx->direction, bufs, 0);
	ifc_mcdma_wb_event_error(tctx->port_id, tctx->channel_id, tctx->direction );
	return rxcount;
#else
		ifc_mcdma_request_submit(tctx, tctx->direction, bufs, 0);
		ifc_mcdma_wb_event_error(tctx->port_id, tctx->channel_id, tctx->direction );
#endif
	return PERFQ_SUCCESS;
}

#ifdef IFC_QDMA_ST_LOOPBACK
static int should_thread_return(struct timespec *timeout_timer)
{
	struct timespec cur_time;
	struct timespec result;
	unsigned long timeout_val;

	memset(&result, 0, sizeof(struct timespec));
	clock_gettime(CLOCK_MONOTONIC, &cur_time);
	time_diff(&cur_time, timeout_timer, &result);
	timeout_val = 50000;
	if (result.tv_sec ||
	   ((result.tv_nsec / 1e3) >= timeout_val))
		return true;

	return false;
}

void perfq_submit_requests(struct queue_context *tctx, uint32_t diff)
{
	struct timespec timeout_timer;
	uint32_t prev_diff = 0;

	clock_gettime(CLOCK_MONOTONIC, &timeout_timer);
	while (true) {
		/* Return when all requests are placed or timeout occured */
		if (diff == 0)
			return;
		else {
			if (prev_diff - diff == 0) {
				if (should_thread_return(&timeout_timer))
					return;
			} else
				/* Reset the timer */
				clock_gettime(CLOCK_MONOTONIC, &timeout_timer);
		}
		prev_diff = diff;
		if (diff >= tctx->cur_comp_counter && tctx->cur_comp_counter != 0) {
			diff = diff - tctx->cur_comp_counter;
			batch_enqueue_wrapper(tctx, tctx->completion_buf,
				tctx->cur_comp_counter, false);
		}
		/* when no previous responses are pending and
		 * diff less than current_comp, place diff rqsts */
		if ((tctx->request_counter == tctx->completion_counter ||
			diff < tctx->cur_comp_counter)) {
				batch_enqueue_wrapper(tctx, tctx->completion_buf,
						   diff, false);
			diff = 0;
		}

		request_completion_poll(tctx);

		if (tctx->cur_comp_counter > 0 && diff) {
			if (tctx->cur_comp_counter > diff) {
				batch_enqueue_wrapper(tctx, tctx->completion_buf,
						   diff, false);
				diff = 0;
			} else {
				diff = diff - tctx->cur_comp_counter;
				batch_enqueue_wrapper(tctx, tctx->completion_buf,
						   tctx->cur_comp_counter, false);
			}
		}
	}
}
#endif


