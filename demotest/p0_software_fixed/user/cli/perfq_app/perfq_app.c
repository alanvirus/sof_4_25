// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#include "perfq_app.h"
#include <inttypes.h>
#include <getopt.h>
#include <ctype.h>

struct ifc_qdma_device *global_qdev;
struct ifc_qdma_device *gqdev[2048];
struct thread_context *global_tctx;
struct struct_flags *global_flags;
struct timespec global_start_time;
struct timespec global_last_checked;
extern struct ifc_qdma_stats ifc_qdma_stats_g;
int cur_assgn_core;
pthread_mutex_t dev_lock;
int cpu_list[512];

#define READ_MAX_TIMES 20000
#define WRITE_COUNT 1024

#ifdef VERIFY_HOL
struct hol_stat global_hol_stat;
char reg_config[] = {1, 2, 4, 8, 0};
#endif

static int ispowerof2(unsigned long num)
{
	return num && (!(num & (num - 1)));
}

#if 0
int get_vf_number(char *bdf)
{
	int vf;
	char vf_prefix[3];

	vf = (int)strtol(bdf + 11, NULL, 16);
	ifc_qdma_strncpy(vf_prefix, sizeof(vf_prefix), bdf + 8, 2);
	vf +=  (8 * (int)strtol(bdf + 8, NULL, 16));

	return vf;
}
#endif

void sig_handler(int sig)
{
	int ret;

	/* dump stats and cleanup */
	printf("You have pressed ctrl+c\n");
	printf("Calling Handler for Signal %d\n", sig);
	ret = show_summary(global_tctx, global_flags, &global_last_checked,
			   &global_start_time);
	printf("Calling cleanup crew\n");
	cleanup(global_qdev, global_flags, global_tctx);
	exit(ret ? 1 : 0);
}

unsigned long min(unsigned long a, unsigned long b)
{
	return a <= b ? a : b;
}

#ifdef IFC_QDMA_ST_PACKETGEN
#ifdef IFC_QDMA_ST_MULTI_PORT
static int pio_init(struct ifc_qdma_device *qdev, struct struct_flags *flags)
{

        uint64_t file_size;
        uint32_t val = 0U;
        uint64_t offset;

        offset = PIO_REG_PKT_GEN_EN;
#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_pio_write32(qdev, offset, val);
#else
	ifc_qdma_pio_write64(qdev, offset, val);
#endif
        val = (1ULL << flags->chnls) - 1;
        offset = PIO_REG_PORT_PKT_RST;
#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_pio_write32(qdev, offset, val);
#else
	ifc_qdma_pio_write64(qdev, offset, val);
#endif

        if (flags->direction != REQUEST_LOOPBACK) {
                usleep(100000);

                /* clear */
                val &= ~((1ULL << flags->chnls) - 1);
#ifdef IFC_32BIT_SUPPORT
		ifc_qdma_pio_write32(qdev, offset, val);
#else
		ifc_qdma_pio_write64(qdev, offset, val);
#endif
        }


        file_size = flags->file_size * flags->packet_size;

        /* case: tx */
        if (flags->direction == REQUEST_TRANSMIT ||
            flags->direction == REQUEST_BOTH) {
                offset = PIO_REG_EXP_PKT_LEN;
#ifdef IFC_32BIT_SUPPORT
		ifc_qdma_pio_write32(qdev, offset, val);
#else
		ifc_qdma_pio_write64(qdev, offset, file_size);
#endif
        }

        /* case: rx */
        if (flags->direction == REQUEST_RECEIVE ||
            flags->direction == REQUEST_BOTH) {

                /* Setting the file size that will be generated */
                offset = PIO_REG_PKT_LEN;
#ifdef IFC_32BIT_SUPPORT
		ifc_qdma_pio_write32(qdev, offset, file_size);
#else
		ifc_qdma_pio_write64(qdev, offset, file_size);
#endif

                /* Setting number of files that will be generated */
                /* Let's just put a very high number here */
                offset = PIO_REG_PKT_CNT_2_GEN;
#ifdef IFC_32BIT_SUPPORT
		ifc_qdma_pio_write32(qdev, offset, 1 << 31);
#else
		ifc_qdma_pio_write64(qdev, offset, 1 << 31);
#endif
        }
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

int time_diff(struct timespec *a, struct timespec *b, struct timespec *result)
{
	long nsec_diff = a->tv_nsec - b->tv_nsec;
	long sec_diff = difftime(a->tv_sec, b->tv_sec);

	if (!sec_diff) {
		result->tv_sec = sec_diff;
		result->tv_nsec = a->tv_nsec - b->tv_nsec;
		return 0;
	}

	if (nsec_diff < 0) {
		result->tv_sec = sec_diff - 1;
		result->tv_nsec = (a->tv_nsec + 1e9) - b->tv_nsec;
		return 0;
	}
	result->tv_sec = sec_diff;
	result->tv_nsec = nsec_diff;
	return 0;
}

int count_queues(struct struct_flags *flags)
{
	if (flags->direction == REQUEST_LOOPBACK || flags->direction == REQUEST_BOTH)
		return flags->chnls * 2;

	return flags->chnls;
}

#ifdef VERIFY_HOL
int hol_config(struct struct_flags *flags, struct thread_context *tctx, unsigned long elapsed_sec)
{
	static unsigned long last_hol = 0;
	unsigned long cur_min;
	unsigned long cur_sec;
	unsigned long cur_msec;
	struct timespec cur_time;
	int threads_count = count_threads(flags);
	char val;
	int i = 0;

	if ((elapsed_sec - last_hol) < global_hol_stat.interval)
		return -1;
	if (global_hol_stat.size <= global_hol_stat.index)
		return -1;

	val = global_hol_stat.reg_config[global_hol_stat.index++];

	while(i < threads_count) {
		if (val & (1 << tctx[i].channel_id))
			ifc_qdma_channel_block(tctx[i].qchnl);
		else
			ifc_qdma_channel_unblock(tctx[i].qchnl);

		i = ((flags->direction == REQUEST_TRANSMIT) || (flags->direction == REQUEST_RECEIVE)) ? i + 1 : i + 2;
	}

	clock_gettime(CLOCK_MONOTONIC, &cur_time);
	cur_min = (cur_time.tv_sec / 60);
	cur_sec = cur_time.tv_sec - (cur_min * 60);
	cur_msec = cur_time.tv_nsec / 1e6;

	printf("HOL: Time Stamp: %02ld:%02ld:%03ld\t\tValue Passed: 0x%x\n", cur_min, cur_sec, cur_msec, (uint32_t)val);
	last_hol = elapsed_sec;
	return 0;
}
#endif

/* should_update_progress: checks if it is time for stat dump
 * return true if stat dump occured, else false
 */
static int should_update_progress(struct thread_context *tctx,
			    struct struct_flags *flags,
			    struct timespec *last_checked,
			    struct timespec *start_time)
{
	struct timespec cur_time;
	struct timespec timediff;

	clock_gettime(CLOCK_MONOTONIC, &cur_time);
	time_diff(&cur_time, last_checked, &timediff);
#ifdef IFC_32BIT_SUPPORT
	if (timediff.tv_sec >= (int)flags->interval) {
#else
	if (timediff.tv_sec >= flags->interval) {
#endif
		show_progress(tctx, flags, last_checked, start_time);
		/* update last checked time */
		clock_gettime(CLOCK_MONOTONIC, last_checked);
		return true;
	}
	return false;
}

#ifndef IFC_QDMA_ST_MULTI_PORT
#ifdef IFC_QDMA_ST_PACKETGEN
#ifndef IFC_QDMA_DYN_CHAN
/*
static void pio_qcsr_cleanup(struct ifc_qdma_device *qdev)
{
	// Clear out the PIO CSR
//	ifc_qdma_pio_write64(qdev, 0x10008, 0ULL);
//	ifc_qdma_pio_write64(qdev, 0x10010, 0ULL);
}
*/
#endif

static void pio_cleanup(struct queue_context *tctx __attribute__((unused)))
{
	uint32_t ch_cnt;

#ifndef IFC_ED_CONFIG_TID_UPDATE
#ifdef IFC_32BIT_SUPPORT
	uint32_t base;
	uint32_t offset;
#else
	uint64_t base;
	uint64_t offset;
#endif
	offset = pio_queue_config_offset(tctx);

	if (tctx->direction == REQUEST_TRANSMIT)
		base = 0x20000;
	else
		base = 0x30000;

#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_pio_write32(global_qdev, base + offset, 0UL);
#else
	ifc_qdma_pio_write64(global_qdev, base + offset, 0ULL);
#endif
#endif
	/* Update channel count */
#ifdef IFC_32BIT_SUPPORT
	ch_cnt = ifc_qdma_pio_read32(global_qdev, 0x10008);
#else
	ch_cnt = ifc_qdma_pio_read64(global_qdev, 0x10008);
#endif
	ch_cnt--;
#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_pio_write32(global_qdev, 0x10008, ch_cnt);
#else
	ifc_qdma_pio_write64(global_qdev, 0x10008, ch_cnt);
#endif
}
#endif
#endif

int thread_cleanup(struct thread_context *tctx)
{
	unsigned long i;
	struct queue_context *qctx;
	int ret, q;

	if (tctx->pthread_id > 0) {
		ret = pthread_cancel(tctx->pthread_id);
		if (ret != 0 && ret != ESRCH)
			return -1;

		/* wait till thread stops */
		ret = pthread_join(tctx->pthread_id, NULL);
		if (ret)
			return -1;
	}
	for (q = 0; q < tctx->qcnt; q++) {
		qctx = &(tctx->qctx[q]);
		if (qctx->completion_buf) {
			for (i = 0; i < qctx->cur_comp_counter; i++)
				ifc_request_free(((struct ifc_qdma_request **)
							(qctx->completion_buf))[i]);
			free(qctx->completion_buf);
		}
#ifndef IFC_QDMA_ST_MULTI_PORT
#ifdef IFC_QDMA_ST_PACKETGEN
		pio_cleanup(qctx);
#endif
#endif
	}
	return 0;
}


void cleanup(struct ifc_qdma_device *qdev, struct struct_flags *flags,
	     struct thread_context *tctx)
{
	int threads_count;
	int i, q;
#ifdef IFC_QDMA_INTF_ST
#ifndef IFC_QDMA_ST_MULTI_PORT
#ifdef IFC_QDMA_ST_PACKETGEN
#ifdef IFC_DEBUG_STATS
	int base;
#endif //IFC_DEBUG_STATS
#endif //IFC_QDMA_ST_PACKETGEN
#endif //IFC_QDMA_ST_MULTI_PORT
#endif //IFC_QDMA_INTF_ST

	if (flags == NULL)
		return;

	threads_count = flags->num_threads;

	if (tctx) {
		ifc_qdma_dump_stats(global_qdev);
		for (i = 0; i < flags->chnls; i++) {
			if (pthread_mutex_destroy(&(flags->locks[i])) != 0) {
				printf("Failed while destroying mutex\n");
			}
			sem_destroy(&((flags->loop_locks[i]).tx_lock));
			sem_destroy(&((flags->loop_locks[i]).rx_lock));
#if 0
			/* reset the PIO registers */
			pio_bit_writer(0x00, i, false);
			pio_bit_writer(0x10, i, false);
#endif
		}

		for (i = 0; i < threads_count; i++) {
			for (q = 0; q < tctx[i].qcnt; q++) {
				ifc_qdma_print_stats(tctx[i].qctx[q].qchnl, tctx[i].qctx[q].direction);
#ifdef IFC_QDMA_INTF_ST
#ifndef IFC_QDMA_ST_MULTI_PORT
#ifdef IFC_QDMA_ST_PACKETGEN
#ifdef IFC_DEBUG_STATS
				/* Dump ED config for debugging */
				if (tctx[i].qctx[q].direction == REQUEST_TRANSMIT)
					base = 0x20000;
				else
					base = 0x30000;
#ifdef IFC_32BIT_SUPPORT
				uint32_t offset = pio_queue_config_offset(&(tctx[i].qctx[q]));
				printf("\tedc: 0x%x\n",ifc_qdma_pio_read32(qdev, base + offset));
#else
				uint64_t offset = pio_queue_config_offset(&(tctx[i].qctx[q]));
				printf("\tedc: 0x%lx\n",ifc_qdma_pio_read64(qdev, base + offset));
#endif
#ifdef IFC_ED_CONFIG_TID_UPDATE
				printf("\tfs:%u hw:%u dreq:%u\n",
					tctx[i].qctx[q].sw_files_submitted,
					tctx[i].qctx[q].ed_files_submitted, tctx[i].qctx[q].desc_requested);
#endif
#endif //IFC_DEBUG_STATS
#endif //IFC_QDMA_ST_PACKETGEN
#endif //IFC_QDMA_ST_MULTI_PORT
#endif //IFC_QDMA_INTF_ST
				ifc_qdma_channel_put(tctx[i].qctx[q].qchnl, tctx[i].qctx[q].direction);
			}
			if (thread_cleanup(&tctx[i]))
				printf("Thread%d: Falied to cleanup thread\n", tctx[i].tid);
		}
		free(tctx);
	}
#ifndef IFC_QDMA_ST_MULTI_PORT
#ifndef IFC_QDMA_DYN_CHAN
#ifdef IFC_QDMA_ST_PACKETGEN
//	if (!flags->fpio)
//		pio_qcsr_cleanup(qdev);
#endif
#endif
#endif
	if (qdev) {
#ifdef IFC_QDMA_DYN_CHAN
		ifc_qdma_release_all_channels(qdev);
#endif
		ifc_qdma_device_put(qdev);
	}

	free(flags->locks);
	free(flags->loop_locks);
	free(flags);
	ifc_app_stop();
}

#ifdef IFC_PIO_MIX 
#ifndef IFC_32BIT_SUPPORT
static int pio_perf_util_mixed(struct ifc_qdma_device *qdev)
{
        uint64_t addr = PIO_ADDRESS;
        uint64_t laddr;
        unsigned int j;
        uint64_t wval = 0;
        uint64_t wvalue[4];
        uint64_t rvalue[4];
        int ret;
	int read64 = 0;
	int read128 = 0;
        int read_count;
        __uint128_t rvalue_128= 0ULL;
        __uint128_t wvalue_128= 0ULL;
        uint64_t wvalue_64 = 0ULL;
        uint64_t rvalue_64 = 0ULL;
        static int data_fail;
        int read_iteration_failed = 0;
        for (j = 0; j < 4; j++)
                wvalue[j] = wval++;

        printf("PIO mix write and read test started\n");
        addr = PIO_ADDRESS;
        laddr = addr;
        int write_256;
        for(write_256 = 0; write_256 < 32; write_256++) {
                ret = ifc_qdma_pio_write256(qdev, addr, wvalue, global_flags->wbar);
                if (ret < 0) {
                        printf("Failed to write PIO\n");
                        return 1;
                }
        }

        for(read_count = 0; read_count < READ_MAX_TIMES; read_count++) {
                ifc_qdma_pio_read256(qdev, laddr, rvalue, global_flags->rbar);
                for (j = 0; j < 4; j++) {
                        if (wvalue[j] != rvalue[j]) {
                                data_fail++;
                                read_iteration_failed = read_count;
                                printf("Data compare failed  in the iteration = %d\n", read_iteration_failed);
                        }
                }
        }
        addr = PIO_ADDRESS;
        int wr64_iter;
	int read_each_write64;
        for (wr64_iter = 0; wr64_iter < WRITE_COUNT; wr64_iter++) {
                ifc_qdma_write64(qdev, addr, wvalue_64, global_flags->wbar);
                for( read_each_write64 = 0; read_each_write64 < READ_MAX_TIMES ; read_each_write64++) {
                        rvalue_64 = ifc_qdma_read64(qdev, addr, global_flags->rbar);
                        if (wvalue_64 != rvalue_64) {
                                printf("Failed 64b data\n");
                                read64 = 1;
                                break;
                        }
                }
                wvalue_64++;
                addr += 8;
        }
        addr = PIO_ADDRESS;
        int wr128_iter ;
	int read_each_write128;
     	for (wr128_iter = 0; wr128_iter < WRITE_COUNT; wr128_iter++) {
                ifc_qdma_pio_write128(qdev, addr, wvalue_128, global_flags->wbar);
                for(read_each_write128 = 0; read_each_write128 < READ_MAX_TIMES; read_each_write128++) {
                        rvalue_128 = ifc_qdma_pio_read128(qdev, addr, global_flags->rbar);
                        if ((uint64_t)wvalue_128 != (uint64_t)rvalue_128) {
                                printf("Failed 128b data\n");
                                read128 = 1;
                                break;
                        }
                }
                wvalue_128++;
                addr += 8;
        }

        if(read64){
                printf("PIO 64b mix test failed\n");
                ret = -1;
        }
        else if(read128){
                printf("PIO 128b mix test...failed\n");
                ret = -1;
        }
	else 
	{
                printf("PIO mix test passed\n");
	}

        return ret;
}
static int pio_util_mixed(struct ifc_qdma_device *qdev)
{
        uint64_t addr = PIO_ADDRESS;
        unsigned int i,j;
        uint64_t wval = 0;
        uint64_t wvalue[4];
        uint64_t rvalue[4];
        int ret;

        for (j = 0; j < 4; j++)
                wvalue[j] = wval++;

        printf("PIO MIXED Write and Read Test ...\n");

        for (i = 0; i < 32; i++) {
                ret = ifc_qdma_pio_write256(qdev, addr, wvalue, global_flags->wbar);
                if (ret < 0) {
                        printf("Failed to write PIO\n");
                        printf("Fail\n");
                        return 1;
                }
                ret = ifc_qdma_pio_read256(qdev, addr, rvalue, global_flags->rbar);
                if (ret < 0) {
                        printf("Failed to read PIO\n");
                        printf("Fail\n");
                        return 1;
                }

                for (j = 0; j < 4; j++) {
                        if (wvalue[j] != rvalue[j]) {
                                printf("Fail\n");
                                return 1;
                        }
                }
                for (j = 0; j < 4; j++)
                        wvalue[j] = wval++;
                addr += 32;
        }
        printf("Pass\n");
        return 0;
}
#endif
#endif

#ifdef IFC_PIO_256
#ifndef IFC_32BIT_SUPPORT
static int pio_perf_util_256(struct ifc_qdma_device *qdev)
{
        uint64_t addr = PIO_ADDRESS;
        uint64_t laddr;
        unsigned int i,j;
	unsigned int ite;
        uint64_t wval = 0;
        uint64_t wvalue[4];
        uint64_t rvalue[4];
	struct timespec start_time;
	struct timespec end_time;
	struct struct_time t; 
	long timediff_sec;
	int ret;

        for (j = 0; j < 4; j++)
                wvalue[j] = wval++;

        printf("PIO 256 Write Performance Test ...\n");

	clock_gettime(CLOCK_MONOTONIC, &start_time);
	ite = 0;
	while (1) {
        	addr = PIO_ADDRESS;
		for (i = 0; i < 32; i++) {
			laddr = addr;
			ret = ifc_qdma_pio_write256(qdev, addr, wvalue, global_flags->wbar);
			if (ret < 0) {
				printf("Failed to write PIO\n");
				printf("Fail\n");
				return 1;
			}
			addr += 32;
		}
		ite++;
		if (ite % 100) {
			clock_gettime(CLOCK_MONOTONIC, &end_time);
			timediff_sec = difftime(end_time.tv_sec,
					start_time.tv_sec);
                	if (timediff_sec >= 10 &&
                    	   (end_time.tv_nsec >= start_time.tv_nsec)) {
				break;
			}
		}
		uint32_t done = 0;
		while (done == 0) {
			ifc_qdma_pio_read256(qdev, laddr, rvalue, global_flags->rbar);
			for (j = 0; j < 4; j++) {
				if (wvalue[j] != rvalue[j]) {
					break;
				}
			}
			if (j == 4) {
				done = 1;
				break;
			}
		}
		for (j = 0; j < 4; j++)
			wvalue[j] = wval++;
	}
	compute_bw(&t, &start_time, &end_time, ite * 32, IFC_MCDMA_BAM_BURST_BYTES);
	printf("Total Bandwidth:\t\t%.2fGBPS\n",t.ovrall_bw);
	printf("Pass\n");
	return 0;
}
static int pio_util_256(struct ifc_qdma_device *qdev)
{
        uint64_t addr = PIO_ADDRESS;
        unsigned int i,j;
        uint64_t wval = 0;
        uint64_t wvalue[4];
        uint64_t rvalue[4];
	int ret;

        for (j = 0; j < 4; j++)
                wvalue[j] = wval++;

        printf("PIO 256 Write and Read Test ...\n");

        for (i = 0; i < 32; i++) {
                ret = ifc_qdma_pio_write256(qdev, addr, wvalue, global_flags->wbar);
		if (ret < 0) {
			printf("Failed to write PIO\n");
			printf("Fail\n");
			return 1;
		}
                ret = ifc_qdma_pio_read256(qdev, addr, rvalue, global_flags->rbar);
		if (ret < 0) {
			printf("Failed to read PIO\n");
			printf("Fail\n");
			return 1;
		}

                for (j = 0; j < 4; j++) {
                        if (wvalue[j] != rvalue[j]) {
				printf("Fail\n");
				return 1;
			}
                }
                for (j = 0; j < 4; j++)
                        wvalue[j] = wval++;
                addr += 32;
	}
	printf("Pass\n");
	return 0;
}
#endif //32BIT_SUPORT
#endif //PIO_256

#ifdef IFC_32BIT_SUPPORT
int pio_perf_read(struct ifc_qdma_device *qdev, uint32_t addr )
#else
int pio_perf_read(struct ifc_qdma_device *qdev, uint64_t addr )
#endif
{
#ifdef IFC_32BIT_SUPPORT
        uint32_t rvalue = 0UL;
#else
        uint64_t rvalue = 0ULL;
#endif

	if(qdev == NULL){
		printf("Fail: device is null\n");
		return -EINVAL;
	}

	if(global_flags->wbar != 2){
		printf("PIO write on bar - %d is not allowed\n", global_flags->wbar);
		return -EINVAL;
	}

#ifdef IFC_32BIT_SUPPORT
        rvalue = ifc_qdma_read32(qdev, addr, global_flags->rbar);
        printf("READ: PIO  Address = 0x%x \t Value = 0x%x, bar = %d \n", addr, rvalue, global_flags->rbar);
#else
        rvalue = ifc_qdma_read64(qdev, addr, global_flags->rbar);
        printf("READ: PIO  Address = 0x%lx \t Value = 0x%lx, bar = %d \n", addr, rvalue, global_flags->rbar);
#endif
        return 0;

}


#ifdef IFC_32BIT_SUPPORT
int pio_perf_write(struct ifc_qdma_device *qdev, uint32_t addr, uint32_t wvalue)
#else
int pio_perf_write(struct ifc_qdma_device *qdev, uint64_t addr, uint64_t wvalue)
#endif
{
#ifdef IFC_32BIT_SUPPORT
        uint32_t rvalue = 0UL;
#else
        uint64_t rvalue = 0ULL;
#endif

	if(qdev == NULL){
		printf("Fail: device is null\n");
		return -EINVAL;
	}

	if(global_flags->wbar != 2 ){
		printf("PIO write on bar - %d is not allowed\n", global_flags->wbar);
		return -EINVAL;
	}
//TODO ADD FOR 32 BIT w/r
#ifdef IFC_32BIT_SUPPORT 
        printf("WRITE: PIO  Address = 0x%x \t Value = 0x%x, bar = %d \n", addr, wvalue, global_flags->wbar);
	ifc_qdma_write32(qdev, addr, wvalue, global_flags->wbar);
	rvalue = ifc_qdma_read32(qdev, addr, global_flags->rbar);
#else
        printf("WRITE: PIO  Address = 0x%lx \t Value = 0x%lx, bar = %d \n", addr, wvalue, global_flags->wbar);
	ifc_qdma_write64(qdev, addr, wvalue, global_flags->wbar);
	rvalue = ifc_qdma_read64(qdev, addr, global_flags->rbar);
#endif
	if(rvalue != wvalue){
#ifdef IFC_32BIT_SUPPORT 
		printf("Failed to access the written value at addr = 0x%x\n", addr);
#else
		printf("Failed to access the written value at addr = 0x%lx\n", addr);
#endif
}
return 0;		
}

#ifdef IFC_PIO_32
/* 32b performance  mode */
static int pio_perf_util_32(struct ifc_qdma_device *qdev)
{
        uint32_t addr = PIO_ADDRESS;
        uint32_t laddr;
        unsigned int i;
	unsigned int ite;
        uint32_t wvalue = 0UL ;
        uint32_t rvalue = 0UL;
	struct timespec start_time;
	struct timespec end_time;
	struct struct_time t; 
	long timediff_sec;
	//int ret;

        printf("PIO 32 Write Performance Test ...\n");
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	ite = 0;
	while (1) {
        	addr = PIO_ADDRESS;
		for (i = 0; i < 1024; i++) {
			laddr = addr;
			ifc_qdma_write32(qdev, addr, wvalue, global_flags->wbar);
			addr += 4;
		}
		ite++;
		if (ite % 100) {
			clock_gettime(CLOCK_MONOTONIC, &end_time);
			timediff_sec = difftime(end_time.tv_sec,
					start_time.tv_sec);
                	if (timediff_sec >= 10 &&
                    	   (end_time.tv_nsec >= start_time.tv_nsec)) {
				break;
			}
		}
		uint32_t done = 0;
		while (done == 0) {
			rvalue = ifc_qdma_read32(qdev, laddr, global_flags->wbar);
			if (wvalue != rvalue) {
				continue;
			}
			done = 1;
		}
	}
	compute_bw(&t, &start_time, &end_time, ite * 1024, 4);
	printf("Total Bandwidth:\t\t%.2fGBPS\n",t.ovrall_bw);
	printf("Pass\n");
	return 0;
}
/* 32b Functionality mode */
int pio_util_32(struct ifc_qdma_device *qdev)
{
	uint32_t addr = PIO_ADDRESS;
	uint32_t wvalue = 0ULL;
	uint32_t rvalue = 0ULL;
	int i;

	printf("PIO32 Write and Read Test ...\n");

#ifdef VERIFY_FUNC
	if (global_flags->fvalidation)
		printf("%-12s\t%-9s\n", "Written", "Read");
#endif

	for (i = 0; i < 100; i++) {
		ifc_qdma_write32(qdev, addr, wvalue, global_flags->wbar);
		rvalue = ifc_qdma_read32(qdev, addr, global_flags->rbar);
#ifdef VERIFY_FUNC
		if (global_flags->fvalidation)
			printf("0x%08x\t0x%08x\n", wvalue, rvalue);
#endif

		if (wvalue != rvalue) {
		printf("Fail\n");
			return 1;
		}

		wvalue++;
		addr += 4;
	}
	printf("Pass\n");
	return 0;

}
#endif /* IFC_PIO_32 */

#ifdef IFC_PIO_64
/* 64b performance  mode */
static int pio_perf_util(struct ifc_qdma_device *qdev)
{
        uint64_t addr = PIO_ADDRESS;
        uint64_t laddr;
        unsigned int i;
	unsigned int ite;
        uint64_t wvalue = 0ULL ;
        uint64_t rvalue = 0ULL;
	struct timespec start_time;
	struct timespec end_time;
	struct struct_time t; 
	long timediff_sec;
	//int ret;

        printf("PIO 64 Write Performance Test ...\n");
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	ite = 0;
	while (1) {
        	addr = PIO_ADDRESS;
		for (i = 0; i < 1024; i++) {
			laddr = addr;
			ifc_qdma_write64(qdev, addr, wvalue, global_flags->wbar);
			addr += 8;
		}
		ite++;
		if (ite % 100) {
			clock_gettime(CLOCK_MONOTONIC, &end_time);
			timediff_sec = difftime(end_time.tv_sec,
					start_time.tv_sec);
                	if (timediff_sec >= 10 &&
                    	   (end_time.tv_nsec >= start_time.tv_nsec)) {
				break;
			}
		}
		uint32_t done = 0;
		while (done == 0) {
			rvalue = ifc_qdma_read64(qdev, laddr, global_flags->wbar);
			if (wvalue != rvalue) {
				continue;
			}
			done = 1;
		}
	}
	compute_bw(&t, &start_time, &end_time, ite * 1024, 8);
	printf("Total Bandwidth:\t\t%.2fGBPS\n",t.ovrall_bw);
	printf("Pass\n");
	return 0;
}
/* 64b Functionality mode */
int pio_util(struct ifc_qdma_device *qdev)
{
	uint64_t addr = PIO_ADDRESS;
	uint64_t wvalue = 0ULL;
	uint64_t rvalue = 0ULL;
	int i;

	printf("PIO Write and Read Test ...\n");

#ifdef VERIFY_FUNC
	if (global_flags->fvalidation)
		printf("%-12s\t%-9s\n", "Written", "Read");
#endif

	for (i = 0; i < 100; i++) {
		ifc_qdma_write64(qdev, addr, wvalue, global_flags->wbar);
		rvalue = ifc_qdma_read64(qdev, addr, global_flags->rbar);
#ifdef VERIFY_FUNC
		if (global_flags->fvalidation)
			printf("0x%08lx\t0x%08lx\n", wvalue, rvalue);
#endif

		if (wvalue != rvalue) {
			printf("Fail\n");
			return 1;
		}

		wvalue++;
		addr += 8;
	}
	printf("Pass\n");
	return 0;
}
#endif /* IFC_PIO_64 */

#ifdef IFC_PIO_128
static int pio_perf_util_128(struct ifc_qdma_device *qdev)
{
        uint64_t addr = PIO_ADDRESS;
        uint64_t laddr;
        unsigned int i,j;
	unsigned int ite;
        __uint128_t wvalue = 0ULL ;
        __uint128_t rvalue = 0ULL;
	struct timespec start_time;
	struct timespec end_time;
	struct struct_time t; 
	long timediff_sec;
	//int ret;

        printf("PIO 128 Write Performance Test ...\n");
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	ite = 0;
	while (1) {
        	addr = PIO_ADDRESS;
		for (i = 0; i < 32; i++) {
			laddr = addr;
			ifc_qdma_pio_write128(qdev, addr, wvalue, global_flags->wbar);
			addr += 16;
		}
		ite++;
		if (ite % 100) {
			clock_gettime(CLOCK_MONOTONIC, &end_time);
			timediff_sec = difftime(end_time.tv_sec,
					start_time.tv_sec);
                	if (timediff_sec >= 10 &&
                    	   (end_time.tv_nsec >= start_time.tv_nsec)) {
				break;
			}
		}
		uint32_t done = 0;
		while (done == 0) {
			rvalue = ifc_qdma_pio_read128(qdev, laddr, global_flags->wbar);
			for (j = 0; j < 4; j++) {
				if (wvalue != rvalue) {
					break;
				}
			}
			if (j == 4) {
				done = 1;
				break;
			}
		}
	}
	compute_bw(&t, &start_time, &end_time, ite * 32, 16);
	printf("Total Bandwidth:\t\t%.2fGBPS\n",t.ovrall_bw);
	printf("Pass\n");
	return 0;
}
/* BAM Functionality mode */
int pio_util_128(struct ifc_qdma_device *qdev)
{
        uint64_t addr = PIO_ADDRESS;
       __uint128_t wvalue = 0ULL ;
       __uint128_t rvalue = 0ULL;
        int i;

        printf("PIO 128b Write and Read Test ...\n");

#ifdef VERIFY_FUNC
        if (global_flags->fvalidation)
                printf("%-12s\t%-9s\n", "Written", "Read");
#endif

	for (i = 0; i < 32; i++) {
                ifc_qdma_pio_write128(qdev, addr, wvalue, global_flags->wbar);
		rvalue = ifc_qdma_pio_read128(qdev, addr, global_flags->rbar);
#ifdef VERIFY_FUNC
                if (global_flags->fvalidation)
                        printf("0x%08lx\t0x%08lx\n", wvalue, rvalue);
#endif

                if ((uint64_t)wvalue != (uint64_t)rvalue) {
                        printf("Fail\n");
                        return 1;
                }
                wvalue++;
                addr += 8;
        }
        printf("Pass\n");
        return 0;
}
#endif

void show_help(void)
{
	printf("--bar <num>\tBAR number to be configured for BAM/BAS\n");
	printf("--bas_perf\tEnable BAS Performance Mode\n");
	printf("--bam_perf\tEnable BAM Performance Mode\n");
	printf("--pio_r_addr <address>\tread PIO address\n");
	printf("--pio_w_addr <address>\t write PIO address\n");
	printf("--pio_w_val <value>\t write PIO value\n");
	printf("-h\t\tShow Help\n");
	printf("-a <threads>\tNumber of Threads to be used\n");
	printf("-b <bdf>\tBDF of Device\n");
	printf("-c <chnls>\tNumber of Channels to be used\n");
	printf("-d <seconds>\tRefresh rate in seconds for Performance logs\n");
	printf("-e\t\tEnable BAS Mode\n");
	printf("-p <bytes>\tPayload Size of each descriptor\n");
	printf("-s <bytes>\tRequest Size in Bytes\n");
	printf("-g <start chno>\tStarting channel number while acquiring\n");
#ifndef IFC_QDMA_ST_MULTI_PORT
	printf("-f <#dsriptrs>\tFile Size in Descriptors\n");
#endif
	printf("-o\t\tPIO Test\n");
	printf("-v\t\tEnable Data Validation\n");
	printf("-x <#dscriptrs>\tBatch Size in Descriptors\n");
//	printf("-y <#locations>\tHex dump example design memory\n");
//	printf("-q <#dscriptrs>\t#descriptors per page\n");
#ifdef IFC_QDMA_ST_PACKETGEN
#ifndef IFC_QDMA_ST_MULTI_PORT
	printf("-n \t\tconfigure channel count in HW incase of"
               " Performance mode\n");
	printf("--files <files>\tPacket Gen files\n");
#endif
#endif
#ifdef IFC_QDMA_INTF_ST
#ifdef IFC_QDMA_ST_LOOPBACK
	printf("-u\t\tLoop back Transfer\n");
	printf("Required parameters: b & ((p & u) & (s | l) & c & a) "
		"| o)\n");
	printf("-i\t\tIndependent Loop back\n");
	printf("Required parameters: b & (i & (s | l) & c & a) | o)\n");
#else
	printf("-t\t\tTransmit Operation\n");
	printf("-r\t\tReceive Operation\n");
	printf("-z\t\tBidirectional Transfer\n");
	printf("-l <seconds>\tTime limit in Seconds\n");
	printf("Required parameters: b & ((p & (t | r | z ) & (s | l) & c & a) "
		"| o)\n");

#endif
#else
	printf("-l <seconds>\tTime limit in Seconds\n");
	printf("-t\t\tTransmit Operation\n");
	printf("-r\t\tReceive Operation\n");
	printf("-u\t\tLoop back Transfer\n");
	printf("Required parameters: b & ((p & (t | r | u) & (s | l) & c & a) "
		"| o)\n");
#endif
	printf("Required parameters BAS Mode:\n");
	printf("\tPerf:\t\tb & z & s & --bas_perf\n");
	printf("\tNon perf:\tb & (t | r | z) & s & e\n");
	printf("Output format:\n");
        printf("Ch_ID            - Channel ID and Direction\n");
        printf("Req              - Number of Descriptors submitted for DMA Transfers\n");
        printf("Rsp              - Number of Descriptors procesed\n");
        printf("Time             - Total time Elapsed\n");
        printf("Good Descriptors - Number of good descriptors which match the expected data\n");
        printf("Bad Descriptors  - Number of bad descriptors which doesn't match the expected data\n");
        printf("Good Files       - Number of files which had SOF and EOF matched\n");
        printf("Bad Files        - Number of files which didn't match EOF/SOF/Data\n");
}
void dump_flags(struct struct_flags *flags)
{
	printf("----------------------------------------------------------------------------------------------------------------------------\n");
	printf("Limit: %d\n", flags->flimit);
	printf("PIO Test: %d\n", flags->fpio);
	printf("Validation: %d\n", flags->fvalidation);
	printf("BDF: %s\n", flags->bdf);
	printf("#Channels: %d\n", flags->chnls);
	printf("Request Size: %zu\n", flags->request_size);
	printf("Packet Size: %zu\n", flags->packet_size);
	printf("#Packets: %lu\n", flags->packets);
	printf("Batch Size: %lu\n", flags->batch_size);
#ifdef IFC_QDMA_INTF_ST
	printf("File Size: %lu\n", flags->file_size);
#endif
	printf("Direction: %d\n", flags->direction);
	printf("Dump Interval: %d\n", flags->interval);
	printf("Time Limit: %u\n", flags->time_limit);
	printf("------------------:Compilation Flags status:---------------------------\n");

#ifdef IFC_QDMA_INTF_ST
	printf("Mode: AVST\n");
#else
	printf("Mode: AVMM\n");
#endif

#ifdef PERFQ_PERF
	printf("PERFQ_PERF: ON\n");
#else
	printf("PERFQ_PERF: OFF\n");
#endif

#ifdef PERFQ_DATA
	printf("PERFQ_DATA: ON\n");
#else
	printf("PERFQ_DATA: OFF\n");
#endif

#ifdef PERFQ_LOAD_DATA
	printf("PERFQ_LOAD_DATA: ON\n");
#else
	printf("PERFQ_LOAD_DATA: OFF\n");
#endif

#ifdef DUMP_DATA
	printf("DUMP_DATA: ON\n");
#else
	printf("DUMP_DATA: OFF\n");
#endif


#ifdef VERIFY_FUNC
	printf("VERIFY_FUNC: ON\n");
#else
	printf("VERIFY_FUNC: OFF\n");
#endif

#ifdef VERIFY_HOL
	printf("VERIFY_HOL: ON\n");
#else
	printf("VERIFY_HOL: OFF\n");
#endif
	printf("----------------------------------------------------------------------------------------------------------------------------\n");
}

static int parse_long_int(char *arg)
{
	unsigned long arg_val;
	char *c = arg;

	while(*c != '\0') {
		if (*c < 48 || *c > 57)
			return -1;
		c++;
	}
	sscanf(arg, "%lu", &arg_val);
	return arg_val;
}

/* Parse input arguments with "0x" prefix to be taken as hexadecimal value */
static int parse_long_hex(char *arg)
{
	unsigned long arg_val;
	char *c = arg;
	int is_hex = 0;

	while(*c != '\0') {

		if(*c == 88 || *c == 120){
			c++;
			is_hex = 1;
		}
		if(!isxdigit(*c))
			return -1;
		c++;
	}
	if(is_hex)
		sscanf(arg, "%lx", &arg_val);
	else
		sscanf(arg, "%lu", &arg_val);
	return arg_val;
}

static void generate_file_name(char *file_name)
{
	time_t cur_time;

	time(&cur_time);
	strftime(file_name, FILE_PATH_SIZE, "perfq_log_%Y%m%d_%H%M%S.txt", localtime(&cur_time));
	return;
}

int cmdline_option_parser(int argc, char *argv[], struct struct_flags *flags)
{
	char *end;
	int opt;
	int fbdf = false;
	int fpacket_size = false;
	int transmit_counter = -1;
	int arg_val;
	int queue_count;
	int opt_idx;
	int comp_mode = 0;
	int pktgen_files = 0;
	int fbar = 0;
#ifdef PROFILING
	int lower_ch = PERFQ_LOWER_CH;
	uint32_t lower_bs = PERFQ_LOWER_BATCH_SIZE;
	uint32_t lower_pkt_size = PERFQ_LOWER_PKT_SIZE;
#endif
	/* Initialize certain flags */
	flags->memalloc_fails = 0;
	flags->file_size = DEFAULT_FILE_SIZE;
	flags->request_size = 0;
	flags->flimit = -1;
	flags->fpio = false;
	flags->fvalidation = false;
	flags->fpkt_gen_files = false;
	flags->interval = 0;
	flags->packet_size = 0;
	flags->time_limit = 0;
	flags->qdepth_per_page = 128;
	flags->batch_size = DEFAULT_BATCH_SIZE;
#if defined(IFC_QDMA_ST_PACKETGEN) && defined(IFC_QDMA_INTF_ST)
	flags->pkt_gen_files = DEFAULT_PKT_GEN_FILES;
#endif
	flags->direction = -1;
	flags->single_fn = 0;
	flags->pf = 1; //By default, PF1
	flags->vf = 0; //By default, PF and not VF
	flags->fcomp_mode = 0;
	flags->completion_mode = 0;
	flags->wbar = 2;
	flags->rbar = 2;

        static struct option lgopts[] = {
			{ "bas", 	0, 0, 0 },
			{ "bas_perf", 	0, 0, 0 },
			{ "bam", 	0, 0, 0 },
			{ "bam_perf", 	0, 0, 0 },
#ifdef IFC_QDMA_ST_PACKETGEN
			{ "files",   	1, 0, 0 },
#ifdef IFC_MCDMA_SINGLE_FUNC
			{ "pf",   	1, 0, 0 },
			{ "vf",   	1, 0, 0 },
#endif
#endif
			{ "wb", 	0, 0, 0 },
			{ "msix", 	0, 0, 0 },
			{ "reg", 	0, 0, 0 },
			{ "bar",   	1, 0, 0 },
			{ "pio_r_addr",	1, 0, 0 },
			{ "pio_w_addr",	1, 0, 0 },
			{ "pio_w_val",	1, 0, 0 },
			{ 0, 0, 0, 0 }
		};

	while ((opt = getopt_long(argc, argv, "eg:iq:y:za:b:c:d:f:hl:op:rs:tuvwx:nz",
				  lgopts, &opt_idx)) != -1) {
		switch (opt) {
		case 0: /* long options */
			if (!strcmp(lgopts[opt_idx].name, "bas")) {
                                 if (flags->fbas != false) {
                                         printf("ERR: Flags can not be repeated\n");
                                         return -EINVAL;
                                 }
                                flags->fbas = true;
                                flags->params_mask |= PERFQ_ARG_BAS;
			}
			else if (!strcmp(lgopts[opt_idx].name, "bas_perf")) {
				if (flags->fbas_perf != false) {
					printf("ERR: Flags can not be repeated\n");
					return -EINVAL;
				}
				flags->fbas_perf = true;
				flags->params_mask |= PERFQ_ARG_BAS_PERF;
			} else if (!strcmp(lgopts[opt_idx].name, "bam_perf")) {
				if (flags->fbam_perf != false) {
					printf("ERR: Flags can not be repeated\n");
					return -EINVAL;
				}
				flags->fbam_perf = true;
			} else if (!strcmp(lgopts[opt_idx].name, "files")) {
				if (flags->fpkt_gen_files != false) {
					printf("ERR: Flags can not be repeated\n");
					return -EINVAL;
				}
				flags->fpkt_gen_files = true;
				arg_val = parse_long_int(optarg);
				if (arg_val <= 0) {
					printf("Invalid --files value\n");
					return -EINVAL;
				}
				pktgen_files = arg_val;
				flags->params_mask |= PERFQ_ARG_PKT_GEN_FILES;
			} else if (!strcmp(lgopts[opt_idx].name, "pf")) {
				if (flags->fpf != false) {
					printf("ERR: PF can not be repeated\n");
					return -EEXIST;
				}
				flags->fpf = true;
				arg_val = parse_long_int(optarg);
				if (arg_val <= 0) {
					printf("Invalid --pf value\n");
					return -EINVAL;
				}
				flags->pf = arg_val;
				flags->params_mask |= PERFQ_ARG_PF;
			} else if (!strcmp(lgopts[opt_idx].name, "vf")) {
				if (flags->fvf != false) {
					printf("ERR: VF can not be repeated\n");
					return -EEXIST;
				}
				flags->fvf = true;
				arg_val = parse_long_int(optarg);
				if (arg_val < 0) {
					printf("Invalid --vf value\n");
					return -EINVAL;
				}
				flags->vf = arg_val;
				flags->params_mask |= PERFQ_ARG_VF;
			} else if (!strcmp(lgopts[opt_idx].name, "wb")) {
				if (flags->fcomp_mode != false) {
					printf("ERR: Flags can not be repeated\n");
					return -EEXIST;
				}
				flags->fcomp_mode = 1;
				comp_mode = CONFIG_QDMA_QUEUE_WB;
				flags->params_mask |= PERFQ_ARG_COMP_MODE_WB;
			} else if (!strcmp(lgopts[opt_idx].name, "msix")) {
				if (flags->fcomp_mode != false) {
					printf("ERR: Flags can not be repeated\n");
					return -EEXIST;
				}
				flags->fcomp_mode = 1;
				comp_mode = CONFIG_QDMA_QUEUE_MSIX;
				flags->params_mask |= PERFQ_ARG_COMP_MODE_MSIX;
			} else if (!strcmp(lgopts[opt_idx].name, "reg")) {
				if (flags->fcomp_mode != false) {
					printf("ERR: Flags can not be repeated\n");
					return -EEXIST;
				}
				flags->fcomp_mode = 1;
				comp_mode = CONFIG_QDMA_QUEUE_REG;
				flags->params_mask |= PERFQ_ARG_COMP_MODE_REG;
			} else if (!strcmp(lgopts[opt_idx].name, "bar")) {
				arg_val = parse_long_int(optarg);
				if (arg_val < 0) {
					printf("Invalid --bar value\n");
					return -EINVAL;
				}
				flags->rbar = flags->wbar = arg_val;
				fbar = 1;
			} else if (!strcmp(lgopts[opt_idx].name, "pio_r_addr")) {
				if (flags->fpio_r_addr != false) {
					printf("ERR: PIO read addr can not be repeated\n");
					return -EEXIST;
				}
				flags->fpio_r_addr = true;
				arg_val = parse_long_hex(optarg);
				if (arg_val <= 0) {
					printf("Invalid pio address value  arg_val = 0x%x \n",arg_val);
					return -EINVAL;
				}
				if((flags->rbar == 0) || (flags->wbar == 0)){
					printf("ERR: PIO read write on bar 0 is not allowed");
					return -EINVAL;
				}

				flags->pio_r_addr = arg_val;
			} else if (!strcmp(lgopts[opt_idx].name, "pio_w_addr")) {
				if (flags->fpio_w_addr != false) {
					printf("ERR: PIO write addr can not be repeated\n");
					return -EEXIST;
				}
				flags->fpio_w_addr = true;
				arg_val = parse_long_hex(optarg);
				if (arg_val <= 0) {
					printf("Invalid pio address value\n");
					return -EINVAL;
				}
				flags->pio_w_addr = arg_val;
			}else if (!strcmp(lgopts[opt_idx].name, "pio_w_val")) {
				if (flags->fpio_w_val != false) {
					printf("ERR: PIO write val can not be repeated\n");
					return -EEXIST;
				}
				if (flags->fpio_w_addr == false) {
					printf("ERR: PIO write address is not provided\n");
					return -EINVAL;
				}
				flags->fpio_w_val = true;
				arg_val = parse_long_hex(optarg);
				if (arg_val <= 0) {
					printf("Invalid pio address value\n");
					return -EINVAL;
				}
				flags->pio_w_val = arg_val;
			}
                        break;
		case 'y':
			arg_val = strtol(optarg, &end, 10);
			if (arg_val <= 0) {
				printf("Invalid -y value\n");
				return -EINVAL;
			}
			flags->read_counter = (unsigned int)arg_val;
			flags->params_mask |= PERFQ_ARG_READ_COUTNER;
			break;
		case 'a':
			if (flags->num_threads != 0) {
				printf("ERR: Flags can not be repeated\n");
				return -EEXIST;
			}
			arg_val = parse_long_int(optarg);
			if (arg_val <= 0) {
				printf("Invalid -a value\n");
				return -EINVAL;
			}
			flags->num_threads = (unsigned int)arg_val;
			flags->params_mask |= PERFQ_ARG_NUM_THREADS;
			break;
		case 'n':
#ifndef IFC_QDMA_ST_MULTI_PORT
#ifdef IFC_QDMA_ST_PACKETGEN
                        flags->single_fn = 1;
                        printf("Simultaneous processes hangs with -n option\n");
#else
                        printf("ERR: -n only supported for single port AVST Pkt gen\n");
                        return -EINVAL;
#endif
#endif
			flags->params_mask |= PERFQ_ARG_SINGLE_FN;
			break;
		case 'b':
			if (fbdf == true) {
				printf("ERR: Flags can not be repeated\n");
				return -EEXIST;
			}
#ifndef IFC_MCDMA_SINGLE_FUNC
			printf("ERR: BDF not allowed when Multi PF enabled \n");
			return -EINVAL;
#endif
			ifc_qdma_strncpy(flags->bdf, sizeof(flags->bdf), optarg, 20);
			fbdf = true;
			flags->params_mask |= PERFQ_ARG_SINGLE_BDF;
			break;
		case 'c':
			if (flags->chnls != 0) {
				printf("ERR: Flags can not be repeated\n");
				return -EEXIST;
			}
			arg_val = parse_long_int(optarg);
			if (arg_val <= 0) {
				printf("Invalid -c value\n");
				return -EINVAL;
			}
			flags->chnls = (int)arg_val;
			flags->params_mask |= PERFQ_ARG_NUM_CHNLS;
			break;
		case 'd':
			if (flags->interval != 0UL) {
				printf("ERR: Flags can not be repeated\n");
				return -EEXIST;
			}
			arg_val = parse_long_int(optarg);
			if (arg_val == -1) {
				printf("Invalid -d value\n");
				return -EINVAL;
			}
			flags->interval = (unsigned int)arg_val;
			flags->params_mask |= PERFQ_ARG_DEBUG;
			break;
		case 'f':
#ifndef IFC_MCDMA_FUNC_VER
#ifndef IFC_QDMA_ST_MULTI_PORT
			printf("No -f flag support in Single port MCDMA\n");
			return -EINVAL;
#endif
#endif //IFC_MCDMA_FUNC_VER
			if (flags->params_mask & PERFQ_ARG_FILE_SIZE) {
                                printf("ERR: Flags can not be repeated\n");
                                return -EEXIST;
			}
			arg_val = parse_long_int(optarg);
			if (arg_val <= 0) {
				printf("Invalid -f value\n");
				return -EINVAL;
			}
#ifndef IFC_MCDMA_FUNC_VER
#ifdef PERFQ_PERF
			printf("No file size support in Performance Mode, undefine PERFQ_PERF in common.mk\n");
			return -EINVAL;
#endif
#endif //IFC_MCDMA_FUNC_VER
#ifndef IFC_QDMA_INTF_ST
			printf("No file size Support in AVMM\n");
			return -EINVAL;
#else
			flags->file_size = (unsigned long)arg_val;
			flags->params_mask |= PERFQ_ARG_FILE_SIZE;
			break;
#endif
		case 'p':
                        if (flags->packet_size != 0UL) {
                                printf("ERR: Flags can not be repeated\n");
                                return -EEXIST;
                        }
			arg_val = parse_long_int(optarg);
			if (arg_val <= 0) {
				printf("Invalid -p value\n");
				return -EINVAL;
			}
			flags->packet_size = (unsigned long)arg_val;
			#if (IFC_DATA_WIDTH == 1024)
                        if ((flags->packet_size % 128) != 0) {
                                printf("Invalid -p value\n");
                                printf("Packet size must be a multiple of 128 bytes in 1024 bit data width support\n");
                                return -EINVAL;
                        }
                        #endif
			fpacket_size = true;
			flags->params_mask |= PERFQ_ARG_PKT_SIZE;
			break;
		case 'q':
			arg_val = strtol(optarg, &end, 10);
			if (arg_val <= 0) {
				printf("Invalid -p value\n");
				return -EINVAL;
			}
			flags->qdepth_per_page = (unsigned long)arg_val;
			flags->params_mask |= PERFQ_ARG_QUEUE_SIZE;
			break;
		case 'g':
			arg_val = strtol(optarg, &end, 10);
			if (arg_val <= 0) {
				printf("Invalid -g value\n");
				return -EINVAL;
			}
			flags->start_cid = (unsigned long)arg_val;
			break;
		case 'h':
			if (optind != argc) {
				printf("ERR: Invalid parameter: %s\n",
                                        argv[optind]);
				printf("Try help: -h\n");
				return -EINVAL;
			}
                        show_help();
			return -EINVAL;
		case 's':
			if (flags->request_size != 0UL) {
				printf("ERR: Flags can not be repeated\n");
				return -EEXIST;
			}
			arg_val = parse_long_int(optarg);
			if (arg_val <= 0) {
				printf("Invalid -s value\n");
				return -EEXIST;
			}
			if (flags->flimit != -1) {
				printf("Invalid Option\n");
				printf("ERR: Both -s & -l not allowed\n");
				printf("Try -h for help\n");
				return -EINVAL;
			}
			flags->request_size = (unsigned long)arg_val;
			flags->flimit = REQUEST_BY_SIZE;
			flags->params_mask |= PERFQ_ARG_TRANSFER_SIZE;
			break;
		case 'l':
			if (flags->time_limit != 0UL) {
				printf("ERR: Flags can not be repeated\n");
				return -EEXIST;
			}
			arg_val = parse_long_int(optarg);
			if (arg_val <= 0) {
				printf("Invalid -l value\n");
				return -EINVAL;
			}
			if (flags->flimit != -1) {
				printf("Invalid Option\n");
				printf("ERR: Both -s & -l not allowed\n");
				printf("Try -h for help\n");
				return -EINVAL;
			}
			flags->time_limit = (unsigned int)arg_val;
			flags->flimit = REQUEST_BY_TIME;
			flags->params_mask |= PERFQ_ARG_TRANSFER_TIME;
			break;
		case 't':
			if (flags->direction == REQUEST_TRANSMIT) {
				printf("ERR: Flags can not be repeated\n");
				return -EEXIST;
			}
			flags->direction = REQUEST_TRANSMIT;
			transmit_counter++;
			flags->params_mask |= PERFQ_ARG_TRANSFER_TX;
			break;
		case 'r':
			if (flags->direction == REQUEST_RECEIVE) {
				printf("ERR: Flags can not be repeated\n");
				return -EEXIST;
			}
			flags->direction = REQUEST_RECEIVE;
			transmit_counter++;
			flags->params_mask |= PERFQ_ARG_TRANSFER_RX;
			break;
		case 'u':
			if (flags->hol == true) {
				printf("ERR: Flags can not be repeated\n");
				return -EEXIST;
			}
			flags->direction = REQUEST_LOOPBACK;
			flags->hol = 1;
			transmit_counter++;
			flags->params_mask |= PERFQ_ARG_TRANSFER_U;
			break;
		case 'i':
#ifndef IFC_QDMA_INTF_ST
                        printf("No -i flag Support in AVMM use -u with AVMM\n");
                        return -EINVAL;
#else
			if (flags->direction == REQUEST_LOOPBACK) {
				printf("ERR: Flags can not be repeated\n");
				return -EEXIST;
			}
			flags->direction = REQUEST_LOOPBACK;
			transmit_counter++;
			flags->params_mask |= PERFQ_ARG_TRANSFER_I;
			break;
#endif
		case 'z':
#if defined(IFC_QDMA_ST_LOOPBACK) && defined(IFC_QDMA_MM_LOOPBACK)
			printf("No -z flag Support in AVMM\n");
			return -EINVAL;
#else
			if (flags->direction == REQUEST_BOTH) {
				printf("ERR: Flags can not be repeated\n");
				return -EEXIST;
			}
			flags->direction = REQUEST_BOTH;
			transmit_counter++;
			flags->params_mask |= PERFQ_ARG_TRANSFER_Z;
			break;
#endif
		case 'v':
#ifdef PERFQ_PERF
			printf("Please disable PERF mode to valiadate data\n");
			return -EINVAL;
#endif
			if (flags->fvalidation != false) {
				printf("ERR: Flags can not be repeated\n");
				return -EEXIST;
			}
			flags->fvalidation = true;
			flags->params_mask |= PERFQ_ARG_DATA_VAL;
			break;
		case 'o':
			if (flags->fpio != false) {
				printf("ERR: Flags can not be repeated\n");
				return -EEXIST;
			}
			flags->fpio = true;
			flags->params_mask |= PERFQ_ARG_PIO;
			break;
		case 'e':
			if (flags->fbas != false) {
				printf("ERR: Flags can not be repeated\n");
				return -EEXIST;
			}
			flags->fbas = true;
			flags->params_mask |= PERFQ_ARG_BAS;
			break;
		case 'w':
			flags->comp_policy = (int)strtoul(optarg, &end, 10);
			if ((flags->comp_policy < 0) ||
			     (flags->comp_policy > 2)) {
				printf("Invalid Option\n");
				printf("Try -h for help\n");
				return -EINVAL;
			}
			break;
		case 'x':
#ifndef IFC_MCDMA_FUNC_VER
#ifdef PERFQ_PERF
			printf("Please disable PERF mode to change batch size\n");
			return -EINVAL;
#endif
#endif //IFC_MCDMA_FUNC_VER
			if (flags->params_mask & PERFQ_ARG_BATCH) {
                                printf("ERR: Flags can not be repeated\n");
                                return -EEXIST;
			}
			arg_val = parse_long_int(optarg);
			if (arg_val <= 0) {
				printf("Invalid -x value\n");
				return -EINVAL;
			}
			flags->batch_size = (unsigned long)arg_val;
			flags->params_mask |= PERFQ_ARG_BATCH;
			break;
		case '?':
			printf("Invalid option\n");
			printf("Try -h for help\n");
			return -EINVAL;
		default:
			printf("Try -h for help\n");
			return -EINVAL;
		}
	}

	if (optind != argc) {
		printf("ERR: Invalid parameter: %s\n", argv[optind]);
		printf("Try -h for help\n");
		return -EDOM;
	}


#ifdef IFC_MCDMA_SINGLE_FUNC
	if (!fbdf) {
		printf("ERR: Missing BDF\n");
		printf("Try -h for help\n");
		return -ENODEV;
	}
#endif
#if defined(IFC_QDMA_INTF_ST) && defined(IFC_QDMA_ST_LOOPBACK)
        if(flags->flimit == REQUEST_BY_SIZE && flags->hol == false && flags->fbas_perf == false && flags->fbas == false )
        {
                printf("ERR: -s option cannout be used without -u option\n");
                return -EPERM;
        }
#if defined(IFC_MCDMA_FUNC_VER)
        if((flags->params_mask & PERFQ_ARG_FILE_SIZE) && (flags->flimit == REQUEST_BY_TIME) && (flags->file_size > 1))
        {
                printf("ERR: File Size must be 1 when Request by Time  option is used\n");
                return -EPERM;
        }
#endif
#endif
 	if (fbar && !(flags->wbar == 0 || flags->wbar == 2 || flags->wbar == 4)) {
		printf("ERR: Invalid BAR to configure\n");
		return -EACCES;
	}
	if (flags->fpio)
		return 0;
	if (flags->fbas || flags->fbas_perf) {
		if (flags->fbas) {
			if ((flags->params_mask == BAS_EXPECTED_MASK1) ||
			    (flags->params_mask == BAS_EXPECTED_MASK2) ||
			    (flags->params_mask == BAS_EXPECTED_MASK3) ||
			    (flags->params_mask == BAS_EXPECTED_MASK4)) {
				if ((flags->request_size %
				     IFC_MCDMA_BAS_BURST_BYTES) == 0)
					return 0;
				else {
					printf("ERR: Request size needs to be"
						" multiple of %d\n",
						IFC_MCDMA_BAS_BURST_BYTES);
					return -EPERM;
				}
			}
		} else {
			if ((flags->params_mask == BAS_PERF_EXPECTED_MASK1) ||
			    (flags->params_mask == BAS_PERF_EXPECTED_MASK2)) {
				if ((flags->request_size %
				     IFC_MCDMA_BAS_BURST_BYTES) == 0)
					return 0;
				else {
					printf("ERR: Request size needs to be"
						" multiple of %d\n",
						IFC_MCDMA_BAS_BURST_BYTES);
					return -EPERM;
				}
			}
		}
		printf("Required parameters: b & (t | r | z | i) & s & e\n");
		printf("Required parameters for perf: b & (z | i) & s & --bas_perf\n");
		return -EINVAL;
	}

	if (flags->read_counter)
		return 0;

	if (flags->chnls == 0) {
		printf("ERR: Channels needs to be specified\n");
		printf("Try -h for help\n");
		return -EINVAL;
	}

#ifdef IFC_QDMA_INTF_ST
#ifdef IFC_QDMA_ST_LOOPBACK
	if (flags->chnls > AVST_CHANNELS) {
		printf("AVST supports %d channels\n", AVST_CHANNELS);
		return -EINVAL;
	}
#endif
#else
	if (flags->chnls > AVMM_CHANNELS) {
		printf("AVMM supports %d channels\n", AVMM_CHANNELS);
		return -EINVAL;
	}
#endif

#ifndef IFC_MCDMA_FUNC_VER
/* single port should have batch size same as file size */
#ifndef IFC_QDMA_ST_MULTI_PORT
        flags->file_size = 1;
#endif
#endif //IFC_MCDMA_FUNC_VER

#ifdef IFC_QDMA_INTF_ST
#ifdef IFC_QDMA_ST_LOOPBACK
	if (flags->direction == REQUEST_TRANSMIT ||
	    flags->direction == REQUEST_RECEIVE ||
	    flags->direction == REQUEST_BOTH) {
		printf("Loopback doesn't support t/r/z flags\n");
		return -EINVAL;
	}
#else
	if (flags->direction == REQUEST_LOOPBACK) {
		printf("Packet Gen doesn't support -i flag\n");
		return -EINVAL;
	}
#endif
#endif

#ifndef IFC_MCDMA_FUNC_VER
/* performance mode overwrite */
#ifdef PERFQ_PERF
        //flags->file_size = 64;
	flags->batch_size = DEFAULT_BATCH_SIZE;
	if (((flags->direction == REQUEST_LOOPBACK) ||
              (flags->direction == REQUEST_BOTH)) &&
	    (flags->num_threads < (flags->chnls * 2))) {
		printf("Number of threads should be same as number of queues for better BW\n");
	}
#endif
#endif

#ifdef IFC_ED_CONFIG_TID_UPDATE
//	flags->file_size = flags->batch_size;
	flags->file_size = 1;
#endif

#if 0
#ifdef IFC_QDMA_INTF_ST
#ifndef IFC_QDMA_ST_MULTI_PORT
	if (flags->file_size > 1 &&
		(flags->file_size != flags->batch_size)) {
		printf("ERR: Single Port Design should have equal batch and file size\n");
		printf("\t Current Config: Batch size: %lu File size: %lu\n",
			flags->batch_size, flags->file_size);
		return -EINVAL;
	}
#endif
#endif
#endif
	queue_count = count_queues(flags);

	/* check for required flags */
	if (flags->num_threads == 0) {
		printf("Number of threads should be passed\n");
		return -EINVAL;
	}

	/* check whether number of threads are more then required */
	if ((flags->num_threads > queue_count) ||
	    (queue_count % flags->num_threads)) {
		printf("Threads must be less or factor of queue count\n");
		printf("\tQueue count: %u Thread count %u\n",
			queue_count, flags->num_threads);
		return -EINVAL;
	}

	/* check whether qdepth_per_page is a power of 2 */
	if (!ispowerof2(flags->qdepth_per_page)) {
		printf("Queue length must be a power of 2\n");
		return -EINVAL;
	}

	/* check for required flags */
	if (!fpacket_size) {
		printf("Try -h for help\n");
		return -EINVAL;
	}

	if (flags->packet_size > 1048576) {
		printf("Payload Size must be less than 1MB\n");
		return -ENOMEM;
	}

#ifdef IFC_QDMA_INTF_ST
#ifdef IFC_QDMA_ST_LOOPBACK
	if (flags->file_size > 1) {
		if (flags->packet_size % 64 != 0) {
			if (flags->packet_size != IFC_MTU_LEN) {
				printf("Packet Size must be a multiple of 64 or MTU size, if file size > 1\n");
				return -EINVAL;
			}
		}
	}
	else if (flags->file_size == 1){
		if (flags->packet_size % 4 != 0) {
			printf("Packet Size must be a multiple of 4, if file size = 1\n");
			return -EINVAL;
		}
	}
#endif
#ifdef IFC_QDMA_ST_PACKETGEN
	if (flags->packet_size % 64 != 0) {
		if (flags->packet_size != IFC_MTU_LEN) {
			printf("Payload Size must be a multiple of 64 or MTU size in AVST Packet Gen\n");
			return -EINVAL;
		}
	}
#endif
#endif
#ifdef IFC_QDMA_MM_LOOPBACK
	if (flags->packet_size % 4 != 0) {
		printf("Payload size must be a multiple of 4 in AVMM\n");
		return -EINVAL;
	}

#endif

	/* Check for only one of -t, -r, -u and -z are set */
	if (transmit_counter) {
		printf("Try -h for help\n");
		return -EINVAL;
	}

	/* check for only one of -s and -l are set */
	if (flags->flimit == -1) {
		printf("Try -h for help\n");
		return -EINVAL;
	}

	if (flags->flimit == REQUEST_BY_SIZE) {
		if (flags->request_size < flags->packet_size) {
			printf("ERR: Invalid Option\n");
			printf("ERR: Request Size is smaller than Packet "
				"Size\n");
#ifdef IFC_32BIT_SUPPORT
			printf("\tRequest_size: %zu < Packet_size: %zu\n",
				flags->request_size, flags->packet_size);
#else
			printf("\tRequest_size: %lu < Packet_size: %lu\n",
				flags->request_size, flags->packet_size);
#endif
			return -EINVAL;
		}
		flags->packets = flags->request_size / flags->packet_size;
#ifdef IFC_QDMA_ST_PACKETGEN
#ifndef IFC_ED_CONFIG_TID_UPDATE
		if (flags->packets >= (1 << PIO_REG_PORT_PKT_CONFIG_FILES_WIDTH)) {
			printf("ERR: Number of descriptors count reached max\n");
			printf("ERR: Try with reduced data size\n");
			return -EINVAL;
		}
#endif //IFC_ED_CONFIG_TID_UPDATE
#endif

		/* request_size is not divisible by packet_size */
		if (flags->request_size % flags->packet_size) {
			/* allowed only for loopback with request of less than QDEPTH */
			if (flags->direction == REQUEST_LOOPBACK && flags->packets < QDEPTH) {
				flags->packets++;
			} else {
				printf("ERR: Invalid Option\n");
				printf("ERR: Request Size is not modulus"
					" of Packet size\n");
#ifdef IFC_32BIT_SUPPORT
				printf("\tRequest_size: %zu  Packet_size: %zu\n",
					flags->request_size, flags->packet_size);
#else
				printf("\tRequest_size: %lu  Packet_size: %lu\n",
					flags->request_size, flags->packet_size);
#endif
				return -EINVAL;
			}
		}
		else if ((flags->request_size > 512) && (flags->request_size % QDEPTH)) {
			printf("ERR: Request_size must be mod of QDEPTH = %d\n", QDEPTH);
			return -EINVAL;
		}
	}

	if (flags->batch_size >  PREFILL_QDEPTH) {
		printf("Invalid Option\n");
		printf("Error: Batch size is greater than queue size\n");
		return -EINVAL;
	}

#ifdef IFC_QDMA_INTF_ST
	if (flags->flimit == REQUEST_BY_SIZE &&
		flags->packets % flags->file_size) {
		printf("ERR: #descriptors should be aligned with #files\n");
		printf("ERR: Descriptors should be modulus of file size\n");
		printf("\tDescriptors specified: %lu File_size: %lu\n",
			flags->packets,flags->file_size);
		return -EINVAL;
	}
#endif
	if ((flags->flimit == REQUEST_BY_TIME) &&
	    (flags->time_limit < 3) &&
	    (flags->packet_size > 8192)) {
		printf("Please run more number of seconds to capture correct values\n");
	}

#ifndef IFC_QDMA_INTF_ST
	if ((flags->direction == REQUEST_LOOPBACK) &&
	     (flags->hol == 0) &&
	     (flags->fvalidation)) {
		printf("validation enablement not support with -i option\n");
		return -EINVAL;
	}
        if (((PERFQ_AVMM_BUF_LIMIT / (unsigned int)flags->chnls) < flags->packet_size)
	   && flags->fvalidation) {
		printf("%d Byets memory is connected\n", PERFQ_AVMM_BUF_LIMIT);
                printf("%d Bytes memory can be availed for %d channels\n", PERFQ_AVMM_BUF_LIMIT / (unsigned int)flags->chnls, flags->chnls);
                printf("Select Packet size less than %d Bytes\n", PERFQ_AVMM_BUF_LIMIT/ (unsigned int) flags->chnls);
		return -EINVAL;
	}
#endif

#if 0
#ifdef IFC_QDMA_INTF_ST
#ifndef PERFQ_LOAD_DATA
	if (flags->file_size != 127 && flags->file_size != 1) {
		printf("Invalid Option\n");
		printf("For flexible file size please switch on PERFQ_LOAD_DATA compilation flag\n");
		return -EINVAL;
	}
#endif
#endif
#endif

	if (flags->file_size > QDEPTH) {
		printf("Invalid Option\n");
		printf("file size can not be greater then QDEPTH\n");
		return -EINVAL;
	}

	flags->tx_batch_size = flags->batch_size;
	flags->rx_batch_size = flags->batch_size;
#if 0
#ifdef IFC_MCDMA_DIDF
	if (!(flags->batch_size == DIDF_ALLOWED_BATCH_SIZE)) {
		printf("ERR: For DIDF: Allowed batch_size: %d & file_size: %d\n",
			DIDF_ALLOWED_BATCH_SIZE, DIDF_ALLOWED_FILE_SIZE);
		printf("\tCurrently batch size: %lu file size %lu\n",
			flags->batch_size, flags->file_size);
		return -EINVAL;
	}
#endif
#endif

#if 0
#ifdef IFC_QDMA_ST_PACKETGEN
#ifdef PERFQ_PERF
	if (flags->direction != REQUEST_RECEIVE) {
		flags->tx_batch_size = (QDEPTH / 2) + 1;
	}
	if (flags->direction != REQUEST_TRANSMIT) {
		flags->rx_batch_size = (QDEPTH / 4) + 1;
		if (flags->chnls > 8) {
			flags->file_size = 1;
		}
		if (flags->chnls > 16) {
			//flags->rx_batch_size = 80;  // UIO
			flags->file_size = 10;
		}
		if (flags->chnls > 32) {
			flags->file_size = 8;
		}
	}
#endif	/* PERFQ_PERF */
#endif	/* IFC_QDMA_ST_PACKETGEN */
#endif

	flags->completion_mode = IFC_CONFIG_QDMA_COMPL_PROC;
#ifdef PROFILING
	if (flags->packet_size <= lower_pkt_size &&
	    flags->chnls >= lower_ch)
		flags->completion_mode = CONFIG_QDMA_QUEUE_REG;

	if (flags->packet_size <= lower_pkt_size &&
	    flags->batch_size <= lower_bs)
		flags->completion_mode = CONFIG_QDMA_QUEUE_REG;
#ifndef PERFQ_PERF
	if (flags->packet_size <= lower_pkt_size) {
		flags->pkt_gen_files = 1;
		/* Lower batch size: Register Mode
		 * Higher batch size: WB Mode
		 */
		if (flags->batch_size > lower_bs)
			flags->completion_mode = IFC_CONFIG_QDMA_COMPL_PROC;
	}
#endif
#endif
	if (flags->fcomp_mode)
		flags->completion_mode = comp_mode;
	if (flags->fpkt_gen_files)
		flags->pkt_gen_files = pktgen_files;
	else
#ifndef IFC_MCDMA_DIDF
#ifndef PERFQ_PERF
		flags->pkt_gen_files = flags->batch_size;
#endif
//#else
//		flags->pkt_gen_files = 1;
#endif

#ifdef IFC_MCDMA_EXTERNL_DESC
	if (flags->packet_size > PERFQ_EXT_MAX_PAYLOAD) {
		printf("ERR: Invalid Payload. Max Payload supported %d Bytes\n",
		       PERFQ_EXT_MAX_PAYLOAD);
		return -EINVAL;
	}
	if (flags->chnls > PERFQ_EXT_MAX_CH ||
	    flags->packets > PERFQ_EXT_MAX_DESC) {
	    printf("ERR: Invalid input for External desc fetch design\n");
	    printf("Valid: Max chnls: %d Max Desc: %d Mode: Request by size\n",
		   PERFQ_EXT_MAX_CH, PERFQ_EXT_MAX_DESC);
	    return -EINVAL;
	}
	/* Allow only TX and RX */
	if ((((flags->direction == REQUEST_LOOPBACK) ||
	    (flags->direction == REQUEST_BOTH)) &&
	    (flags->flimit == REQUEST_BY_TIME)))  {
	    printf("ERR: Invalid input for External desc fetch design\n");
	    return -EINVAL;
	}
	/* If it is -l, allow only 1 channel */
	if ((flags->flimit == REQUEST_BY_TIME) &&
	    (flags->chnls > 1))  {
	    printf("ERR: Please pass number of channels as 1\n");
	    return -EINVAL;
	}
	if ((flags->flimit == REQUEST_BY_TIME) &&
	    (flags->fvalidation ))  {
	    printf("ERR: Validation check not supported\n");
	    return -EINVAL;
	}
#endif
	if (PREFILL_QDEPTH % flags->file_size) {
	    printf("ERR: Invalid file_size\n");
	    printf("ERR: Queue size should be divisible by file_size. change the file size or PREFILL_QDEPTH. qdepth:%u fs:%lu\n",
	    PREFILL_QDEPTH, flags->file_size);
	    return -EINVAL;
	}
	generate_file_name(flags->fname);
	return 0;
}

int should_app_exit(struct timespec *timeout_timer)
{
	struct timespec cur_time;
	struct timespec result;
	unsigned long timeout_val;

	memset(&result, 0, sizeof(struct timespec));
	clock_gettime(CLOCK_MONOTONIC, &cur_time);
	time_diff(&cur_time, timeout_timer, &result);
#ifdef IFC_QDMA_ST_PACKETGEN
	timeout_val = PERFQ_TIMEOUT_PCKTGEN;
	if (result.tv_sec > (timeout_val / 1e6))
#else
	timeout_val = PERFQ_TIMEOUT_LOOPBACK;
	if (result.tv_sec ||
	    ((result.tv_nsec / 1e3) >= timeout_val))
#endif
		return true;

	return false;

}

static int
thread_monitor(struct thread_context *tctx, struct struct_flags *flags,
		   struct timespec *last_checked,
		   struct timespec *start_time)
{
	struct timespec timeout_timer;
	struct queue_context *qctx;
	int finished_counter;
	int threads_count;
	int queue_count;
	int i, q;
	int ready_count;

	threads_count = flags->num_threads;
	queue_count = count_queues(flags);

	/* Make sure all threads are ready */
	printf("Thread initialization in progress ...\n");
	while (true) {
		ready_count = 0;
		for (i = 0; i < threads_count; i++) {
			for(q = 0; q < tctx[i].qcnt; q++) {
				qctx = &(tctx[i].qctx[q]);
				if ((qctx->status == THREAD_READY)||
				    (qctx->status == THREAD_DEAD))
					ready_count++;
			}
		}

		if (ready_count == queue_count)
			break;
	}
	if (flags->memalloc_fails) {
		goto wait_for_last;
	}

	/* Let the data transfer begin */
	flags->ready = true;

	/* wait for all the threads to start their clocks */
	while (true) {
		ready_count = 0;
		for (i = 0; i < threads_count; i++) {
			for(q = 0; q < tctx[i].qcnt; q++) {
				qctx = &(tctx[i].qctx[q]);
				if ((qctx->fstart) ||
				    (qctx->status == THREAD_DEAD))
					ready_count++;
			}
		}
		if (ready_count == queue_count)
			break;
	}
	printf("Thread initialization done\n");

	/* Initialize the global clocks */
	clock_gettime(CLOCK_MONOTONIC, start_time);
	clock_gettime(CLOCK_MONOTONIC, last_checked);

	while (true) {

		if (flags->interval)
			should_update_progress(tctx, flags, last_checked,
					       start_time);

		/* reset the finished_counter */
		finished_counter = 0;
		for (i = 0; i < threads_count; i++) {
			for(q=0; q < tctx[i].qcnt; q++) {
				qctx = &(tctx[i].qctx[q]);
				/* are you alive? */
				if ((qctx->valid) && (qctx->status == THREAD_DEAD ||
						qctx->status == THREAD_ERROR_STATE)) {
					finished_counter++;

					/* poll if pending requests are present */
					if (qctx->completion_counter <
							qctx->request_counter) {
						request_completion_poll(qctx);
					}
				}
			}
		}
		if (finished_counter == queue_count)
			break;
	}
	printf("All Threads exited\n");

#ifdef IFC_QDMA_ST_LOOPBACK
	for (i = 0; i < threads_count; i++) {
		for (q = 0; q < tctx[i].qcnt; q++) {
			qctx = &(tctx[i].qctx[q]);
			if (qctx->direction == 0)
				continue;
			struct queue_context *rxctx =
				ifc_get_que_ctx(qctx->channel_id, 0);
			if (rxctx == NULL ||
			    qctx->request_counter > rxctx->request_counter)
				continue;
			uint32_t diff = rxctx->request_counter - qctx->request_counter;
			perfq_submit_requests(qctx, diff);
		}
	}
#endif
wait_for_last:

	/* threads have exited, wait for DMA to complete the requests */
	/* start the timeout timer */
	clock_gettime(CLOCK_MONOTONIC, &timeout_timer);

	while (true) {

		if (should_app_exit(&timeout_timer)) {
#ifdef IFC_QDMA_ST_PACKETGEN
			printf("TIME OUT while waiting for completions\n");
			printf("Leaving...\n");
#endif
			return -1;
		}

		/* reset the finished_counter */
		finished_counter = 0;

		if (flags->interval)
			should_update_progress(tctx, flags, last_checked,
						start_time);

		for (i = 0; i < threads_count; i++) {
			for (q = 0; q < tctx[i].qcnt; q++) {
				qctx = &(tctx[i].qctx[q]);
				if (qctx->completion_counter ==
						qctx->request_counter) {
					finished_counter++;
				} else {
					request_completion_poll(qctx);
				}
			}
		}
		if (finished_counter == queue_count)
			return 0;
	}
	return -1;
}


#ifdef IFC_MCDMA_EXTERNL_DESC
static int dump_ext_fetch_regs(struct ifc_qdma_device *qdev,
			       struct struct_flags *flags)
{
	int counter = flags->read_counter;
	int q;

	for (q = 0; q < counter; q++) {
		ifc_qdma_dbg_ext_fetch_chnl_qcsr(qdev, IFC_QDMA_DIRECTION_RX, q);
		ifc_qdma_dbg_ext_fetch_chnl_qcsr(qdev, IFC_QDMA_DIRECTION_TX, q);
	}
	return 0;
}
#else
static int pio_read(struct ifc_qdma_device *qdev, struct struct_flags *flags)
{
	int counter = flags->read_counter;
#ifdef IFC_32BIT_SUPPORT
	uint32_t offset;
	uint32_t base;
	uint32_t rvalue;
#else
	uint64_t offset;
	uint64_t base;
	uint64_t rvalue;
#endif
	uint32_t *ptr;
	base = 0x10000;
	offset = 0x40;
	int i;
//TODO 32 BIT PIO w/r
#ifdef IFC_32BIT_SUPPORT
	printf("Starting address: 0x%x, counter: %d\n", base, counter);
#else
	printf("Starting address: 0x%lx, counter: %d\n", base, counter);
#endif
	for (i = 0; i < 2; i++) {
#ifdef IFC_32BIT_SUPPORT
		rvalue = ifc_qdma_pio_read32(qdev, base + offset);
#else
		rvalue = ifc_qdma_pio_read64(qdev, base + offset);
#endif
		ptr = (uint32_t *)(&rvalue);
		printf("Addr: 0x%lx\tVal: 0x%x 0x%x\n",
				(unsigned long)(base + offset), *ptr,
				*(ptr + 1));
#ifdef IFC_32BIT_SUPPORT
		offset += 32;
#else
		offset += 64;
#endif
	}

	base = 0x20000;
	offset = 0x00;

#ifdef IFC_32BIT_SUPPORT
	printf("Starting address: 0x%x, counter: %d\n", base, counter);
#else
	printf("Starting address: 0x%lx, counter: %d\n", base, counter);
#endif
	if (flags->direction == REQUEST_BOTH ||
	     flags->direction == REQUEST_TRANSMIT) {
		for (i = 0; i < counter; i++) {
#ifdef IFC_32BIT_SUPPORT
			rvalue = ifc_qdma_pio_read32(qdev, base + offset);
#else
			rvalue = ifc_qdma_pio_read64(qdev, base + offset);
#endif
			ptr = (uint32_t *)(&rvalue);
			printf("Addr: 0x%lx\tVal: 0x%x 0x%x\n",
				(unsigned long)(base + offset), *ptr,
				*(ptr + 1));
#ifdef IFC_32BIT_SUPPORT
			offset += 4;
#else
			offset += 8;
#endif
		}
	}

	base = 0x30000;
	offset = 0x00;

#ifdef IFC_32BIT_SUPPORT
	printf("Starting address: 0x%x, counter: %d\n", base, counter);
#else
	printf("Starting address: 0x%lx, counter: %d\n", base, counter);
#endif
	if (flags->direction == REQUEST_BOTH ||
	     flags->direction == REQUEST_RECEIVE) {
		for (i = 0; i < counter; i++) {
#ifdef IFC_32BIT_SUPPORT
			rvalue = ifc_qdma_pio_read32(qdev, base + offset);
#else
			rvalue = ifc_qdma_pio_read64(qdev, base + offset);
#endif
			ptr = (uint32_t *)(&rvalue);
			printf("Addr: 0x%lx\tVal: 0x%x 0x%x\n",
				(unsigned long)(base + offset), *ptr,
				*(ptr + 1));
#ifdef IFC_32BIT_SUPPORT
			offset += 4;
#else
			offset += 8;
#endif
		}
	}
	return 0;
}
#endif

int context_init_split(struct struct_flags *flags, struct thread_context **ptr)
{
#ifndef IFC_QDMA_INTF_ST
#ifdef IFC_32BIT_SUPPORT
        uint32_t mem_reg_size = PERFQ_AVMM_BUF_LIMIT / flags->chnls;
#else
        uint64_t mem_reg_size = PERFQ_AVMM_BUF_LIMIT / flags->chnls;
#endif
#endif
	struct queue_context *q;
	int entries, ret;
	int i, tid, qno;
	int tx_entries = 0, rx_entries = 0;
	uint32_t cid = flags->start_cid;
#ifdef IFC_QDMA_INTF_ST
	int max_chnls = AVST_CHANNELS;
#else
	int max_chnls = AVMM_CHANNELS;
#endif

	entries = count_queues(flags);
	int queues_per_thread = (entries/flags->num_threads);

	/* TODO: don't malloc for non-loopback modes */
	/* malloc memory for loop locks */
	flags->loop_locks = (struct loop_lock *)malloc(max_chnls * sizeof(struct loop_lock));
	if (flags->loop_locks == NULL)
		return -1;

	/* malloc memory for locks */
	flags->locks = (pthread_mutex_t *)malloc(max_chnls * sizeof(pthread_mutex_t));
	if (flags->locks == NULL)
		return -1;

	/* init the locks */
	for (i = 0; i < max_chnls; i++) {
		/* TODO: put some checks for the return value */
		pthread_mutex_init(&(flags->locks[i]), NULL);
#ifdef IFC_QDMA_INTF_ST
		/* Ordering rx -> tx -> rx ... */
		sem_init(&((flags->loop_locks[i]).tx_lock), 0, 0);
		sem_init(&((flags->loop_locks[i]).rx_lock), 0, 1);
#else
		/* Ordering tx -> rx -> tx... */
		sem_init(&((flags->loop_locks[i]).tx_lock), 0, 1);
		sem_init(&((flags->loop_locks[i]).rx_lock), 0, 0);
#endif
	}

	/* malloc memory for thread contexts, it's an array */
	*ptr = (struct thread_context *)
		malloc(flags->num_threads * sizeof(struct thread_context));
	if (!(*ptr)) {
		printf("Failed to allocate context for the threads\n");
		cleanup(global_qdev, flags, NULL);
		return -1;
	}

	/* initialize request_context */
	memset(*ptr, 0, flags->num_threads * sizeof(struct thread_context));

	if (flags->direction == REQUEST_LOOPBACK || flags->direction == REQUEST_BOTH) {
		tx_entries = entries/2;
		rx_entries = entries/2;
	} else if (flags->direction == REQUEST_RECEIVE) {
		rx_entries = entries;
	} else {
		tx_entries = entries;
	}

	tid = 0;
	qno = 0;
	for (i = 0; i < tx_entries; i++) {
		q = &((*ptr + tid)->qctx[qno++]);
		q->flags = flags;
		q->valid = 1;
		q->status = THREAD_NEW;
		q->channel_id = cid;
		cid = ((cid + 1) % max_chnls);
		q->direction = 1;
#ifndef IFC_QDMA_INTF_ST
		q->base = i * mem_reg_size;
#endif
#ifndef IFC_QDMA_INTF_ST
		q->dest = q->src = q-> base;
		q->limit = q->base + mem_reg_size;
#endif
                ret = queue_init(q);
                if (ret != PERFQ_SUCCESS) {
			printf("ERR: Queue initialization failed\n");
			return -1;
                }

		if (qno == queues_per_thread) {
			tid++;
			qno = 0;
		}

		/* Set batch & file size */
		q->file_size =  flags->file_size;
		if (q->direction == REQUEST_TRANSMIT)
			q->batch_size = flags->tx_batch_size;
		else
			q->batch_size = flags->rx_batch_size;
	}

	qno = 0;
	cid = flags->start_cid;
	for (i = 0; i < rx_entries; i++) {
		q = &((*ptr + tid)->qctx[qno++]);
		q->flags = flags;
		q->valid = 1;
		q->status = THREAD_NEW;
		q->channel_id = cid;
		cid = ((cid + 1) % max_chnls);
		q->direction = 0;
#ifndef IFC_QDMA_INTF_ST
			q->base = i * mem_reg_size;
#endif
#ifndef IFC_QDMA_INTF_ST
		q->dest = q->src = q-> base;
		q->limit = q->base + mem_reg_size;
#endif
                ret = queue_init(q);
                if (ret != PERFQ_SUCCESS) {
			printf("TEST: Queue initialization failed\n");
                }

		if (qno == queues_per_thread) {
			tid++;
			qno = 0;
		}

		/* Set batch & file size */
		q->file_size =  flags->file_size;
		if (q->direction == REQUEST_TRANSMIT)
			q->batch_size = flags->tx_batch_size;
		else
			q->batch_size = flags->rx_batch_size;
	}
	return 0;
}

static int get_dev_id_from_phy_ch(int pch, int *devid, int *lch)
{
	int sum_pf_ch;
	int num_ch_per_vf;
	int num_ch_per_pf;

	num_ch_per_pf = IFC_QDMA_PER_PF_CHNLS;
	sum_pf_ch = IFC_QDMA_PFS * num_ch_per_pf;
	if (pch < sum_pf_ch) {
		/* This channel number belongs to PF. */
		*devid = pch / num_ch_per_pf;
		*lch = pch % num_ch_per_pf;
	} else {
		num_ch_per_vf = IFC_QDMA_PER_VF_CHNLS;
		/* This channel number belongs to VF */
		if (num_ch_per_vf == 0) {
			/* VF do not have channels */
			return -1;
		}
		uint32_t vf = ((pch - sum_pf_ch) / num_ch_per_vf);
		*lch = (pch - sum_pf_ch) % num_ch_per_vf;
		*devid = IFC_QDMA_PFS + vf;
	}
	return 0;
}

int context_init(struct struct_flags *flags, struct thread_context **ptr)
{
	struct queue_context *q;
	int id_counter = 0;
	int entries, ret;
	int i, tid, qno;
	int devid;

#ifndef IFC_QDMA_INTF_ST
	int port = ifc_mcdma_port_by_name(global_flags->bdf);
	if (port < 0)
		return 1;
#ifdef 	IFC_32BIT_SUPPORT
        uint32_t mem_reg_size = (PERFQ_AVMM_BUF_LIMIT / get_num_ports());
#else
        uint64_t mem_reg_size = (PERFQ_AVMM_BUF_LIMIT / get_num_ports());
#endif
#if defined(IFC_QDMA_MM_LOOPBACK)
        if ((flags->packet_size * flags->chnls) > (size_t)(PERFQ_AVMM_BUF_LIMIT/get_num_ports())) {
                printf("AVMM memory exceeding %dK\n", ((PERFQ_AVMM_BUF_LIMIT/get_num_ports())/1024));
                return -1;
        }
#endif
#endif

	entries = count_queues(flags);
	int queues_per_thread = (entries/flags->num_threads);

	/* TODO: don't malloc for non-loopback modes */
	/* malloc memory for loop locks */
	flags->loop_locks = (struct loop_lock *)malloc(flags->chnls * sizeof(struct loop_lock));
	if (flags->loop_locks == NULL)
		return -1;

	/* malloc memory for locks */
	flags->locks = (pthread_mutex_t *)malloc(flags->chnls * sizeof(pthread_mutex_t));
	if (flags->locks == NULL)
		return -1;

	/* init the locks */
	for (i = 0; i < flags->chnls; i++) {
		/* TODO: put some checks for the return value */
		pthread_mutex_init(&(flags->locks[i]), NULL);
#ifdef IFC_QDMA_INTF_ST
		/* Ordering rx -> tx -> rx ... */
		sem_init(&((flags->loop_locks[i]).tx_lock), 0, 0);
		sem_init(&((flags->loop_locks[i]).rx_lock), 0, 1);
#else
		/* Ordering tx -> rx -> tx... */
		sem_init(&((flags->loop_locks[i]).tx_lock), 0, 1);
		sem_init(&((flags->loop_locks[i]).rx_lock), 0, 0);
#endif
	}

	/* malloc memory for thread contexts, it's an array */
	*ptr = (struct thread_context *)
		malloc(flags->num_threads * sizeof(struct thread_context));
	if (!(*ptr)) {
		printf("Failed to allocate context for the threads\n");
		cleanup(global_qdev, flags, NULL);
		return -1;
	}

	/* initialize request_context */
	memset(*ptr, 0, flags->num_threads * sizeof(struct thread_context));

	tid = 0;
	qno = 0;
	for (i = 0; i < entries; i++) {
		q = &((*ptr + tid)->qctx[qno++]);
		q->flags = flags;
		q->valid = 1;
		q->status = THREAD_NEW;
		if ((flags->direction == REQUEST_LOOPBACK) || (flags->direction == REQUEST_BOTH)) {
			q->phy_channel_id = id_counter;
#ifdef IFC_QDMA_INTF_ST
			q->direction = i % 2;
#else
			q->direction = (i + 1) % 2;
			q->base = (id_counter * (mem_reg_size/flags->chnls)) + (port * mem_reg_size);
#endif
			id_counter += i % 2;

		} else {
			q->phy_channel_id = i;
			q->direction = flags->direction;
#ifndef IFC_QDMA_INTF_ST
			q->base = (i * (mem_reg_size/flags->chnls)) + (port * mem_reg_size);
#endif
		}
#ifndef IFC_QDMA_INTF_ST
		q->dest = q->src = q->base;
		q->limit = (mem_reg_size/flags->chnls);
#endif
		ret = get_dev_id_from_phy_ch(q->phy_channel_id, &devid, &(q->channel_id));
		if (ret < 0) {
			printf("Getting device ID failed\n");
			return -1;
		}
		q->qdev = gqdev[devid];
                ret = queue_init(q);
                if (ret != PERFQ_SUCCESS) {
			printf("Queue initialization failed\n");
			return -1;
                }

		if (qno == queues_per_thread) {
			tid++;
			qno = 0;
		}

		/* Set batch & file size */
		q->file_size =  flags->file_size;
		if (q->direction == REQUEST_TRANSMIT)
			q->batch_size = flags->tx_batch_size;
		else
			q->batch_size = flags->rx_batch_size;
	}
	return 0;
}

enum perfq_status thread_init(struct thread_context *tctx)
{
	int ret;
	signal_mask();

	if (pthread_mutex_lock(&dev_lock) != 0) {
		printf("acquiring mutex failed\n");
		return PERFQ_CH_INIT_FAILURE;
	}
	ret = core_assign(tctx);
	if (ret) {
		printf("thread%d: Failed to set thread affinity\n",
				tctx->tid);
		if (pthread_mutex_unlock(&dev_lock) != 0) {
			printf("releasing mutex failed\n");
			return -1;
		}
		return PERFQ_CORE_ASSGN_FAILURE;
	}
	if (pthread_mutex_unlock(&dev_lock) != 0) {
		printf("releasing mutex failed\n");
		return -1;
	}

	return PERFQ_SUCCESS;
}

void* queues_schedule(void *ptr)
{
	int i, entries, finished_counter;
	struct thread_context *tctx = (struct thread_context *)ptr;
	entries = count_queues(global_flags);
	tctx->qcnt = entries/global_flags->num_threads;
	static int ret = 0;

	/* Disable cancel signal till init completes */
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	thread_init(tctx);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	while (true) {
		ret++;
		finished_counter = 0;
		for (i = 0; i < tctx->qcnt; i++) {
			if (global_flags->memalloc_fails == 1) {
				tctx->qctx[i].status = THREAD_DEAD;
				finished_counter++;
				continue;
			}
			if (tctx->qctx[i].status == THREAD_DEAD) {
				finished_counter++;
				continue;
			}

			if ((tctx->qctx[i].status != THREAD_NEW) &&
			   (tctx->qctx[i].status != THREAD_WAITING))
				tctx->qctx[i].status = THREAD_READY;

			tctx->qctx[i].epoch_done = 0;
			clock_gettime(CLOCK_MONOTONIC, &tctx->qctx[i].start_time_epoch);

			if ((tctx->flags->direction == REQUEST_LOOPBACK) && (tctx->flags->hol)) {
				transfer_handler_loopback(&(tctx->qctx[i]));
			} else {
				transfer_handler(&(tctx->qctx[i]));
			}
		}
		if (finished_counter == tctx->qcnt) {
			break;
		}
	}
	return 0;
}

static int cpu_list_init(void)
{
	char str[512] = THREAD_SEQ;
	const char delims[DELIMLEN] = "-,";//This change s[2] to s[3] as ubuntu change has updated glibc 
	char *token;
	int start, end;
	int cur = 0, i;

	/* get the first token */
	token = strtok(str, delims);
	if (token == NULL)
		return -1;

	start = atoi(token);

	/* walk through other tokens */
	while( token != NULL ) {

		token = strtok(NULL, delims);
		if (token == NULL) {
			printf("cpu list format is wrong\n");
			break;
		}
		end = atoi(token);
		if (!end) break;
		for(i = start; i <= end; i++)
			cpu_list[cur++] = i;
		token = strtok(NULL, delims);
		if (token == NULL) break;
		start = atoi(token);
	}
	return 0;
}

int thread_creator(struct ifc_qdma_device *qdev, struct struct_flags *flags,
		   struct thread_context *tctx)
{
	int threads_count;
	int ret;
	int i;

	threads_count = flags->num_threads;

	for (i = 0; i < threads_count; i++) {

		/* change thread status */
		tctx[i].status = THREAD_RUNNING;
		tctx[i].flags = flags;
		tctx[i].tid = i;
		ret = pthread_create(&(tctx[i].pthread_id), NULL,
				    queues_schedule, (void *)(tctx + i));
		if (ret) {
			printf("Failed to create thread\n");
			tctx[i].status = THREAD_DEAD;
			cleanup(qdev, flags, tctx);
			return -1;
		}
	}
	return 0;
}

#ifdef VERIFY_HOL
int hol_stat_init(void)
{
	global_hol_stat.reg_config = reg_config;
	global_hol_stat.size = sizeof(reg_config) / sizeof(char);
	global_hol_stat.interval = HOL_REG_CONFIG_INTERVAL;

	return 0;
}
#endif

static uint32_t next_power_of_2(uint32_t n)
{
	unsigned c = 0;
	if (n && !(n & (n - 1)))
		return n;
	while( n != 0) {
		n >>= 1;
		c += 1;
	}
	return 1 << c;
}

static int mem_requirement_check(struct struct_flags *flags)
{
	int req, alloc;

	alloc = ifc_allocated_chunks();
	req = (unsigned long)QDEPTH * flags->chnls;
	if (flags->direction == REQUEST_BOTH || flags->direction == REQUEST_LOOPBACK)
		req *= 2;
	if (req > alloc){
                printf("Required memory(%u) is greater than allocated memory(%u) in -z option required double memory .\n ", req, alloc);
                return -1;
        }
	return 0;
}

int main(int argc, char *argv[])
{
	uint32_t chunk_size = 0;
	int avl_chnls;
#ifdef IFC_QDMA_ST_PACKETGEN
#ifndef IFC_QDMA_ST_MULTI_PORT
	int ch_cnt;
#endif
#endif
	int ret;
	int ret_bas;
#ifdef IFC_MCDMA_SINGLE_FUNC
	int port;
#endif
#ifndef IFC_MCDMA_SINGLE_FUNC
	int i;
	int devcnt;
#endif

	signal(SIGINT, sig_handler);
	signal(SIGUSR1, sig_handler);

	cur_assgn_core = 0;
	pthread_mutex_init(&dev_lock, NULL);

	/* allocate memory to struct_flags */
	global_flags = (struct struct_flags *)
			calloc(1,sizeof(struct struct_flags));
	if (!global_flags) {
		printf("Failed to allocate memory for flags\n");
		return -ENOMEM;
	}

	ret = cmdline_option_parser(argc, argv, global_flags);
	if (ret) {
		free(global_flags);
		return ret;
	}

	ret = cpu_list_init();
	if (ret) {
		printf("cpu list initialization not done error\n");
	}

#ifdef VERIFY_FUNC
	dump_flags(global_flags);
#endif
#ifdef VERIFY_HOL
	hol_stat_init();
#endif
	chunk_size = next_power_of_2(global_flags->packet_size);
	if (global_flags->fbas || global_flags->fbas_perf)
		chunk_size = IFC_MCDMA_BUF_LIMIT;
#ifndef UIO_SUPPORT
        set_num_channels(global_flags->chnls);
#endif
	ret = ifc_app_start(global_flags->bdf, chunk_size);
	if (ret) {
		printf("Failed to start application ret:%u\n", ret);
		return ret;
	}

#ifdef IFC_MCDMA_SINGLE_FUNC
	port = ifc_mcdma_port_by_name(global_flags->bdf);
	if (port < 0)
		return 1;

	/*get DMA device*/
	ret = ifc_qdma_device_get(port, &global_qdev,
				  global_flags->qdepth_per_page,
				  global_flags->completion_mode);
	if (ret) {
		cleanup(NULL, global_flags, NULL);
		return 1;
	}
	gqdev[0] = global_qdev;
#else
	devcnt =  IFC_QDMA_PFS + (IFC_QDMA_PFS * IFC_QDMA_PER_PF_VFS);
	for (i = 0; i < devcnt; i++) {
		/*get DMA device*/
		ret = ifc_qdma_device_get(i, &(gqdev[i]),
				global_flags->qdepth_per_page,
				global_flags->completion_mode);
		if (ret) {
			break;
		}
	}
	printf("Acquired %u functions\n", i);
	global_qdev = gqdev[0];
#endif

	if (global_flags->fbas || global_flags->fbas_perf) {
		ret_bas = bas_test(global_qdev, global_flags);
		cleanup(NULL, global_flags, NULL);
		return ret_bas;
	}

	/* check for PIO Read Write */
	if (global_flags->fpio) {
		if(global_flags->pio_r_addr){
			ret = pio_perf_read(global_qdev, global_flags->pio_r_addr);
			cleanup(global_qdev, global_flags, NULL);
			return ret;
		}
		if(global_flags->pio_w_addr){
			ret = pio_perf_write(global_qdev, global_flags->pio_w_addr,
			      global_flags->pio_w_val);
			cleanup(global_qdev, global_flags, NULL);
			return ret;
		}
#ifdef IFC_PIO_256
		if (global_flags->fbam_perf)
			ret = pio_perf_util_256(global_qdev);
		else
			ret = pio_util_256(global_qdev);
#endif
#ifdef IFC_PIO_MIX
		if (global_flags->fbam_perf)
			ret = pio_perf_util_mixed(global_qdev);
		else
			ret = pio_util_mixed(global_qdev);
#endif
#ifdef IFC_PIO_128
		if (global_flags->fbam_perf)
			ret = pio_perf_util_128(global_qdev);
		else
			ret = pio_util_128(global_qdev);
#endif
#ifdef IFC_PIO_64
		if (global_flags->fbam_perf)
			ret = pio_perf_util(global_qdev);
		else
			ret = pio_util(global_qdev);
#endif
#ifdef IFC_PIO_32
		if (global_flags->fbam_perf)
			ret = pio_perf_util_32(global_qdev);
		else
			ret = pio_util_32(global_qdev);
#endif
		cleanup(global_qdev, global_flags, NULL);
		return ret;
	}

	if (global_flags->read_counter) {
#ifdef IFC_MCDMA_EXTERNL_DESC
		ret = dump_ext_fetch_regs(global_qdev, global_flags);
#else
		ret = pio_read(global_qdev, global_flags);
#endif
		cleanup(global_qdev, global_flags, NULL);
		return ret;
	}
	if (mem_requirement_check(global_flags)) {
		printf("ERR: DMA memory allocation failed\n");
		printf("ERR: Insufficent memory\n");
		cleanup(global_qdev, global_flags, NULL);
		return -ENOMEM;
	}

	/* get number of available_channels */
	avl_chnls = ifc_num_channels_get(global_qdev);
	if (avl_chnls <= 0) {
		printf("No available channels found\n");
		cleanup(global_qdev, global_flags, NULL);
		return 1;
	}

	/* check for available channels */
	if (avl_chnls < global_flags->chnls) {
		printf("Available Channels on single function: %d\n", avl_chnls);
#ifdef IFC_MCDMA_SINGLE_FUNC
		printf("Allocating %d Channels...\n", avl_chnls);
		global_flags->chnls = avl_chnls;
#endif
	} else {
		printf("Allocating %d Channels...\n", global_flags->chnls);
	}

#ifdef IFC_QDMA_DYN_CHAN
	/* acquire channels */
	ret = ifc_qdma_acquire_channels(global_qdev, global_flags->chnls);
	if (ret < 0) {
		printf("Could not acquire the channels : Exiting\n");
		return -1;
	}
#endif


#ifdef IFC_QDMA_ST_PACKETGEN
#ifdef IFC_QDMA_ST_MULTI_PORT
	pio_init(global_qdev, global_flags);
#else
	/* write number of channels */
#ifdef IFC_32BIT_SUPPORT
        ch_cnt = ifc_qdma_pio_read32(global_qdev, 0x00008);
#else
        ch_cnt = ifc_qdma_pio_read64(global_qdev, 0x00008);
#endif
        if (global_flags->single_fn)
                ch_cnt = global_flags->chnls - 1;
        else
                ch_cnt = AVST_CHANNELS - 1;
#ifdef IFC_32BIT_SUPPORT
        ifc_qdma_pio_write32(global_qdev, 0x00008, ch_cnt);
#else
        ifc_qdma_pio_write64(global_qdev, 0x00008, ch_cnt);
#endif

        #if defined  CID_PAT && defined IFC_PROG_DATA_EN
        //Enbale the pattern switch;
#ifdef IFC_32BIT_SUPPORT
                ifc_qdma_pio_write32(global_qdev, PIO_REG_PORT_PKT_ENABLE_PATTERN, 1ULL);
#else
                ifc_qdma_pio_write64(global_qdev, PIO_REG_PORT_PKT_ENABLE_PATTERN, 1ULL);
#endif
        #else
#ifdef IFC_32BIT_SUPPORT
                ifc_qdma_pio_write32(global_qdev, PIO_REG_PORT_PKT_ENABLE_PATTERN, 0ULL);
#else
                ifc_qdma_pio_write64(global_qdev, PIO_REG_PORT_PKT_ENABLE_PATTERN, 0ULL);
#endif
        #endif

        if (global_flags->flimit == REQUEST_BY_SIZE) {
#ifdef IFC_32BIT_SUPPORT
                ifc_qdma_pio_write32(global_qdev, 0x00010, 0ULL);
#else
                ifc_qdma_pio_write64(global_qdev, 0x00010, 0ULL);
#endif
        } else {
#ifdef IFC_ED_CONFIG_TID_UPDATE
#ifdef IFC_32BIT_SUPPORT
                ifc_qdma_pio_write32(global_qdev, 0x00010, DEFAULT_BURST_COUNT);
#else
                ifc_qdma_pio_write64(global_qdev, 0x00010, DEFAULT_BURST_COUNT);
#endif
#else
#ifdef IFC_32BIT_SUPPORT
                ifc_qdma_pio_write32(global_qdev, 0x00010, 1ULL);
#else
                ifc_qdma_pio_write64(global_qdev, 0x00010, 1ULL);
#endif
#endif
	}
#endif
#endif

	/* initialize thread context */
#ifdef PERFQ_IND_THREADS
	context_init_split(global_flags, &global_tctx);
#else
	ret = context_init(global_flags, &global_tctx);
	if (ret < 0) {
		cleanup(global_qdev, global_flags, global_tctx);
		return ret;
	}
#endif

	/* start device */
	ifc_mcdma_dev_start(global_qdev);

	/* startup the threads */
	thread_creator(global_qdev, global_flags, global_tctx);

	/* print common stats */
	show_header(global_flags);

	ret = thread_monitor(global_tctx, global_flags, &global_last_checked,
		       &global_start_time);

	/* show summary */
	if (ret != PERFQ_MALLOC_FAILURE) {
		ret = show_summary(global_tctx, global_flags, &global_last_checked,
			   &global_start_time);
	}
	cleanup(global_qdev, global_flags, global_tctx);
	return ret ? 1 : 0;
}
