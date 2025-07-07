// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#include "perfq_app.h"
#include <inttypes.h>

#define  BAS_ED PCIe_SLOT

#undef BAS_DEBUG


/* Globals */
struct bas_ctx_s bas_ctx;


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

#ifdef BAS_DEBUG
static void qdma_hex_dump(unsigned char *base, int len)
{
        int i;

        for (i = 0; i < len; i++) {
                if ((i % 16) == 0)
#ifdef IFC_32BIT_SUPPORT
                        printf("\n%8llx ", (uint64_t)(uintptr_t) base + i);
#else
			printf("\n%8lx ", (uint64_t) base + i);
#endif
                printf("%02x ", base[i]);
        }
        printf ("\n");
}
#endif

static void bas_wait_ctrl_deassert(struct ifc_qdma_device *qdev,
				   uint64_t ctrl_offset, uint64_t enable_mask)
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
#ifdef IFC_32BIT_SUPPORT
		uint64_t ctrl_val = ifc_qdma_read32(qdev, ed_addr, global_flags->rbar);
#else
		uint64_t ctrl_val = ifc_qdma_read64(qdev, ed_addr, global_flags->rbar);
#endif
		if (!(ctrl_val & enable_mask))
			break;
	}
}

static void disable_tx_non_stop_transfer_mode(struct ifc_qdma_device *qdev)
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

#ifdef IFC_32BIT_SUPPORT
	val = ifc_qdma_read32(qdev, ed_addr, global_flags->rbar);
#else
	val = ifc_qdma_read64(qdev, ed_addr, global_flags->rbar);
#endif
	val &= IFC_MCDMA_BAS_NON_STOP_TRANSFER_DISABLE_MASK;
	
#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, val, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, val, global_flags->wbar);
#endif

	bas_wait_ctrl_deassert(qdev, IFC_MCDMA_BAS_WRITE_CTRL,
				IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_MASK);
}

static void disable_rx_non_stop_transfer_mode(struct ifc_qdma_device *qdev)
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
	
#ifdef IFC_32BIT_SUPPORT
	val = ifc_qdma_read32(qdev, ed_addr, global_flags->rbar);
#else
	val = ifc_qdma_read64(qdev, ed_addr, global_flags->rbar);
#endif
	val &= IFC_MCDMA_BAS_NON_STOP_TRANSFER_DISABLE_MASK;

#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, val, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, val, global_flags->wbar);
#endif

	bas_wait_ctrl_deassert(qdev, IFC_MCDMA_BAS_READ_CTRL,
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

#ifdef IFC_QDMA_MSI_ENABLE
static int bas_configure_intr(struct ifc_qdma_device *qdev, int dir)
{
	int ret;

	bas_ctx.poll_ctx = ifc_qdma_poll_init(qdev);
	if (bas_ctx.poll_ctx == NULL)
		return PERFQ_CMPLTN_NTFN_FAILURE;

	ret = ifc_mcdma_poll_add_event(qdev, dir, bas_ctx.poll_ctx);
	if (ret < 0)
		return PERFQ_CMPLTN_NTFN_FAILURE;

	return 0;
}
#endif

static int bas_configure_tx(struct ifc_qdma_device *qdev,
		     struct struct_flags *flags __attribute__((unused)))
{
	struct ifc_qdma_request *req = NULL;
	struct timespec start_time, end_time, diff;
	uint64_t addr = BAS_ADDRESS;
	uint32_t payload = 1 << 20; // 1MB
	uint64_t val = 0;
	uint64_t ed_addr;
	int shift_off;

	printf("Configuration BAS Write region...\n");

#if BAS_ED == 0
	shift_off = IFC_MCDMA_BAS_X16_SHIFT_WIDTH;
#elif BAS_ED == 1
	shift_off = IFC_MCDMA_BAS_X8_SHIFT_WIDTH;
#elif BAS_ED == 2
	shift_off = IFC_MCDMA_BAS_X4_SHIFT_WIDTH;
#endif

#ifdef IFC_QDMA_MSI_ENABLE
	bas_configure_intr(qdev, 1);
#endif

	/* BAS Write configure */
	/* Configure physical address */
	req = ifc_request_malloc(payload);
	if (!req) {
		printf("ERR: Failed to allocate memory\n");
		return -1;
	}


	ed_addr = IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off;

#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, req->phy_addr, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, req->phy_addr, global_flags->wbar);
#endif

	/* Configure start address */
	val = addr  << shift_off;
	ed_addr = IFC_MCDMA_BAS_WRITE_ADDR << shift_off;

#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, val, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, val, global_flags->wbar);
#endif

	/* Configure burst count */
	val = (global_flags->request_size / IFC_MCDMA_BAS_BURST_BYTES);
	ed_addr = IFC_MCDMA_BAS_WRITE_COUNT << shift_off;

#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, val, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, val, global_flags->wbar);
#endif

#ifdef BAS_DEBUG
	printf("printing after memset ...\n");
	qdma_hex_dump(req->buf, 256);
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
	val = ((val & ~IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_MASK) | IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_MASK);
	ed_addr = IFC_MCDMA_BAS_WRITE_CTRL << shift_off;

#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, val, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, val, global_flags->wbar);
#endif
	clock_gettime(CLOCK_MONOTONIC, &start_time);

        /* wait for ctrl to deassert */
	bas_wait_ctrl_deassert(qdev, IFC_MCDMA_BAS_WRITE_CTRL,
			        IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_MASK);
	clock_gettime(CLOCK_MONOTONIC, &(end_time));

	time_diff(&end_time, &start_time, &diff);

#ifdef BAS_DEBUG
#ifdef IFC_32BIT_SUPPORT
	printf("printing after BAS write...\n");
	printf("DEBUG: addr:0x%lx phy: 0x%x\n", IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off,
	      ifc_qdma_read32(qdev, IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx start: 0x%x\n", IFC_MCDMA_BAS_WRITE_ADDR << shift_off,
		ifc_qdma_read32(qdev, IFC_MCDMA_BAS_WRITE_ADDR << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx burst: 0x%x\n", IFC_MCDMA_BAS_WRITE_COUNT << shift_off,
		ifc_qdma_read32(qdev, IFC_MCDMA_BAS_WRITE_COUNT << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx ed_addr: 0x%x\n", IFC_MCDMA_BAS_WRITE_CTRL << shift_off,
		ifc_qdma_read32(qdev, IFC_MCDMA_BAS_WRITE_CTRL << shift_off, global_flags->rbar));
	qdma_hex_dump(req->buf, 256);
#else
	printf("printing after BAS write...\n");
	printf("DEBUG: addr:0x%lx phy: 0x%lx\n", IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off,
	      ifc_qdma_read64(qdev, IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx start: 0x%lx\n", IFC_MCDMA_BAS_WRITE_ADDR << shift_off,
		ifc_qdma_read64(qdev, IFC_MCDMA_BAS_WRITE_ADDR << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx burst: 0x%lx\n", IFC_MCDMA_BAS_WRITE_COUNT << shift_off,
		ifc_qdma_read64(qdev, IFC_MCDMA_BAS_WRITE_COUNT << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx ed_addr: 0x%lx\n", IFC_MCDMA_BAS_WRITE_CTRL << shift_off,
		ifc_qdma_read64(qdev, IFC_MCDMA_BAS_WRITE_CTRL << shift_off, global_flags->rbar));
	qdma_hex_dump(req->buf, 256);
#endif
#endif
	ed_addr = IFC_MCDMA_BAS_WRITE_ERR << shift_off;

#ifdef IFC_32BIT_SUPPORT
	val = ifc_qdma_read32(qdev, ed_addr, global_flags->rbar);
#else
	val = ifc_qdma_read64(qdev, ed_addr, global_flags->rbar);
#endif
	
	if (val == 0)
		printf("Tx:\t\t\tPass\n");
	else
#ifdef IFC_32BIT_SUPPORT
		printf("TX:\t\t\tFailed error:%llu\n", val);
#else
		printf("TX:\t\t\tFailed error:%lu\n", val);
#endif

	val = pattern_checker(req->buf, global_flags->request_size, 0, 0x00);
	if (val == PERFQ_SUCCESS)
		printf("Data validation:\tPass\n");
	else
		printf("Data validation:\tFailed\n");
	printf("Completion time:\t%lus %luns\n", diff.tv_sec, diff.tv_nsec);

	ifc_request_free(req);
	return val;
}

static int bas_configure_rx(struct ifc_qdma_device *qdev,
		struct struct_flags *flags __attribute__((unused)))
{
	struct ifc_qdma_request *req = NULL;
	struct timespec start_time, end_time, diff;
	uint64_t addr = BAS_ADDRESS;
	uint32_t payload = 1 << 20; // 1MB
	uint64_t val = 0;
	uint64_t ed_addr;
	int shift_off;

	printf("Configuration BAS Read region...\n");

#if BAS_ED == 0
	shift_off = IFC_MCDMA_BAS_X16_SHIFT_WIDTH;
#elif BAS_ED == 1
	shift_off = IFC_MCDMA_BAS_X8_SHIFT_WIDTH;
#elif BAS_ED == 2
	shift_off = IFC_MCDMA_BAS_X4_SHIFT_WIDTH;
#endif

	/* BAS Write configure */
	/* Configure physical address */
	req = ifc_request_malloc(payload);
	if (!req) {
		printf("ERR: Failed to allocate memory\n");
		return -1;
	}
	ed_addr = IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off;

#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, req->phy_addr, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, req->phy_addr, global_flags->wbar);
#endif
	perfq_load_data(req->buf, global_flags->request_size, 0);

#ifdef BAS_DEBUG
	printf("printing before Issuing BAS READ...\n");
	qdma_hex_dump(req->buf, 64);
#endif

	/* BAS Read configure */
	/* Configure physical address */
	ed_addr = IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off;

#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, req->phy_addr, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, req->phy_addr, global_flags->wbar);
#endif

	/* Configure start address */
	val = addr  << shift_off;
	ed_addr = IFC_MCDMA_BAS_READ_ADDR << shift_off;

#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, val, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, val, global_flags->wbar);
#endif


	/* Configure burst count */
	val = (global_flags->request_size / IFC_MCDMA_BAS_BURST_BYTES);
	ed_addr = IFC_MCDMA_BAS_READ_COUNT << shift_off;

#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, val, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, val, global_flags->wbar);
#endif

	/* Configure read control register */
	val = 0;
#if BAS_ED == 0
	val = IFC_MCDMA_BAS_X16_BURST_LENGTH;
#elif BAS_ED == 1
	val = IFC_MCDMA_BAS_X8_BURST_LENGTH;
#elif BAS_ED == 2
	val = IFC_MCDMA_BAS_X4_BURST_LENGTH;
#endif
	val = ((val & ~IFC_MCDMA_BAS_READ_CTRL_ENABLE_MASK) | IFC_MCDMA_BAS_READ_CTRL_ENABLE_MASK);
	ed_addr = IFC_MCDMA_BAS_READ_CTRL << shift_off;
#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, val, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, val, global_flags->wbar);
#endif
	clock_gettime(CLOCK_MONOTONIC, &start_time);

#ifdef BAS_DEBUG
#ifdef IFC_32BIT_SUPPORT
	printf("printing after BAS READ...\n");
	printf("DEBUG: addr:0x%lx phy: 0x%x\n", IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off,
	      ifc_qdma_read32(qdev, IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx start: 0x%x\n", IFC_MCDMA_BAS_READ_ADDR << shift_off,
		ifc_qdma_read32(qdev, IFC_MCDMA_BAS_READ_ADDR << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx burst: 0x%x\n", IFC_MCDMA_BAS_READ_COUNT << shift_off,
		ifc_qdma_read32(qdev, IFC_MCDMA_BAS_READ_COUNT << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx ed_addr: 0x%x\n", IFC_MCDMA_BAS_READ_CTRL << shift_off,
		ifc_qdma_read32(qdev, IFC_MCDMA_BAS_READ_CTRL << shift_off, global_flags->rbar));
	qdma_hex_dump(req->buf, 256);
#else
	printf("printing after BAS READ...\n");
	printf("DEBUG: addr:0x%lx phy: 0x%lx\n", IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off,
	      ifc_qdma_read64(qdev, IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx start: 0x%lx\n", IFC_MCDMA_BAS_READ_ADDR << shift_off,
		ifc_qdma_read64(qdev, IFC_MCDMA_BAS_READ_ADDR << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx burst: 0x%lx\n", IFC_MCDMA_BAS_READ_COUNT << shift_off,
		ifc_qdma_read64(qdev, IFC_MCDMA_BAS_READ_COUNT << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx ed_addr: 0x%lx\n", IFC_MCDMA_BAS_READ_CTRL << shift_off,
		ifc_qdma_read64(qdev, IFC_MCDMA_BAS_READ_CTRL << shift_off, global_flags->rbar));
	qdma_hex_dump(req->buf, 256);
#endif
#endif
        /* wait for ctrl to deassert */
	bas_wait_ctrl_deassert(qdev, IFC_MCDMA_BAS_READ_CTRL,
				IFC_MCDMA_BAS_READ_CTRL_ENABLE_MASK);
	clock_gettime(CLOCK_MONOTONIC, &(end_time));

	time_diff(&end_time, &start_time, &diff);

	ed_addr = IFC_MCDMA_BAS_READ_ERR << shift_off;
#ifdef IFC_32BIT_SUPPORT
	val = ifc_qdma_read32(qdev, ed_addr, global_flags->rbar);
#else
	val = ifc_qdma_read64(qdev, ed_addr, global_flags->rbar);
#endif
	if (val == 0)
		printf("RX:\t\t\tPass\n");
	else
#ifdef IFC_32BIT_SUPPORT
		printf("RX:\t\t\tFailed error:%llu\n", val);
#else
		printf("RX:\t\t\tFailed error:%lu\n", val);
#endif
	printf("Completion time:\t%lus %luns\n", diff.tv_sec, diff.tv_nsec);

	ifc_request_free(req);
	return val;
}

static int bas_configure_bi(struct ifc_qdma_device *qdev,
		struct struct_flags *flags __attribute__((unused)))
{
	struct ifc_qdma_request *req = NULL;
	struct timespec start_time, end_time, diff;
	uint64_t addr = BAS_ADDRESS;
	uint32_t payload = 1 << 20; // 1MB
	uint64_t val = 0;
	int shift_off;
	uint64_t ed_addr;

	printf("Configuration BAS Write region...\n");

#if BAS_ED == 0
	shift_off = IFC_MCDMA_BAS_X16_SHIFT_WIDTH;
#elif BAS_ED == 1
	shift_off = IFC_MCDMA_BAS_X8_SHIFT_WIDTH;
#elif BAS_ED == 2
	shift_off = IFC_MCDMA_BAS_X4_SHIFT_WIDTH;
#endif

	/* BAS Write configure */
	/* Configure physical address */
	req = ifc_request_malloc(payload);
	if (!req) {
		printf("ERR: Failed to allocate memory\n");
		return -1;
	}
	ed_addr = IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off;
#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, req->phy_addr, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, req->phy_addr, global_flags->wbar);
#endif
	memset (req->buf, 0xaa, 256);

	/* Configure start address */
	val = addr  << shift_off;
	ed_addr = IFC_MCDMA_BAS_WRITE_ADDR << shift_off;
#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, val, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, val, global_flags->wbar);
#endif

	/* Configure burst count */
	val = (global_flags->request_size / IFC_MCDMA_BAS_BURST_BYTES);
	ed_addr = IFC_MCDMA_BAS_WRITE_COUNT << shift_off;
#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, val, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, val, global_flags->wbar);
#endif

#ifdef BAS_DEBUG
	sleep(1);
	printf("printing after memset ...\n");
	qdma_hex_dump(req->buf, 256);
#endif

	/* Configure write control register */
#if BAS_ED == 0
	val = IFC_MCDMA_BAS_X16_BURST_LENGTH;
#elif BAS_ED == 1
	val = IFC_MCDMA_BAS_X8_BURST_LENGTH;
#elif BAS_ED == 2
	val = IFC_MCDMA_BAS_X4_BURST_LENGTH;
#endif
	val = ((val & ~IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_MASK) | IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_MASK);
	ed_addr = IFC_MCDMA_BAS_WRITE_CTRL << shift_off;
#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, val, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, val, global_flags->wbar);
#endif
	clock_gettime(CLOCK_MONOTONIC, &start_time);

        /* wait for ctrl to deassert */
	bas_wait_ctrl_deassert(qdev, IFC_MCDMA_BAS_WRITE_CTRL,
			        IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_MASK);
	clock_gettime(CLOCK_MONOTONIC, &(end_time));

#ifdef BAS_DEBUG
#ifdef IFC_32BIT_SUPPORT
	printf("printing after BAS write...\n");
	printf("DEBUG: addr:0x%lx phy: 0x%x\n", IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off,
	      ifc_qdma_read32(qdev, IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx start: 0x%x\n", IFC_MCDMA_BAS_WRITE_ADDR << shift_off,
		ifc_qdma_read32(qdev, IFC_MCDMA_BAS_WRITE_ADDR << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx burst: 0x%x\n", IFC_MCDMA_BAS_WRITE_COUNT << shift_off,
		ifc_qdma_read32(qdev, IFC_MCDMA_BAS_WRITE_COUNT << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx ed_addr: 0x%x\n", IFC_MCDMA_BAS_WRITE_CTRL << shift_off,
		ifc_qdma_read32(qdev, IFC_MCDMA_BAS_WRITE_CTRL << shift_off, global_flags->rbar));
#else
	printf("printing after BAS write...\n");
	printf("DEBUG: addr:0x%lx phy: 0x%lx\n", IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off,
	      ifc_qdma_read64(qdev, IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx start: 0x%lx\n", IFC_MCDMA_BAS_WRITE_ADDR << shift_off,
		ifc_qdma_read64(qdev, IFC_MCDMA_BAS_WRITE_ADDR << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx burst: 0x%lx\n", IFC_MCDMA_BAS_WRITE_COUNT << shift_off,
		ifc_qdma_read64(qdev, IFC_MCDMA_BAS_WRITE_COUNT << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx ed_addr: 0x%lx\n", IFC_MCDMA_BAS_WRITE_CTRL << shift_off,
		ifc_qdma_read64(qdev, IFC_MCDMA_BAS_WRITE_CTRL << shift_off, global_flags->rbar));
#endif
#endif

#ifdef BAS_DEBUG
#ifdef IFC_32BIT_SUPPORT
	printf("TEST: ed_addr: 0x%x\n", ifc_qdma_read32(qdev, ed_addr, global_flags->rbar));
#else
	printf("TEST: ed_addr: 0x%lx\n", ifc_qdma_read64(qdev, ed_addr, global_flags->rbar));
#endif
	qdma_hex_dump(req->buf, 256);
	printf("printing after BAS write...\n");
	qdma_hex_dump(req->buf, 256);
#endif
	ed_addr = IFC_MCDMA_BAS_WRITE_ERR << shift_off;
#ifdef IFC_32BIT_SUPPORT
	val = ifc_qdma_read32(qdev, ed_addr, global_flags->rbar);
#else
	val = ifc_qdma_read64(qdev, ed_addr, global_flags->rbar);
#endif
	if (val == 0)
		printf("Tx:\t\t\tPass\n");
	else
#ifdef IFC_32BIT_SUPPORT
		printf("TX:\t\t\tFailed error:%llu\n", val);
#else
		printf("TX:\t\t\tFailed error:%lu\n", val);
#endif

	val = pattern_checker(req->buf, global_flags->request_size, 0, 0x00);
	if (val == PERFQ_SUCCESS)
		printf("Data validation:\tPass\n");
	else
		printf("Data validation:\tFailed\n");
	time_diff(&end_time, &start_time, &diff);
	printf("Completion time:\t%lus %luns\n", diff.tv_sec, diff.tv_nsec);

	/* BAS Read configure */
	printf("Configuration BAS Read region...\n");
	/* Configure physical address */
	ed_addr = IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off;
#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, req->phy_addr, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, req->phy_addr, global_flags->wbar);
#endif

#ifdef BAS_DEBUG
	printf("printing before Issuing BAS READ...\n");
	qdma_hex_dump(req->buf, 256);
#endif

	/* Configure start address */
	val = addr  << shift_off;
	ed_addr = IFC_MCDMA_BAS_READ_ADDR << shift_off;
#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, val, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, val, global_flags->wbar);
#endif

	/* Configure burst count */
	val = (global_flags->request_size / IFC_MCDMA_BAS_BURST_BYTES);
	ed_addr = IFC_MCDMA_BAS_READ_COUNT << shift_off;
#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, val, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, val, global_flags->wbar);
#endif

	/* Configure read control register */
	val = 0;
#if BAS_ED == 0
	val = IFC_MCDMA_BAS_X16_BURST_LENGTH;
#elif BAS_ED == 1
	val = IFC_MCDMA_BAS_X8_BURST_LENGTH;
#elif BAS_ED == 2
	val = IFC_MCDMA_BAS_X4_BURST_LENGTH;
#endif
	val = ((val & ~IFC_MCDMA_BAS_READ_CTRL_ENABLE_MASK) | IFC_MCDMA_BAS_READ_CTRL_ENABLE_MASK);
	ed_addr = IFC_MCDMA_BAS_READ_CTRL << shift_off;
#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, val, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, val, global_flags->wbar);
#endif
	clock_gettime(CLOCK_MONOTONIC, &start_time);

#ifdef BAS_DEBUG
#ifdef IFC_32BIT_SUPPORT
	sleep(1);
	printf("printing after Issuing BAS READ...\n");
	printf("DEBUG: addr:0x%lx phy: 0x%x\n", IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off,
	      ifc_qdma_read32(qdev, IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx start: 0x%x\n", IFC_MCDMA_BAS_READ_ADDR << shift_off,
		ifc_qdma_read32(qdev, IFC_MCDMA_BAS_READ_ADDR << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx burst: 0x%x\n", IFC_MCDMA_BAS_READ_COUNT << shift_off,
		ifc_qdma_read32(qdev, IFC_MCDMA_BAS_READ_COUNT << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx ed_addr: 0x%x\n", IFC_MCDMA_BAS_READ_CTRL << shift_off,
		ifc_qdma_read32(qdev, IFC_MCDMA_BAS_READ_CTRL << shift_off, global_flags->rbar));
	qdma_hex_dump(req->buf, 256);
#else
	sleep(1);
	printf("printing after Issuing BAS READ...\n");
	printf("DEBUG: addr:0x%lx phy: 0x%lx\n", IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off,
	      ifc_qdma_read64(qdev, IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx start: 0x%lx\n", IFC_MCDMA_BAS_READ_ADDR << shift_off,
		ifc_qdma_read64(qdev, IFC_MCDMA_BAS_READ_ADDR << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx burst: 0x%lx\n", IFC_MCDMA_BAS_READ_COUNT << shift_off,
		ifc_qdma_read64(qdev, IFC_MCDMA_BAS_READ_COUNT << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx ed_addr: 0x%lx\n", IFC_MCDMA_BAS_READ_CTRL << shift_off,
		ifc_qdma_read64(qdev, IFC_MCDMA_BAS_READ_CTRL << shift_off, global_flags->rbar));
	qdma_hex_dump(req->buf, 256);
#endif
#endif
	bas_wait_ctrl_deassert(qdev, IFC_MCDMA_BAS_READ_CTRL,
				IFC_MCDMA_BAS_READ_CTRL_ENABLE_MASK);
	clock_gettime(CLOCK_MONOTONIC, &(end_time));

	ed_addr = IFC_MCDMA_BAS_READ_ERR << shift_off;
#ifdef IFC_32BIT_SUPPORT
	val = ifc_qdma_read32(qdev, ed_addr, global_flags->rbar);
#else
	val = ifc_qdma_read64(qdev, ed_addr, global_flags->rbar);
#endif
	if (val == 0)
		printf("RX:\t\t\tPass\n");
	else
#ifdef IFC_32BIT_SUPPORT
		printf("RX:\t\t\tFailed error:%llu\n", val);
#else
		printf("RX:\t\t\tFailed error:%lu\n", val);
#endif

	time_diff(&end_time, &start_time, &diff);
	printf("Completion time:\t%lus %luns\n", diff.tv_sec, diff.tv_nsec);

	ifc_request_free(req);
	return val;
}

static int bas_configure_perf(struct ifc_qdma_device *qdev,
		struct struct_flags *flags __attribute__((unused)))
{
	struct ifc_qdma_request *req = NULL;
	struct timespec start_time, diff;
	uint64_t addr = BAS_ADDRESS;
	uint32_t payload = 1 << 20; // 1MB
	uint64_t val = 0;
	int shift_off;
	uint64_t ed_addr;
	uint64_t tx_pkts = 0;
	uint64_t rx_pkts = 0;
	struct timespec stop_time;
	struct struct_time t_tx;
	struct struct_time t_rx;
	long timediff_sec;

	printf("Configuration BAS Write region...\n");

#if BAS_ED == 0
	shift_off = IFC_MCDMA_BAS_X16_SHIFT_WIDTH;
#elif BAS_ED == 1
	shift_off = IFC_MCDMA_BAS_X8_SHIFT_WIDTH;
#elif BAS_ED == 2
	shift_off = IFC_MCDMA_BAS_X4_SHIFT_WIDTH;
#endif

	/* BAS Write configure */
	/* Configure physical address */
	req = ifc_request_malloc(payload);
	if (!req) {
		printf("ERR: Failed to allocate memory\n");
		return -1;
	}
	ed_addr = IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off;
#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, req->phy_addr, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, req->phy_addr, global_flags->wbar);
#endif
	memset (req->buf, 0xaa, 256);

	/* Configure start address */
	val = addr  << shift_off;
	ed_addr = IFC_MCDMA_BAS_WRITE_ADDR << shift_off;
#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, val, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, val, global_flags->wbar);
#endif

	/* Configure burst count */
	val = (global_flags->request_size / IFC_MCDMA_BAS_BURST_BYTES);
	val |= IFC_MCDMA_BAS_NON_STOP_TRANSFER_ENABLE_MASK;
	ed_addr = IFC_MCDMA_BAS_WRITE_COUNT << shift_off;
#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, val, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, val, global_flags->wbar);
#endif

#ifdef BAS_DEBUG
	sleep(1);
	printf("printing after memset ...\n");
	qdma_hex_dump(req->buf, 256);
#endif

	/* Configure write control register */
#if BAS_ED == 0
	val = IFC_MCDMA_BAS_X16_BURST_LENGTH;
#elif BAS_ED == 1
	val = IFC_MCDMA_BAS_X8_BURST_LENGTH;
#elif BAS_ED == 2
	val = IFC_MCDMA_BAS_X4_BURST_LENGTH;
#endif
	val = ((val & ~IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_MASK) | IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_MASK);
	ed_addr = IFC_MCDMA_BAS_WRITE_CTRL << shift_off;
#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, val, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, val, global_flags->wbar);
#endif
#ifdef BAS_DEBUG
#ifdef IFC_32BIT_SUPPORT
	printf("printing after BAS write...\n");
	printf("DEBUG: addr:0x%lx phy: 0x%x\n", IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off,
	      ifc_qdma_read32(qdev, IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx start: 0x%x\n", IFC_MCDMA_BAS_WRITE_ADDR << shift_off,
		ifc_qdma_read32(qdev, IFC_MCDMA_BAS_WRITE_ADDR << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx burst: 0x%x\n", IFC_MCDMA_BAS_WRITE_COUNT << shift_off,
		ifc_qdma_read32(qdev, IFC_MCDMA_BAS_WRITE_COUNT << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx ed_addr: 0x%x\n", IFC_MCDMA_BAS_WRITE_CTRL << shift_off,
		ifc_qdma_read32(qdev, IFC_MCDMA_BAS_WRITE_CTRL << shift_off, global_flags->rbar));
#else
	printf("printing after BAS write...\n");
	printf("DEBUG: addr:0x%lx phy: 0x%lx\n", IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off,
	      ifc_qdma_read64(qdev, IFC_MCDMA_BAS_WRITE_MAP_TABLE << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx start: 0x%lx\n", IFC_MCDMA_BAS_WRITE_ADDR << shift_off,
		ifc_qdma_read64(qdev, IFC_MCDMA_BAS_WRITE_ADDR << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx burst: 0x%lx\n", IFC_MCDMA_BAS_WRITE_COUNT << shift_off,
		ifc_qdma_read64(qdev, IFC_MCDMA_BAS_WRITE_COUNT << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx ed_addr: 0x%lx\n", IFC_MCDMA_BAS_WRITE_CTRL << shift_off,
		ifc_qdma_read64(qdev, IFC_MCDMA_BAS_WRITE_CTRL << shift_off, global_flags->rbar));
#endif
#endif

#ifdef BAS_DEBUG
#ifdef IFC_32BIT_SUPPORT
	printf("TEST: ed_addr: 0x%x\n", ifc_qdma_read32(qdev, ed_addr, global_flags->rbar));
#else
	printf("TEST: ed_addr: 0x%lx\n", ifc_qdma_read64(qdev, ed_addr, global_flags->rbar));
#endif
	printf("printing after BAS write...\n");
	qdma_hex_dump(req->buf, 256);
#endif

	/* BAS Read configure */
	printf("Configuration BAS Read region...\n");
	/* Configure physical address */
	ed_addr = IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off;
#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, req->phy_addr, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, req->phy_addr, global_flags->wbar);
#endif

#ifdef BAS_DEBUG
	printf("printing before Issuing BAS READ...\n");
	qdma_hex_dump(req->buf, 256);
#endif

	/* Configure start address */
	val = addr  << shift_off;
	ed_addr = IFC_MCDMA_BAS_READ_ADDR << shift_off;
#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, val, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, val, global_flags->wbar);
#endif

	/* Configure burst count */
	val = (global_flags->request_size / IFC_MCDMA_BAS_BURST_BYTES);
	val |= IFC_MCDMA_BAS_NON_STOP_TRANSFER_ENABLE_MASK;
	ed_addr = IFC_MCDMA_BAS_READ_COUNT << shift_off;
#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, val, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, val, global_flags->wbar);
#endif

	/* Configure read control register */
	val = 0;
#if BAS_ED == 0
	val = IFC_MCDMA_BAS_X16_BURST_LENGTH;
#elif BAS_ED == 1
	val = IFC_MCDMA_BAS_X8_BURST_LENGTH;
#elif BAS_ED == 2
	val = IFC_MCDMA_BAS_X4_BURST_LENGTH;
#endif
	val = ((val & ~IFC_MCDMA_BAS_READ_CTRL_ENABLE_MASK) | IFC_MCDMA_BAS_READ_CTRL_ENABLE_MASK);
	ed_addr = IFC_MCDMA_BAS_READ_CTRL << shift_off;

#ifdef IFC_32BIT_SUPPORT
	ifc_qdma_write32(qdev, ed_addr, val, global_flags->wbar);
#else
	ifc_qdma_write64(qdev, ed_addr, val, global_flags->wbar);
#endif

#ifdef BAS_DEBUG
#ifdef IFC_32BIT_SUPPORT
	sleep(1);
	printf("printing after Issuing BAS READ...\n");
	printf("DEBUG: addr:0x%lx phy: 0x%x\n", IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off,
	      ifc_qdma_read32(qdev, IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx start: 0x%x\n", IFC_MCDMA_BAS_READ_ADDR << shift_off,
		ifc_qdma_read32(qdev, IFC_MCDMA_BAS_READ_ADDR << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx burst: 0x%x\n", IFC_MCDMA_BAS_READ_COUNT << shift_off,
		ifc_qdma_read32(qdev, IFC_MCDMA_BAS_READ_COUNT << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx ed_addr: 0x%x\n", IFC_MCDMA_BAS_READ_CTRL << shift_off,
		ifc_qdma_read32(qdev, IFC_MCDMA_BAS_READ_CTRL << shift_off, global_flags->rbar));
	qdma_hex_dump(req->buf, 256);
#else
	sleep(1);
	printf("printing after Issuing BAS READ...\n");
	printf("DEBUG: addr:0x%lx phy: 0x%lx\n", IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off,
	      ifc_qdma_read64(qdev, IFC_MCDMA_BAS_READ_MAP_TABLE << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx start: 0x%lx\n", IFC_MCDMA_BAS_READ_ADDR << shift_off,
		ifc_qdma_read64(qdev, IFC_MCDMA_BAS_READ_ADDR << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx burst: 0x%lx\n", IFC_MCDMA_BAS_READ_COUNT << shift_off,
		ifc_qdma_read64(qdev, IFC_MCDMA_BAS_READ_COUNT << shift_off, global_flags->rbar));
	printf("DEBUG: addr:0x%lx ed_addr: 0x%lx\n", IFC_MCDMA_BAS_READ_CTRL << shift_off,
		ifc_qdma_read64(qdev, IFC_MCDMA_BAS_READ_CTRL << shift_off, global_flags->rbar));
	qdma_hex_dump(req->buf, 256);
#endif
#endif

	clock_gettime(CLOCK_MONOTONIC, &start_time);
	while (true) {
		clock_gettime(CLOCK_MONOTONIC, &stop_time);
		timediff_sec = difftime(stop_time.tv_sec,
					start_time.tv_sec);
		if (timediff_sec >= 10 &&
		    (stop_time.tv_nsec >= start_time.tv_nsec)) {

			disable_tx_non_stop_transfer_mode(qdev);
			disable_rx_non_stop_transfer_mode(qdev);

			/* Compute Bw */
			compute_bw(&t_tx, &start_time, &stop_time, tx_pkts, IFC_MCDMA_BAS_BURST_BYTES);
			compute_bw(&t_rx, &start_time, &stop_time, rx_pkts, IFC_MCDMA_BAS_BURST_BYTES);
			break;
		} else {
			usleep(1000);
			ed_addr = IFC_MCDMA_BAS_WRITE_COUNT << shift_off;
#ifdef IFC_32BIT_SUPPORT
			val = ifc_qdma_read32(qdev, ed_addr, global_flags->rbar);
#else
			val = ifc_qdma_read64(qdev, ed_addr, global_flags->rbar);
#endif
			val &= IFC_MCDMA_BAS_TRANSFER_COUNT_MASK;
			tx_pkts += val;

			ed_addr = IFC_MCDMA_BAS_READ_COUNT << shift_off;
			val = ifc_qdma_read64(qdev, ed_addr, global_flags->rbar);
			val &= IFC_MCDMA_BAS_TRANSFER_COUNT_MASK;
			rx_pkts += val;
		}
	}

	ed_addr = IFC_MCDMA_BAS_WRITE_ERR << shift_off;
#ifdef IFC_32BIT_SUPPORT
	val = ifc_qdma_read32(qdev, ed_addr, global_flags->rbar);
#else
	val = ifc_qdma_read64(qdev, ed_addr, global_flags->rbar);
#endif
	if (val == 0)
		printf("Tx:\t\t\tPass\n");
	else
#ifdef IFC_32BIT_SUPPORT
		printf("TX:\t\t\tFailed error:%llu\n", val);
#else
		printf("TX:\t\t\tFailed error:%lu\n", val);
#endif

	ed_addr = IFC_MCDMA_BAS_READ_ERR << shift_off;
#ifdef IFC_32BIT_SUPPORT
	val = ifc_qdma_read32(qdev, ed_addr, global_flags->rbar);
#else
	val = ifc_qdma_read64(qdev, ed_addr, global_flags->rbar);
#endif
	if (val == 0)
		printf("RX:\t\t\tPass\n");
	else
#ifdef IFC_32BIT_SUPPORT
		printf("RX:\t\t\tFailed error:%llu\n", val);
#else
		printf("RX:\t\t\tFailed error:%lu\n", val);
#endif

	time_diff(&stop_time, &start_time, &diff);
	printf("TX Bandwidth:\t\t%.2fGBPS\n",t_tx.ovrall_bw);
	printf("RX Bandwidth:\t\t%.2fGBPS\n",t_rx.ovrall_bw);
	printf("Total Bandwidth:\t%.2fGBPS\n",(t_rx.ovrall_bw + t_tx.ovrall_bw));
	printf("Completion time:\t%lus %luns\n", diff.tv_sec, diff.tv_nsec);

	ifc_request_free(req);
	return val;
}

int bas_test(struct ifc_qdma_device *qdev,
	     struct struct_flags *flags)
{
	int ret;
	if (flags->direction == IFC_QDMA_DIRECTION_TX)
		ret = bas_configure_tx(qdev, flags);
	else if (flags->direction == IFC_QDMA_DIRECTION_RX)
		ret = bas_configure_rx(qdev, flags);
	else  {
		if (flags->fbas_perf)
			ret = bas_configure_perf(qdev, flags);
		else
			ret = bas_configure_bi(qdev, flags);
	}
	return ret;
}
