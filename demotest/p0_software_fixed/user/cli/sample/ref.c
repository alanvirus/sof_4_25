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
#include <ifc_qdma_utils.h>
#include <ifc_reglib_osdep.h>
#include <ifc_libmqdma.h>

#if IFC_CONFIG_QDMA_NUM_DESC_PAGES_PER_QUEUE > 1
#define QDEPTH ((IFC_CONFIG_QDMA_NUM_DESC_PAGES_PER_QUEUE/2) * IFC_MCDMA_CONFIG_NUM_DESC_PER_PAGE)
#else
#define QDEPTH ((IFC_MCDMA_CONFIG_NUM_DESC_PER_PAGE/2) + 1)
#endif

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
                        printf("\n%8lx ", (uint64_t) base + i);
                printf("%02x ", base[i]);
        }
        printf ("\n");
}
#endif

int main(void)
{
	int dma_dir = IFC_QDMA_DIRECTION_TX;
	struct ifc_qdma_request **r;
	struct ifc_qdma_request *d;
	struct ifc_qdma_device *qdev;
	struct ifc_qdma_channel *qchnl;
	/* initialize with correct BDF value */
	const char *bdf = "0000:00:00.0";
	uint32_t block_size = 512;
	uint32_t qdma_qdepth = QDEPTH;
	int ch = IFC_QDMA_AVAIL_CHNL_ARG;
	void *raw_pkt[32];
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

	/* pre-fill all the descriptor queue */
	r = malloc(qdma_qdepth * sizeof(*r));
	if (r == NULL)
		goto out1;
	for (i = 0; i < qdma_qdepth ; i++) {
		r[i] = ifc_request_malloc(block_size);
		if (r[i] == NULL)
			goto out2;
		r[i]->len = block_size;
		/* TODO: copy src data, for tx */
		ret = ifc_qdma_request_start(qchnl, dma_dir, r[i]);
#ifdef DEBUG
		qdma_hex_dump(r[i]->buf, 48);
#endif
	}

	/* keep pushing same request */
	while (1) {
		nr = ifc_qdma_completion_poll(qchnl,
					      dma_dir,
					      raw_pkt,
					      ARRAY_SIZE(raw_pkt));
		for (i = 0; i < nr; i++) {
			d = (struct ifc_qdma_request *)raw_pkt[i];
			ret = ifc_qdma_request_start(qchnl, dma_dir, d);
		}
	}

out2:
	/* release your allocated channels */
	ifc_qdma_channel_put(qchnl, IFC_QDMA_DIRECTION_BOTH);
	for (i = 0; i < qdma_qdepth; i++)
		ifc_request_free(r[i]);
	free(r);
out1:
	ifc_qdma_device_put(qdev);
out:
	ifc_app_stop();
	return 0;
}
