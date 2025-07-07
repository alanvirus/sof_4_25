// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <ifc_qdma_utils.h>
#include <ifc_reglib_osdep.h>
#include <ifc_libmqdma.h>

#undef LOAD_DATA
#undef DEBUG

#if IFC_CONFIG_QDMA_NUM_DESC_PAGES_PER_QUEUE > 1
#define QDEPTH ((IFC_CONFIG_QDMA_NUM_DESC_PAGES_PER_QUEUE/2) * IFC_MCDMA_CONFIG_NUM_DESC_PER_PAGE)
#else
#define QDEPTH ((IFC_MCDMA_CONFIG_NUM_DESC_PER_PAGE/2) + 1)
#endif

enum perfq_status {
        PERFQ_CORE_ASSGN_FAILURE                                = -1,
        PERFQ_MALLOC_FAILURE                                    = -2,
        PERFQ_CMPLTN_NTFN_FAILURE                               = -3,
        PERFQ_DATA_VALDN_FAILURE                                = -4,
        PERFQ_FILE_VALDN_FAILURE                                = -5,
        PERFQ_DATA_PAYLD_FAILURE                                = -6,
        PERFQ_TASK_FAILURE                                      = -7,
        PERFQ_CH_INIT_FAILURE                                   = -8,
        PERFQ_SUCCESS                                           = 0,
        PERFQ_SOF                                               = 1,
        PERFQ_EOF                                               = 2,
        PERFQ_BOTH                                              = 3
};

#ifdef DEBUG
/*
 * In case if any anomalies observed at application, application
 * need to use this function, to dump memory in hex format
 */
static void qdma_hex_dump(unsigned char *base, int len)
{
        int i;

        for (i = 0; i < len; i++) {
                if ((i % 16) == 0)
                        fprintf(stderr, "\n%8lx ", (uint64_t) base + i);
                fprintf(stderr, "%02x ", base[i]);
        }
        fprintf (stderr, "\n");
}
#endif

#ifdef LOAD_DATA
#pragma GCC push_options
#pragma GCC optimize ("O0")
static uint32_t load_data(void *buf, size_t size, uint32_t pattern)
{
        unsigned int i;

        for (i = 0; i < (size / sizeof(uint32_t)); i++)
                *(((uint32_t *)buf) + i) = pattern++;

        return pattern;
}

static enum perfq_status pattern_checker(void *buf, size_t size, uint32_t expected_pattern)
{
        uint32_t actual_pattern;
        unsigned int i;
#ifdef DUMP_DATA
        char data_to_write[50];
        char file_name[50] = DUMP_FILE;
#endif
        for (i = 0; i < size / sizeof(uint32_t); i++) {
                actual_pattern = ((uint32_t *)buf)[i];
#ifdef DUMP_DATA
                memset(data_to_write, 0, 50);
                snprintf(data_to_write, sizeof(data_to_write), "0x%x,0x%x\n", actual_pattern, expected_pattern);
                append_to_file(file_name, data_to_write);
#endif
                if (actual_pattern != expected_pattern) {
#ifdef DUMP_DATA
                        memset(data_to_write, 0, 50);
                        snprintf(data_to_write, sizeof(data_to_write), "Pattern Validation Failed for Packet\n");
                        append_to_file(file_name, data_to_write);
#endif
                        return PERFQ_DATA_VALDN_FAILURE;
                }
                expected_pattern++;
        }
#ifdef DUMP_DATA
        memset(data_to_write, 0, 50);
        snprintf(data_to_write, sizeof(data_to_write), "Pattern Validation Successful for Packet\n");
        append_to_file(file_name, data_to_write);
#endif
        return PERFQ_SUCCESS;
}

#pragma GCC pop_options
#endif

int main(void)
{
	int dma_dir = IFC_QDMA_DIRECTION_TX;
	struct ifc_qdma_request **txd;
	struct ifc_qdma_request **rxd;
	struct ifc_qdma_request *d;
	struct ifc_qdma_device *qdev;
	struct ifc_qdma_channel *qchnl;
	/* initialize with correct BDF value */
	const char *bdf = "0000:01:00.0";
	uint32_t block_size = 64;
	uint32_t qdma_qdepth = QDEPTH;
	int ch = IFC_QDMA_AVAIL_CHNL_ARG;
	void *tx_raw_pkt[32];
	void *rx_raw_pkt[32];
	uint32_t i;
	uint32_t nr;
	int ret;
	int port;
	

	ifc_app_start(bdf, block_size);

	port = ifc_mcdma_port_by_name(bdf); 
        if (port < 0) {
		goto out;
	}

	/* get DMA device */
	ret = ifc_qdma_device_get(port, &qdev, 128, IFC_CONFIG_QDMA_COMPL_PROC);
	if (ret)
		goto out;

	/* get number of channel */
	ret = ifc_num_channels_get(qdev);
	if (ret <= 0) {
		printf("no channels found in the dma device\n");
		goto out1;
	}

	/*
 	 * allocate a channel.
 	 * In case, if we have multi-threaded application, start calling this
 	 * functionality from channel specific thread
 	 */
	ret = ifc_qdma_channel_get(qdev, &qchnl, ch, IFC_QDMA_DIRECTION_BOTH);
	if (ret < 0)
		goto out1;

	/* pre-fill TX the descriptor queue */
	txd = malloc(qdma_qdepth * sizeof(*txd));
	if (txd == NULL)
		goto out1;
	for (i = 0; i < qdma_qdepth ; i++) {
		txd[i] = ifc_request_malloc(block_size);
		if (txd[i] == NULL)
			goto out2;
		txd[i]->len = block_size;
#ifdef LOAD_DATA
		load_data(txd[i]->buf, block_size, 0x00);
#endif
		/* TODO: copy src data, for tx */
		ret = ifc_qdma_request_start(qchnl, dma_dir, txd[i]);
#ifdef DEBUG
		qdma_hex_dump(txd[i]->buf, 64);
#endif
	}

	/* pre-fill RX the descriptor queue */
	dma_dir = IFC_QDMA_DIRECTION_RX;
	rxd = malloc(qdma_qdepth * sizeof(*rxd));
	if (rxd == NULL)
		goto out1;
	for (i = 0; i < qdma_qdepth ; i++) {
		rxd[i] = ifc_request_malloc(block_size);
		if (rxd[i] == NULL)
			goto out2;
		rxd[i]->len = block_size;
		/* TODO: copy src data, for tx */
		ret = ifc_qdma_request_start(qchnl, dma_dir, rxd[i]);
#ifdef DEBUG
		qdma_hex_dump(rxd[i]->buf, 64);
#endif
	}

	/* keep pushing same request */
	while (1) {
		dma_dir = IFC_QDMA_DIRECTION_TX;
		nr = ifc_qdma_completion_poll(qchnl,
					      dma_dir,
					      tx_raw_pkt,
					      ARRAY_SIZE(tx_raw_pkt));
		for (i = 0; i < nr; i++) {
			d = (struct ifc_qdma_request *)tx_raw_pkt[i];
#ifdef LOAD_DATA
			load_data(d->buf, block_size, 0x00);
#endif
			ret = ifc_qdma_request_start(qchnl, dma_dir, d);
		}
		if (nr) {
			printf("TEST: submitted %u TX descriptors\n", nr);
		}
		dma_dir = IFC_QDMA_DIRECTION_RX;
		nr = ifc_qdma_completion_poll(qchnl,
					      dma_dir,
					      rx_raw_pkt,
					      ARRAY_SIZE(rx_raw_pkt));
		for (i = 0; i < nr; i++) {
			d = (struct ifc_qdma_request *)rx_raw_pkt[i];
#ifdef DEBUG
			qdma_hex_dump(d->buf, 64);
#endif
#ifdef LOAD_DATA
			ret = pattern_checker(d->buf, block_size, 0x00);
			if (ret)
				printf("TEST: Data valiation Failed\n");
#endif
			ret = ifc_qdma_request_start(qchnl, dma_dir, d);
		}
		if (nr) {
			printf("TEST: subitted %u RX descriptors\n", nr);
		}
		usleep(1000);
	}

out2:
	/* release your allocated channels */
	ifc_qdma_channel_put(qchnl, IFC_QDMA_DIRECTION_BOTH);
	for (i = 0; i < qdma_qdepth; i++)
		ifc_request_free(txd[i]);
	free(txd);
out1:
	ifc_qdma_device_put(qdev);
out:
	ifc_app_stop();
	return 0;
}
