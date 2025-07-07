// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#include <limits.h>
#include <fcntl.h>
#include "perfq_app.h"

static int is_stuck(struct queue_context *qctx)
{
	if (qctx->status == THREAD_DEAD || qctx->status == THREAD_ERROR_STATE)
		return false;

	/* Anything got completed? */
	if (qctx->completion_counter > qctx->prev_comp_counter)
		return false;

	/* Submitted any new batch? */
	if (qctx->ovrall_tid > qctx->prev_tid)
		return false;

	qctx->stuck_count++;
	return true;
}

void time_computations(struct struct_time *t, struct queue_context *tctx,
		       struct struct_flags *flags,
		       struct timespec *last_checked,
		       struct timespec *cur_time)
{

	struct timespec ovrall_diff;
	struct timespec intrvl_diff;
	double intrvl_tm_diff;
	double ovrall_tm_diff;
	uint64_t completion_counter = 0;

	if (tctx->end_completion_counter)
		completion_counter = tctx->end_completion_counter;
	else
		completion_counter = tctx->completion_counter;

	/* get elapsed time */
	time_diff(cur_time, &(tctx->start_time), &ovrall_diff);

#ifndef IFC_QDMA_ST_PACKETGEN
#ifdef IFC_32BIT_SUPPORT
	if (global_flags->flimit == REQUEST_BY_TIME &&
		ovrall_diff.tv_sec > (int)global_flags->time_limit)
#else
	if (global_flags->flimit == REQUEST_BY_TIME &&
		ovrall_diff.tv_sec > global_flags->time_limit)
#endif
	ovrall_diff.tv_sec = global_flags->time_limit;
#endif
	time_diff(cur_time, last_checked, &intrvl_diff);

	/* for BW computation */
	ovrall_tm_diff = ovrall_diff.tv_sec + ((double)ovrall_diff.tv_nsec / 1e9);
	intrvl_tm_diff = intrvl_diff.tv_sec + ((double)intrvl_diff.tv_nsec / 1e9);

	/* for displaying purpose */
	t->timediff_sec = ovrall_diff.tv_sec;
	t->timediff_msec = ovrall_diff.tv_nsec / 1e6;

	/* don't want to hit divide by zero exception */
	if (ovrall_tm_diff <= 0.0) {
		t->ovrall_bw = 0.0;
		t->ovrall_mpps = 0.0;
	} else {
		t->ovrall_bw = (completion_counter / ovrall_tm_diff)
				* flags->packet_size;
		t->ovrall_mpps = completion_counter / ovrall_tm_diff;
	}

	if (intrvl_tm_diff <= 0.0) {
		t->intrvl_bw = 0.0;
		t->intrvl_mpps = 0.0;
	} else {
		t->intrvl_bw = ((tctx->completion_counter -
				 tctx->prev_comp_counter) / intrvl_tm_diff)
				* flags->packet_size;
		t->intrvl_mpps = tctx->completion_counter - tctx->prev_comp_counter;
		t->intrvl_mpps = t->intrvl_mpps / intrvl_tm_diff;
	}

	/* convert bandwidth from BPS to GBPS */
	t->ovrall_bw = (t->ovrall_bw) / (1<<30);
	t->intrvl_bw = (t->intrvl_bw) / (1<<30);
	t->ovrall_mpps = (t->ovrall_mpps) / (1e6);
	t->intrvl_mpps = (t->intrvl_mpps) / (1e6);

	t->timediff_min = t->timediff_sec / 60;
	t->timediff_sec = t->timediff_sec - (t->timediff_min * 60);
	t->cpercentage = (completion_counter / (double)tctx->request_counter)
			 * 100;
}


int show_header(struct struct_flags *flags)
{
#ifndef IFC_QDMA_INTF_ST
#ifdef VERIFY_FUNC
	struct thread_context *tctx;
	int i;
#endif
#endif
	uint32_t num_of_pages = IFC_CONFIG_QDMA_NUM_DESC_PAGES_PER_QUEUE;
	int comp_mode = flags->completion_mode;;

	printf("\n----------------------------------------------------------------------\n");
	printf("BDF: %s\n", flags->bdf);
	printf("Channels Allocated: %d\n", flags->chnls);
#if ((!defined(HW_FIFO_ENABLED)) && defined(TID_FIFO_ENABLED))
	printf("HEAD Reg tracking enabled\n");
#endif
#if defined(HW_FIFO_ENABLED) && defined(TID_FIFO_ENABLED)
	printf("TID FIFO checks enabled\n");
#endif
#if defined(WB_CHK_TID_UPDATE)
	printf("WB tracking enabled\n");
#endif
	printf("QDepth %u\n", QDEPTH);
	printf("Number of pages: %u\n", num_of_pages);
	if (comp_mode == CONFIG_QDMA_QUEUE_WB)
		printf("Completion mode: WB\n");
	else if (comp_mode == CONFIG_QDMA_QUEUE_REG)
		printf("Completion mode: REG\n");
	else if (comp_mode == CONFIG_QDMA_QUEUE_MSIX)
		printf("Completion mode: MSIX\n");

	printf("Payload Size per descriptor: %zu Bytes\n", flags->packet_size);
	if (flags->flimit == REQUEST_BY_SIZE)
		printf("#Descriptors per channel: %lu\n", flags->packets);
#ifdef IFC_QDMA_INTF_ST
	printf("SOF on descriptor: %d\n", 1);
	printf("EOF on descriptor: %lu\n", flags->file_size);
	printf("File Size: %lu Bytes\n", flags->file_size * flags->packet_size);
#ifdef IFC_QDMA_ST_PACKETGEN
	printf("PKG Gen Files: %u\n", flags->pkt_gen_files);
#endif
#endif
	if (flags->direction != REQUEST_RECEIVE)
		printf("Tx Batch Size: %lu Descriptors\n", flags->tx_batch_size);
	if (flags->direction != REQUEST_TRANSMIT)
		printf("Rx Batch Size: %lu Descriptors\n", flags->rx_batch_size);

#ifdef IFC_QDMA_DYN_CHAN
	printf("DCA: ON\n");
#else
	printf("DCA: OFF\n");
#endif
	printf("-----------------------------------------------------------------------\n");
#ifndef IFC_QDMA_INTF_ST
#ifdef VERIFY_FUNC
	printf("AVMM Memory Region Allocation Details:\n");
	printf("\n%-11s%-11s%s\n", "chnl_D", "Start", "End");
	for (i = 0; i < count_threads(flags); i++) {
		tctx = &(global_tctx[i]);
		printf("%d%-10s0x%-9lx0x%lx\n", tctx->channel_id, tctx->direction ? "Tx" : "Rx", tctx->base, tctx->limit - 1);
	}
	printf("----------------------------------------------------------------------------------------------------------------------------\n");
#endif
#endif
	return 0;
}

int show_progress(struct thread_context *tctx, struct struct_flags *flags,
		  struct timespec *last_checked,
		  struct timespec *start_time)
{
	double cmlt_ovrall_bw = 0.0;
	double cmlt_intrvl_bw = 0.0;
	struct timespec cur_time;
	struct queue_context *qctx;
	struct struct_time t;
	char direction[3] = { 0 };
	int threads_count;
	int i, q;


	FILE *fptr;
	double tx_tbw = 0.0;
	double rx_tbw = 0.0;
	double tx_ibw = 0.0;
	double rx_ibw = 0.0;
	double tx_mean = 0.0;
	double rx_mean = 0.0;
	double tx_hbw  = INT_MIN;
	double rx_hbw = INT_MIN;
	double tx_lbw = INT_MAX;
	double rx_lbw = INT_MAX;
	int tx_stuck = 0;
	int rx_stuck = 0;
	int tx_counter = 0;
	int rx_counter = 0;
	double tx_btrnsfrd = 0.0;
	double rx_btrnsfrd = 0.0;

	struct timespec global_diff;
	long gdiff_min, gdiff_sec, gdiff_msec;

	double tx_mpps = 0.0;
	double rx_mpps = 0.0;


	fptr = ifc_qdma_fopen(flags->fname, "a");
	if (fptr == NULL) {
		printf("Failed to open file: %s\n", flags->fname);
		return -1;
	}
	memset(&t, 0, sizeof(struct struct_time));

	threads_count = flags->num_threads;

	fprintf(fptr, "%s\n", "----------------------------------------------------------------------------------------------------------------------------");
	fprintf(fptr, "%-8s%-14s%-14s%-15s%-12s%-11s%-9s%-11s%-11s%s\n",
		"Chnl_d", "Req_sent", "Req_comp", "Time_elpsd",
		"Cmp%", "TBW", "IBW", "Cml_TBW", "Cml_IBW", "MPPS");

	clock_gettime(CLOCK_MONOTONIC, &cur_time);

	for (i = 0; i < threads_count; i++) {
		for (q = 0; q < tctx[i].qcnt; q++) {
			qctx = &(tctx[i].qctx[q]);

			/* perform time computations */
			time_computations(&t, qctx, flags, last_checked, &cur_time);

			cmlt_ovrall_bw += t.ovrall_bw;
			cmlt_intrvl_bw += t.intrvl_bw;

			/* Make direction value more human readable */
			if (qctx->direction == REQUEST_TRANSMIT) {
				ifc_qdma_strncpy(direction, sizeof(direction), "Tx", 2);
				tx_tbw += t.ovrall_bw;
				tx_ibw += t.intrvl_bw;
				tx_btrnsfrd += (qctx->completion_counter * flags->packet_size);
				if (tx_hbw < t.intrvl_bw)
					tx_hbw = t.intrvl_bw;
				if (tx_lbw > t.intrvl_bw)
					tx_lbw = t.intrvl_bw;
				if (is_stuck(qctx))
					tx_stuck++;
				tx_counter++;
				tx_mpps += t.intrvl_mpps;
			}
			else {
				ifc_qdma_strncpy(direction, sizeof(direction), "Rx", 2);
				rx_tbw += t.ovrall_bw;
				rx_ibw += t.intrvl_bw;
				rx_btrnsfrd += (qctx->completion_counter * flags->packet_size);
				if (rx_hbw < t.intrvl_bw)
					rx_hbw = t.intrvl_bw;
				if (rx_lbw > t.intrvl_bw)
					rx_lbw = t.intrvl_bw;
				if (is_stuck(qctx))
					rx_stuck++;
				rx_counter++;
				rx_mpps += t.intrvl_mpps;
			}

			/* stats format
			 * channelid_direction #requests_completed
			 * #failed_attemps time_elapsed(mm:ss:mm)
			 * completion% overall_bandwidth interval_bandwith
			 * cumulative_overall_bw cumulative_interval_bw
			 * #good_packets #bad_packets
			 */

			fprintf(fptr, "%2d%-6s%-14lu%-14lu%02ld:%02ld:%03ld     %-10.2f%05.2fGBPS  %05.2fGBPS  %05.2fGBPS  %05.2fGBPS  %05.2fMPPS\n",
					qctx->channel_id, direction,
					qctx->request_counter,
					qctx->completion_counter,
					t.timediff_min,
					t.timediff_sec, t.timediff_msec, t.cpercentage,
					t.ovrall_bw, t.intrvl_bw, cmlt_ovrall_bw,
					cmlt_intrvl_bw, t.intrvl_mpps);

			qctx->prev_comp_counter = qctx->completion_counter;
			qctx->prev_tid = qctx->ovrall_tid;
			qctx->prev_nonb_tid = qctx->ovrall_nonb_tid;
		}
	}

	fprintf(fptr, "%s\n", "----------------------------------------------------------------------------------------------------------------------------");

	if (tx_counter)
		tx_mean = tx_ibw / tx_counter;

	if (rx_counter)
		rx_mean = rx_ibw / rx_counter;

	time_diff(&cur_time, start_time, &global_diff);
	gdiff_sec = global_diff.tv_sec;
	gdiff_msec = global_diff.tv_nsec / 1e6;
	gdiff_min = gdiff_sec / 60;
	gdiff_sec = gdiff_sec - (gdiff_min * 60);

	printf("-------------------------------------------------------------------------------------------------------------------\n");
	printf("%-5s%-10s%-15s%-15s%-11s%-11s%-11s%-11s%-10s%-10s%s\n",
		"Dir", "#queues", "Time_elpsd", "B_trnsfrd", "TBW", "IBW", "MIBW", "HIBW", "LIBW", "MPPS", "#stuck");
	if (flags->direction != REQUEST_RECEIVE)
		printf("%-8s%-8d%02ld:%02ld:%03ld     %8.2fKB  %05.2fGBPS  %05.2fGBPS  %05.2fGBPS  %05.2fGBPS  %05.2fGBPS %05.2fMPPS %5d\n",
			"Tx", tx_counter, gdiff_min, gdiff_sec,
			gdiff_msec, tx_btrnsfrd / (1 << 10), tx_tbw,
			tx_ibw, tx_mean, tx_hbw, tx_lbw, tx_mpps, tx_stuck);

	if (flags->direction != REQUEST_TRANSMIT)
		printf("%-8s%-8d%02ld:%02ld:%03ld     %8.2fKB  %05.2fGBPS  %05.2fGBPS  %05.2fGBPS  %05.2fGBPS  %05.2fGBPS %05.2fMPPS %5d\n",
			"Rx", rx_counter, gdiff_min, gdiff_sec,
			gdiff_msec, rx_btrnsfrd / (1 << 10), rx_tbw,
			rx_ibw, rx_mean, rx_hbw, rx_lbw, rx_mpps, rx_stuck);
	printf("-------------------------------------------------------------------------------------------------------------------\n");
#ifdef VERIFY_HOL
	hol_config(flags, tctx, t.timediff_sec);
	printf("----------------------------------------------------------------------------------------------------------------------------\n");
#endif
	fclose(fptr);
	return 0;
}


int test_evaluation(struct queue_context *tctx, struct struct_flags *flags, struct task_stat *stat)
{
	/* all requests served? */
	if (tctx->request_counter != tctx->completion_counter) {
		if ((flags->direction == REQUEST_LOOPBACK ||
		    flags->direction == REQUEST_BOTH) &&
		    (flags->flimit == REQUEST_BY_TIME)) {
			if (tctx->request_counter - tctx->completion_counter > QDEPTH)
				stat->completion_status = false;
		} else {
#ifdef IFC_QDMA_INTF_ST
#ifdef IFC_QDMA_ST_PACKETGEN
			if (tctx->direction == REQUEST_RECEIVE &&
			    ((tctx->request_counter - tctx->completion_counter) <= QDEPTH))
				stat->completion_status = true;
			else
#endif
#endif
			stat->completion_status = false;
		}
	}
	if (tctx->completion_counter == 0)
		stat->completion_status = false;


#ifdef IFC_QDMA_INTF_ST
	if (flags->fvalidation) {
		if (tctx->bfile_counter > 0ULL)
			stat->gfile_status = false;
	}
#endif
	if (flags->fvalidation && (tctx->direction == REQUEST_RECEIVE)) {
		if (tctx->gpckt_counter != tctx->completion_counter)
			stat->gpckt_status = false;
#ifdef IFC_QDMA_META_DATA
		if (tctx->mdata_bpckt_counter)
			stat->gpckt_status = false;
#endif
	}

#if 0
	if (tctx->stuck_count && tctx->flags->flimit == REQUEST_BY_TIME)
		return false;
#endif

	return (stat->gpckt_status & stat->gfile_status & stat->completion_status);
}

enum perfq_status
show_summary(struct thread_context *tctx, struct struct_flags *flags,
		  struct timespec *last_checked,
		  struct timespec *start_time)
{
	struct task_stat ovrall_stat;
	struct task_stat dir_stat;
	struct queue_context *qctx;
	struct timespec stop_time;
	struct timespec cur_time;
	struct struct_time t;
	double total_ovrall_bw = 0.0;
	double total_ovrall_mpps = 0.0;
	double total_tx_bw = 0.0;
	double total_rx_bw = 0.0;
	int threads_count;
	char direction[3] = { 0 };
	int i, j, q;

	FILE *fptr;
	int tx_counter = 0;
	int rx_counter = 0;
	int tx_passed = 0;
	int rx_passed = 0;
	int tx_failed = 0;
	int rx_failed = 0;
	double tx_btrnsfrd = 0.0;
	double rx_btrnsfrd = 0.0;
	char tx_bstrnsfrd_strng[20];
	char rx_bstrnsfrd_strng[20];
	double tx_mpps = 0.0;
	double rx_mpps = 0.0;
	char tx_mpps_strng[20];
	char rx_mpps_strng[20];
	int total_rx_data_drop = 0;
	int total_tx_data_drop = 0;

	struct timespec global_diff;
	long gdiff_min, gdiff_sec, gdiff_msec;

#ifdef PERFQ_DATA
	char data_to_write[50];
	char file_name[50] = PERFQ_FILE;
#endif
#ifdef IFC_QDMA_ST_PACKETGEN
#ifdef IFC_QDMA_ST_MULTI_PORT
	uint64_t addr;
#else
	uint64_t addr;
	uint64_t vf_offset = 0ULL;
	uint64_t status;
	int pckt_count;
	int pcktlen_err;
	int pcktdata_err;
	int pckt_error;
#endif // multi port
#endif // Pkt gen

	double tx_pass_percent = 0.0;
	double rx_pass_percent = 0.0;

	memset(&t, 0, sizeof(t));


	fptr = ifc_qdma_fopen(flags->fname, "a");
	if (fptr == NULL) {
		printf("Failed to open file: %s\n", flags->fname);
		return -1;
	}

	threads_count = flags->num_threads;
	memset(&ovrall_stat, 1, sizeof(struct task_stat));
	clock_gettime(CLOCK_MONOTONIC, &cur_time);

	fprintf(fptr, "-------------------------------------------------------OUTPUT SUMMARY---------------------------------------------------------------\n");
	for (i = 0; i < threads_count; i++) {
		for (q = 0; q < tctx[i].qcnt; q++) {
			qctx = &(tctx[i].qctx[q]);
			/* perform time computations */
			if (qctx->status == THREAD_DEAD &&
				flags->flimit == REQUEST_BY_TIME) {
				stop_time = qctx->end_time;
			}
			else
				clock_gettime(CLOCK_MONOTONIC, &stop_time);
			time_computations(&t, qctx, flags, last_checked, &stop_time);
			memset(&dir_stat, 1, sizeof(struct task_stat));
			usleep(100);

			if(qctx->direction == REQUEST_TRANSMIT) {
				qctx->tx_data_drop = ifc_mcdma_get_drop_count(qctx->qchnl, qctx->direction);
				total_tx_data_drop += qctx->rx_data_drop;
			} else {
				qctx->rx_data_drop = ifc_mcdma_get_drop_count(qctx->qchnl, qctx->direction);	
				total_rx_data_drop += qctx->tx_data_drop;
			}

#ifdef IFC_QDMA_ST_PACKETGEN
#ifdef IFC_QDMA_ST_MULTI_PORT
			if (flags->fvalidation && flags->direction != REQUEST_LOOPBACK
					&& qctx->direction == REQUEST_TRANSMIT) {
				addr = PIO_BASE + ((1 << 8)  + ((1 << 8) * qctx->channel_id));
				qctx->gfile_counter =  ifc_qdma_pio_read64(global_qdev, addr);

				addr += 8;
				qctx->bfile_counter = ifc_qdma_pio_read64(global_qdev, addr);
			}
#else
			vf_offset = pio_queue_config_offset(qctx);
			/* Read the good & bad packet counter from FPGA */
			if (flags->fvalidation && flags->direction != REQUEST_LOOPBACK
					&& qctx->direction == REQUEST_TRANSMIT) {
				usleep(5000);
				addr = 0x20000 + vf_offset;

#ifdef IFC_32BIT_SUPPORT
                                status =  ifc_qdma_pio_read32(global_qdev, addr);
                                pckt_count = (status >> PIO_REG_PORT_STATS_PKT_COUNT_SHIFT_32BIT_SUPPORT) & ((1 << PIO_REG_PORT_STATS_PKT_COUNT_SHIFT_32BIT_SUPPORT) - 1);
                                pcktdata_err = (status >> PIO_REG_PORT_STATS_PKT_DATA_PATTERN_ERROR_BIT_32BIT_SUPPORT) & 1;
                                pcktlen_err = (status >> PIO_REG_PORT_STATS_PKT_LEN_ERROR_BIT_32BIT_SUPPORT) & 1;
                                pckt_error = pcktlen_err | pcktdata_err;
#else
                                status =  ifc_qdma_pio_read64(global_qdev, addr);
                                pckt_count = (status >> PIO_REG_PORT_STATS_PKT_COUNT_SHIFT) & ((1 << PIO_REG_PORT_STATS_PKT_COUNT_SHIFT_WIDTH) - 1);
                                pcktdata_err = (status >> PIO_REG_PORT_STATS_PKT_DATA_PATTERN_ERROR_BIT) & 1;
                                pcktlen_err = (status >> PIO_REG_PORT_STATS_PKT_LEN_ERROR_BIT) & 1;
                                pckt_error = pcktlen_err | pcktdata_err;
#endif
				if (pckt_error)
					qctx->bfile_counter = pckt_count;
				else
					qctx->gfile_counter = pckt_count;
			}
#if 0 
			if (flags->fvalidation && flags->direction != REQUEST_LOOPBACK
					&& qctx->direction == REQUEST_RECEIVE) {
				addr = 0x30000 + vf_offset + (8 * qctx->channel_id);
			}
#endif /* Unused */
#endif
#endif
			total_ovrall_bw += t.ovrall_bw;
			total_ovrall_mpps += t.ovrall_mpps;
			qctx->transfer_status = test_evaluation(qctx, flags, &dir_stat);

			if (qctx->direction == REQUEST_TRANSMIT) {
				ifc_qdma_strncpy(direction, sizeof(direction), "Tx", 2);
				total_tx_bw += t.ovrall_bw;
				tx_btrnsfrd += (qctx->completion_counter * flags->packet_size);
				tx_mpps += t.ovrall_mpps;
				tx_passed += qctx->transfer_status ? 1 : 0;
				tx_counter++;
			}
			else {
				ifc_qdma_strncpy(direction, sizeof(direction), "Rx", 2);
				total_rx_bw += t.ovrall_bw;
				rx_mpps += t.ovrall_mpps;
				rx_btrnsfrd += (qctx->completion_counter * flags->packet_size);
				rx_passed += qctx->transfer_status ? 1 : 0;
				rx_counter++;
			}

			fprintf(fptr, "Ch_ID: %d %s,", qctx->channel_id, direction);
			fprintf(fptr, "Req: %lu,", qctx->request_counter);
			fprintf(fptr, "Rsp: %lu,", qctx->completion_counter);
#ifdef IFC_32BIT_SUPPORT
			fprintf(fptr, "Bytes Transferred: %lu,", qctx->completion_counter * flags->packet_size);
#else
			fprintf(fptr, "Bytes Transferred: %zu,", qctx->completion_counter * flags->packet_size);
#endif
			fprintf(fptr, "Time: %02ld:%02ld:%03ld,", t.timediff_min, t.timediff_sec, t.timediff_msec);
			fprintf(fptr, "MPPS: %.4fMPPS ", t.ovrall_mpps);
			fprintf(fptr, "Status - %s\n", qctx->transfer_status ? "PASS" : "FAIL");
			if (flags->fvalidation && qctx->direction == REQUEST_RECEIVE) {
				fprintf(fptr, "Good Descriptors: %lu,", qctx->gpckt_counter);
				fprintf(fptr, "Bad Descriptors: %lu,", qctx->bpckt_counter);
			}
#ifdef IFC_QDMA_INTF_ST
			if (flags->fvalidation && ((qctx->direction == REQUEST_TRANSMIT
							&& flags->direction != REQUEST_LOOPBACK)
						|| (qctx->direction == REQUEST_RECEIVE))) {
				fprintf(fptr, "Good Files: %lu,", qctx->gfile_counter);
				fprintf(fptr, "Bad Files: %lu\n", qctx->bfile_counter);
#ifdef IFC_QDMA_META_DATA
				fprintf(fptr, "Metadata Fails: %lu\n", qctx->mdata_bpckt_counter);
#endif
			}
#endif
#ifdef VERIFY_FUNC
			fprintf(fptr, "Prepared Count: %lu ", qctx->prep_counter);
			fprintf(fptr, "Compl touts: %lu ", qctx->tout_err_cnt);
#ifdef IFC_QDMA_INTF_ST
			fprintf(fptr, "HOL Seen: %lu\n", qctx->failed_attempts);
#endif
#endif
#ifndef PERFQ_LOAD_DATA
			if (!flags->fvalidation)
				fprintf(fptr, "Bandwidth: %.4fGBPS ", t.ovrall_bw);
#endif
			fprintf(fptr, "\n");

			ovrall_stat.completion_status &= dir_stat.completion_status;
			ovrall_stat.gfile_status &= dir_stat.gfile_status;
			ovrall_stat.gpckt_status &= dir_stat.gpckt_status;
		}
	}
	fprintf(fptr, "------------------------------------------------------------------------------------------\n");
	printf("-------------------------------------OUTPUT SUMMARY-------------------------------------------------\n");
	tx_failed = tx_counter - tx_passed;
	rx_failed = rx_counter - rx_passed;
	snprintf(tx_bstrnsfrd_strng, sizeof(tx_bstrnsfrd_strng), "%.2f%s", tx_btrnsfrd / (1 << 10), "KB");
	snprintf(rx_bstrnsfrd_strng, sizeof(rx_bstrnsfrd_strng), "%.2f%s", rx_btrnsfrd / (1 << 10), "KB");
	snprintf(tx_mpps_strng, sizeof(tx_mpps_strng), "%.2f%s", tx_mpps / (1 << 10), "KB");
	snprintf(rx_mpps_strng, sizeof(rx_mpps_strng), "%.2f%s", rx_mpps / (1 << 10), "KB");

	time_diff(&cur_time, start_time, &global_diff);
	gdiff_sec = global_diff.tv_sec;
	gdiff_msec = global_diff.tv_nsec / 1e6;
	gdiff_min = gdiff_sec / 60;
	gdiff_sec = gdiff_sec - (gdiff_min * 60);

	if (tx_counter)
		tx_pass_percent = ((double)(tx_passed * 100)) / tx_counter;
	if (rx_counter)
		rx_pass_percent = ((double)(rx_passed * 100)) / rx_counter;

	printf("%-5s%-10s%-15s%-20s%-8s%-8s%-12s%-8s%-8s%s\n",
		"Dir", "#queues", "Time_elpsd", "B_trnsfrd", "TBW", "MPPS", "d_drop_cnt", "Passed", "Failed", "\%passed");
	if (flags->direction != REQUEST_RECEIVE)
		printf("%-8s%-8d%02ld:%02ld:%03ld     %-15s  %05.2fGBPS %05.2fMPPS %8d %6d%7d %10.2f%%\n",
			"Tx", tx_counter, gdiff_min, gdiff_sec,
			gdiff_msec, tx_bstrnsfrd_strng, total_tx_bw, tx_mpps,
			total_tx_data_drop, tx_passed, tx_failed,
			tx_pass_percent);
	if (flags->direction != REQUEST_TRANSMIT)
		printf("%-8s%-8d%02ld:%02ld:%03ld     %-15s  %05.2fGBPS %05.2fMPPS %8d %6d%7d %10.2f%%\n",
			"Rx", rx_counter, gdiff_min, gdiff_sec,
			gdiff_msec, rx_bstrnsfrd_strng, total_rx_bw, rx_mpps,
			total_rx_data_drop, rx_passed, rx_failed,
			rx_pass_percent);
	printf("------------------------------------------------------------------------------------------------------\n");

	j = 0;
	if (tx_failed > 0 || rx_failed > 0) {
		printf("Failed queues:");
		for (i = 0; i < threads_count; i++) {
			for (q = 0; q < tctx[i].qcnt; q++) {
				if (j % 10 == 0) {
					printf("\n");
					j = 1;
				}
				qctx = &(tctx[i].qctx[q]);
				if (qctx->transfer_status == 0) {
					printf(" %2d%s", qctx->channel_id, qctx->direction == REQUEST_TRANSMIT ? "Tx" : "Rx");
					j++;
				}
			}
		}
		printf("\n");
	}
	printf("------------------------------------------------------------------------------------------------------\n");
/*
	printf("Anomaly Statistics:\n");
	printf("bad ds:%lu\n",ifc_qdma_stats_g.ifc_qdma_bad_ds);
	printf("No queue space:%lu\n",ifc_qdma_stats_g.ifc_qdma_no_queue_space);
	printf("zero fifo_len:%lu\n",ifc_qdma_stats_g.ifc_qdma_zero_fifo_len);
	printf("rx load fail:%lu\n",ifc_qdma_stats_g.ifc_qdma_rx_load_fail);
	printf("tx load fail:%lu\n",ifc_qdma_stats_g.ifc_qdma_tx_load_fail);
	printf("invalid length:%lu\n",ifc_qdma_stats_g.ifc_qdma_inv_len);
	printf("------------------------------------------------------------------------------------------\n");
*/
#ifndef PERFQ_LOAD_DATA
	if (!flags->fvalidation) {
		fprintf(fptr, "------------------------------------------------------------------------------------------\n");
		fprintf(fptr, "Total Bandwidth: %.2fGBPS, %.2fMPPS\n", total_ovrall_bw, total_ovrall_mpps);
		fprintf(fptr, "------------------------------------------------------------------------------------------\n");
		printf("------------------------------------------------------------------------------------------\n");
		printf("Total Bandwidth: %.2fGBPS, %.2fMPPS\n", total_ovrall_bw, total_ovrall_mpps);
		printf("------------------------------------------------------------------------------------------\n");
	}
#endif

#ifdef PERFQ_DATA
	memset(data_to_write, 0, 50);
	snprintf(data_to_write, sizeof(data_to_write),"%lu,%.2f,%.2f,%.2f,%s,%s,%s\n",
		flags->packet_size, total_tx_bw, total_rx_bw,
		total_ovrall_bw,
		ovrall_stat.completion_status ? "PASS" : "FAIL",
		ovrall_stat.gfile_status ? "PASS" : "FAIL",
		ovrall_stat.gpckt_status ? "PASS" : "FAIL");
	append_to_file(file_name, data_to_write);
#endif
	printf("%s\n", "Full Forms:");
	printf("%s\t\t%s\n", "TBW:", "Total Bandwidth");
	printf("%s\t\t%s\n", "IBW:", "Interval Bandwidth");
	printf("%s\t\t%s\n", "MIBW:", "Mean Interval Bandwidth");
	printf("%s\t\t%s\n", "HIBW:", "Highest Interval Bandwidth");
	printf("%s\t\t%s\n", "LIBW:", "Lowest Interval Bandwidth");
	printf("Please refer to %s for more details\n", flags->fname);
	fclose(fptr);
	return (ovrall_stat.completion_status & ovrall_stat.gfile_status & ovrall_stat.gpckt_status) ? PERFQ_SUCCESS : PERFQ_TASK_FAILURE;
}
