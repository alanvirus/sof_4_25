/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#include "perfq_app.h"
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_common.h>
#include <rte_malloc.h>
#include <rte_string_fns.h>
#include <getopt.h>
#ifndef DPDK_21_11_RC2
#include <rte_ethdev_pci.h>
#else
#include <ethdev_pci.h>
#endif
#include "ifc_qdma_utils.h"
#ifdef IFC_MCDMA_RANDOMIZATION
#include "perfq_random.h"
#endif

static const struct rte_eth_conf port_conf_default = {
#ifndef DPDK_21_11_RC2
	.rxmode = { .max_rx_pkt_len = RTE_ETHER_MAX_LEN }
#else
	.rxmode = { .max_lro_pkt_size = RTE_ETHER_MAX_LEN }
#endif
};

uint16_t port_ids[RTE_MAX_ETHPORTS];
struct thread_context *global_tctx;
#ifdef IFC_MCDMA_MAILBOX
struct mailbox_thread_context *global_mb_tctx;
#endif
struct struct_flags *global_flags;
struct timespec global_last_checked;
struct timespec global_start_time;
pthread_mutex_t dev_lock;
bool force_exit;
const struct rte_memzone *bas_mzone;

#ifdef IFC_QDMA_DYN_CHAN
uint16_t mcdma_ph_chno[AVST_MAX_NUM_CHAN];
#endif

void compute_bw(struct struct_time *t,
			      struct timespec *start_time,
			      struct timespec *cur_time,
			      uint64_t pkts, uint32_t size);

void sig_handler(int sig)
{
	int ret;

	force_exit = true;
	/* dump stats and cleanup */
	printf("You have pressed ctrl+c\n");
	printf("Calling Handler for Signal %d\n", sig);
	ret = show_summary(global_tctx, global_flags, &global_last_checked,
			   &global_start_time);
	printf("Calling cleanup crew\n");
	rte_eal_mp_wait_lcore();
	cleanup(global_flags, global_tctx);
	exit(ret ? 1 : 0);
}

void memory_sig_handler(int sig)
{
        int ret;

        force_exit = true;
        /* dump stats and cleanup */
        printf("################################################################################\n");
        printf("Insufficient system memory or RAM memory in system..please cleanup system memory\n");
        printf("#################################################################################\n");
        printf("Calling Handler for Signal %d\n", sig);
        ret = show_summary(global_tctx, global_flags, &global_last_checked,
                           &global_start_time);
        printf("Calling cleanup crew\n");
        rte_eal_mp_wait_lcore();
        cleanup(global_flags, global_tctx);
        exit(ret ? 1 : 0);
}

static int update_desc_payload_len(struct struct_flags *flags, char *pylds)
{
	const char s[2] = ",";
	char *token;
	int i = 0;
	uint32_t pyld_sum = 0;
	flags->h2d_max_pyld = 0;

	memset(flags->desc_pyld, 0, sizeof(flags->desc_pyld));
	/* get the first token */
	token = strtok(pylds, s);
	if (token == NULL)
		return -1;

	flags->desc_pyld[i++] = atoi(token);
	pyld_sum += atoi(token);
	if (flags->h2d_max_pyld < (uint32_t )atoi(token))
		flags->h2d_max_pyld = atoi(token);
	/* walk through other tokens */
	while( token != NULL ) {

		token = strtok(NULL, s);
		if (token == NULL) {
			break;
		}
		if (!token) break;
		flags->desc_pyld[i++] = atoi(token);
		pyld_sum += atoi(token);
		if (flags->h2d_max_pyld < (uint32_t)atoi(token))
			flags->h2d_max_pyld = atoi(token);
	}
	flags->num_pylds = i;
	flags->h2d_pyld_sum = pyld_sum;
	return 0;
}
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
	if (flags->direction == REQUEST_LOOPBACK ||
	    flags->direction == REQUEST_BOTH)
		return flags->chnls * 2;

	return flags->chnls;
}

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
	if (timediff.tv_sec >= flags->interval) {
		show_progress(tctx, flags, last_checked, start_time);
		/* update last checked time */
		clock_gettime(CLOCK_MONOTONIC, last_checked);
		return true;
	}
	return false;
}

void pio_reset(uint16_t port_id)
{
	int i;

	for (i = 0; i < global_flags->chnls; i++) {
	/* reset the PIO registers */
		pio_bit_writer(port_id, 0x00, i, false);
		pio_bit_writer(port_id, 0x10, i, false);
	}
}

int thread_cleanup(struct thread_context *tctx)
{
	unsigned long i;
	struct queue_context *qctx;
	int q;

	rte_eal_mp_wait_lcore();

	for (q = 0; q < tctx->qcnt; q++) {
		qctx = &(tctx->qctx[q]);
		if (qctx->completion_buf) {
			for (i = 0; i < qctx->cur_comp_counter; i++)
				rte_pktmbuf_free(((struct rte_mbuf **)
					(qctx->completion_buf))[i]);
			rte_free(qctx->completion_buf);
		}
		if (qctx->direction == REQUEST_TRANSMIT)
			rte_mempool_free(qctx->tx_mbuf_pool);
	}
	return 0;
}

void cleanup(struct struct_flags *flags,
	     struct thread_context *tctx)
{
	int threads_count;
	int i, q;
	uint16_t portno = 0;
	uint32_t total_drops = 0;

	if (tctx == NULL)
		return;

	if (flags == NULL)
		return;

#ifdef IFC_MCDMA_SINGLE_FUNC
	rte_eth_dev_get_port_by_name(flags->bdf, &portno);
#endif
	threads_count = flags->num_threads;

	if (tctx) {
#ifndef IFC_MCDMA_SINGLE_FUNC
		RTE_ETH_FOREACH_DEV(portno)
#endif
		ifc_mcdma_dump_stats(portno);
		for (i = 0; i < flags->chnls; i++) {
			if (pthread_mutex_destroy(&(flags->locks[i])) != 0)
				printf("failed while destroying mutex\n");
			sem_destroy(&((flags->loop_locks[i]).tx_lock));
			sem_destroy(&((flags->loop_locks[i]).rx_lock));
#if 0
#ifndef IFC_QDMA_DYN_CHAN
			/* reset the PIO registers */
			pio_bit_writer(flags, 0x00, i, false);
			pio_bit_writer(flags, 0x10, i, false);
#endif
#endif
		}
		for (i = 0; i < threads_count; i++) {
			for (q = 0; q < tctx[i].qcnt; q++) {
				ifc_mcdma_print_stats(tctx[i].qctx[q].port_id,
					tctx[i].qctx[q].channel_id,
					tctx[i].qctx[q].direction);
				total_drops += tctx[i].qctx[q].tid_err;
			}
			if (thread_cleanup(&tctx[i]))
				printf("Thread%d: Falied to cleanup thread\n",
					tctx[i].tid);
		}
		rte_free(tctx);
	}

#ifndef IFC_MCDMA_SINGLE_FUNC
	RTE_ETH_FOREACH_DEV(portno) {
#endif
	rte_eth_dev_stop(portno);
#ifndef IFC_MCDMA_SINGLE_FUNC
	}
#endif
	rte_free(flags->locks);
	rte_free(flags->loop_locks);
	rte_free(flags);
	printf("\t total_drops:%u\n", total_drops);
}

#ifdef IFC_PIO_512
static int pio_util_512(uint16_t portid)
{
	uint64_t addr = PIO_ADDRESS;
	uint64_t wval = 0;
	uint64_t wvalue[8];
	uint64_t rvalue[8];
	unsigned int i, j;
	int ret;

	printf("PIO 512 Write and Read Test on port %u\n", portid);

#ifdef VERIFY_FUNC
	if (global_flags->fvalidation)
		printf("%-12s\t%-9s\n", "Written", "Read");
#endif

	for (i = 0; i < 32; i++) {
		ret = ifc_mcdma_pio_write512(portid, addr, wvalue, global_flags->bar);
		if (ret < 0) {
			printf("Failed to write PIO\n");
			printf("Fail\n");
			return 1;
		}
		ret = ifc_mcdma_pio_read512(portid, addr, rvalue, global_flags->bar);
		if (ret < 0) {
			printf("Failed to read PIO\n");
			printf("Fail\n");
			return 1;
		}
		for (j = 0; j < 8; j++) {
#ifdef VERIFY_FUNC
			if (global_flags->fvalidation)
				printf("0x%08lx\t0x%08lx\n",
					wvalue[j], rvalue[j]);
#endif
			if (wvalue[j] != rvalue[j]) {
				printf("Fail\n");
				return 1;
			}
		}
		for (j = 0; j < 8; j++)
			wvalue[j] = wval++;
		addr += 64;
	}
	printf("Pass\n");
	return 0;
}
#endif

#ifdef IFC_PIO_256
static int pio_perf_util_256(int16_t portid)
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
	uint32_t done = 0;

    for (j = 0; j < 4; j++)
            wvalue[j] = wval++;

	printf("PIO 256 Write Performance Test ...\n");

	clock_gettime(CLOCK_MONOTONIC, &start_time);
	ite = 0;
	while (1) {
		addr = PIO_ADDRESS;
		for (i = 0; i < 32; i++) {
			laddr = addr;
			ret = ifc_mcdma_pio_write256(portid, addr, wvalue, global_flags->bar);
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
		done = 0;
		while (done == 0) {
			ifc_mcdma_pio_read256(portid, laddr, rvalue, global_flags->bar);
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
	if (done) {
		compute_bw(&t, &start_time, &end_time, ite * 32, IFC_MCDMA_BAM_BURST_BYTES);
		printf("Total Bandwidth:\t\t%.2fGBPS\n",t.ovrall_bw);
		printf("Pass\n");
	}
	else {
		printf("PIO 256 performance Test failed\n");
	}

	return 0;
}

static int pio_util_256(uint16_t portid)
{
	uint64_t addr = PIO_ADDRESS;
	uint64_t wval = 0;
	uint64_t wvalue[4];
	uint64_t rvalue[4];
	unsigned int i, j;
	int ret;

	printf("PIO 256 Write and Read Test on port %u\n", portid);

#ifdef VERIFY_FUNC
	if (global_flags->fvalidation)
		printf("%-12s\t%-9s\n", "Written", "Read");
#endif

	for (i = 0; i < 32; i++) {
		ret = ifc_mcdma_pio_write256(portid, addr, wvalue, global_flags->bar);
		if (ret < 0) {
			printf("Failed to write PIO\n");
			printf("Fail\n");
			return 1;
		}
		ret = ifc_mcdma_pio_read256(portid, addr, rvalue, global_flags->bar);
		if (ret < 0) {
			printf("Failed to read PIO\n");
			printf("Fail\n");
			return 1;
		}
		for (j = 0; j < 4; j++) {
#ifdef VERIFY_FUNC
			if (global_flags->fvalidation)
				printf("0x%08lx\t0x%08lx\n",
					wvalue[j], rvalue[j]);
#endif
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

#ifdef IFC_PIO_128
static int pio_perf_util_128(uint16_t portid)
{
        uint64_t addr = PIO_ADDRESS;
        uint64_t laddr;
        unsigned int i,j;
	unsigned int ite;
        //__uint128_t wvalue = 0ULL ;
        //__uint128_t rvalue = 0ULL;
	uint64_t wval = 0;
	uint64_t wvalue[4];
	uint64_t rvalue[4];
	struct timespec start_time;
	struct timespec end_time;
	struct struct_time t;
	long timediff_sec;
	int ret;
	uint32_t done = 0;

		for (j = 0; j < 2; j++)
				wvalue[j] = wval++;

	printf("PIO 128 Write Performance Test ...\n");
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	ite = 0;
	while (1) {
		addr = PIO_ADDRESS;
		for (i = 0; i < 32; i++) {
			laddr = addr;
			ret = ifc_mcdma_pio_write128(portid, addr, wvalue, global_flags->bar);
			if (ret < 0) {
				printf("Failed to write PIO\n");
				printf("Fail\n");
				return 1;
			}
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
		done = 0;
		while (done == 0) {
			ifc_mcdma_pio_read128(portid, laddr, rvalue, global_flags->bar);
			for (j = 0; j < 2; j++) {
				if (wvalue[j] != rvalue[j]) {
					break;
				}
			}
			if (j == 2) {
				done = 1;
				break;
			}
		}
	}
	if (done) {
		compute_bw(&t, &start_time, &end_time, ite * 32, 16);
		printf("Total Bandwidth:\t\t%.2fGBPS\n",t.ovrall_bw);
		printf("Pass\n");
	}
	else {
		printf("PIO 128 performance Test failed\n");
	}

	return 0;
}

static int pio_util_128(uint16_t portid)
{
	uint64_t addr = PIO_ADDRESS;
	uint64_t wval = 0;
	uint64_t wvalue[2];
	uint64_t rvalue[2];
	unsigned int i, j;
	int ret;

	printf("PIO 128 Write and Read Test on port %u\n", portid);

#ifdef VERIFY_FUNC
	if (global_flags->fvalidation)
		printf("%-12s\t%-9s\n", "Written", "Read");
#endif

	for (i = 0; i < 32; i++) {
		ret = ifc_mcdma_pio_write128(portid, addr, wvalue, global_flags->bar);
		if (ret < 0) {
			printf("Failed to write PIO\n");
			printf("Fail\n");
			return 1;
		}
		ret = ifc_mcdma_pio_read128(portid, addr, rvalue, global_flags->bar);
		if (ret < 0) {
			printf("Failed to read PIO\n");
			printf("Fail\n");
			return 1;
		}
		for (j = 0; j < 2; j++) {
#ifdef VERIFY_FUNC
			if (global_flags->fvalidation)
				printf("0x%08lx\t0x%08lx\n",
					wvalue[j], rvalue[j]);
#endif
			if (wvalue[j] != rvalue[j]) {
				printf("Fail\n");
				return 1;
			}
		}
		for (j = 0; j < 2; j++)
			wvalue[j] = wval++;
		addr += 16;
	}
	printf("Pass\n");
	return 0;
}
#endif
int pio_perf_util(uint16_t portid)
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
	uint32_t done = 0;
	//int ret;

        printf("PIO 64 Write Performance Test ...\n");
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	ite = 0;
	while (1) {
		addr = PIO_ADDRESS;
		for (i = 0; i < 1024; i++) {
			laddr = addr;
			ifc_mcdma_write64(portid, addr, wvalue, global_flags->bar);
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
		done = 0;
		while (done == 0) {
			rvalue = ifc_mcdma_read64(portid, laddr, global_flags->bar);
			if (wvalue != rvalue) {
				continue;
			}
			done = 1;
		}
	}
	
	if (done) {
		compute_bw(&t, &start_time, &end_time, ite * 1024, 8);
		printf("Total Bandwidth:\t\t%.2fGBPS\n",t.ovrall_bw);
		printf("Pass\n");
	}
	else {
		printf("PIO 256 performance Test failed\n");
	}

	return 0;
}

int pio_util(uint16_t portid)
{
	uint64_t addr = PIO_ADDRESS;
	uint64_t wvalue = -1ULL;
	uint64_t rvalue = -1ULL;
	int i;

	printf("PIO Write and Read Test on port %u\n", portid);

#ifdef VERIFY_FUNC
	if (global_flags->fvalidation)
		printf("%-12s\t%-9s\n", "Written", "Read");
#endif

	for (i = 0; i < 100; i++) {
		ifc_mcdma_write64(portid, addr, wvalue, global_flags->bar);
		rvalue = ifc_mcdma_read64(portid, addr, global_flags->bar);
#ifdef VERIFY_FUNC
		if (global_flags->fvalidation)
			printf("0x%08lx\t0x%08lx\n", wvalue, rvalue);
#endif

		if (wvalue != rvalue) {
			printf("Fail\n");
			return 1;
		}

		wvalue--;
		addr += 8;
	}
	printf("Pass\n");
	return 0;
}

void show_help(void)
{
	printf("--bar <num>\tBAR number to be configured for BAM/BAS\n");
	printf("--bas_perf\tEnable BAS Performance Mode\n");
	printf("--bas\t\tEnable BAS Non-perf Mode\n");
	printf("--bam_perf\tEnable BAM Performance Mode\n");
	printf("--bam\t\tEnable BAM Non-perf Mode\n");
	printf("-h\t\tShow Help\n");
	printf("-a <threads>\tNumber of Threads to be used\n");
	printf("-b <bdf>\tBDF of Device\n");
	printf("-c <chnls>\tNumber of Channels to be used\n");
#ifdef IFC_QDMA_ST_PACKETGEN
#ifndef IFC_QDMA_ST_MULTI_PORT
#ifndef IFC_QDMA_DYN_CHAN
	printf("-n\t\tTo configure channel count in HW incase of"
	       " Performance mode\n");
	printf("--files <files>\tPacket Gen files\n");
#endif
#endif
#endif
	printf("-d <seconds>\tRefresh rate in seconds for Performance logs\n");
	printf("-e\t\tIP Reset\n");
	printf("-p <bytes>\tPayload Size of each descriptor\n");
	printf("-s <bytes>\tRequest Size in Bytes\n");
#ifndef IFC_QDMA_ST_MULTI_PORT
	printf("-f <#dsriptrs>\tFile Size in Descriptors\n");
#endif
	printf("-l <seconds>\tTime limit in Seconds\n");
	printf("-o\t\tPIO Test\n");
	printf("-v\t\tEnable Data Validation\n");
	printf("-j\t\tD2H Payload per Descriptor\n");
	printf("-k\t\tD2H expected File Size in bytes\n");
	printf("-g\t\tClear the allocated Channel\n");
	printf("-x <#dscriptrs>\tBatch Size in Descriptors\n");
#ifdef IFC_QDMA_INTF_ST
#ifdef IFC_QDMA_ST_LOOPBACK
	printf("-u\t\tLoop back Transfer\n");
        printf("Required parameters: b & ((p & u) & (s | l) & c & a) "
                "| o)\n");
	printf("-i\t\tIndependent Loop back\n");
	printf("Required parameters: b & (i & (s | l) & c & a) | o | e)\n");
#else
	printf("-t\t\tTransmit Operation\n");
	printf("-r\t\tReceive Operation\n");
	printf("-z\t\tBidirectional Transfer\n");
	printf("Required parameters: b & ((p & (t | r | z ) & (s | l) & c & a) "
		"| o | e)\n");
#endif
#else
	printf("-t\t\tTransmit Operation\n");
	printf("-r\t\tReceive Operation\n");
	printf("-u\t\tLoop back Transfer\n");
	printf("Required parameters: b & ((p & (t | r | u) & (s | l) & c & a) "
		"| o | e)\n");
#endif
	printf("Required parameters BAS Mode:\n");
	printf("\tPerf:\t\tb & z & s & --bas_perf\n");
	printf("\tNon perf:\tb & (t | r | z) & s & --bas\n");
	printf("CLI format:\n");
	printf("\t./build/mcdma-test <EAL_OPTIONS> -- <MCDMA_OPTIONS>\n");

	printf("Output format:\n");
	printf("Ch_ID            - Channel ID and Direction\n");
	printf("Req              - Number of Descriptors submitted for DMA "
		"Transfers\n");
	printf("Rsp              - Number of Descriptors procesed\n");
	printf("Time             - Total time Elapsed\n");
	printf("Good Descriptors - Number of good descriptors which match the "
		"expected data\n");
	printf("Bad Descriptors  - Number of bad descriptors which doesn't "
		"match the expected data\n");
	printf("Good Files       - Number of files which had SOF and EOF "
		"matched\n");
	printf("Bad Files        - Number of files which didn't "
		"match EOF/SOF/Data\n");
}
void dump_flags(struct struct_flags *flags)
{
	printf("---------------------------------------------------------------"
	       "-----------------------------------------------------------\n");
	printf("Limit: %d\n", flags->flimit);
	printf("PIO Test: %d\n", flags->fpio);
	printf("Validation: %d\n", flags->fvalidation);
	printf("BDF: %s\n", flags->bdf);
	printf("#Channels: %d\n", flags->chnls);
	printf("Request Size: %zu\n", flags->request_size);
	printf("Packet Size: %zu\n", flags->packet_size);
	printf("#H2D Packets: %lu\n", flags->packets);
	printf("#D2H Packets: %lu\n", flags->rx_packets);
	printf("Batch Size: %lu\n", flags->batch_size);
#ifdef IFC_QDMA_INTF_ST
	printf("File Size: %lu\n", flags->file_size);
#endif
	printf("Direction: %d\n", flags->direction);
	printf("Dump Interval: %d\n", flags->interval);
	printf("Time Limit: %u\n", flags->time_limit);

	printf("----------------:Compilation Flags status:-----------------\n");

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
	printf("---------------------------------------------------------------"
	       "-----------------------------------------------------------\n");
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

static void generate_file_name(char *file_name)
{
	time_t cur_time;
	struct tm *timeinfo;

	time(&cur_time);
	timeinfo = localtime(&cur_time);
	if (timeinfo == NULL) {
		printf("Failed to generate file\n");
		return;
	}

	strftime(file_name, FILE_PATH_SIZE, "perfq_log_%Y%m%d_%H%M%S.txt",
		 timeinfo);
#ifdef DUMP_FAIL_DATA
	strftime(global_flags->dump_file_name, FILE_PATH_SIZE, "dump_data_%Y%m%d_%H%M%S.txt",
		 timeinfo);
#endif
}

/* TODO: make sure packet_size  is not less than the #channels */
int cmdline_option_parser(int argc, char *argv[], struct struct_flags *flags)
{
	char *end;
	int opt;
	int fbdf = false;
	int fpacket_size = false;
	int transmit_counter = -1;
	int queue_count;
	int ret;
	int fbar = 0;
	int i;
	uint32_t file_pyld_size;	
#ifdef IFC_MCDMA_SINGLE_FUNC
	int nb_ports;
	char bdf[21];
	struct rte_eth_dev *dev;
#endif
	int opt_idx;
	int pktgen_files = 0;
#ifdef IFC_QDMA_ST_PACKETGEN
#ifdef ED_VER0
	uint32_t fsize;
#endif
#endif
	uint16_t port = 0;

	/* Initialize certain flags */
	flags->file_size = DEFAULT_FILE_SIZE;
	flags->rx_file_size = DEFAULT_FILE_SIZE;
	flags->mbuf_pool_fail = 0;
	flags->hol = 0;
	flags->request_size = 0;
	flags->flimit = -1;
	flags->fpio = false;
	flags->fvalidation = false;
	flags->fpkt_gen_files = false;
	flags->chnl_cnt = false;
	flags->interval = 0;
	flags->packet_size = 0;
	flags->time_limit = 0;
	flags->batch_size = DEFAULT_BATCH_SIZE;
	flags->direction = -1;
	flags->pf = 1; //By default, PF1
	flags->vf = 0; //By default, PF and not VF
	flags->cleanup = false;
	flags->rx_file_pyld_size = 0;
	flags->rx_packet_size = 0;
#if defined(CID_PFVF_PAT)
	flags->is_pf = 0;
	flags->is_vf = 0;
#endif 
#if defined(IFC_QDMA_ST_PACKETGEN) && defined(IFC_QDMA_INTF_ST)
	flags->pkt_gen_files = DEFAULT_PKT_GEN_FILES;
#endif
	flags->bar = 2;
	flags->start_ch = 0;

        static struct option lgopts[] = {
                { "bas", 0, 0, 0 },
		{ "bas_perf", 0, 0, 0 },
                { "bam", 0, 0, 0 },
		{ "bam_perf", 0, 0, 0 },
#ifdef IFC_QDMA_ST_PACKETGEN
		{ "files",   1, 0, 0 },
#ifdef IFC_MCDMA_SINGLE_FUNC
		{ "pf",   	1, 0, 0 },
		{ "vf",   	1, 0, 0 },
#endif
#endif
		{ "bar",   	1, 0, 0 },
		{ "start_ch", 1, 0, 0 },
#ifdef IFC_MCDMA_RANDOMIZATION
		{ "rand", 1, 0, 0 },
#endif
		{ 0, 0, 0, 0 }
		};

	while ((opt = getopt_long(argc, argv, "a:b:c:d:f:ghj:k:l:eop:rs:tiuvwx:nz",
				  lgopts, &opt_idx)) != -1) {
		switch (opt) {
		case 0: /*long options */
			if (!strcmp(lgopts[opt_idx].name, "bas")) {
				if (flags->fbas != false) {
					printf("ERR: Flags can not be repeated\n");
					return -1;
				}
				flags->fbas = true;
				flags->params_mask |= PERFQ_ARG_BAS;
			} else	if (!strcmp(lgopts[opt_idx].name, "bas_perf")) {
				if (flags->fbas_perf != false) {
					printf("ERR: Flags can not be repeated\n");
					return -1;
				}
				flags->fbas_perf = true;
				flags->params_mask |= PERFQ_ARG_BAS_PERF;
			} else if (!strcmp(lgopts[opt_idx].name, "bam_perf")) {
				if (flags->fbam_perf != false) {
					printf("ERR: Flags can not be repeated\n");
					return -1;
				}
				flags->fbam_perf = true;
			} else if (!strcmp(lgopts[opt_idx].name, "files")) {
				if (flags->fpkt_gen_files != false) {
					printf("ERR: Flags can not be repeated\n");
					return -1;
				}
				flags->fpkt_gen_files = true;
				ret = parse_long_int(optarg);
				if (ret <= 0) {
					printf("Invalid --files value\n");
					return -1;
				}
				pktgen_files = ret;
				flags->params_mask |= PERFQ_ARG_PKT_GEN_FILES;
			} else if (!strcmp(lgopts[opt_idx].name, "pf")) {
				if (flags->fpf != false) {
					printf("ERR: PF can not be repeated\n");
					return -1;
				}
				flags->fpf = true;
				ret = parse_long_int(optarg);
				if (ret <= 0) {
					printf("Invalid --pf value\n");
					return -1;
				}
				flags->pf = ret;
				#if defined(CID_PFVF_PAT)
					flags->is_pf = 1;
			        #endif
				flags->params_mask |= PERFQ_ARG_PF;
			} else if (!strcmp(lgopts[opt_idx].name, "vf")) {
				if (flags->fvf != false) {
					printf("ERR: VF can not be repeated\n");
					return -1;
				}
				flags->fvf = true;
				ret = parse_long_int(optarg);
				if (ret < 0) {
					printf("Invalid --vf value\n");
					return -1;
				}
				flags->vf = ret;
				#if defined(CID_PFVF_PAT)
					flags->is_vf = 1;
				#endif
				flags->params_mask |= PERFQ_ARG_VF;
			} else if (!strcmp(lgopts[opt_idx].name, "bar")) {
				ret = parse_long_int(optarg);
				if (ret < 0) {
					printf("Invalid --bar value\n");
					return -1;
				}
				flags->bar = ret;
				fbar = 1;
			}else if(!strcmp(lgopts[opt_idx].name, "start_ch")) {
                               ret = parse_long_int(optarg);
                                if (ret < 0) {
                                        printf("Invalid --start_ch value\n");
                                        return -1;
                                }
				flags->start_ch = ret;
			}
#ifdef IFC_MCDMA_RANDOMIZATION
			else if (!strcmp(lgopts[opt_idx].name, "rand")) {
				flags->rand_enteries = load_rand_conf(&(flags->rand_param_list));
				if(flags->rand_enteries > 0) {
					ret = parse_long_int(optarg);
					flags->frand = ret;
					printf("randomisation with code %d activated....\n", ret);
				}
                                else {
                                        printf("randomisation could not start! please check rand.conf....\n");
					return -1;
                                }
			}
#endif
			break;
		case 'a':
			if (flags->num_threads != 0) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			ret = parse_long_int(optarg);
			if (ret <= 0) {
				printf("ERR: Invalid -a value\n");
				return -1;
			}
			flags->num_threads = (int)ret;
			flags->params_mask |= PERFQ_ARG_NUM_THREADS;
			break;
		case 'b':
			if (fbdf == true) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
#ifndef IFC_MCDMA_SINGLE_FUNC
			printf("ERR: BDF not allowed when Multi PF enabled \n");
			return -1;
#endif
			rte_strscpy(flags->bdf, optarg, sizeof(flags->bdf));
			fbdf = true;
			flags->params_mask |= PERFQ_ARG_SINGLE_BDF;
			break;
		case 'c':
			if (flags->chnls != 0) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			ret = parse_long_int(optarg);
			if (ret <= 0) {
				printf("ERR: Invalid -c value\n");
				return -1;
			}
			flags->chnls = (int)ret;
			flags->params_mask |= PERFQ_ARG_NUM_CHNLS;
			break;
		case 'd':
			if (flags->interval != 0UL) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			ret = parse_long_int(optarg);
			if (ret == -1) {
				printf("ERR: Invalid -d value\n");
				return -1;
			}
			flags->interval = ret;
			flags->params_mask |= PERFQ_ARG_DEBUG;
			break;
		case 'f':
#ifndef IFC_MCDMA_FUNC_VER
#ifndef IFC_QDMA_ST_MULTI_PORT
			printf("ERR: No -f flag support in Single port MCDMA\n");
			return -1;
#endif
#endif //IFC_MCDMA_FUNC_VER
			if (flags->params_mask & PERFQ_ARG_FILE_SIZE) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			ret = parse_long_int(optarg);
			if (ret <= 0) {
				printf("ERR: Invalid -f value\n");
				return -1;
			}
#ifndef IFC_MCDMA_FUNC_VER 
#ifdef PERFQ_PERF
			printf("ERR: No file size support in Performance Mode, "
				"undefine PERFQ_PERF in Makefile\n");
			return -1;
#endif
#endif //IFC_MCDMA_FUNC_VER
#ifndef IFC_QDMA_INTF_ST
			printf("ERR: No file size Support in AVMM\n");
			return -1;
#else
			flags->file_size = ret;
			flags->params_mask |= PERFQ_ARG_FILE_SIZE;
			break;
#endif
		case 'p':
			if (flags->packet_size != 0UL) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			update_desc_payload_len(flags, optarg);
			ret = parse_long_int(optarg);
			if (ret <= 0) {
				printf(" ERR: Invalid -p value\n");
				return -1;
			}
			flags->packet_size = ret;
			#if (IFC_DATA_WIDTH == 1024)
                        if ((flags->packet_size % 128) != 0) {
                                printf("Invalid -p value\n");
                                printf("Packet size must be a multiple of 128 bytes in 1024 bit data width support\n");
                                return -1;
                        }
                        #endif
			fpacket_size = true;
			flags->params_mask |= PERFQ_ARG_PKT_SIZE;
			break;
		case 'h':
			if (optind != argc) {
				printf("ERR: Invalid parameter: %s\n",
					argv[optind]);
				printf("Try help:\n\t./build/mcdma-test -- -h\n");
				return -1;
			}
			show_help();
			return -1;
		case 's':
			if (flags->request_size != 0UL) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			ret = parse_long_int(optarg);
			if (ret <= 0) {
				printf("ERR: Invalid -s value\n");
				return -1;
			}
			if (flags->flimit != -1) {
				printf("ERR: Both -s & -l not allowed\n");
				printf("Try help:\n\t./build/mcdma-test -- -h\n");
				return -1;
			}
			flags->request_size = ret;
			flags->flimit = REQUEST_BY_SIZE;
			flags->params_mask |= PERFQ_ARG_TRANSFER_SIZE;
			break;
		case 'j':
			if (flags->rx_packet_size != 0UL) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			ret = parse_long_int(optarg);
			if (ret == -1) {
				printf("ERR: Invalid -s value\n");
				return -1;
			}
			flags->rx_packet_size = ret;
			break;
		case 'k':
			ret = parse_long_int(optarg);
			if (ret == -1) {
				printf("ERR: Invalid -s value\n");
				return -1;
			}
			flags->rx_file_pyld_size = ret;
			break;
		case 'l':
			if (flags->time_limit != 0UL) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			ret = parse_long_int(optarg);
			if (ret <= 0) {
				printf("ERR: Invalid -l value\n");
				return -1;
			}
			if (flags->flimit != -1) {
				printf("ERR: Both -s & -l not allowed\n");
				printf("Try help:\n\t./build/mcdma-test -- -h\n");
				return -1;
			}
			flags->time_limit = ret;
			flags->flimit = REQUEST_BY_TIME;
			flags->params_mask |= PERFQ_ARG_TRANSFER_TIME;
			break;
		case 'n':
#ifdef IFC_QDMA_ST_PACKETGEN
#ifndef IFC_QDMA_ST_MULTI_PORT
#ifndef IFC_QDMA_DYN_CHAN
			if (flags->chnl_cnt == true) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			flags->chnl_cnt = true;
			printf("Simultaneous processes hangs with -n option\n");
#else
			printf("ERR: -n only supported for single port AVST Pkt gen\n");
			flags->params_mask |= PERFQ_ARG_CHNL_CNT;
			return -1;
#endif
#endif
#endif
			break;
		case 't':
			if (flags->direction == REQUEST_TRANSMIT) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			flags->direction = REQUEST_TRANSMIT;
			transmit_counter++;
			flags->params_mask |= PERFQ_ARG_TRANSFER_TX;
			break;
		case 'r':
			if (flags->direction == REQUEST_RECEIVE) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			flags->direction = REQUEST_RECEIVE;
			transmit_counter++;
			flags->params_mask |= PERFQ_ARG_TRANSFER_RX;
			break;
		case 'u':
			if (flags->hol == true) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			flags->direction = REQUEST_LOOPBACK;
			flags->hol = 1;
			transmit_counter++;
			flags->params_mask |= PERFQ_ARG_TRANSFER_U;
			break;
		case 'i':
#ifndef IFC_QDMA_INTF_ST
			printf("ERR: -i flag is not supported in AVMM\n");
			return -1;
#else
			if (flags->direction == REQUEST_LOOPBACK) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			flags->direction = REQUEST_LOOPBACK;
			transmit_counter++;
			flags->params_mask |= PERFQ_ARG_TRANSFER_I;
			break;
#endif
		case 'z':
#if defined(IFC_QDMA_ST_LOOPBACK) && defined(IFC_QDMA_MM_LOOPBACK)
			printf("ERR: No -z flag Support in AVMM\n");
			return -1;
#else
			if (flags->direction == REQUEST_BOTH) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			flags->direction = REQUEST_BOTH;
			transmit_counter++;
			flags->params_mask |= PERFQ_ARG_TRANSFER_Z;
			break;
#endif
		case 'v':
#ifdef PERFQ_PERF
			printf("ERR: Please disable PERF mode to valiadate data\n");
			return -1;
#endif
			if (flags->fvalidation != false) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			flags->fvalidation = true;
			flags->params_mask |= PERFQ_ARG_DATA_VAL;
			break;
		case 'o':
			if (flags->fpio != false) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			flags->fpio = true;
			flags->params_mask |= PERFQ_ARG_PIO;
			break;
		case 'w':
			flags->comp_policy = (int)strtoul(optarg, &end, 10);
			if ((flags->comp_policy < 0) ||
			     (flags->comp_policy > 2)) {
				printf("ERR: Invalid Option\n");
				printf("Try help:\n\t./build/mcdma-test -- -h\n");
				return -1;
			}
			break;
		case 'x':
#ifdef ED_VER0
			printf("ERR: -x not supported for this release\n");
			return -1;
#endif
#ifndef IFC_MCDMA_FUNC_VER
#ifdef PERFQ_PERF
			printf("ERR: Please disable PERF mode to check with batch size\n");
			return -1;
#endif
#endif
			if (flags->params_mask & PERFQ_ARG_BATCH) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			ret = parse_long_int(optarg);
			if (ret <= 0) {
				printf("ERR: Invalid -x value\n");
				return -1;
			}
			flags->batch_size = ret;
			flags->params_mask |= PERFQ_ARG_BATCH;
			break;
		case 'g':
			if (flags->cleanup != false) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			flags->cleanup = true;
			break;
		case 'e':
			if (flags->ip_reset != false) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			flags->ip_reset = true;
			flags->params_mask |= PERFQ_ARG_IP_RESET;
			break;
		case '?':
			printf("ERR: Invalid option\n");
			printf("Try help:\n\t./build/mcdma-test -- -h\n");
			return -1;
		default:
			printf("Try help:\n\t./build/mcdma-test -- -h\n");
			return -1;
		}
	}

	if (optind != argc) {
		printf("ERR: Invalid parameter: %s\n", argv[optind]);
		printf("Try help:\n\t./build/mcdma-test -- -h\n");
		return -1;
        }
#ifdef IFC_MCDMA_SINGLE_FUNC
	if (!fbdf) {
		printf("ERR: BDF not specified\n");
		printf("Try help:\n\t./build/mcdma-test -- -h\n");
		return -1;
	}
#endif
#ifdef IFC_QDMA_INTF_ST
#ifdef IFC_QDMA_ST_LOOPBACK
        if(flags->flimit == REQUEST_BY_SIZE && !flags->hol && flags->fbas_perf == false && flags->fbas == false ){
                        printf("ERR: -s needs to be used with -u and without -i\n");
                        return -1;
        }
        if(flags->flimit == REQUEST_BY_TIME && flags->hol){
                        printf("ERR: -u can't be used\n");
                        return -1;
        }
#endif
#endif
#ifdef IFC_MCDMA_SINGLE_FUNC
	nb_ports = rte_eth_dev_count_avail();
	if (nb_ports == 0)
		rte_exit(EXIT_FAILURE, "No Device Found.\n");
	for (i = 0; i < nb_ports; i++) {
		dev = &rte_eth_devices[i];
		rte_strscpy(bdf, dev->device->name, sizeof(bdf));
		if ((strncmp(global_flags->bdf, bdf, 256) == 0)) {
                        break;
                }
	}

	if (i == rte_eth_dev_count_avail()) {
		printf("ERR: Invalid BDF value: %s\n", global_flags->bdf);
		printf("Valid BDF's are :\n");
		for (i = 0; i < nb_ports; i++) {
			dev = &rte_eth_devices[i];
			rte_strscpy(bdf, dev->device->name, sizeof(bdf));
			printf("%s\n", dev->device->name);
		}
		rte_exit(EXIT_FAILURE, "Cannot init ports"
			" with provided BDF value\n");
	}
	rte_eth_dev_get_port_by_name(global_flags->bdf, &port);
	port_ids[0] = port;
#else
	RTE_ETH_FOREACH_DEV(port) {
		port_ids[port] = port;
	}
#endif
	if (fbar && !(flags->bar == 0 || flags->bar == 2 || flags->bar == 4)) {
		printf("ERR: Invalid BAR to configure\n");
		return -1;
	}
	if (flags->fpio)
		return 0;

	if (flags->ip_reset)
		return 0;

	if(flags->cleanup)
		return 0;
	
	if (flags->fbas || flags->fbas_perf) {
		if (flags->fbas) {
			if ((flags->params_mask == BAS_EXPECTED_MASK1) ||
			    (flags->params_mask == BAS_EXPECTED_MASK2) ||
			    (flags->params_mask == BAS_EXPECTED_MASK3)) {
				if ((flags->request_size %
				     IFC_MCDMA_BAS_BURST_BYTES) == 0)
					return 0;
				else {
					printf("ERR: Request size needs to be"
						" multiple of %d\n",
						IFC_MCDMA_BAS_BURST_BYTES);
					return -1;
				}
			}
		} else {
			if (flags->params_mask == BAS_PERF_EXPECTED_MASK) {
				if ((flags->request_size %
				     IFC_MCDMA_BAS_BURST_BYTES) == 0)
					return 0;
				else {
					printf("ERR: Request size needs to be"
						" multiple of %d\n",
						IFC_MCDMA_BAS_BURST_BYTES);
					return -1;
				}
			}
		}
		printf("Required parameters: b & (t | r | z) & s & (--bas)\n");
		printf("Required parameters for perf: b & z & s & --bas_perf\n");
		return -1;
	}

	if (flags->chnls == 0) {
		printf("ERR: Channels needs to be specified\n");
		printf("Try help:\n\t./build/mcdma-test -- -h\n");
		return -1;
	}

#ifdef IFC_QDMA_INTF_ST
	if (flags->chnls > AVST_CHANNELS) {
		printf("ERR: AVST supports %d channels\n", AVST_CHANNELS);
		return -1;
	}
#else
	if (flags->chnls > AVMM_CHANNELS) {
		printf("ERR: AVMM supports %d channels\n", AVMM_CHANNELS);
		return -1;
	}
#endif


#ifndef IFC_MCDMA_FUNC_VER
/* single port should have batch size same as file size */
#ifndef IFC_QDMA_ST_MULTI_PORT
	flags->file_size = 1;
#endif

/* performance mode overwrite */
#ifdef PERFQ_PERF
#ifndef ED_VER0
	flags->file_size = 1;
#endif
	flags->batch_size = DEFAULT_BATCH_SIZE;
#endif
#endif //IFC_MCDMA_FUNC_VER

#ifdef IFC_QDMA_INTF_ST
#ifdef IFC_QDMA_ST_LOOPBACK
#ifndef IFC_MCDMA_FUNC_VER
	flags->file_size = 1;
#endif //IFC_MCDMA_FUNC_VER
	if (flags->direction == REQUEST_TRANSMIT ||
	    flags->direction == REQUEST_RECEIVE ||
	    flags->direction == REQUEST_BOTH) {
		printf("ERR: Loopback doesn't support t/r/z flags\n");
		return -1;
	}
#else
	if (flags->direction == REQUEST_LOOPBACK) {
		printf("ERR: Packet Gen doesn't support -i flag\n");
		return -1;
	}
#endif
#endif

#ifdef IFC_ED_CONFIG_TID_UPDATE
	flags->file_size = flags->batch_size;
#endif
	queue_count = count_queues(flags);

#ifdef IFC_QDMA_INTF_ST
#ifndef IFC_QDMA_ST_MULTI_PORT
	if (flags->file_size > 1 &&
		(flags->file_size != flags->batch_size)) {
		printf("ERR: Single Port Design should have equal batch and file size\n");
		printf("\tCurrent Config: Batch size: %lu File size: %lu\n",
			flags->batch_size, flags->file_size);
		return -1;
	}
#endif
#endif
	/* check for required flags */
	if (flags->num_threads == 0) {
		printf("ERR: Number of threads should be passed\n");
		printf("Try help:\n\t./build/mcdma-test -- -h\n");
		return -1;
	}

	if (rte_lcore_count() < (unsigned int) flags->num_threads) {
		printf("ERR: Number of cores (%d < %d)"
		       " Number of Requested threads\n",
		       rte_lcore_count(), flags->num_threads);
		return -1;
        }

	/* check whether number of threads are more then required */
	if ((flags->num_threads > queue_count) ||
	    (queue_count % flags->num_threads)) {
		printf("ERR: Threads must be less or factor of queue count\n");
		printf("\tQueue count: %u Thread count %u\n",
			queue_count, flags->num_threads);
		return -1;
	}

	/* check for required flags */
	if (!fpacket_size) {
		printf("ERR: Payload not specified\n");
		printf("Try help:\n\t./build/mcdma-test -- -h\n");
		return -1;
	}

	if (flags->packet_size > 1048576) {
		printf("ERR: Payload Size must be less than 1MB\n");
		return -1;
	}

#ifdef IFC_QDMA_INTF_ST
#ifdef IFC_QDMA_ST_LOOPBACK
#if 0
	if (flags->file_size > 1) {
		if (flags->packet_size % 64 != 0) {
			if (flags->packet_size != IFC_MTU_LEN) {
				printf("ERR: Packet Size must be a multiple of"
					" 64 or MTU size, if file size > 1\n");
				return -1;
			}
		}
	} else if (flags->file_size == 1) {
		if (flags->packet_size % 4 != 0) {
			printf("ERR: Packet Size must be a multiple of 4,"
				"if file size = 1\n");
			return -1;
		}
	}
#endif
#endif // IFC_QDMA_ST_LOOPBACK
#ifdef IFC_QDMA_ST_PACKETGEN
#if 0
	if (flags->packet_size % 64 != 0) {
		if (flags->packet_size != IFC_MTU_LEN) {
			printf("ERR: Payload Size must be a multiple of 64 or "
				"MTU size in AVST Packet Gen\n");
			return -1;
		}
	}
#endif
#ifdef ED_VER0
	fsize = flags->file_size * flags->packet_size;
	if (fsize > ((1ULL << 24) - 1)) {
		printf("File size must be less than equal to 15MB.\n");
		printf("Please reduce file size or payload size.\n");
		return -1;
	}
#endif
#endif
#endif
#ifdef IFC_QDMA_MM_LOOPBACK
	if (flags->packet_size % 4 != 0) {
		printf("ERR: Payload size must be a multiple of 4 in AVMM\n");
		return -1;
	}
#endif

	/* Check for only one of -t, -r, -u and -z are set */
	if (transmit_counter) {
		printf("Try help:\n\t./build/mcdma-test -- -h\n");
		return -1;
	}

	/* check for only one of -s and -l are set */
	if (flags->flimit == -1) {
		printf("Try help:\n\t./build/mcdma-test -- -h\n");
		return -1;
	}

	if (flags->flimit == REQUEST_BY_SIZE) {
		if (flags->request_size < flags->packet_size) {
			printf("ERR: Invalid Option\n");
			printf("ERR: Request Size is smaller than Packet "
				"Size\n");
			printf("\tRequest_size: %lu < Packet_size: %lu\n",
				flags->request_size, flags->packet_size);
			return -1;
		}

		/* H2D file payload size */
		file_pyld_size = 0;
		for (i = 0; i < (int)flags->file_size; i++)
			file_pyld_size += flags->desc_pyld[(i%flags->num_pylds)];
		flags->packets = (flags->request_size / file_pyld_size) * flags->file_size;
#ifdef IFC_QDMA_ST_PACKETGEN
#ifndef IFC_ED_CONFIG_TID_UPDATE
		if (flags->packets >= (1 << PIO_REG_PORT_PKT_CONFIG_FILES_WIDTH)) {
			printf("ERR: Number of descriptors count reached max\n");
			printf("ERR: Try with reduced data size\n");
			return -1;
		}
#endif //IFC_ED_CONFIG_TID_UPDATE
#endif

		if (flags->h2d_pyld_sum > flags->request_size) {
			printf("ERR: H2D payloads sum should be <= request size");
			return -1;
		}

		if (flags->h2d_pyld_sum == flags->request_size) {
			flags->packets = flags->num_pylds;

		} else if (flags->request_size % flags->packet_size) {
		/* request_size is not divisible by packet_size */
			/* allowed only for loopback with request of
			 * less than QDEPTH
			 */
			if (flags->direction == REQUEST_LOOPBACK &&
				flags->packets < QDEPTH) {
				//flags->packets++;
			} else {
				printf("ERR: Invalid Option\n");
				printf("ERR: Request Size is not modulus"
					" of Packet size\n");
				printf("\tRequest_size: %lu  Packet_size: %lu\n",
					flags->request_size, flags->packet_size);
				return -1;
			}
		}

		if (flags->request_size % file_pyld_size) {
			printf("ERR: packets are not aligned to file size 0x%x\n", file_pyld_size);
			return -1;
		}
	}
#ifdef IFC_QDMA_ST_PACKETGEN
	if ((flags->request_size > DEFAULT_PKT_GEN_FILES) && (flags->request_size % QDEPTH)) {
		printf("ERR: Request_size must be mod of QDEPTH = %d\n", QDEPTH);
		return -1;
	}
#endif
	if (flags->batch_size >  QDEPTH) {
		printf("ERR: Invalid Option\n");
		printf("ERR: Batch size is greater than queue size\n");
		return -1;
	}

#ifdef IFC_QDMA_INTF_ST
	if (flags->flimit == REQUEST_BY_SIZE &&
		flags->packets % flags->file_size) {
		printf("ERR: #descriptors should be aligned with #files\n");
		printf("ERR: Descriptors should be modulus of file size\n");
		printf("\tDescriptors specified: %lu File_size: %lu\n",
			flags->packets,flags->file_size);
		return -1;
	}
#endif
	if ((flags->flimit == REQUEST_BY_TIME) &&
	    (flags->time_limit < 3) &&
	    (flags->packet_size > 8192)) {
		printf("Please run for more number of seconds to capture correct values\n");
	}

#ifndef IFC_QDMA_INTF_ST
	if ((flags->direction == REQUEST_LOOPBACK) &&
	     (flags->hol == 0) &&
	     (flags->fvalidation)) {
		printf("ERR: Validation enablement not support with -i option\n");
		return -1;
	}
        if (((PERFQ_AVMM_BUF_LIMIT / (unsigned int)flags->chnls) <
		flags->packet_size) && flags->fvalidation) {
		printf("%d Byets memory is connected\n", PERFQ_AVMM_BUF_LIMIT);
                printf("%d Bytes memory can be availed for %d channels\n",
			PERFQ_AVMM_BUF_LIMIT / (unsigned int)flags->chnls,
			flags->chnls);
                printf("Select Packet size less than %d Bytes\n",
			PERFQ_AVMM_BUF_LIMIT / (unsigned int) flags->chnls);
		return -1;
	}
#endif
	if (flags->rx_packet_size <= 0) {
		/* Same as H2D */
		flags->rx_packet_size  = flags->packet_size;
	}

	if (flags->rx_file_pyld_size <= 0) {
		/* Same as H2D */
		flags->rx_file_pyld_size  = flags->packet_size * flags->file_size;
		flags->rx_file_size  = flags->file_size;
	} else  {
		if (flags->rx_packet_size > flags->rx_file_pyld_size)
			flags->rx_file_size  = 1;
		else
			flags->rx_file_size  = ceil(((double)flags->rx_file_pyld_size) / ((double)flags->rx_packet_size));
	}
	if (flags->flimit == REQUEST_BY_SIZE) {
		if (flags->rx_packet_size > flags->request_size) {
			flags->rx_packets = 1;
		} else {
			/* calculate the number of RX packets */
			file_pyld_size = 0;
			for (i = 0; i < (int)flags->file_size; i++)
				file_pyld_size += flags->desc_pyld[(i%flags->file_size)];

			if (flags->rx_file_pyld_size != file_pyld_size) {
				printf("ERR: RX and Tx file sizes are different\n");
				return -1;
			}

			uint32_t desc_per_file = flags->rx_file_pyld_size / flags->rx_packet_size;
			if ( flags->rx_file_pyld_size % flags->rx_packet_size)
				desc_per_file++;

			flags->rx_packets = ((flags->request_size / flags->rx_file_pyld_size) * (desc_per_file));
		}
	}

	if (flags->file_size > QDEPTH) {
		printf("ERR: Invalid Option\n");
		printf("ERR: File size can not be greater then QDEPTH\n");
		return -1;
	}
	
	if (PREFILL_QDEPTH % flags->file_size) {
		printf("ERR: Invalid file_size\n");
		printf("ERR: Queue size should be divisible by file_size. change the file size or PREFILL_QDEPTH. qdepth:%u fs     :%lu\n",PREFILL_QDEPTH, flags->file_size);
		return -1;
	}

	if (flags->file_size == 1UL)
		flags->fmpps = true;
#if 0
#ifdef IFC_MCDMA_DIDF
	if (!(flags->batch_size == DIDF_ALLOWED_BATCH_SIZE
	    && flags->file_size == DIDF_ALLOWED_FILE_SIZE)) {
		printf("ERR: For DIDF: Allowed batch_size: %d & file_size: %d\n",
			DIDF_ALLOWED_BATCH_SIZE, DIDF_ALLOWED_FILE_SIZE);
		printf("\tCurrently batch size: %lu file size %lu\n",
			flags->batch_size, flags->file_size);
		return -1;
	}
#endif
#endif

#ifdef PERFQ_PERF
	flags->fshow_stuck = true;
#endif

	if (flags->fpkt_gen_files)
		flags->pkt_gen_files = pktgen_files;
	else
#ifndef IFC_MCDMA_DIDF
		flags->pkt_gen_files = flags->batch_size;
#else
		flags->pkt_gen_files = 1;
#endif

	flags->tx_batch_size = flags->batch_size;
	flags->rx_batch_size = flags->batch_size;
	generate_file_name(flags->fname);
#ifdef DUMP_FAIL_DATA
	flags->dump_fd = ifc_mcdma_fopen(global_flags->dump_file_name, "a+");
#endif
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
	if (global_flags->mbuf_pool_fail)
		return -1;
	/* Let the data transfer begin */
	flags->ready = true;
	printf("Thread is in READY state...\n");
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

	threads_count = flags->num_threads;

	/* Initialize the last_checked time */
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
				if ((qctx->valid) &&
					(qctx->status == THREAD_DEAD ||
					qctx->status == THREAD_ERROR_STATE)) {
					finished_counter++;

					/* poll if pending requests
					 * are present */
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

#if 0
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

#ifdef IFC_QDMA_ST_PACKETGEN
#ifdef IFC_QDMA_ST_MULTI_PORT
static int pio_init(uint16_t portid, struct struct_flags *flags)
{

	uint64_t file_size;
	uint64_t val;
	uint64_t addr;

	/* case: loopback */
	addr = PIO_REG_PORT_PKT_RST;
	val = ifc_mcdma_pio_read64(portid, addr);

	/* if loopback, set the required flags */
	if (flags->direction == REQUEST_LOOPBACK) {
		val |= (1ULL << flags->chnls) - 1;
		ifc_mcdma_pio_write64(portid, addr, val);
	} else { /* else reset and clear the flags */
		/* Reset */
		val |= (1ULL << flags->chnls) - 1;
		ifc_mcdma_pio_write64(portid, addr, val);
		usleep(100000);

		/* clear */
		val &= ~((1ULL << flags->chnls) - 1);
		ifc_mcdma_pio_write64(portid, addr, val);
	}

	file_size = flags->file_size * flags->packet_size;
	/* case: tx */
	if (flags->direction == REQUEST_TRANSMIT || flags->direction ==
	    REQUEST_BOTH) {
		addr = PIO_REG_EXP_PKT_LEN;
		ifc_mcdma_pio_write64(portid, addr, file_size);
	}
	/* case: rx */
	if (flags->direction == REQUEST_RECEIVE || flags->direction ==
	    REQUEST_BOTH) {

		/* Setting the file size that will be generated */
		addr = PIO_REG_PKT_LEN;
		ifc_mcdma_pio_write64(portid, addr, file_size);

		/* Setting number of files that will be generated */
		/* Let's just put a very high number here */
		addr = PIO_REG_PKT_CNT_2_GEN;
		ifc_mcdma_pio_write64(portid, addr, 1 << 31);
	}
	return 0;
}
#endif
#endif

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
	int id_counter = 0;
	int entries;
	int i, tid, qno;
	struct queue_context *q;
	char tx_pool_name[64];
	int devid, ret;

	entries = count_queues(flags);
	int queues_per_thread = (entries/flags->num_threads);

	/* TODO: don't malloc for non-loopback modes */
	/* malloc memory for loop locks */
	flags->loop_locks = (struct loop_lock *)rte_zmalloc("loop_lock",
				flags->chnls * sizeof(struct loop_lock),
				0);
	if (flags->loop_locks == NULL)
		return -1;

	/* malloc memory for locks */
	flags->locks = (pthread_mutex_t *)rte_zmalloc("lock",flags->chnls *
				sizeof(pthread_mutex_t), 0);
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

	/* malloc memory for contexts, it's an array */
	*ptr = (struct thread_context *)
		rte_zmalloc("thread_context",
			flags->num_threads * sizeof(struct thread_context), 0);
	if (!(*ptr)) {
		printf("Failed to allocate context for the threads\n");
		cleanup(flags, NULL);
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
		if ((flags->direction == REQUEST_LOOPBACK) ||
			(flags->direction == REQUEST_BOTH)) {
#ifndef IFC_QDMA_DYN_CHAN
			q->phy_channel_id = id_counter + flags->start_ch;
#else
			q->phy_channel_id = id_counter;
#endif
#ifdef IFC_QDMA_INTF_ST
			q->direction = i % 2;
#else
			q->direction = (i + 1) % 2;
#endif
			id_counter += i % 2;

		} else {
#ifndef IFC_QDMA_DYN_CHAN
			q->phy_channel_id = i + flags->start_ch;
#else
			q->phy_channel_id = i;
#endif
			q->direction = flags->direction;
		}
		ret = get_dev_id_from_phy_ch(q->phy_channel_id, &devid, &(q->channel_id));
		if (ret < 0) {
			printf("Getting device ID failed\n");
			return -1;
		}
		q->port_id = port_ids[devid];
		if (qno == queues_per_thread) {
			tid++;
			qno = 0;
		}

		/* Set batch & file size */
		q->file_size = flags->file_size;
		if (q->direction == REQUEST_TRANSMIT)
			q->batch_size = flags->tx_batch_size;
		else
			q->batch_size = flags->rx_batch_size;
#ifdef	IFC_QDMA_META_DATA
		q->file_size = 1;
#endif
		if (q->direction == REQUEST_TRANSMIT) {
			snprintf(tx_pool_name, sizeof(tx_pool_name),
				"McDMA TX POOL %u%u", q->port_id, q->channel_id);
			q->tx_mbuf_pool = rte_pktmbuf_pool_create(tx_pool_name,
					IFC_QDMA_NUM_CHUNKS_IN_EACH_POOL,
					MBUF_CACHE_SIZE, MBUF_PRIVATE_DATA_SIZE,
					rte_align32pow2(q->flags->h2d_max_pyld), rte_socket_id());					
					//global_flags->packet_size, rte_socket_id());
			if (q->tx_mbuf_pool == NULL)
				return -1;
		}
	}
	return 0;
}

#ifdef IFC_MCDMA_MAILBOX
/*created thread context for mailbox */
int mailbox_context_init(struct mailbox_thread_context **mbctx, uint16_t portid)
{
        /* malloc memory for contexts, it's an array */
        *mbctx = (struct mailbox_thread_context *)
                rte_zmalloc("mailbox_thread_context",
                        sizeof(struct mailbox_thread_context), 0);
        if (!(*mbctx)) {
                printf("Failed to allocate mailbox context for the threads\n");
                return -1;
        }
        /* initialize request_context */
        memset(*mbctx, 0, sizeof(struct mailbox_thread_context));

        /*memory allocation for cmd  fifo*/
        (*mbctx)->cmd_fifo = (struct rte_kni_fifo*)
                 rte_zmalloc("rte_kni_fifo",
                         sizeof(struct rte_kni_fifo), 0);
         if (!((*mbctx)->cmd_fifo)) {
                 printf("Failed to allocate memory for cmd_fifo\n");
                 return -1;
         }
         /*Adding portid to mailbox global context to get device information for gcsr address space
 	 * acess
 	 */
        global_mb_tctx->portid = portid;

        /*Enable the WB method*/
        ifc_mb_intr_wb_control(portid, 0x1);

        return 0;
}

/*schedule, process and monitor the cmd status and also delete processd cmd type */
void* mailbox_schedule_msg_send(void *mbctx)
{
        struct mailbox_thread_context *mctx = (struct mailbox_thread_context *)mbctx;
        int index, ret;
        struct ifc_mcdma_device *mcdma_dev = NULL;
        struct ifc_mb_ctx  mb_ctx ;

        /*Validate mailbox context base address*/
        if (mctx == NULL) {
                printf(" mailbox context address is invalid\n");
                return -1;
        }

        /*Check cmd fifo based address*/
        if (mctx->cmd_fifo ==  NULL) {
                printf("cmd fifo address is invalid\n");
                return -1;
        }
        /*Check the cmd fifo size is not  empty*/
        if (mctx->cmd_fifo->len < 0) {
                printf("cmd fifo size is empty\n");
                return -1;
        }

	/*Get the cmds from cmd FIFO continuously until get*/
        while (true) {
        	/*check the cmd fifo is empty*/
                if (kni_fifo_count(mctx->cmd_fifo) == 0) {
                        /*wait and continue*/
                        continue;
                }
                /*Get the cmds from cmd fifo and send to hw module*/
                for (index = 0 ; index < kni_fifo_count(mctx->cmd_fifo); index++) {
                        /*Get the cmds from cmd fifo*/
                        ret = kni_fifo_get(mctx->cmd_fifo, &(mctx->cmd_list[index]), 1);
                        if (ret < 0) {
                                printf("Fail to get cmd from cmd fifo\n");
                        }

                        /*Identify the cmd*/
                        switch (mctx->cmd_list[index].cmd_type) {
                                case  IFC_MB_CMD_PING:
                                      mb_ctx.msg_payload.ping.cmd    = mctx->cmd_list[index].cmd_type;
                                      mb_ctx.msg_payload.ping.is_vf  = global_flags->pf ? 0: 1;;
                                      mb_ctx.msg_payload.ping.func_num  = 0; //sending to PF0 from PF1rom PF1
                                      /*Invoke the send cmd function*/
				      ifc_mcdma_mb_send(mctx->portid, &mb_ctx);
                                      break;
				case IFC_MB_CMD_DOWN:
                                     break;
                                case IFC_MB_CMD_CSR:
                                      break;
                                case IFC_MB_CMD_CHAN_ADD:
                                      break;
                                case IFC_MB_CMD_CHAN_DEL:
                                      break;
                                case IFC_MB_CMD_CSR_RESP:
                                      break;
                                case IFC_MB_CMD_CHAN_ADD_RESP:
                                      break;
                                case IFC_MB_CMD_CHAN_DEL_RESP:
                                      break;
                                default:
                                      printf("Invalid command\n");
                                      break;
                        }
                }

         /*Add the exit logic here*/

        } /*end of while */
        return 0;
}
			
/*To get  wb status and msg related functionality*/
void* mailbox_schedule_recv_msg(void *mbctx)
{
        struct mailbox_thread_context *mctx = (struct mailbox_thread_context *)mbctx;
        struct ifc_mcdma_device *mcdma_dev = NULL;
        struct ifc_mb_ctx  mb_ctx;
        int ret;
        struct ifc_mailbox_msg mailbox_msg;
        uint16_t portid;
	uint64_t wb_event;

        /*Validate the mailbox context address*/
	if (mctx == NULL) {
                printf(" mailbox context address is invalid\n");
                return -1;
        }
 	portid = mctx->portid;

        /*Continuously poll and monitor the WB cmd events from HW*/
        while (true) {
                /*poll for either WB event or Inbox status from HW*/
                do {
                         wb_event = ifc_mb_wb_receive_status(portid);
		/*Check WB event is an available or not*/
                } while (wb_event == 0);

                /*recv cmd response from hw and parse the data*/
                ret = ifc_mcdma_mb_cmd_recv(portid, &mailbox_msg);
		/*Validate recv cmd response*/
		if (ret != 0) {
			printf("Invalid address\n");
		}
		
		/*parse the cmd responce */

                /*need the break condition to exit loop*/
        }
	 
	return 0;
}

#endif

void* queues_schedule(void *ptr)
{
	int i, entries, finished_counter;
	struct thread_context *tctx = (struct thread_context *)ptr;
	entries = count_queues(global_flags);
	tctx->qcnt = entries/global_flags->num_threads;
	static int ret = 0;

	signal_mask();

	while (true) {
		ret++;
		finished_counter = 0;
		for (i = 0; i < tctx->qcnt; i++) {
			if (global_flags->mbuf_pool_fail) {
				tctx->qctx[i].status = THREAD_DEAD;
				finished_counter++;
				continue;
			}
			if (tctx->qctx[i].status == THREAD_DEAD) {
				finished_counter++;
				continue;
			}

			if (tctx->qctx[i].status != THREAD_NEW &&
			    tctx->qctx[i].status != THREAD_WAITING)
				tctx->qctx[i].status = THREAD_READY;

			tctx->qctx[i].epoch_done = 0;
			clock_gettime(CLOCK_MONOTONIC,
				&tctx->qctx[i].start_time_epoch);

			if ((tctx->flags->direction == REQUEST_LOOPBACK)
				&& (tctx->flags->hol)) {
				transfer_handler_loopback(&(tctx->qctx[i]));
			} else {
			#ifdef BUFFER_FORWARD_D2H_TO_H2D
                                 transfer_handler_util(&(tctx->qctx[i]));
                        #else
				transfer_handler(&(tctx->qctx[i]));
			#endif
			}
			tctx->qctx[i].switch_count++;
		}
		if (finished_counter == tctx->qcnt) {
			break;
		}
	}
	return 0;
}

#ifdef IFC_MCDMA_MAILBOX
int thread_creator(struct struct_flags *flags,
                struct thread_context *tctx,
                struct mailbox_thread_context *mbctx)
{
#else
int thread_creator(struct struct_flags *flags,
                   struct thread_context *tctx)
{
#endif
	int threads_count;
	int ret;
	int i = 0;
	unsigned lcore_id;
#ifdef IFC_MCDMA_MAILBOX
	int mb_threads_count;
	unsigned lcore_list[RTE_MAX_LCORE] = {0};
	/*Added two extra thread for mailbox feature purposes.
 	* one thread for send cmd and another thread for recv cmd.
 	*/
        threads_count = flags->num_threads + 2;
        mb_threads_count = flags->num_threads;
#else
	threads_count = flags->num_threads;
#endif

#ifndef DPDK_21_11_RC2
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
#else
	RTE_LCORE_FOREACH_WORKER(lcore_id) {
#endif
	#ifdef IFC_MCDMA_MAILBOX
	     if (i <  flags->num_threads) {
	#endif
		/* change thread status */
		tctx[i].status = THREAD_RUNNING;
		tctx[i].flags = flags;
		tctx[i].tid = i;
		ret = rte_eal_remote_launch((void *)queues_schedule,
				(void *)(tctx + i), lcore_id);
		if (ret) {
			printf("Failed to launch core\n");
			tctx[i].status = THREAD_DEAD;
			cleanup(flags, tctx);
			return -1;
		}

	#ifdef IFC_MCDMA_MAILBOX
	    }
            lcore_list[i] = lcore_id;
	#endif
		i++;
		if (i == threads_count) {
			#ifdef IFC_MCDMA_MAILBOX
                         ret = rte_eal_remote_launch((void *)mailbox_schedule_msg_send,
                                (void *)(mbctx), lcore_list[mb_threads_count]);
                        if (ret == -EBUSY) {
                                printf("The remote lcore is not in a wait state and it is busy val =%d\n",ret);
                        } else if (ret == -EPIPE) {
                                printf("Error reading or writing pipe to worker thread -EPIPE val =%d\n",ret);
                        }

                        ret = rte_eal_remote_launch((void *)mailbox_schedule_recv_msg,
                                (void *)(mbctx), lcore_list[mb_threads_count + 1]);
                        if (ret == -EBUSY) {
                                printf("The remote lcore is not in a wait state and it is busy val =%d\n",ret);
                        } else if (ret == -EPIPE) {
                                printf("Error reading or writing pipe to worker thread -EPIPE val =%d\n",ret);
                        }
                        #endif
			break;
		}
	}
	return 0;
}

static void update_avmm_addr(__rte_unused uint16_t qid,
			     __rte_unused uint64_t *addr,
			     __rte_unused struct struct_flags *flags)
{
#ifdef PERFQ_LOAD_DATA
	/* Using different memory segments to avoid overwrites in non-perf mode*/
#ifndef IFC_QDMA_INTF_ST
	/* Set base and memory limit for a channel */
	uint64_t mem_reg_size = PERFQ_AVMM_BUF_LIMIT / flags->chnls;
	//base
	addr[0] = qid * mem_reg_size;
	//limit
	addr[1] = addr[0] + mem_reg_size;
#endif
#else
	addr[0] = 0;
	addr[1] = 0;
#endif
}

#ifndef IFC_MCDMA_SINGLE_FUNC
static uint16_t get_chnls_per_func(uint16_t portid, uint16_t req_chnls)
{
	uint16_t chnls;
	uint16_t max_chnls;

	if (portid < IFC_QDMA_PFS) {
		/* pf */
		max_chnls = IFC_QDMA_PER_PF_CHNLS;
	} else {
		/* vf */
		max_chnls = IFC_QDMA_PER_VF_CHNLS;
	}

	if (req_chnls < max_chnls)
		chnls = req_chnls;
	else
		chnls = max_chnls;

	return chnls;
}
#endif

static inline int
port_init(uint16_t port, uint16_t rings)
{
	struct rte_eth_conf port_conf = port_conf_default;
#ifndef IFC_QDMA_DYN_CHAN
	const uint16_t rx_rings = rings +  global_flags->start_ch;
	const uint16_t tx_rings = rings +  global_flags->start_ch;
#else
	const uint16_t rx_rings = rings;
	const uint16_t tx_rings = rings;
#endif
	int retval;
	uint16_t q;
	struct rte_eth_rxconf rx_conf;
	struct rte_eth_txconf tx_conf;
	struct rte_mempool *rx_mbuf_pool = NULL;
	char rx_pool_name[64];

	memset(&rx_conf, 0, sizeof(struct rte_eth_rxconf));
	memset(&tx_conf, 0, sizeof(struct rte_eth_txconf));

	rx_conf.offloads = 0;
	rx_conf.reserved_ptrs[0] = ifc_mcdma_umsix_irq_handler;
	tx_conf.offloads = 0;
	tx_conf.reserved_ptrs[0] = ifc_mcdma_umsix_irq_handler;

	if (port >= rte_eth_dev_count_avail())
		return -1;

#ifndef DPDK_21_11_RC2
	port_conf.rxmode.max_rx_pkt_len = global_flags->packet_size;
#else
	port_conf.rxmode.max_lro_pkt_size = global_flags->packet_size;
#endif
	/* Configure the Ethernet device. */
	retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;
	/* Allocate and set up 1 RX queue per Ethernet port. */
#ifndef IFC_QDMA_DYN_CHAN
	for (q = global_flags->start_ch; q < rx_rings; q++) {
#else
	for (q = 0; q < rx_rings; q++) {
#endif
		snprintf(rx_pool_name, sizeof(rx_pool_name),
			 "McDMA RX POOL %u%u",port, q);
		/* Creates a new mempool in memory to hold the mbufs. */
		rx_mbuf_pool = rte_pktmbuf_pool_create(rx_pool_name,
			IFC_QDMA_NUM_CHUNKS_IN_EACH_POOL,
			MBUF_CACHE_SIZE, MBUF_PRIVATE_DATA_SIZE,
			global_flags->rx_packet_size, rte_socket_id());
		if (rx_mbuf_pool == NULL) {
			printf("Cannot create RX mbuf pool"
				" qid: %u\n", q);
			return -1;
		}
		update_avmm_addr(q, rx_conf.reserved_64s, global_flags);
		retval = rte_eth_rx_queue_setup(port, q, QUEUE_SIZE,
				rte_eth_dev_socket_id(port),&rx_conf,
				rx_mbuf_pool);
		if (retval < 0)
			return retval;
#ifdef IFC_QDMA_DYN_CHAN
		/* Get the physical channel number */
		mcdma_ph_chno[q] = retval;
#endif
	}
	/* Allocate and set up 1 TX queue per Ethernet port. */
#ifndef IFC_QDMA_DYN_CHAN
	for (q = global_flags->start_ch; q < tx_rings; q++) {
#else
	for (q = 0; q < tx_rings; q++) {
#endif
		update_avmm_addr(q, tx_conf.reserved_64s, global_flags);
		retval = rte_eth_tx_queue_setup(port, q, QUEUE_SIZE,
				rte_eth_dev_socket_id(port), &tx_conf);
		if (retval < 0)
			return retval;
	}

	/* Set MTU value */
	rte_eth_dev_set_mtu(port, global_flags->rx_packet_size);

	/* Start the Ethernet port. */
	retval = rte_eth_dev_start(port);
	if (retval < 0)
		return retval;

	/* Enable RX in promiscuous mode for the Ethernet device. */
	rte_eth_promiscuous_enable(port);
	return 0;
}

static int ifc_perform_bas_test(uint16_t portid)
{
	int ret = 0;

#ifndef IFC_MCDMA_SINGLE_FUNC
	RTE_ETH_FOREACH_DEV(portid) {
#endif
	/* Creates a new memzone in memory to hold the mbufs */
	bas_mzone = rte_memzone_reserve_bounded("BAS_Zone",
						IFC_MCDMA_BUF_LIMIT,
						rte_socket_id(),
						RTE_MEMZONE_IOVA_CONTIG |
						RTE_MEMZONE_1GB |
						RTE_MEMZONE_SIZE_HINT_ONLY,
						IFC_MCDMA_BUF_LIMIT,
						IFC_MCDMA_BUF_LIMIT);
	if (bas_mzone == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

	ret = bas_test(portid, global_flags);
	rte_memzone_free(bas_mzone);
#ifndef IFC_MCDMA_SINGLE_FUNC
	}
#endif
	return ret;
}

static int ifc_perform_pio_test(uint16_t portid)
{
	int ret = 0;

#ifndef IFC_MCDMA_SINGLE_FUNC
	RTE_ETH_FOREACH_DEV(portid) {
#endif
#ifdef IFC_PIO_512
		ret = pio_util_512(portid);
#elif defined(IFC_PIO_256)
		ret = pio_util_256(portid);
#elif defined(IFC_PIO_128)
		ret = pio_util_128(portid);
#else
		ret = pio_util(portid);
#endif
		pio_reset(portid);

#ifndef IFC_MCDMA_SINGLE_FUNC
	}
#endif
		return ret;
}

static int ifc_perform_pio_perf_test(uint16_t portid)
{
	int ret = 0;

#ifndef IFC_MCDMA_SINGLE_FUNC
	RTE_ETH_FOREACH_DEV(portid) {
#endif
#if defined(IFC_PIO_256)
	ret = pio_perf_util_256(portid);
#elif defined(IFC_PIO_128)
	ret = pio_perf_util_128(portid);
#else
	ret = pio_perf_util(portid);
#endif
		pio_reset(portid);

#ifndef IFC_MCDMA_SINGLE_FUNC
	}
#endif
		return ret;
}

static int ifc_port_init(void)
{
	uint16_t req_chnls = global_flags->chnls;
	uint16_t port = 0;

#ifndef IFC_MCDMA_SINGLE_FUNC
	RTE_ETH_FOREACH_DEV(port) {
		if (!req_chnls)
			break;
		uint16_t chnls = get_chnls_per_func(port, req_chnls);
		if (port_init(port, chnls) != 0) {
			rte_free(global_flags);
			rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu8 "\n",
			 port);
			return -1;
		}
		req_chnls -= chnls;
	}
	if (req_chnls) {
		printf("Not all devices bineded or Extra channels requested.\n");
		global_flags->chnls -= req_chnls;
		printf("Allocating chnls %d\n", global_flags->chnls);
	}
#else
	rte_eth_dev_get_port_by_name(global_flags->bdf, &port);

	if (port_init(port, req_chnls) != 0) {
		rte_free(global_flags);
		rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu8 "\n",
			 port);
		return -1;
	}
#endif
	return 0;
}

#ifdef IFC_QDMA_ST_PACKETGEN
#ifndef IFC_QDMA_ST_MULTI_PORT
static void ifc_configure_pio_regs(uint16_t portid)
{
	int ch_cnt = ifc_mcdma_pio_read64(portid,
				      PIO_REG_PORT_PKT_CH_CNT);

#ifdef IFC_QDMA_DYN_CHAN
        if (ch_cnt == 0 && mcdma_ph_chno[0] == 0)
                ch_cnt = global_flags->chnls - 1;
        else
                ch_cnt += global_flags->chnls;
#else
	if (!global_flags->chnl_cnt)
	        ch_cnt = AVST_CHANNELS - 1;
	else
		ch_cnt = global_flags->chnls - 1;
#endif

#ifdef IFC_INTF_AVST_256_CHANNEL
        ifc_mcdma_pio_write64(portid,
                              PIO_REG_PORT_PKT_CH_CNT, 256);
#else 
	ifc_mcdma_pio_write64(portid,
			      PIO_REG_PORT_PKT_CH_CNT, ch_cnt);
#endif

	if (global_flags->flimit == REQUEST_BY_SIZE)
		ifc_mcdma_pio_write64(portid, PIO_REG_PORT_PKT_RST, 0ULL);
	else {
#ifdef IFC_ED_CONFIG_TID_UPDATE
		ifc_mcdma_pio_write64(portid, PIO_REG_PORT_PKT_RST, 0ULL);
#else
		ifc_mcdma_pio_write64(portid, PIO_REG_PORT_PKT_RST, 1ULL);
#endif
	}

#if defined  CID_PAT && defined IFC_PROG_DATA_EN
	//Enbale the pattern switch;
	ifc_mcdma_pio_write64(portid, PIO_REG_PORT_PKT_ENABLE_PATTERN, 1ULL);
#else 
	ifc_mcdma_pio_write64(portid, PIO_REG_PORT_PKT_ENABLE_PATTERN, 0ULL);
#endif
	

}
#endif
#endif

int main(int argc, char *argv[])
{
	int ret = 0;
	uint16_t portid = 0;
	struct rte_eth_dev_info dev_info;

	/* setup the signal handler */
	signal(SIGINT, sig_handler);

	/* setup the memory signal handler */
        signal(SIGSEGV, memory_sig_handler);

	/* Initialize the Environment Abstraction Layer (EAL). */
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

	argc -= ret;
    	argv += ret;

	/* allocate memory to struct_flags */
	global_flags = (struct struct_flags *)rte_zmalloc("global_flags",
				sizeof(struct struct_flags), 0);
	if (!global_flags)
		rte_exit(EXIT_FAILURE, "Failed to allocate memory for flags\n");


	/* Applicatio specific parameters */
	ret = cmdline_option_parser(argc, argv, global_flags);
	if (ret) {
		rte_free(global_flags);
		return 1;
	}
	portid = port_ids[0];
	pthread_mutex_init(&dev_lock, NULL);

#ifdef VERIFY_FUNC
	dump_flags(global_flags);
#endif
#ifdef IFC_MCDMA_SINGLE_FUNC
	rte_eth_dev_get_port_by_name(global_flags->bdf, &portid);
	global_flags->portid = portid;
#endif
	/* check for ip-reset */
	if (global_flags->ip_reset) {
		/* Reset pf 0 */
		ret = mcdma_ip_reset(0);
		rte_free(global_flags);
		return ret;
	}

	/* check for PIO Read Write */
	if (global_flags->fpio) {
		if (global_flags->fbam_perf)
			ret = 	ifc_perform_pio_perf_test(portid);
		else
			ret = ifc_perform_pio_test(portid);
		rte_free(global_flags);
		return ret;
	}


	/* check for bas test */
	if (global_flags->fbas || global_flags->fbas_perf) {
		ret = ifc_perform_bas_test(portid);
		rte_free(global_flags);
		return ret;
	}

	/* get number of available_channels */
	ret = rte_eth_dev_info_get(portid, &dev_info);
		if (ret != 0)
			rte_exit(EXIT_FAILURE,
				"Error during getting device (port %u) "
				"info: %s\n",
				portid, strerror(-ret));

	if (global_flags->cleanup) {
		/* Reset pf 0 */
#ifdef IFC_QDMA_DYN_CHAN
		ifc_mcdma_release_all_channels(portid);
#endif
		rte_free(global_flags);
		return ret;
	}

	/* check for available channels */
	if (dev_info.max_rx_queues < global_flags->chnls) {
		printf("Available Channels: %d\nAllocating %d Channels...\n",
			dev_info.max_rx_queues, global_flags->chnls);
#ifdef IFC_MCDMA_SINGLE_FUNC
		global_flags->chnls = dev_info.max_rx_queues;
		rte_exit(EXIT_FAILURE, "channels not available %u \n",
				dev_info.max_rx_queues);
#endif
	} else
		printf("Allocating %d Channels...\n", global_flags->chnls);

	/* Initialize port. */
	if (ifc_port_init() != 0) {
#ifdef IFC_QDMA_DYN_CHAN
		ifc_mcdma_release_all_channels(portid);
#endif
		rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu8 "\n",
				portid);
	}

	srand(time(0));

	/* Initialize thread context */
	ret = context_init(global_flags, &global_tctx);
	if (ret < 0) {
		cleanup(global_flags, global_tctx);
		rte_exit(EXIT_FAILURE, "Cannot create Tx mempool for all queues\n");
	}

#ifdef IFC_MCDMA_MAILBOX
        /*Initialize mail-box thread context */
        ret = mailbox_context_init(&global_mb_tctx, portid);
        if (ret < 0) {
                /*Disable the WB method*/
                ifc_mb_intr_wb_control(portid, 0x0);

                /*free mailbox context memory*/
                if (global_mb_tctx != NULL) {
                        rte_free(global_mb_tctx);
                }
                rte_exit(EXIT_FAILURE, "Cannot created thread context for mailbox \n");
        }

	/*Find vf or pf port and fill the cmd types along with function either pf or vf.
 	*currentlly configured with only one cmd due to  not getting input from user.
 	*Need to discuss either all cmds configured internally or exteranally by user
 	*/
        int index = 0;
        global_mb_tctx->cmd_list[index].cmd_type = IFC_MB_CMD_PING;
        //global_mb_tctx->cmd_list[index].is_pf_vf= global_flags->pf;
	global_flags->pf =  1 ;/*TBD*/

        /*Configure cmd FIFO size based on cmds support for mailbox*/
        kni_fifo_init(global_mb_tctx->cmd_fifo, MB_MAX_NUM_CMDS);
	
	 /*TODO: send for all PFs or vf other than PF0*/
        if ( global_flags->chnls == 1) {
                global_flags->pf = 0;
                global_flags->is_pf = 1;
                global_flags->is_vf = 0;
        } else if (global_flags->chnls == 2) {
                global_flags->pf = 1;
                global_flags->is_pf = 1;
                global_flags->is_vf = 0;
		
		if ((global_flags->is_pf == 1) && (global_flags->pf != 0)) {
        		/*Submit the one cmd to  cmd fifo*/
        		ret = kni_fifo_put(global_mb_tctx->cmd_fifo, &(global_mb_tctx->cmd_list[index]), 1);
        		if (ret < 0) {
                		printf("Fail to submit cmd to cmd fifo\n");
                		return -1;
        		}
		}
	}

#endif

#ifdef IFC_QDMA_ST_PACKETGEN
#ifdef IFC_QDMA_ST_MULTI_PORT
	/* Initialize AVST specific things */
	pio_init(portid, global_flags);
#else
	ifc_configure_pio_regs(portid);
#endif
#endif

#ifdef IFC_MCDMA_MAILBOX
        thread_creator(global_flags, global_tctx, global_mb_tctx);
#else
        /* startup the threads */
        thread_creator(global_flags, global_tctx);
#endif
	/* print common stats */
	show_header(global_flags);

	ret = thread_monitor(global_tctx, global_flags, &global_last_checked,
		       &global_start_time);
	if (!global_flags->mbuf_pool_fail) {
		/* show summary */
		ret = show_summary(global_tctx, global_flags,
				   &global_last_checked, &global_start_time);
	}
	cleanup(global_flags, global_tctx);
	return ret ? 1 : 0;
}
