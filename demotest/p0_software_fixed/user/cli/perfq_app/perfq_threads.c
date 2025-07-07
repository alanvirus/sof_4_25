// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#define _GNU_SOURCE

#include "perfq_app.h"

extern int cpu_list[512];

static void perfq_smp_barrier(void)
{
        __asm volatile("sfence" ::: "memory");
        //__asm volatile("mfence" ::: "memory");
        __asm volatile("" ::: "memory");
}

int core_assign(struct thread_context *tctx)
{
	int cores = sysconf(_SC_NPROCESSORS_ONLN);
	int thr_num;
	cpu_set_t core_mask;

	CPU_ZERO(&core_mask);

	thr_num = cpu_list[cur_assgn_core];
	if (thr_num > cores)
		return -1;
	CPU_SET(thr_num, &core_mask);
	cur_assgn_core = ((cur_assgn_core + 1) % cores);

	return pthread_setaffinity_np(tctx->pthread_id, sizeof(cpu_set_t),
				      &core_mask);
}

void signal_mask(void)
{
	sigset_t thread_mask;

	sigemptyset(&thread_mask);
	sigaddset(&thread_mask, SIGINT);
	pthread_sigmask(SIG_BLOCK, &thread_mask, NULL);

}
#ifdef IFC_32BIT_SUPPORT
int pio_bit_writer(uint32_t offset, int n, int set, struct queue_context *q)
{
	uint32_t addr = PIO_REG_PKT_GEN_EN + offset;
	uint32_t val = ifc_qdma_pio_read32(q->qdev, addr);

	if (set)
		val |= (1UL << n);
	else
		val &= ~(1UL << n);

	ifc_qdma_pio_write32(q->qdev, addr, val);
	return 0;
}
#else
int pio_bit_writer(uint64_t offset, int n, int set, struct queue_context *q)
{
	uint64_t addr = PIO_REG_PKT_GEN_EN + offset;
	uint64_t val = ifc_qdma_pio_read64(q->qdev, addr);

	if (set)
		val |= (1ULL << n);
	else
		val &= ~(1ULL << n);

	ifc_qdma_pio_write64(q->qdev, addr, val);
	return 0;
}
#endif
static uint32_t get_pending_files(struct queue_context *qctx)
{
	uint32_t diff;
	if (qctx->ed_files_submitted >= qctx->sw_files_submitted) {
		/* No rollover */
		/* ed: 10000 sw: 9553 */
		diff = qctx->ed_files_submitted - qctx->sw_files_submitted;
	} else {
		/* rollover
		 * ed: 5, sw: 65534
		 * ed: 0, sw: 15
		 */
		diff = (65535 - qctx->sw_files_submitted) + qctx->ed_files_submitted;
	}
	return diff;
}

static void perf_update_ed(struct queue_context *tctx __attribute__((unused)),
			   int nr __attribute__((unused)))
{
#ifdef IFC_ED_CONFIG_TID_UPDATE
	uint32_t diff = 0;
	if (tctx->direction == 1)
		return;
	if ((tctx->desc_requested  + nr) >= tctx->file_size) {
		uint32_t num_files = (tctx->desc_requested  + nr) / tctx->file_size;
		tctx->sw_files_submitted = (tctx->sw_files_submitted + num_files) % 65536;
		tctx->total_files += num_files;

#ifdef DEBUG_NON_CONT_MODE
		fprintf(stderr, "TEST%u%u:SW update: hw files :%u sw:%u diff:%u num:%u tot:%lu sw:%lu hw:%lu ela:%u\n",
				tctx->channel_id, tctx->direction, tctx->ed_files_submitted,
				tctx->sw_files_submitted, diff, num_files, tctx->total_files * tctx->file_size,
				tctx->total_files * tctx->file_size + (tctx->desc_requested + nr) % tctx->file_size,
				tctx->total_hw_files * tctx->file_size, tctx->time_elapsed);
#endif
		if (tctx->time_limit_reached == 1) {
			tctx->desc_requested =  (tctx->desc_requested + nr) % tctx->file_size;
			return;
		}
		diff = get_pending_files(tctx);

		if (diff < (DEFAULT_STEP_SIZE)) {
			tctx->ed_files_submitted = (tctx->ed_files_submitted + DEFAULT_STEP_SIZE) % 65536;
			tctx->total_hw_files += DEFAULT_STEP_SIZE;
			pio_update_files(tctx->qdev, tctx, tctx->ed_files_submitted);
#ifdef DEBUG_NON_CONT_MODE
			fprintf(stderr, "TEST%u%u:updating: hw files :%u sw:%u diff:%u num:%u\n",
					tctx->channel_id, tctx->direction, tctx->ed_files_submitted,
					tctx->sw_files_submitted, diff, num_files);
#endif
		}
		tctx->desc_requested =  (tctx->desc_requested + nr) % tctx->file_size;
	} else {
#ifdef DEBUG_NON_CONT_MODE
		fprintf(stderr, "TEST%u%u:fs: not satisfied: hw files :%u sw:%u diff:%u nr:%u\n",
				tctx->channel_id, tctx->direction, tctx->ed_files_submitted,
				tctx->sw_files_submitted, diff, nr);
#endif
		tctx->desc_requested += nr;
	}
#endif
}

int ifc_mcdma_umsix_irq_handler(struct ifc_qdma_device *qdev,
				struct ifc_qdma_channel *qchnl,
				int dir, void *data, int *errinfo)
{
	struct queue_context *qctx = (struct queue_context*)data;
	if (qctx == NULL)
		printf("Invalid Queue Context");
	else
		qctx->tout_err_cnt++;
	if (*errinfo)
		ifc_qdma_channel_reset(qdev, qchnl, dir);
#if defined(IFC_QDMA_DYN_CHAN) && defined(IFC_DEBUG_STATS)
	if((*errinfo & CTO_EVENT) || (*errinfo & DESC_FETCH_EVENT) || (*errinfo & DATA_FETCH_EVENT)) {
		if (*errinfo & CTO_EVENT)
			printf("INFO:: Desc or Data fetch error occured due to CTO\n");
		else
			printf("INFO:: Desc or Data fetch error occured\n");
		printf("INFO:: chnlID:%u pf:%d vf:%d\n", qchnl->channel_id, qdev->pf, qdev->vf);
	}
#endif
	return 0;
}

int msix_init(struct queue_context *tctx)
{
	int ret;

	tctx->poll_ctx = ifc_qdma_poll_init(tctx->qdev);
	if (tctx->poll_ctx == NULL)
		return PERFQ_CMPLTN_NTFN_FAILURE;
	/* request for channel and busy wait until you receive it*/
	while (true) {
		if (pthread_mutex_lock(&(tctx->flags->locks[tctx->channel_id])) != 0) {
			printf("acquiring mutex failed\n");
			return PERFQ_CH_INIT_FAILURE;
		}
		ret = ifc_qdma_channel_get(tctx->qdev, &tctx->qchnl,
					  tctx->channel_id,  IFC_QDMA_DIRECTION_BOTH);
		if (pthread_mutex_unlock(&(tctx->flags->locks[tctx->channel_id])) != 0) {
			printf("releasing mutex failed\n");
			return PERFQ_CH_INIT_FAILURE;
		}
		if (ret >= 0 || ret == -2)
			break;
		if (ret == -1)
			return PERFQ_CH_INIT_FAILURE;
		sleep(2);
	}
	if (ret >= 0)
		tctx->channel_id = ret;

	/* Add error handler */
	ifc_qdma_add_irq_handler(tctx->qchnl, tctx->direction,
				 ifc_mcdma_umsix_irq_handler,
				 tctx);

	ret = ifc_qdma_poll_add(tctx->qchnl, tctx->direction, tctx->poll_ctx);
	if (ret < 0)
		return PERFQ_CMPLTN_NTFN_FAILURE;

	return PERFQ_SUCCESS;
}

#ifndef IFC_QDMA_ST_MULTI_PORT
#ifdef IFC_QDMA_ST_PACKETGEN
#ifdef IFC_32BIT_SUPPORT
uint32_t pio_queue_config_offset(struct queue_context *qctx)
#else
uint64_t pio_queue_config_offset(struct queue_context *qctx)
#endif
{
#ifndef IFC_QDMA_DYN_CHAN
#ifdef IFC_32BIT_SUPPORT
	uint32_t pf_chnl_offset;
	uint32_t vf_chnl_offset;
#else
	uint64_t pf_chnl_offset;
	uint64_t vf_chnl_offset;
#endif
#endif
#ifdef IFC_32BIT_SUPPORT
	uint32_t offset = 0;
#else
	uint64_t offset = 0;
#endif

#ifdef IFC_QDMA_DYN_CHAN
	offset = 0;
#else
	if (qctx->flags->vf == 0) {
#ifdef IFC_32BIT_SUPPORT
		pf_chnl_offset = ((uint32_t)qctx->flags->pf - 1) * IFC_QDMA_PER_PF_CHNLS;
#else
		pf_chnl_offset = ((uint64_t)qctx->flags->pf - 1) * IFC_QDMA_PER_PF_CHNLS;
#endif
		vf_chnl_offset = 0;
	} else {
		pf_chnl_offset = IFC_QDMA_PFS * IFC_QDMA_PER_PF_CHNLS;
#ifdef IFC_32BIT_SUPPORT
		vf_chnl_offset = ((uint32_t)qctx->flags->pf - 1) *
				(IFC_QDMA_PER_PF_VFS * IFC_QDMA_PER_VF_CHNLS);
		vf_chnl_offset += ((uint32_t)qctx->flags->vf - 1) * IFC_QDMA_PER_VF_CHNLS;
#else
		vf_chnl_offset = ((uint64_t)qctx->flags->pf - 1) *
				(IFC_QDMA_PER_PF_VFS * IFC_QDMA_PER_VF_CHNLS);
		vf_chnl_offset += ((uint64_t)qctx->flags->vf - 1) * IFC_QDMA_PER_VF_CHNLS;
#endif
	}
	offset = 8 * (pf_chnl_offset + vf_chnl_offset);
#endif
#ifdef IFC_MCDMA_SINGLE_FUNC
	return offset + (8 * qctx->channel_id);
#else
	return offset + (8 * qctx->phy_channel_id);
#endif
}

#ifdef IFC_ED_CONFIG_TID_UPDATE
int pio_update_files(struct ifc_qdma_device *qdev, struct queue_context *tctx,
			    uint32_t fcount)
{
	uint64_t base = 0ULL;
	uint64_t offset = 0ULL;
	uint64_t ed_config = 0ULL;
	unsigned long file_size = 0ULL;
	uint64_t files = 0ULL;

	file_size = tctx->flags->file_size;
	offset = pio_queue_config_offset(tctx);

	if (tctx->direction == REQUEST_TRANSMIT) {
		base = PIO_REG_PORT_PKT_CONFIG_H2D_BASE;
		ed_config = tctx->flags->packet_size * file_size;
	}
	if (tctx->direction == REQUEST_RECEIVE) {
		base = PIO_REG_PORT_PKT_CONFIG_D2H_BASE;
		ed_config = tctx->flags->packet_size * file_size;
		if (tctx->flags->flimit == REQUEST_BY_SIZE)
			files = tctx->flags->packets / file_size;
		else
			files = fcount;

		/* Build ED Configuration */
#ifdef IFC_32BIT_SUPPORT
		ed_config |= (files << PIO_REG_PORT_PKT_CONFIG_FILES_SHIFT_32BIT_SUPPORT);
		ed_config |= (1UL << PIO_REG_PORT_PKT_CONFIG_ENABLE_SHIFT_32BIT_SUPPORT);
		ed_config |= (1UL << PIO_REG_PORT_PKT_CONFIG_IDLE_CYCLES_SHIFT_32BIT_SUPPORT);
#else
		ed_config |= (files << PIO_REG_PORT_PKT_CONFIG_FILES_SHIFT);
		ed_config |= (1UL << PIO_REG_PORT_PKT_CONFIG_ENABLE_SHIFT);
		ed_config |= (1UL << PIO_REG_PORT_PKT_CONFIG_IDLE_CYCLES_SHIFT);
#endif
	}
#ifdef IFC_32BIT_SUPPORT
        ifc_qdma_pio_write32(qdev, base + offset, ed_config);
        ifc_qdma_pio_write32(qdev, (base + offset)+4, ed_config >> 32);
#else

	ifc_qdma_pio_write64(qdev, base + offset, ed_config);
#endif
	return 0;
}
#endif

static int pio_init(struct ifc_qdma_device *qdev, struct queue_context *tctx)
{
	struct struct_flags *flags = tctx->flags; 
#ifdef IFC_32BIT_SUPPORT
	uint32_t base = 0UL;
	uint32_t offset = 0UL;
#else
	uint64_t base = 0ULL;
	uint64_t offset = 0ULL;
#endif
	uint64_t ed_config = 0ULL;
	uint64_t files = 0ULL;
	unsigned long file_size = 0ULL;

	file_size = tctx->file_size;
	offset = pio_queue_config_offset(tctx);

	if (tctx->direction == REQUEST_TRANSMIT) {
		base = 0x20000;
		ed_config = flags->packet_size * file_size;
	}
	if (tctx->direction == REQUEST_RECEIVE) {
		base = 0x30000;
		ed_config = flags->packet_size * file_size;
		if (flags->flimit == REQUEST_BY_SIZE)
			files = flags->packets / file_size;
		else {
			files = flags->pkt_gen_files;
		}
#ifdef IFC_32BIT_SUPPORT
                ed_config |= (files << PIO_REG_PORT_PKT_CONFIG_FILES_SHIFT_32BIT_SUPPORT);
#else
                ed_config |= (files << PIO_REG_PORT_PKT_CONFIG_FILES_SHIFT);
#endif
#ifdef IFC_32BIT_SUPPORT
                ed_config |= (1ULL << (uint64_t)PIO_REG_PORT_PKT_CONFIG_ENABLE_SHIFT_32BIT_SUPPORT);
#else
                ed_config |= (1ULL << (uint64_t)PIO_REG_PORT_PKT_CONFIG_ENABLE_SHIFT);
#endif
        }
#ifdef IFC_32BIT_SUPPORT
        ifc_qdma_pio_write32(qdev, base + offset, ed_config);
        ifc_qdma_pio_write32(qdev, (base + offset)+4, ed_config >> 32);
#else
        ifc_qdma_pio_write64(qdev, base + offset, ed_config);
#endif

	#if defined  CID_PAT && defined IFC_PROG_DATA_EN
        //Enbale the pattern switch;
#ifdef IFC_32BIT_SUPPORT
                ifc_qdma_pio_write32(qdev, PIO_REG_PORT_PKT_ENABLE_PATTERN, 1ULL);
#else
                ifc_qdma_pio_write64(qdev, PIO_REG_PORT_PKT_ENABLE_PATTERN, 1ULL);
#endif
        #else
#ifdef IFC_32BIT_SUPPORT
                ifc_qdma_pio_write32(qdev, PIO_REG_PORT_PKT_ENABLE_PATTERN, 0ULL);
#else
                ifc_qdma_pio_write64(qdev, PIO_REG_PORT_PKT_ENABLE_PATTERN, 0ULL);
#endif
        #endif
        
	return 0;
}
#endif
#endif

int non_msi_init(struct queue_context *tctx)
{
	int ret;

	/* request for channel and busy wait until you receive it*/
	while (true) {
		if (pthread_mutex_lock(&(tctx->flags->locks[tctx->channel_id])) != 0) {
			printf("Acquiring lock failed\n");
			return PERFQ_CH_INIT_FAILURE;
		}
		ret = ifc_qdma_channel_get(tctx->qdev, &tctx->qchnl,
					  tctx->channel_id, IFC_QDMA_DIRECTION_BOTH);
		if (pthread_mutex_unlock(&(tctx->flags->locks[tctx->channel_id])) != 0) {
			printf("releasing mutex failed %u\n", tctx->channel_id);
			return PERFQ_CH_INIT_FAILURE;
		}
		if (ret >= 0 || ret == -2)
			break;
		if (ret == -1)
			return PERFQ_CH_INIT_FAILURE;
		sleep(2);
	}
	if (ret >= 0)
		tctx->channel_id = ret;

	/* Add error handler */
	ifc_qdma_add_irq_handler(tctx->qchnl, tctx->direction,
				 ifc_mcdma_umsix_irq_handler,
				 tctx);
	return PERFQ_SUCCESS;
}

enum perfq_status queue_init(struct queue_context *qctx)
{
	signal_mask();

#ifdef IFC_QDMA_INTF_ST
	struct struct_flags *flags = qctx->flags;
#endif
	int ret;

	/* Initiailize completion notificate policy*/
	if (global_flags->completion_mode == CONFIG_QDMA_QUEUE_MSIX)
		ret = msix_init(qctx);
	else
		ret = non_msi_init(qctx);

	if (ret) {
		printf("Channel%d: Direction%d: Channel Initializaiton failed\n", qctx->channel_id, qctx->direction);
		return PERFQ_CMPLTN_NTFN_FAILURE;
	}

	/* allocate memory for completion_buf */
	qctx->completion_buf = malloc(QDEPTH *
				   sizeof(struct ifc_qdma_request *));
	if (!(qctx->completion_buf)) {
		printf("Channel%d: Direction%d: Failed to allocate memory for completion buffer\n", qctx->channel_id, qctx->direction);
		return PERFQ_MALLOC_FAILURE;
	}

#ifdef IFC_QDMA_INTF_ST
	/* init AVST specific things */
	/* Talk to the Hardware if required */
	if ((flags->direction != REQUEST_LOOPBACK) && (qctx->direction == REQUEST_RECEIVE))
		hw_eof_handler(qctx);
#endif
	qctx->end_completion_counter = 0;

#ifdef CID_PAT
	/* 0-16 => Data, 16-24 -> cid, 24-31 => portid */
#ifdef IFC_QDMA_DYN_CHAN
#ifdef IFC_QDMA_ST_PACKETGEN
#ifdef IFC_PROG_DATA_EN
	set_avst_pattern_data(qctx);
#else
	/*  Correction needed for static channel allocation */
	qctx->data_tag = mcdma_ph_chno[qctx->channel_id];
	qctx->data_tag |= (qctx->data_tag << 8);
	qctx->data_tag |= (qctx->data_tag << 16);
#endif
#else
	qctx->data_tag = (qctx->channel_id | (qctx->flags->portid << 8));
#endif
#endif //End of IFC_QDMA_DYN_CHAN

#ifdef IFC_PROG_DATA_EN
	set_avst_pattern_data(qctx);
#else
	qctx->expected_pattern = (0x00000000 | qctx->data_tag << 16);
#endif
#endif //End of CID_PAT

	return PERFQ_SUCCESS;
}

#pragma GCC push_options
#pragma GCC optimize ("O0")
uint32_t load_data(void *buf, size_t size, uint32_t pattern)
{
	unsigned int i;

	for (i = 0; i < (size / sizeof(uint32_t)); i++)
		*(((uint32_t *)buf) + i) = pattern++;

	return pattern;
}
#pragma GCC pop_options

int append_to_file(char *file_name, char *append_data)
{
	FILE *f;
	int ret;

	f = ifc_qdma_fopen(file_name, "a+");
	if (f == NULL)
		return -1;
	ret = fputs(append_data, f);
	fclose(f);
	if (ret)
		return -1;
	return 0;
}

#pragma GCC push_options
#pragma GCC optimize ("O0")
enum perfq_status pattern_checker(void *buf, size_t size, uint32_t expected_pattern, struct debug_data_dump *data_dump)
{
	uint32_t actual_pattern;
	unsigned int i;
#ifdef DUMP_DATA
	char data_to_write[128];
	char file_name[50] = DUMP_FILE;
#endif
#ifndef DUMP_DATA
	if(data_dump)
		i = data_dump->direction;
#endif 

	for (i = 0; i < size / sizeof(uint32_t); i++) {
		actual_pattern = ((uint32_t *)buf)[i];
#ifdef DUMP_DATA
#if 0
		memset(data_to_write, 0, 50);
		snprintf(data_to_write, sizeof(data_to_write), "0x%x,0x%x\n", actual_pattern, expected_pattern);
		append_to_file(file_name, data_to_write);
#endif
#endif
		perfq_smp_barrier();
                if (actual_pattern != expected_pattern) {
			if (size <= 8192) {
				usleep(1);
				actual_pattern = ((uint32_t *)buf)[i];
				if (actual_pattern == expected_pattern) {
					expected_pattern++;
					continue;
				}
			}
#ifdef DUMP_DATA
	            memset(data_to_write, 0, 128);
		if(data_dump){	
			snprintf(data_to_write, sizeof(data_to_write), "Data Pattern: Packet act:0x%x exp:0x%x channel_id:0x%x",
                   	actual_pattern, expected_pattern, data_dump->channel_id);                                   
        		append_to_file(file_name, data_to_write);                                                                

        		memset(data_to_write, 0, 128);                                                                        
        		snprintf(data_to_write, sizeof(data_to_write), "dir:0x%x didx:0x%x flags:0x%x\n",
                 	data_dump->direction, data_dump->didx, data_dump->flags);
           	 	append_to_file(file_name, data_to_write);
		}
#endif
			return PERFQ_DATA_VALDN_FAILURE;
		}
#ifndef IFC_PROG_DATA_EN 
		expected_pattern++;
#endif
	}
#ifdef DUMP_DATA
#if 0
	memset(data_to_write, 0, 50);
	snprintf(data_to_write, sizeof(data_to_write), "Pattern Validation Successful for Packet\n");
	append_to_file(file_name, data_to_write);
#endif
#endif
	return PERFQ_SUCCESS;
}
#pragma GCC pop_options

inline int should_thread_stop(struct queue_context *tctx)
{
	struct timespec cur_time, diff;
	long timediff_sec = 0;
	long timediff_msec = 0;
	int timeout_sec = tctx->flags->time_limit;
	int epoch_done = false;

	if (tctx->direction == 0)
		timeout_sec += 1;

	if (tctx->fstart == false && tctx->flags->flimit == REQUEST_BY_TIME)
		return false;

	/* does the main thread want me to stop? */
	if (tctx->status == THREAD_STOP)
		return true;

	clock_gettime(CLOCK_MONOTONIC, &cur_time);

	if (tctx->time_limit_reached && (tctx->sw_files_submitted == tctx->ed_files_submitted)) {
#ifdef DEBUG_NON_CONT_MODE
		fprintf(stderr, "TEST%u%u: ****** equal: ed:%u sw:%u\n",
				tctx->channel_id, tctx->direction,
				tctx->ed_files_submitted, tctx->sw_files_submitted);
#endif
		clock_gettime(CLOCK_MONOTONIC, &(tctx->end_time));
		tctx->epoch_done = 0;
		return true;
	} else {
#ifdef DEBUG_NON_CONT_MODE
		if (tctx->time_limit_reached)
			fprintf(stderr, "TEST%u%u: ****** balance: ed:%u sw:%u\n",
				tctx->channel_id, tctx->direction,
				tctx->ed_files_submitted, tctx->sw_files_submitted);
#endif
	}

	/* is the transfer by time or request */
	if (tctx->flags->flimit == REQUEST_BY_TIME) {
		timediff_sec = difftime(cur_time.tv_sec,
				(tctx->start_time).tv_sec);
		tctx->time_elapsed = timediff_sec;
#ifdef IFC_32BIT_SUPPORT
		if ((tctx->time_limit_reached == 0) &&
		    (timediff_sec >= (long)tctx->flags->time_limit)) {
#else
		if ((tctx->time_limit_reached == 0) &&
		    (timediff_sec >= tctx->flags->time_limit)) {
#endif
			tctx->pending_files = get_pending_files(tctx);
#ifdef DEBUG_NON_CONT_MODE
			fprintf(stderr, "TEST%u%u: ****** balance: ed:%u sw:%u pending:%u diff:%lu limit:%u\n",
				tctx->channel_id, tctx->direction,
				tctx->ed_files_submitted, tctx->sw_files_submitted,
				tctx->pending_files, timediff_sec, tctx->flags->time_limit);
#endif
			tctx->time_limit_reached = 1;
		}

		if (timediff_sec >= timeout_sec) {
			clock_gettime(CLOCK_MONOTONIC, &(tctx->end_time));
			tctx->end_completion_counter = tctx->completion_counter;
			tctx->epoch_done = 0;
#ifdef DEBUG_NON_CONT_MODE
			fprintf(stderr, "TEST%u%u: ****** exiting: ed:%u sw:%u pending:%u %lu %u\n",
				tctx->channel_id, tctx->direction,
				tctx->ed_files_submitted, tctx->sw_files_submitted,
				tctx->pending_files, timediff_sec, timeout_sec);
#endif
			return true;
		}
	} else {
		if (tctx->flags->packets <= tctx->request_counter + tctx->prep_counter) {
			clock_gettime(CLOCK_MONOTONIC, &(tctx->end_time));
			tctx->epoch_done = 0;
			return true;
		}
	}
#ifdef DEBUG_NON_CONT_MODE
	if (tctx->time_limit_reached)
		fprintf(stderr, "TEST%u%u: ****** not exiting: ed:%u sw:%u pending:%u %lu %u\n",
			tctx->channel_id, tctx->direction,
			tctx->ed_files_submitted, tctx->sw_files_submitted,
			tctx->pending_files, timediff_sec, timeout_sec);
#endif

	/* Logic for epoch done detection */

	/* For -s flag, we don't consider TDM, we only consider tid updates
	 * For data validation strict ordering will be
	 * maintained */
	if (tctx->flags->flimit == REQUEST_BY_SIZE ||
	    tctx->flags->fvalidation == true) {
		/* For non-batch mode, 64 tid updates must be done
		 * by a queue in one iteration, in batch mode, one batch update
		 * should be done */
		if (tctx->tid_count > 0 || tctx->nonb_tid_count == 64U)
			epoch_done = true;

	/* For -l flag, we also consider TDM */
	} else {
		time_diff(&cur_time, &(tctx->start_time_epoch), &diff);
		timediff_msec = (diff.tv_sec * 1000) + ((double)diff.tv_nsec / 1e6);
		if (timediff_msec >= THR_EXEC_TIME_MSEC ||
		    tctx->tid_count > 0 || tctx->nonb_tid_count == 64U)
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
enum perfq_status set_sof_eof(struct queue_context *tctx, struct ifc_qdma_request *req, unsigned long req_counter)
{
	unsigned long file_size = tctx->file_size;
	int ret = PERFQ_SUCCESS;

	req->flags = 0;
	/* first packet of a file */
	if ((req_counter % file_size) == 0) {
		req->flags |= IFC_QDMA_SOF_MASK;
		ret = PERFQ_SOF;
	} else {
		req->flags &= ~IFC_QDMA_SOF_MASK;
	}

	/* last packet of a file */
	if ((req_counter % file_size) == file_size - 1) {
		req->flags |= IFC_QDMA_EOF_MASK;
		/* Does it also contain SOF */
		ret = ret ? PERFQ_BOTH : PERFQ_EOF;
	} else {
		req->flags &= ~IFC_QDMA_EOF_MASK;
	}
	return ret;
}

/* request_number starts from 1 */
/* TODO: move to request_number starting from 0 */
enum perfq_status check_sof_eof(struct queue_context *tctx, struct ifc_qdma_request *req, unsigned long request_number)
{
	unsigned long file_size = tctx->file_size;
	unsigned long payload = get_payload_size(tctx, request_number - 1);
	int ret = PERFQ_SUCCESS;

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
		if (check_payload(payload, req->pyld_cnt)) {
			tctx->bfile_counter++;
			return PERFQ_DATA_PAYLD_FAILURE;
		}
		if ((req->flags & IFC_QDMA_SOF_MASK) && (req->flags & IFC_QDMA_EOF_MASK)) {
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
	if ((req->flags & IFC_QDMA_SOF_MASK) && ((request_number % file_size) == 1)) {
		tctx->cur_file_status = PERFQ_SOF;
#ifdef VERIFY_FUNC
		printf("Channel%d: Direction%d: File Transfer Started\n",
			tctx->channel_id, tctx->direction);
#endif
	}

	/* Didn't expect SOF but received SOF */
	else if ((req->flags & IFC_QDMA_SOF_MASK) && !((request_number %
		 file_size) == 1)) {
		tctx->cur_file_status = PERFQ_FILE_VALDN_FAILURE;

		/* bad file */
		tctx->bfile_counter++;
#ifdef VERIFY_FUNC
		printf("Channel%d: Direction%d: Unexpected SOF received --> File data invalidated\n",
			tctx->channel_id, tctx->direction);
#endif
	}

	/* Expected SOF but didn't received SOF*/
	else if (!(req->flags & IFC_QDMA_SOF_MASK) && ((request_number %
		 file_size) == 1)) {
		tctx->cur_file_status = PERFQ_FILE_VALDN_FAILURE;

		/* bad file */
		tctx->bfile_counter++;
#ifdef VERIFY_FUNC
		printf("Channel%d: Direction%d: Failed to received SOF --> File data invalidated\n",
			tctx->channel_id, tctx->direction);
#endif
	}

	/* Expected EOF & received EOF */
	if ((req->flags & IFC_QDMA_EOF_MASK) && !(request_number % file_size)) {

		/* Have we seen SOF? */
		if (tctx->cur_file_status != PERFQ_SOF) {
			tctx->cur_file_status = PERFQ_FILE_VALDN_FAILURE;
			tctx->bfile_counter++;
#ifdef VERIFY_FUNC
			printf("Channel%d: Direction%d: Unexpected EOF received --> File data invalidated\n",
				tctx->channel_id, tctx->direction);
#endif
		} else {
			/* check for payload */
			if (check_payload(payload, req->pyld_cnt)) {
#ifdef VERIFY_FUNC
				printf("Channel%d: Direction%d: EOF expected & received but payload check failed --> File data invalidated\n",
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
			printf("Channel%d: Direction%d: File Transfer Completed\n",
				tctx->channel_id, tctx->direction);
#endif
		}

		/* reset status for the next file */
		tctx->cur_file_status = PERFQ_SUCCESS;
		ret = PERFQ_EOF;
	}

	/* Didn't expect EOF but received EOF */
	else if ((req->flags & IFC_QDMA_EOF_MASK) && (request_number %
		 file_size)) {
		tctx->cur_file_status = PERFQ_FILE_VALDN_FAILURE;
		tctx->bfile_counter++;
#ifdef VERIFY_FUNC
		printf("Channel%d: Direction%d: Unexpected EOF received --> File data invalidated\n",
			tctx->channel_id, tctx->direction);
#endif
	}

	/* Expected EOF but didn't received EOF */
	else if (!(req->flags & IFC_QDMA_EOF_MASK) && !(request_number %
		 file_size)) {
		tctx->cur_file_status = PERFQ_FILE_VALDN_FAILURE;
		tctx->bfile_counter++;

		/* reset for the next file */
		tctx->cur_file_status = PERFQ_SUCCESS;
		ret = PERFQ_EOF;
#ifdef VERIFY_FUNC
		printf("Channel%d: Direction%d: Failed to received EOF --> File data invalidated\n",
			tctx->channel_id, tctx->direction);
#endif
	}
	return ret;
}

int hw_eof_handler(struct queue_context *tctx)
{
	/* For Rx: clear & set the PIO flag */
	pio_bit_writer(0x00, tctx->channel_id, false, tctx);
	pio_bit_writer(0x00, tctx->channel_id, true, tctx);
	return 0;
}

enum perfq_status post_processing(struct queue_context *tctx)
{
	struct ifc_qdma_request **buf = tctx->completion_buf;
	uint32_t bkp_ex_pttrn;
	int ret = PERFQ_SUCCESS;
	unsigned long i;
	unsigned long req_number;
	unsigned long payload;
        struct debug_data_dump data_dump;

#ifdef IFC_QDMA_INTF_ST
#ifdef IFC_QDMA_META_DATA
	uint32_t bkp_mdata_ex_pttrn;
#endif
#endif
	for (i = 0; i < tctx->cur_comp_counter; i++) {
		req_number = (tctx->completion_counter - tctx->cur_comp_counter) + 1 + i;
		payload = get_payload_size(tctx, req_number - 1);

		bkp_ex_pttrn = tctx->expected_pattern;
#ifdef IFC_QDMA_INTF_ST
#ifdef IFC_QDMA_META_DATA
		bkp_mdata_ex_pttrn = tctx->mdata_expected_pattern;
		/*Verify the metat data*/
		ret = pattern_checker(&buf[i]->metadata, METADATA_SIZE, tctx->mdata_expected_pattern, 0x00);

		if (ret == PERFQ_DATA_VALDN_FAILURE) {
			/* Update the bad metapacket counter*/
			tctx->mdata_bpckt_counter++;
#ifdef VERIFY_FUNC
			printf("Channel%d: Direction%d: metadata Packet Data Validation Failed\n", tctx->channel_id, tctx->direction);
#endif
		} else {
			/* Update the good metadata packet counter*/
			tctx->mdata_gpckt_counter++;
		}
		tctx->mdata_expected_pattern = bkp_mdata_ex_pttrn + (METADATA_SIZE / sizeof(uint32_t));
#endif // metadata validation
		//perfq_smp_barrier();
		data_dump.channel_id = tctx->channel_id; 
                data_dump.direction = tctx->direction; 
                data_dump.didx = ((struct ifc_qdma_desc *)buf[i]->cur_desc)->didx;
                data_dump.flags = buf[i]->flags; 

		ret = pattern_checker(buf[i]->buf, payload, tctx->expected_pattern, &data_dump);
#else
		ret = pattern_checker(buf[i]->buf, payload, 0x00, &data_dump);
#endif
		if (ret == PERFQ_DATA_VALDN_FAILURE) {
			/* Update the bad packet counter*/
			tctx->bpckt_counter++;

			/* current file data is invalidated */
			if (tctx->cur_file_status != PERFQ_FILE_VALDN_FAILURE)
				tctx->cur_file_status = PERFQ_DATA_VALDN_FAILURE;
#ifdef VERIFY_FUNC
			printf("Channel%d: Direction%d: Packet Data Validation Failed\n", tctx->channel_id, tctx->direction);
#endif
		} else {
			/* Update the good packet counter*/
			tctx->gpckt_counter++;
		}
		perfq_smp_barrier();
		tctx->expected_pattern = bkp_ex_pttrn + (payload / sizeof(uint32_t));

#ifdef IFC_QDMA_INTF_ST
		ret = check_sof_eof(tctx, buf[i], req_number);
		if (ret == PERFQ_EOF || ret == PERFQ_DATA_PAYLD_FAILURE) {
		/* reset the expected pattern */
		#ifdef CID_PAT
		#ifdef IFC_PROG_DATA_EN
			tctx->expected_pattern = tctx->data_tag | AVST_PATTERN_DATA;
		#else
			tctx->expected_pattern = (0x00 | tctx->data_tag << 16);
		#endif
		#else
			tctx->expected_pattern = 0x00;
		#endif
			//tctx->expected_pattern = 0x00;
			tctx->mdata_expected_pattern = 0x00;
			perfq_smp_barrier();
		}
#endif
	}
	return ret;
}

/* req_no starts from 0 */
unsigned long get_payload_size(struct queue_context *tctx, unsigned long req_no)
{
	if (tctx->flags->flimit == REQUEST_BY_TIME)
		return tctx->flags->packet_size;

	return min(tctx->flags->packet_size, tctx->flags->request_size -
		   (tctx->flags->packet_size * req_no));
}

#ifndef IFC_QDMA_INTF_ST
/* returns the starting address for the next packet */
uint64_t avmm_addr_manager(__attribute__((unused)) struct queue_context *tctx,
			   uint64_t *addr, unsigned long payload)
{
	uint64_t start_addr;

#ifdef PERFQ_PERF
	/* In Perf mode using same memory segment */
	*addr = (*addr + payload < PERFQ_AVMM_BUF_LIMIT) ? *addr : 0;
#else
	/* Using different memory segments to avoid overwrites */
	*addr = (*addr + payload < tctx->limit) ? *addr : tctx->base;
#endif

#ifdef VERIFY_FUNC
	printf("Channel: %d\tDirection: %s\tAVMM addr: 0x%lx\n", tctx->channel_id, tctx->direction ? "Tx" : "Rx", *addr);
#endif
	start_addr = *addr;
	*addr = (*addr + payload);

	return start_addr;
}
#endif

static enum perfq_status batch_load_request(struct queue_context *tctx,
		    __attribute__((unused)) struct ifc_qdma_request **req_buf,
		    int nr, int do_submit, __attribute__((unused)) int force)
{
	int ret = 0;

	if (tctx->flags->flimit == REQUEST_BY_SIZE) {
		if (tctx->flags->packets <= tctx->request_counter + nr)
			nr = tctx->flags->packets - tctx->request_counter;
	}
	ret = ifc_qdma_descq_queue_batch_load(tctx->qchnl, req_buf,
			tctx->direction, nr);
	if (ret)
		tctx->failed_attempts++;

	tctx->prep_counter += nr;

	while (do_submit) {
		perf_update_ed(tctx, nr);
		ret = ifc_qdma_request_submit(tctx->qchnl, tctx->direction);
		if (!ret) {
			tctx->request_counter += tctx->prep_counter;
			tctx->prep_counter = 0ULL;
			tctx->tid_count++;
			/* need to submit requests? */
			break;
		}
		tctx->failed_attempts++;
		tctx->status = THREAD_WAITING;
		return -1;
	}
	return PERFQ_SUCCESS;
}


enum perfq_status enqueue_request(struct queue_context *tctx,
		    struct ifc_qdma_request **req_buf, int nr,
		    int do_submit, int force, int check_thread_status)
{
	struct ifc_qdma_request *req = NULL;
	int do_load_data;
#ifndef IFC_QDMA_INTF_ST
	uint64_t addr;
#endif
	unsigned long payload;
	unsigned long req_number;
	int ret = 0;
	int i = 0;

	while (i < nr) {
		/* check stopping condition */
		if (check_thread_status &&
		    !force && should_thread_stop(tctx)) {
			if (tctx->epoch_done == 0) {
				/* time limit teached */
				tctx->status = THREAD_STOP;
			}

                        if (tctx->flags->flimit == REQUEST_BY_TIME)
                                return PERFQ_SUCCESS;

                        /* Return by packets limit reached. and not epoch */
                        if ((tctx->flags->flimit == REQUEST_BY_SIZE) &&
                            (tctx->epoch_done == 0))
                                return PERFQ_SUCCESS;

		}

		req_number = tctx->request_counter + tctx->prep_counter;
		payload = get_payload_size(tctx, req_number);

#ifdef PERFQ_LOAD_DATA
		do_load_data = true;
#else
		do_load_data = false;
#endif
		if (!ret) {
			if (req_buf) {
				req = req_buf[i];
			} else {
				req =
				ifc_request_malloc(payload);
				if (!req) {
					tctx->flags->memalloc_fails = 1;
					tctx->failed_attempts++;
					printf("Try increasing Hugepage "
					       "count in ifc_libmqdma.h\n");
					tctx->status = THREAD_STOP;
                                	return PERFQ_SUCCESS;
				}
			}
		}

#ifndef IFC_QDMA_INTF_ST
		/* set the address */
		addr = avmm_addr_manager(tctx, tctx->direction == REQUEST_RECEIVE ? &tctx->src : &tctx->dest, payload);
		if (tctx->direction == REQUEST_RECEIVE)
			req->src = addr;
		else
			req->dest = addr;
#endif

		if (tctx->direction == REQUEST_TRANSMIT) {
			/* Load data if not already done */
			if (do_load_data) {
#ifdef IFC_QDMA_INTF_ST
#ifdef IFC_QDMA_META_DATA
				tctx->mdata_cur_pattern = load_data(&(req->metadata), METADATA_SIZE, tctx->mdata_cur_pattern);
#endif
				tctx->cur_pattern = load_data(req->buf, payload, tctx->cur_pattern);
#else
				load_data(req->buf, payload, 0x00);
#endif
			}

#ifdef IFC_QDMA_INTF_ST
			/* check if SOP or EOP is to be set */
			ret = set_sof_eof(tctx ,req, tctx->request_counter + tctx->prep_counter);

			/* have you seen and EOF then reset the cur_pattern */
			if (ret == PERFQ_EOF || ret == PERFQ_BOTH)
				tctx->cur_pattern = 0x00;
			tctx->mdata_cur_pattern = 0x00;
#endif
		}


		if (tctx->batch_size) {
			ret = ifc_qdma_request_prepare(tctx->qchnl,
					       tctx->direction, req);
			if (ret) {
				tctx->failed_attempts++;
				continue;
			}
			tctx->prep_counter++;

		} else {
			ret = ifc_qdma_request_start(tctx->qchnl,
						     tctx->direction, req);
			while (ret == -1) {
				if (check_thread_status &&
				    !force && should_thread_stop(tctx)) {
					if (tctx->epoch_done == 0) {
						tctx->status = THREAD_STOP;
						return PERFQ_SUCCESS;
					}
				}
				tctx->failed_attempts++;
				sched_yield();
				ret = ifc_qdma_request_submit(tctx->qchnl, tctx->direction);
			}
			tctx->nonb_tid_count++;
			tctx->request_counter++;
		}
		i++;
	}


	/* need to submit requests? */
	while (tctx->batch_size && do_submit) {
		perf_update_ed(tctx, nr);
		ret = ifc_qdma_request_submit(tctx->qchnl, tctx->direction);
		if (!ret) {
			tctx->request_counter += tctx->prep_counter;
			tctx->prep_counter = 0ULL;
			tctx->tid_count++;
			break;
		}
		tctx->failed_attempts++;
		tctx->status = THREAD_WAITING;
		return -1;
	}
	return PERFQ_SUCCESS;
}

int batch_enqueue_wrapper(struct queue_context *tctx, struct ifc_qdma_request
			  **buf, unsigned long nr, int check_thread_status)
{
	unsigned long next_push_size;
	unsigned long batch_size = tctx->batch_size;
	int buf_offset = 0;
	int rc = 0;

	/* no submit required, one chunk */
	if (batch_size - tctx->prep_counter > nr) {
		rc = enqueue_request(tctx, buf, nr, false, NO_FORCE_ENQUEUE,
				check_thread_status);
		return rc;
	}

	while ((nr + tctx->prep_counter) >= batch_size) {
		/* commands with -s option, may exit from here */
		if (check_thread_status && should_thread_stop(tctx)) {
			if (tctx->epoch_done == 0)
				tctx->status = THREAD_STOP;
			tctx->tid_count = 0;
			tctx->nonb_tid_count = 0;
			if (tctx->flags->flimit == REQUEST_BY_TIME)
				return -1;

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
			tctx->backlog = nr - next_push_size + tctx->prep_counter;
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
		rc = enqueue_request(tctx, buf ? buf + buf_offset : buf, nr,
				false, NO_FORCE_ENQUEUE, check_thread_status);
	}
	tctx->backlog = 0;
	return rc;
}

int batch_load_wrapper(struct queue_context *tctx, struct ifc_qdma_request
			  **buf, unsigned long nr)
{
	unsigned long next_push_size;
	unsigned long batch_size = tctx->batch_size;
	int buf_offset = 0;
	int rc = 0;

#ifdef IFC_ED_CONFIG_TID_UPDATE
	if (tctx->time_limit_reached) {
		uint32_t num_files = (tctx->desc_requested  + nr) / tctx->file_size;
		uint32_t diff = get_pending_files(tctx);
		if (num_files > diff) {
			/*
			 * desc_requested = 10
			 * diff = 1 (1 file give to HW, SW not submitted)
			 * nr = 127
			 * nr = 1 * 64
			 * as desc_request 10, we need to subtract 10
			 */
			nr = diff * tctx->file_size;
			nr -= tctx->desc_requested;
			tctx->desc_requested = 0;
			batch_load_request(tctx, buf, nr, true, NO_FORCE_ENQUEUE);
#ifdef DEBUG_NON_CONT_MODE
			fprintf(stderr, "TEST%u%u: reducing  hw files :%u sw:%u diff:%u num:%u"
				"  pending:%u nr %lu req:%lu rsp:%lu tsw:%lu thw:%lu\n",
				tctx->channel_id, tctx->direction, tctx->ed_files_submitted,
				tctx->sw_files_submitted, diff, num_files, tctx->pending_files, nr,
				tctx->request_counter, tctx->completion_counter,
				(tctx->total_files * tctx->file_size + nr), tctx->total_hw_files * tctx->file_size);

#endif
			return 0;
		}
	}
#endif

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
			tctx->backlog = nr - next_push_size + tctx->prep_counter;
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
		rc = batch_load_request(tctx, buf ? buf + buf_offset : buf,
					nr, false, NO_FORCE_ENQUEUE);
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

	while (qctx->request_counter != qctx->completion_counter) {
		if (should_thread_stop(qctx)) {
			if ((qctx->prep_counter)  &&
			    (qctx->flags->flimit == REQUEST_BY_SIZE) &&
			    (qctx->epoch_done == 0)) {
				ret = ifc_qdma_request_submit(qctx->qchnl, qctx->direction);
				if (!ret) {
					qctx->request_counter += qctx->prep_counter;
					qctx->prep_counter = 0ULL;
					qctx->tid_count++;
				}
			}
			qctx->cur_comp_counter = pending + count;
			return 1;
		}

		/* in case of MSIX, we busy wait on the signal */
		request_completion_poll(qctx);
		count += qctx->cur_comp_counter;
		/* accumulate */
		for (i = 0; i < qctx->cur_comp_counter; i++)
			qctx->accumulator[qctx->accumulator_index++] =
				((struct ifc_qdma_request **)(qctx->completion_buf))[i];
	}
	qctx->cur_comp_counter = pending + count;
	/* copy them back */
	for (i = 0; i < qctx->accumulator_index; i++)
		((struct ifc_qdma_request **)(qctx->completion_buf))[i] = qctx->accumulator[i];

	/* reset accumulator index for next time */
	qctx->accumulator_index = 0;

	return 0;
}

#if 0
#ifdef IFC_QDMA_INTF_ST
#ifdef IFC_QDMA_ST_PACKETGEN
static int pio_file_topup(struct ifc_qdma_device *qdev, struct queue_context *tctx)
{
	long int file_counter;
	long int last_topup;
	uint64_t ed_config;
	char *bdf = tctx->flags->bdf;
	uint64_t vf_offset = 0ULL;
	uint64_t offset;
	uint64_t files = 0ULL;
	uint64_t base;
	int vf;
	

	/* check if topup is required or not */
	last_topup = tctx->last_topup;
	file_counter = tctx->completion_counter / tctx->flags->file_size;	
	if (file_counter - last_topup < (1 << 14))
		return 0;

	/* Compute the offset based on BDF */
	base = 0x30000;
	vf = get_vf_number(bdf);
	if (vf != 0)
		vf_offset = ((vf - 1) * (8 * IFC_QDMA_PER_VF_CHNLS)) +
			    (8 * IFC_QDMA_PER_PF_CHNLS);
	offset = vf_offset + (8 * tctx->channel_id);

	/* Update the file counters in HW */
	ed_config = ifc_qdma_pio_read64(qdev, base + offset);
	files = (1ULL << 17) - 1; 
	ed_config |= (files << 32);
	ifc_qdma_pio_write64(qdev, base + offset, ed_config);
	tctx->last_topup = tctx->completion_counter;
	return 0;
}
#endif
#endif
#endif

#ifndef IFC_QDMA_ST_MULTI_PORT
#ifdef IFC_QDMA_ST_PACKETGEN
#ifndef IFC_ED_CONFIG_TID_UPDATE
static int checkpoint(struct queue_context *qctx)
{
#if 0
	unsigned long remainder;
	unsigned long submit_size;
#endif

	uint64_t base;
	uint64_t offset;
	uint64_t ed_config = 0ULL;

	/* Get the offset */
	base = 0x30000;
	offset = pio_queue_config_offset(qctx);

	/* Disable the channel in ED logic */
#ifdef IFC_32BIT_SUPPORT
	uint64_t ed_config1 = 0ULL;
	ed_config = ifc_qdma_pio_read32(qctx->qdev, base + offset);
	ed_config1 = ifc_qdma_pio_read32(qctx->qdev, (base + offset)+4);
        ed_config1= ed_config1 << 32;
        ed_config |= ed_config1;
	
#else
	ed_config = ifc_qdma_pio_read64(qctx->qdev, base + offset);
#endif
	ed_config &= ~(1ULL << (uint64_t)PIO_REG_PORT_PKT_CONFIG_ENABLE_SHIFT);

#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_pio_write32(qctx->qdev, base + offset, ed_config);
	ifc_qdma_pio_write32(qctx->qdev, base + offset+4 , ed_config>>32);
#else
	ifc_qdma_pio_write64(qctx->qdev, base + offset, ed_config);
#endif

#if 0
	/* Just submit the requests that will get us to the PREFILL_QDEPTH */
	remainder = qctx->request_counter % PREFILL_QDEPTH;

	if (remainder == 0) {
		return 0;
	}

	/* Compute the actual number of descriptors to submit */
	submit_size = PREFILL_QDEPTH - remainder;
	submit_size -= qctx->prep_counter;
	enqueue_request(qctx, NULL, submit_size, true, FORCE_ENQUEUE);
#endif

	return 0;
}
#endif //IFC_ED_CONFIG_TID_UPDATE
#endif
#endif

static int submit_pending_reqs(struct queue_context *qctx)
{
	int ret;

	/* Submit already prepared requests */
	if (qctx->prep_counter) {
		ret = ifc_qdma_request_submit(qctx->qchnl, qctx->direction);
		if (!ret) {
			qctx->request_counter += qctx->prep_counter;
			qctx->prep_counter = 0ULL;
			qctx->tid_count++;
		} else {
			qctx->failed_attempts++;
			qctx->status = THREAD_WAITING;
			return -1;
		}
	}

	return 0;
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
			if (tctx->batch_size)
				batch_enqueue_wrapper(tctx, NULL,
						PREFILL_QDEPTH, true);
			else
				enqueue_request(tctx, NULL,
						PREFILL_QDEPTH, true,
						FORCE_ENQUEUE, true);
			prefill_lock = tctx->direction == REQUEST_RECEIVE ?
					tx_lock : rx_lock;
			sem_post(prefill_lock);
		} else {
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
					tctx->state = IFC_QDMA_QUE_WAIT_FOR_COMP;
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
						      tctx->completion_buf : NULL,
						      tctx->cur_comp_counter,
						      true);
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
						      tctx->cur_comp_counter,
						      true);
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
					tctx->state = IFC_QDMA_QUE_WAIT_FOR_COMP;
					return NULL;
				}
				tctx->status = THREAD_DEAD;
				return NULL;
			}
		}
	}
	tctx->status = THREAD_ERROR_STATE;
	return NULL;
}

static int check_for_backlog(struct queue_context *tctx)
{
	int ret;
	struct ifc_qdma_request **buf = tctx->completion_buf;
	if (tctx->status == THREAD_WAITING) {
		/* backlog */
		if (tctx->backlog <= 0)
			return 0;

		ret = ifc_qdma_request_submit(tctx->qchnl, tctx->direction);
		if (ret)
			return -1;

		tctx->request_counter += tctx->prep_counter;
		tctx->backlog -= tctx->prep_counter;
		tctx->prep_counter = 0ULL;
		tctx->tid_count++;
		if (tctx->backlog) {
#ifdef PERFQ_PERF
			ret = batch_load_wrapper(tctx, buf + tctx->comp_buf_offset,
					   tctx->backlog);
#else
			ret = batch_enqueue_wrapper(tctx, buf + tctx->comp_buf_offset,
					      tctx->backlog, true);
#endif
			if (ret)
				return -1;
		}
	}
	return 0;
}

static void pkt_gen_config(struct queue_context *tctx __attribute__((unused)))
{
#ifndef IFC_QDMA_ST_MULTI_PORT
#ifdef IFC_QDMA_ST_PACKETGEN
#ifndef IFC_ED_CONFIG_TID_UPDATE
	/* Initialize ED */
	pio_init(tctx->qdev, tctx);
#else
	pio_non_continuous_mode_init(tctx);
#endif ////IFC_ED_CONFIG_TID_UPDATE
#endif
#endif
	return;
}

static int queue_cleanup(struct queue_context *tctx)
{
	int ret;
	if (tctx->epoch_done == 1)
		return 0;

	/* For -s, submit pending requests */
	if (tctx->flags->flimit == REQUEST_BY_SIZE) {
		ret = submit_pending_reqs(tctx);
		if (ret == -1)
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

#ifdef IFC_ED_CONFIG_TID_UPDATE
static void pio_non_continuous_mode_init(struct queue_context *tctx)
{
	uint64_t offset, val, hwf;

	if (tctx->direction == 1) {
		pio_init(tctx->qdev, tctx);
	} else {
		offset = pio_queue_config_offset(tctx);
		val = ifc_qdma_pio_read64(tctx->qdev, 0x30000 + offset);
		hwf = (val & 0xffff00000000) >> 32;

		pio_update_files(tctx->qdev, tctx, hwf + DEFAULT_START_FILES);
		tctx->ed_files_submitted = hwf + DEFAULT_START_FILES;
		tctx->total_hw_files = hwf + DEFAULT_START_FILES;
		tctx->sw_files_submitted = hwf;
#ifdef DEBUG_NON_CONT_MODE
		fprintf(stderr,"TEST%u%u ed_files %u sw_files %u val 0x%lx hwf 0x%lx offset 0x%lx\n",
			tctx->channel_id, tctx->direction, tctx->ed_files_submitted,
			tctx->sw_files_submitted, val, hwf, offset);
#endif
	}
}
#endif

void *transfer_handler(void *ptr)
{
	struct queue_context *tctx = (struct queue_context *)ptr;
	int ret;
re_init:
	if (tctx->init_done == 0) {
#if 0
		/* fill up the ring first */
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
				ret = ifc_qdma_request_submit(tctx->qchnl,
						tctx->direction);
				if (!ret) {
					tctx->request_counter += tctx->prep_counter;
					tctx->prep_counter = 0ULL;
					tctx->tid_count++;
				}
			}
			tctx->tid_count = 0;
			tctx->nonb_tid_count = 0;
			tctx->status = THREAD_DEAD;
			pkt_gen_config(tctx);
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
		tctx->nonb_tid_count = 0;

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
			if (tctx->epoch_done == 1)
				return NULL;

			/* For -s, submit pending requests */
			if (tctx->flags->flimit == REQUEST_BY_SIZE) {
				ret = submit_pending_reqs(tctx);
				if (ret == -1)
					return NULL;
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
			return NULL;
		}

		if(PERFQ_TASK_FAILURE == request_completion_poll(tctx)){
			tctx->init_done = 0;
			goto re_init;
		}

#if 0
#ifdef IFC_QDMA_ST_PACKETGEN
		if (tctx->flags->flimit == REQUEST_BY_TIME &&
		    tctx->direction == REQUEST_RECEIVE)
			pio_file_topup(qctx->qdev, tctx);
#endif
#endif

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
						      tctx->cur_comp_counter,
						      true, NO_FORCE_ENQUEUE,
						      true);
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
	tctx->status = THREAD_ERROR_STATE;
	return NULL;
}

enum perfq_status request_completion_poll(struct queue_context *tctx)
{
	struct ifc_qdma_channel *chnl = NULL;
	int ret;
	int dir;

	if (global_flags->completion_mode == CONFIG_QDMA_QUEUE_MSIX) {
		ret = ifc_qdma_poll_wait(tctx->poll_ctx, 1, &chnl, &dir);
		if (ret <= 0) {
			tctx->cur_comp_counter = 0;
			return PERFQ_CMPLTN_NTFN_FAILURE;
		}
	}

	ret = ifc_qdma_completion_poll
				(tctx->qchnl,
				 tctx->direction,
				 tctx->completion_buf,
				 QDEPTH);
	if(ret == -1){
		tctx->completion_counter = tctx->cur_comp_counter = tctx->request_counter;

		if(tctx->direction == REQUEST_TRANSMIT){
			tctx->qchnl->tx.tail = 0;
			tctx->qchnl->tx.tid_updates = 0;
			tctx->qchnl->tx.processed_tail = 0;
			tctx->qchnl->tx.consumed_head = 0;
			tctx->qchnl->tx.head = 0;
			tctx->qchnl->tx.last_head = 0;
			tctx->qchnl->tx.didx = 0;
			tctx->qchnl->tx.processed_head = 0;
			return PERFQ_TASK_FAILURE;
		}
		else {
			tctx->qchnl->rx.tail = 0;
			tctx->qchnl->rx.tid_updates = 0;
			tctx->qchnl->rx.processed_tail = 0;
			tctx->qchnl->rx.consumed_head = 0;
			tctx->qchnl->rx.head = 0;
			tctx->qchnl->rx.last_head = 0;
			tctx->qchnl->rx.didx = 0;
			tctx->qchnl->rx.processed_head = 0;
			return PERFQ_TASK_FAILURE;
		}
	}
	else{
		tctx->cur_comp_counter = ret;
		tctx->completion_counter += tctx->cur_comp_counter;

		/* check data if required */
		if ((tctx->flags->fvalidation) && (tctx->direction == REQUEST_RECEIVE))
			post_processing(tctx);
	}

	return PERFQ_SUCCESS;
}

/*This function used to create a data pattern or tag for avst pkt gen*/
int set_avst_pattern_data(struct queue_context *tctx)
{
	/*
 	*Function: 16 bit, vfnum, vf active, pfnum
 	*Channel : 11 Bit
 	*data :Remaining.
 	* switch off : old test case (Increamental DW  data pattern),
 	* switch on  : programmable DW data pattern
 	*default switch state  is off (0);
 	**/

        int channel_index_offset = 0;
        uint32_t data = AVST_PATTERN_DATA;

        channel_index_offset = tctx->channel_id * PATTERN_CHANNEL_MULTIPLIER;

        tctx->data_tag = tctx->flags->portid << PORT_TOTAL_WIDTH | tctx->channel_id << CHANNEL_TOTAL_WIDTH | data;
#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_pio_write32(tctx->qdev,  PATTERN_CHANNEL_BASE + channel_index_offset, tctx->data_tag);
#else
	ifc_qdma_pio_write64(tctx->qdev,  PATTERN_CHANNEL_BASE + channel_index_offset, tctx->data_tag);
#endif

        tctx->expected_pattern = tctx->data_tag | data;

        return 0;
}


#ifdef IFC_QDMA_ST_LOOPBACK
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
                                                      tctx->cur_comp_counter,
						      false);
                        }
                }
        }
}
#endif

