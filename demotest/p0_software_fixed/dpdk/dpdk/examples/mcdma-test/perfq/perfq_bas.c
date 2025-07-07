/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#include <inttypes.h>
#include <rte_hexdump.h>
#include "rte_mbuf.h"
#include "perfq_app.h"

#define BAS_ED PCIe_SLOT

extern const struct rte_memzone *bas_mzone;
extern struct struct_flags *global_flags;

#undef BAS_DEBUG
#pragma GCC push_options
#pragma GCC optimize ("O0")
static uint32_t perfq_load_data(void *buf, size_t size, uint32_t pattern)
{
        unsigned int i;

        for (i = 0; i < (size / sizeof(uint32_t)); i++)
                *(((uint32_t *)buf) + i) = pattern++;

        return pattern;
}
#pragma GCC pop_options

#ifdef IFC_QDMA_MSI_ENABLE
static void bas_wait_for_intr(void *ctx, int d)
{
	uint32_t ret;
	int dir;

	for (;;) {
		ret = ifc_mcdma_poll_wait_for_intr(ctx, 2, &dir);
		if (ret && dir == d)
			break;
	}
}
#endif

static void bas_wait_ctrl_deassert(uint16_t port_id,
				   uint64_t ctrl_offset,
				   uint64_t enable_mask)
{
	uint64_t val = 0;
	uint64_t ed_addr;
	int shift_off;
#if BAS_ED == 0
	shift_off = IFC_MCDMA_BAS_X16_SHIFT_WIDTH;
	val = IFC_MCDMA_BAS_X16_BURST_LENGTH;
#elif BAS_ED == 1
	shift_off = IFC_MCDMA_BAS_X8_SHIFT_WIDTH;
	val = IFC_MCDMA_BAS_X8_BURST_LENGTH;
#elif BAS_ED == 2
	shift_off = IFC_MCDMA_BAS_X4_SHIFT_WIDTH;
	val = IFC_MCDMA_BAS_X4_BURST_LENGTH;
#endif
	val = ((val & ~enable_mask) | enable_mask);
	ed_addr = ctrl_offset << shift_off;
	for (;;) {
		uint64_t ctrl_val = ifc_mcdma_read64(port_id, ed_addr, global_flags->bar);
		if (!(ctrl_val & enable_mask))
			break;
	}
}

static void disable_tx_non_stop_transfer_mode(uint16_t port_id)
{
	uint64_t val = 0;
	uint64_t ed_addr;
	int shift_off;
#if BAS_ED == 0
	shift_off = IFC_MCDMA_BAS_X16_SHIFT_WIDTH;
#elif BAS_ED == 1
	shift_off = IFC_MCDMA_BAS_X8_SHIFT_WIDTH;
#elif BAS_ED == 2
	shift_off = IFC_MCDMA_BAS_X4_SHIFT_WIDTH;
#endif
	/* Stop Writing */
	val = 0;
	ed_addr = IFC_MCDMA_BAS_WRITE_COUNT << shift_off;
	val = ifc_mcdma_read64(port_id, ed_addr, global_flags->bar);
	val &= IFC_MCDMA_BAS_NON_STOP_TRANSFER_DISABLE_MASK;
	ifc_mcdma_write64(port_id, ed_addr, val, global_flags->bar);

	bas_wait_ctrl_deassert(port_id, IFC_MCDMA_BAS_WRITE_CTRL,
				IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_MASK);
}

static void disable_rx_non_stop_transfer_mode(uint16_t port_id)
{
	uint64_t val = 0;
	uint64_t ed_addr;
	int shift_off;
#if BAS_ED == 0
	shift_off = IFC_MCDMA_BAS_X16_SHIFT_WIDTH;
#elif BAS_ED == 1
	shift_off = IFC_MCDMA_BAS_X8_SHIFT_WIDTH;
#elif BAS_ED == 2
	shift_off = IFC_MCDMA_BAS_X4_SHIFT_WIDTH;
#endif
	/* Stop Reading */
	val = 0;
	ed_addr = IFC_MCDMA_BAS_READ_COUNT << shift_off;
	val = ifc_mcdma_read64(port_id, ed_addr, global_flags->bar);
	val &= IFC_MCDMA_BAS_NON_STOP_TRANSFER_DISABLE_MASK;
	ifc_mcdma_write64(port_id, ed_addr, val, global_flags->bar);

	bas_wait_ctrl_deassert(port_id, IFC_MCDMA_BAS_READ_CTRL,
				IFC_MCDMA_BAS_READ_CTRL_ENABLE_MASK);

}

void compute_bw(struct struct_time *t,
			      struct timespec *start_time,
			      struct timespec *cur_time,
			      uint64_t pkts, uint32_t size)
{
	struct timespec ovrall_diff;
	double ovrall_tm_diff = 0;

	memset(&ovrall_diff, 0, sizeof(struct timespec));

	/* get elapsed time */
	time_diff(cur_time, start_time, &ovrall_diff);
	/* for BW computation */
	ovrall_tm_diff = ovrall_diff.tv_sec + ((double)ovrall_diff.tv_nsec / 1e9);

	/* don't want to hit divide by zero exception */
	if (ovrall_tm_diff <= 0.0) {
		t->ovrall_bw = 0.0;
	} else {
		t->ovrall_bw = (((double)pkts * size) /
				  ovrall_tm_diff);
	}
	/* convert bandwidth from BPS to GBPS */
	t->ovrall_bw = (t->ovrall_bw) / (1<<30);
}

static int bas_configure_tx(uint16_t port_id,
		            struct struct_flags *flags)
{
	struct timespec start_time, end_time, diff;
	uint64_t addr = BAS_ADDRESS;
	uint64_t val = 0;
	uint64_t ed_addr;
	int shift_off;
	uint32_t extra_bytes;	
#ifdef IFC_QDMA_MSI_ENABLE
	void *ctx;
#endif

	printf("Configuring BAS Write region...\n");

#if BAS_ED == 0
	shift_off = IFC_MCDMA_BAS_X16_SHIFT_WIDTH;
#elif BAS_ED == 1
	shift_off = IFC_MCDMA_BAS_X8_SHIFT_WIDTH;
#elif BAS_ED == 2
	shift_off = IFC_MCDMA_BAS_X4_SHIFT_WIDTH;
#endif
	/* create the polling handlers */
#ifdef IFC_QDMA_MSI_ENABLE
	ctx = ifc_mcdma_poll_init(port_id);
	ifc_mcdma_eventfd_poll_add(port_id, 1, ctx);
#endif

	/* BAS Write configure */
	/* Configure physical address */
	ed_addr = IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off;
#ifndef DPDK_21_11_RC2
	ifc_mcdma_write64(port_id, ed_addr, bas_mzone->phys_addr, global_flags->bar);
#else
	ifc_mcdma_write64(port_id, ed_addr, bas_mzone->iova, global_flags->bar);
#endif
	/* Configure start address */
	val = addr  << shift_off;
	ed_addr = IFC_MCDMA_BAS_WRITE_ADDR << shift_off;
	ifc_mcdma_write64(port_id, ed_addr, val, global_flags->bar);

	/* Configure burst count */
	val = (flags->request_size / IFC_MCDMA_BAS_BURST_BYTES);
	ed_addr = IFC_MCDMA_BAS_WRITE_COUNT << shift_off;
	ifc_mcdma_write64(port_id, ed_addr, val, global_flags->bar);

#ifdef BAS_DEBUG
	printf("printing after memset ...\n");
	rte_hexdump(stdout, "BEFORE_WRITE:", bas_mzone->addr, 256);
#endif

	/* Configure write control register */
	val = 0;
#if BAS_ED == 0
	val = IFC_MCDMA_BAS_X16_BURST_LENGTH;
#elif BAS_ED == 1
	val = IFC_MCDMA_BAS_X8_BURST_LENGTH;
#elif BAS_ED == 2
	val = IFC_MCDMA_BAS_X4_BURST_LENGTH;
#endif
	val = ((val & ~IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_MASK) |
		IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_MASK);
	ed_addr = IFC_MCDMA_BAS_WRITE_CTRL << shift_off;
	ifc_mcdma_write64(port_id, ed_addr, val, global_flags->bar);
	clock_gettime(CLOCK_MONOTONIC, &start_time);

#ifndef IFC_QDMA_MSI_ENABLE
        /* wait for ctrl to deassert */
	bas_wait_ctrl_deassert(port_id, IFC_MCDMA_BAS_WRITE_CTRL,
			        IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_MASK);
#else
	bas_wait_for_intr(ctx, 0);

#endif
	clock_gettime(CLOCK_MONOTONIC, &(end_time));

	time_diff(&end_time, &start_time, &diff);

#ifdef BAS_DEBUG
	printf("printing after BAS write...\n");
	printf("DEBUG: addr:0x%lx phy: 0x%lx\n",
		IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off,
	      ifc_mcdma_read64(port_id,
		IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off, global_flags->bar));
	printf("DEBUG: addr:0x%lx start: 0x%lx\n",
		IFC_MCDMA_BAS_WRITE_ADDR << shift_off,
		ifc_mcdma_read64(port_id,
				     IFC_MCDMA_BAS_WRITE_ADDR << shift_off, global_flags->bar));
	printf("DEBUG: addr:0x%lx burst: 0x%lx\n",
		IFC_MCDMA_BAS_WRITE_COUNT << shift_off,
		ifc_mcdma_read64(port_id,
				     IFC_MCDMA_BAS_WRITE_COUNT << shift_off, global_flags->bar));
	printf("DEBUG: addr:0x%lx ed_addr: 0x%lx\n",
		IFC_MCDMA_BAS_WRITE_CTRL << shift_off,
		ifc_mcdma_read64(port_id,
				     IFC_MCDMA_BAS_WRITE_CTRL << shift_off, global_flags->bar));
	rte_hexdump(stdout, "AFTER_WRITE:", bas_mzone->addr, 256);
#endif
	ed_addr = IFC_MCDMA_BAS_WRITE_ERR << shift_off;
	val = ifc_mcdma_read64(port_id, ed_addr, global_flags->bar);
	if (val == 0)
		printf("Tx:\t\t\tPass\n");
	else
		printf("TX:\t\t\tFailed error:%lu\n", val);

	val = pattern_checker(bas_mzone->addr, flags->request_size, 0, &extra_bytes, NULL);
	if (val == PERFQ_SUCCESS)
		printf("Data validation:\tPass\n");
	else
		printf("Data validation:\tFailed\n");
	printf("Completion time:\t%lus %luns\n", diff.tv_sec, diff.tv_nsec);

	return 0;
}

static int bas_configure_rx(uint16_t port_id,
			    struct struct_flags *flags)
{
	struct timespec start_time, end_time, diff;
	uint64_t addr = BAS_ADDRESS;
	uint64_t val = 0;
	uint64_t ed_addr;
	int shift_off;
#ifdef IFC_QDMA_MSI_ENABLE
	void *ctx;
#endif

	printf("Configuring BAS Read region...\n");

#if BAS_ED == 0
	shift_off = IFC_MCDMA_BAS_X16_SHIFT_WIDTH;
#elif BAS_ED == 1
	shift_off = IFC_MCDMA_BAS_X8_SHIFT_WIDTH;
#elif BAS_ED == 2
	shift_off = IFC_MCDMA_BAS_X4_SHIFT_WIDTH;
#endif

	/* create the polling handlers */
#ifdef IFC_QDMA_MSI_ENABLE
	ctx = ifc_mcdma_poll_init(port_id);
	ifc_mcdma_eventfd_poll_add(port_id, 0, ctx);
#endif

	/* BAS Read configure */
	/* Configure physical address */
	ed_addr = IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off;
#ifndef DPDK_21_11_RC2
	ifc_mcdma_write64(port_id, ed_addr, bas_mzone->phys_addr, global_flags->bar);
#else
	ifc_mcdma_write64(port_id, ed_addr, bas_mzone->iova, global_flags->bar);
#endif
	perfq_load_data(bas_mzone->addr, flags->request_size, 0);

	/* Configure start address */
	val = addr  << shift_off;
	ed_addr = IFC_MCDMA_BAS_READ_ADDR << shift_off;
	ifc_mcdma_write64(port_id, ed_addr, val, global_flags->bar);

	/* Configure burst count */
	val = (flags->request_size / IFC_MCDMA_BAS_BURST_BYTES);
	ed_addr = IFC_MCDMA_BAS_READ_COUNT << shift_off;
	ifc_mcdma_write64(port_id, ed_addr, val, global_flags->bar);

	/* Configure read control register */
	val = 0;
#if BAS_ED == 0
	val = IFC_MCDMA_BAS_X16_BURST_LENGTH;
#elif BAS_ED == 1
	val = IFC_MCDMA_BAS_X8_BURST_LENGTH;
#elif BAS_ED == 2
	val = IFC_MCDMA_BAS_X4_BURST_LENGTH;
#endif
	val = ((val & ~IFC_MCDMA_BAS_READ_CTRL_ENABLE_MASK) |
		IFC_MCDMA_BAS_READ_CTRL_ENABLE_MASK);
	ed_addr = IFC_MCDMA_BAS_READ_CTRL << shift_off;
	ifc_mcdma_write64(port_id, ed_addr, val, global_flags->bar);
	clock_gettime(CLOCK_MONOTONIC, &start_time);

#ifdef BAS_DEBUG
	sleep(1);
	printf("printing after BAS READ...\n");
	printf("DEBUG: addr:0x%lx phy: 0x%lx\n",
	       IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off,
	       ifc_mcdma_read64(port_id,
				    IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off, global_flags->bar));
	printf("DEBUG: addr:0x%lx start: 0x%lx\n",
	       IFC_MCDMA_BAS_READ_ADDR << shift_off,
	       ifc_mcdma_read64(port_id,
				    IFC_MCDMA_BAS_READ_ADDR << shift_off, global_flags->bar));
	printf("DEBUG: addr:0x%lx burst: 0x%lx\n",
	       IFC_MCDMA_BAS_READ_COUNT << shift_off,
	       ifc_mcdma_read64(port_id,
				    IFC_MCDMA_BAS_READ_COUNT << shift_off, global_flags->bar));
	printf("DEBUG: addr:0x%lx ed_addr: 0x%lx\n",
	       IFC_MCDMA_BAS_READ_CTRL << shift_off,
	       ifc_mcdma_read64(port_id,
				    IFC_MCDMA_BAS_READ_CTRL << shift_off, global_flags->bar));
	rte_hexdump(stdout, "AFTER_READ:", bas_mzone->addr, 256);
#endif
#ifndef IFC_QDMA_MSI_ENABLE
        /* wait for ctrl to deassert */
	bas_wait_ctrl_deassert(port_id, IFC_MCDMA_BAS_READ_CTRL,
				IFC_MCDMA_BAS_READ_CTRL_ENABLE_MASK);
#else
	bas_wait_for_intr(ctx, 0);
#endif

	clock_gettime(CLOCK_MONOTONIC, &(end_time));

	time_diff(&end_time, &start_time, &diff);

	ed_addr = IFC_MCDMA_BAS_READ_ERR << shift_off;
	val = ifc_mcdma_read64(port_id, ed_addr, global_flags->bar);
	if (val == 0)
		printf("RX:\t\t\tPass\n");
	else
		printf("RX:\t\t\tFailed error:%lu\n", val);
	printf("Completion time:\t%lus %luns\n", diff.tv_sec, diff.tv_nsec);

	return 0;
}

static int bas_configure_bi(uint16_t port_id,
			    struct struct_flags *flags)
{
	struct timespec start_time, end_time, diff;
	uint64_t addr = BAS_ADDRESS;
	uint64_t val = 0;
	int shift_off;
	uint64_t ed_addr;
	uint32_t extra_bytes;

	printf("Configuring BAS Write region...\n");

#if BAS_ED == 0
	shift_off = IFC_MCDMA_BAS_X16_SHIFT_WIDTH;
#elif BAS_ED == 1
	shift_off = IFC_MCDMA_BAS_X8_SHIFT_WIDTH;
#elif BAS_ED == 2
	shift_off = IFC_MCDMA_BAS_X4_SHIFT_WIDTH;
#endif
	/* BAS Write configure */
	/* Configure physical address */
	ed_addr = IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off;
#ifndef DPDK_21_11_RC2
	ifc_mcdma_write64(port_id, ed_addr, bas_mzone->phys_addr, global_flags->bar);
#else
	ifc_mcdma_write64(port_id, ed_addr, bas_mzone->iova, global_flags->bar);
#endif
	memset(bas_mzone->addr, 0xaa, 256);

	/* Configure start address */
	val = addr  << shift_off;
	ed_addr = IFC_MCDMA_BAS_WRITE_ADDR << shift_off;
	ifc_mcdma_write64(port_id, ed_addr, val, global_flags->bar);

	/* Configure burst count */
	val = (flags->request_size / IFC_MCDMA_BAS_BURST_BYTES);
	ed_addr = IFC_MCDMA_BAS_WRITE_COUNT << shift_off;
	ifc_mcdma_write64(port_id, ed_addr, val, global_flags->bar);

#ifdef BAS_DEBUG
	sleep(1);
	printf("printing after memset ...\n");
	rte_hexdump(stdout, "BEFORE_WRITE", bas_mzone->addr, 256);
#endif
	/* Configure write control register */
#if BAS_ED == 0
	val = IFC_MCDMA_BAS_X16_BURST_LENGTH;
#elif BAS_ED == 1
	val = IFC_MCDMA_BAS_X8_BURST_LENGTH;
#elif BAS_ED == 2
	val = IFC_MCDMA_BAS_X4_BURST_LENGTH;
#endif
	val = ((val & ~IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_MASK) |
		IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_MASK);
	ed_addr = IFC_MCDMA_BAS_WRITE_CTRL << shift_off;
	ifc_mcdma_write64(port_id, ed_addr, val, global_flags->bar);
	clock_gettime(CLOCK_MONOTONIC, &start_time);

        /* wait for ctrl to deassert */
	bas_wait_ctrl_deassert(port_id, IFC_MCDMA_BAS_WRITE_CTRL,
			        IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_MASK);
	clock_gettime(CLOCK_MONOTONIC, &(end_time));

#ifdef BAS_DEBUG
	printf("printing after BAS write...\n");
	printf("DEBUG: addr:0x%lx phy: 0x%lx\n",
	       IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off,
	       ifc_mcdma_read64(port_id,
				    IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off, global_flags->bar));
	printf("DEBUG: addr:0x%lx start: 0x%lx\n",
	       IFC_MCDMA_BAS_WRITE_ADDR << shift_off,
	       ifc_mcdma_read64(port_id,
				    IFC_MCDMA_BAS_WRITE_ADDR << shift_off, global_flags->bar));
	printf("DEBUG: addr:0x%lx burst: 0x%lx\n",
	       IFC_MCDMA_BAS_WRITE_COUNT << shift_off,
	       ifc_mcdma_read64(port_id,
				    IFC_MCDMA_BAS_WRITE_COUNT << shift_off, global_flags->bar));
	printf("DEBUG: addr:0x%lx ed_addr: 0x%lx\n",
	       IFC_MCDMA_BAS_WRITE_CTRL << shift_off,
	       ifc_mcdma_read64(port_id,
				    IFC_MCDMA_BAS_WRITE_CTRL << shift_off, global_flags->bar));
	rte_hexdump(stdout, "AFTER_WRITE:", bas_mzone->addr, 256);
#endif
	ed_addr = IFC_MCDMA_BAS_WRITE_ERR << shift_off;
	val = ifc_mcdma_read64(port_id, ed_addr, global_flags->bar);
	if (val == 0)
		printf("Tx:\t\t\tPass\n");
	else
		printf("TX:\t\t\tFailed error:%lu\n", val);

	val = pattern_checker(bas_mzone->addr, flags->request_size, 0, &extra_bytes, NULL);
	if (val == PERFQ_SUCCESS)
		printf("Data validation:\tPass\n");
	else
		printf("Data validation:\tFailed\n");
	time_diff(&end_time, &start_time, &diff);
	printf("Completion time:\t%lus %luns\n", diff.tv_sec, diff.tv_nsec);

	/* BAS Read configure */
	printf("Configuring BAS Read region...\n");
	/* Configure physical address */
	ed_addr = IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off;
#ifndef DPDK_21_11_RC2
	ifc_mcdma_write64(port_id, ed_addr, bas_mzone->phys_addr, global_flags->bar);
#else
	ifc_mcdma_write64(port_id, ed_addr, bas_mzone->iova, global_flags->bar);
#endif

	/* Configure start address */
	val = addr  << shift_off;
	ed_addr = IFC_MCDMA_BAS_READ_ADDR << shift_off;
	ifc_mcdma_write64(port_id, ed_addr, val, global_flags->bar);

	/* Configure burst count */
	val = (flags->request_size / IFC_MCDMA_BAS_BURST_BYTES);
	ed_addr = IFC_MCDMA_BAS_READ_COUNT << shift_off;
	ifc_mcdma_write64(port_id, ed_addr, val, global_flags->bar);

	/* Configure read control register */
	val = 0;
#if BAS_ED == 0
	val = IFC_MCDMA_BAS_X16_BURST_LENGTH;
#elif BAS_ED == 1
	val = IFC_MCDMA_BAS_X8_BURST_LENGTH;
#elif BAS_ED == 2
	val = IFC_MCDMA_BAS_X4_BURST_LENGTH;
#endif
	val = ((val & ~IFC_MCDMA_BAS_READ_CTRL_ENABLE_MASK) |
		IFC_MCDMA_BAS_READ_CTRL_ENABLE_MASK);
	ed_addr = IFC_MCDMA_BAS_READ_CTRL << shift_off;
	ifc_mcdma_write64(port_id, ed_addr, val, global_flags->bar);
	clock_gettime(CLOCK_MONOTONIC, &start_time);

#ifdef BAS_DEBUG
	sleep(1);
	printf("printing after BAS READ...\n");
	printf("DEBUG: addr:0x%lx phy: 0x%lx\n",
	       IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off,
	       ifc_mcdma_read64(port_id,
				    IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off, global_flags->bar));
	printf("DEBUG: addr:0x%lx start: 0x%lx\n",
	       IFC_MCDMA_BAS_READ_ADDR << shift_off,
	       ifc_mcdma_read64(port_id,
				    IFC_MCDMA_BAS_READ_ADDR << shift_off, global_flags->bar));
	printf("DEBUG: addr:0x%lx burst: 0x%lx\n",
	       IFC_MCDMA_BAS_READ_COUNT << shift_off,
	       ifc_mcdma_read64(port_id,
				    IFC_MCDMA_BAS_READ_COUNT << shift_off, global_flags->bar));
	printf("DEBUG: addr:0x%lx ed_addr: 0x%lx\n",
	       IFC_MCDMA_BAS_READ_CTRL << shift_off,
	       ifc_mcdma_read64(port_id,
				    IFC_MCDMA_BAS_READ_CTRL << shift_off, global_flags->bar));
	rte_hexdump(stdout, "AFTER_READ:", bas_mzone->addr, 256);
#endif
	bas_wait_ctrl_deassert(port_id, IFC_MCDMA_BAS_READ_CTRL,
				IFC_MCDMA_BAS_READ_CTRL_ENABLE_MASK);
	clock_gettime(CLOCK_MONOTONIC, &(end_time));

	ed_addr = IFC_MCDMA_BAS_READ_ERR << shift_off;
	val = ifc_mcdma_read64(port_id, ed_addr, global_flags->bar);
	if (val == 0)
		printf("RX:\t\t\tPass\n");
	else
		printf("RX:\t\t\tFailed error:%lu\n", val);

	time_diff(&end_time, &start_time, &diff);
	printf("Completion time:\t%lus %luns\n", diff.tv_sec, diff.tv_nsec);

	return 0;
}

static int bas_configure_perf(uint16_t port_id,
			      struct struct_flags *flags)
{
	struct timespec start_time, diff;
	uint64_t addr = BAS_ADDRESS;
	uint64_t val = 0;
	int shift_off;
	uint64_t ed_addr;
	uint64_t tx_pkts = 0;
	uint64_t rx_pkts = 0;
	struct timespec stop_time;
	struct struct_time t_tx;
	struct struct_time t_rx;
	long timediff_sec;

	printf("Configuring BAS Write region...\n");


#if BAS_ED == 0
	shift_off = IFC_MCDMA_BAS_X16_SHIFT_WIDTH;
#elif BAS_ED == 1
	shift_off = IFC_MCDMA_BAS_X8_SHIFT_WIDTH;
#elif BAS_ED == 2
	shift_off = IFC_MCDMA_BAS_X4_SHIFT_WIDTH;
#endif
	/* BAS Write configure */
	/* Configure physical address */
	ed_addr = IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off;
#ifndef DPDK_21_11_RC2
	ifc_mcdma_write64(port_id, ed_addr, bas_mzone->phys_addr, global_flags->bar);
#else
	ifc_mcdma_write64(port_id, ed_addr, bas_mzone->iova, global_flags->bar);
#endif
	memset(bas_mzone->addr, 0xaa, 256);

	/* Configure start address */
	val = addr  << shift_off;
	ed_addr = IFC_MCDMA_BAS_WRITE_ADDR << shift_off;
	ifc_mcdma_write64(port_id, ed_addr, val, global_flags->bar);

	/* Configure burst count */
	val = (flags->request_size / IFC_MCDMA_BAS_BURST_BYTES);
	val |= IFC_MCDMA_BAS_NON_STOP_TRANSFER_ENABLE_MASK;
	ed_addr = IFC_MCDMA_BAS_WRITE_COUNT << shift_off;
	ifc_mcdma_write64(port_id, ed_addr, val, global_flags->bar);

#ifdef BAS_DEBUG
	sleep(1);
	printf("printing after memset ...\n");
	rte_hexdump(stdout, "", bas_mzone->addr, 256);
#endif

	/* Configure write control register */
#if BAS_ED == 0
	val = IFC_MCDMA_BAS_X16_BURST_LENGTH;
#elif BAS_ED == 1
	val = IFC_MCDMA_BAS_X8_BURST_LENGTH;
#elif BAS_ED == 2
	val = IFC_MCDMA_BAS_X4_BURST_LENGTH;
#endif
	val = ((val & ~IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_MASK) |
		IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_MASK);
	ed_addr = IFC_MCDMA_BAS_WRITE_CTRL << shift_off;
	ifc_mcdma_write64(port_id, ed_addr, val, global_flags->bar);

#ifdef BAS_DEBUG
	printf("printing after BAS write...\n");
	printf("DEBUG: addr:0x%lx phy: 0x%lx\n",
	       IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off,
	       ifc_mcdma_read64(port_id,
				    IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off, global_flags->bar));
	printf("DEBUG: addr:0x%lx start: 0x%lx\n",
	       IFC_MCDMA_BAS_WRITE_ADDR << shift_off,
	       ifc_mcdma_read64(port_id,
				    IFC_MCDMA_BAS_WRITE_ADDR << shift_off, global_flags->bar));
	printf("DEBUG: addr:0x%lx burst: 0x%lx\n",
	       IFC_MCDMA_BAS_WRITE_COUNT << shift_off,
	       ifc_mcdma_read64(port_id,
				    IFC_MCDMA_BAS_WRITE_COUNT << shift_off, global_flags->bar));
	printf("DEBUG: addr:0x%lx ed_addr: 0x%lx\n",
	       IFC_MCDMA_BAS_WRITE_CTRL << shift_off,
	       ifc_mcdma_read64(port_id,
				    IFC_MCDMA_BAS_WRITE_CTRL << shift_off, global_flags->bar));
	rte_hexdump(stdout, "AFTER_WRITE:", bas_mzone->addr, 256);
#endif

	/* BAS Read configure */
	printf("Configuring BAS Read region...\n");
	/* Configure physical address */
	ed_addr = IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off;
#ifndef DPDK_21_11_RC2
	ifc_mcdma_write64(port_id, ed_addr, bas_mzone->phys_addr, global_flags->bar);
#else
	ifc_mcdma_write64(port_id, ed_addr, bas_mzone->iova, global_flags->bar);
#endif

#ifdef BAS_DEBUG
	printf("printing before Issuing BAS READ...\n");
	rte_hexdump(stdout, "BEFORE_READ:", bas_mzone->addr, 256);
#endif

	/* Configure start address */
	val = addr  << shift_off;
	ed_addr = IFC_MCDMA_BAS_READ_ADDR << shift_off;
	ifc_mcdma_write64(port_id, ed_addr, val, global_flags->bar);

	/* Configure burst count */
	val = (flags->request_size / IFC_MCDMA_BAS_BURST_BYTES);
	val |= IFC_MCDMA_BAS_NON_STOP_TRANSFER_ENABLE_MASK;
	ed_addr = IFC_MCDMA_BAS_READ_COUNT << shift_off;
	ifc_mcdma_write64(port_id, ed_addr, val, global_flags->bar);

	/* Configure read control register */
	val = 0;
#if BAS_ED == 0
	val = IFC_MCDMA_BAS_X16_BURST_LENGTH;
#elif BAS_ED == 1
	val = IFC_MCDMA_BAS_X8_BURST_LENGTH;
#elif BAS_ED == 2
	val = IFC_MCDMA_BAS_X4_BURST_LENGTH;
#endif
	val = ((val & ~IFC_MCDMA_BAS_READ_CTRL_ENABLE_MASK) |
		IFC_MCDMA_BAS_READ_CTRL_ENABLE_MASK);
	ed_addr = IFC_MCDMA_BAS_READ_CTRL << shift_off;
	ifc_mcdma_write64(port_id, ed_addr, val, global_flags->bar);

#ifdef BAS_DEBUG
	sleep(1);
	printf("printing after Issuing BAS READ...\n");
	printf("DEBUG: addr:0x%lx phy: 0x%lx\n",
	       IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off,
	       ifc_mcdma_read64(port_id,
				    IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off, global_flags->bar));
	printf("DEBUG: addr:0x%lx start: 0x%lx\n",
	       IFC_MCDMA_BAS_READ_ADDR << shift_off,
	       ifc_mcdma_read64(port_id,
				    IFC_MCDMA_BAS_READ_ADDR << shift_off, global_flags->bar));
	printf("DEBUG: addr:0x%lx burst: 0x%lx\n",
	       IFC_MCDMA_BAS_READ_COUNT << shift_off,
	       ifc_mcdma_read64(port_id,
				    IFC_MCDMA_BAS_READ_COUNT << shift_off, global_flags->bar));
	printf("DEBUG: addr:0x%lx ed_addr: 0x%lx\n",
	       IFC_MCDMA_BAS_READ_CTRL << shift_off,
	       ifc_mcdma_read64(port_id,
				    IFC_MCDMA_BAS_READ_CTRL << shift_off, global_flags->bar));
	rte_hexdump(stdout, "AFTER_READ:", bas_mzone->addr, 256);
#endif

	clock_gettime(CLOCK_MONOTONIC, &start_time);
	while (true) {
		clock_gettime(CLOCK_MONOTONIC, &stop_time);
		timediff_sec = difftime(stop_time.tv_sec,
					start_time.tv_sec);
		if (timediff_sec >= 10 &&
		    (stop_time.tv_nsec >= start_time.tv_nsec)) {

			disable_tx_non_stop_transfer_mode(port_id);
			disable_rx_non_stop_transfer_mode(port_id);

			/* Compute Bw */
			compute_bw(&t_tx, &start_time, &stop_time, tx_pkts, IFC_MCDMA_BAS_BURST_BYTES);
			compute_bw(&t_rx, &start_time, &stop_time, rx_pkts, IFC_MCDMA_BAS_BURST_BYTES);
			break;
		} else {
			usleep(1000);
			ed_addr = IFC_MCDMA_BAS_WRITE_COUNT << shift_off;
			val = ifc_mcdma_read64(port_id, ed_addr, global_flags->bar);
			val &= IFC_MCDMA_BAS_TRANSFER_COUNT_MASK;
			tx_pkts += val;

			ed_addr = IFC_MCDMA_BAS_READ_COUNT << shift_off;
			val = ifc_mcdma_read64(port_id, ed_addr, global_flags->bar);
			val &= IFC_MCDMA_BAS_TRANSFER_COUNT_MASK;
			rx_pkts += val;
		}
	}

	ed_addr = IFC_MCDMA_BAS_WRITE_ERR << shift_off;
	val = ifc_mcdma_read64(port_id, ed_addr, global_flags->bar);
	if (val == 0)
		printf("Tx:\t\t\tPass\n");
	else
		printf("TX:\t\t\tFailed error:%lu\n", val);

	ed_addr = IFC_MCDMA_BAS_READ_ERR << shift_off;
	val = ifc_mcdma_read64(port_id, ed_addr, global_flags->bar);
	if (val == 0)
		printf("RX:\t\t\tPass\n");
	else
		printf("RX:\t\t\tFailed error:%lu\n", val);

	time_diff(&stop_time, &start_time, &diff);
	printf("TX Bandwidth:\t\t%.2fGBPS\n",t_tx.ovrall_bw);
	printf("RX Bandwidth:\t\t%.2fGBPS\n",t_rx.ovrall_bw);
	printf("Total Bandwidth:\t%.2fGBPS\n",(t_rx.ovrall_bw + t_tx.ovrall_bw));
	printf("Completion time:\t%lus %luns\n", diff.tv_sec, diff.tv_nsec);
	return 0;
}

int bas_test(uint16_t port_id,
	     struct struct_flags *flags)
{
	int ret = -1;

	if (flags->direction == IFC_QDMA_DIRECTION_TX)
		ret = bas_configure_tx(port_id, flags);
	else if (flags->direction == IFC_QDMA_DIRECTION_RX)
		ret = bas_configure_rx(port_id, flags);
	else  {
		if (flags->fbas_perf)
			ret = bas_configure_perf(port_id, flags);
		else
			ret = bas_configure_bi(port_id, flags);
	}
	return ret;
}
