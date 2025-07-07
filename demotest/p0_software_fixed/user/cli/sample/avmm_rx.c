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
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <ifc_qdma_utils.h>
#include <ifc_reglib_osdep.h>
#include <ifc_libmqdma.h>
#include <signal.h>

static char *in_file = "";
static char *out_file = "";
const char *bdf = "0000:00:00.0";
static int rx_buf_len = 1024;
static int app_exit = 0;
static int verbose = 0;
static long count = 64;
static long current_submitted = 0, current_done = 0;

/* Block size */
#define MAXBUFLEN  rx_buf_len

/* Enable to validate data for default data*/
#undef LOAD_DATA
/* Enable to dump data */
#undef DEBUG

#if IFC_CONFIG_QDMA_NUM_DESC_PAGES_PER_QUEUE > 1
#define DEFAULT_QDEPTH ((IFC_CONFIG_QDMA_NUM_DESC_PAGES_PER_QUEUE/2) * IFC_MCDMA_CONFIG_NUM_DESC_PER_PAGE)
#else
#define DEFAULT_QDEPTH ((IFC_MCDMA_CONFIG_NUM_DESC_PER_PAGE/2) + 1)
#endif
static int queue_deep = DEFAULT_QDEPTH;

#define QDEPTH queue_deep

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

int get_first_available_port(void);

void print_usage(char *app_name)
{
	printf("usage:\n");
	printf("   %s bdf=0000:22:00.0 out=out.bin blksize=1024 qdeep=128 count=64\n", app_name);
	printf("   empty args is allowed, blksize=1024 by default\n");
	printf("   bdf can be default, it will scan the first one\n");
}

static void take_args(int argc, char **argv)
{
	int idx;
	char *current_arg;
	for(idx = 0; idx < argc; idx++){
		current_arg = argv[idx];
		if(strcmp(current_arg, "help") == 0 || 
		   strcmp(current_arg, "-h") == 0 ||
		   strcmp(current_arg, "--h") == 0){
           print_usage(argv[0]);
		   exit(0);
		}
		if(strstr(current_arg, "bdf=") == current_arg ||
		   strstr(current_arg, "pci=") == current_arg ||
		   strstr(current_arg, "device=") == current_arg){
		   bdf = current_arg + strlen("bdf=");
		   printf("%s:take arg bdf: %s\n", argv[0], bdf);
		} else if(strstr(current_arg, "in=") == current_arg){
		   in_file = current_arg + strlen("in=");
		   printf("%s:take arg in_file: %s\n", argv[0], in_file);
		} else if(strstr(current_arg, "out=") == current_arg){
		   out_file = current_arg + strlen("out=");
		   printf("%s:take arg out_file: %s\n", argv[0], out_file);
		} else if(strstr(current_arg, "blksize=") == current_arg){
		   current_arg = current_arg + strlen("blksize=");
		   rx_buf_len = strtoul(current_arg, NULL, 0);
		   if(rx_buf_len < 1)
		      rx_buf_len = 4096;
		   printf("%s:take arg blksize: %d\n", argv[0],rx_buf_len);
		} else if(strstr(current_arg, "qdeep=") == current_arg){
		   current_arg = current_arg + strlen("qdeep=");
		   queue_deep = strtoul(current_arg, NULL, 0);
		   if(queue_deep < 1)
		      queue_deep = DEFAULT_QDEPTH;
		   printf("%s:take arg qdeep: %d\n", argv[0], queue_deep);
		} else if(strstr(current_arg, "count=") == current_arg){
		   current_arg = current_arg + strlen("count=");
		   count = strtoul(current_arg, NULL, 0);
		   if(count < 1)
		      count = 64;
		   printf("%s:take arg count: %ld\n", argv[0], count);
		} else if(strcmp(current_arg, "-v") == 0 || 
		    strcmp(current_arg, "--verbose") == 0){
			verbose = 1;
		}
	}
}

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
static enum perfq_status pattern_checker(void *buf, size_t size,
					 uint32_t expected_pattern)
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
                snprintf(data_to_write, sizeof(data_to_write), "0x%x,0x%x\n",
			 actual_pattern, expected_pattern);
                append_to_file(file_name, data_to_write);
#endif
                if (actual_pattern != expected_pattern) {
#ifdef DUMP_DATA
                        memset(data_to_write, 0, 50);
                        snprintf(data_to_write, sizeof(data_to_write),
				 "Pattern Validation Failed for Packet\n");
                        append_to_file(file_name, data_to_write);
#endif
                        return PERFQ_DATA_VALDN_FAILURE;
                }
                expected_pattern++;
        }
#ifdef DUMP_DATA
        memset(data_to_write, 0, 50);
        snprintf(data_to_write, sizeof(data_to_write),
		 "Pattern Validation Successful for Packet\n");
        append_to_file(file_name, data_to_write);
#endif
        return PERFQ_SUCCESS;
}
#pragma GCC pop_options
#endif

pthread_t delay_thread;
static void *delay_thread_func(void *arg)
{
	int idx;
	(void) arg;
	for(idx = 0; idx < 3; idx++)
	   usleep(1000000);
	exit(0);
}

static void signal_handler(int sig_num)
{
	app_exit = 1;
	pthread_create(&delay_thread, NULL, delay_thread_func, NULL);
}

int main(int argc, char **argv)
{
	int out_file_fd = -1;
	int dma_dir = IFC_QDMA_DIRECTION_RX;
	struct ifc_qdma_request **rxd;
	struct ifc_qdma_request *d;
	struct ifc_qdma_device *qdev;
	struct ifc_qdma_channel *qchnl;
	/* initialize with correct BDF value */
	uint32_t block_size = MAXBUFLEN;
	uint32_t qdma_qdepth = QDEPTH;
	int ch = IFC_QDMA_AVAIL_CHNL_ARG;
	void *rx_raw_pkt[32];
	uint32_t i;
	uint32_t nr;
	int ret;
	int port;
	long total_bytes;
	unsigned char *out_buffer = NULL, *out_ptr = NULL; 

	signal(SIGINT, signal_handler);
    take_args(argc, argv);
	block_size = MAXBUFLEN;
	qdma_qdepth = QDEPTH;
	total_bytes = (block_size * qdma_qdepth);

    if(out_file[0]){
	   out_file_fd = open(out_file, O_RDWR | O_CREAT | O_TRUNC, 0777);
       if(out_file_fd < 0){
           perror(out_file);
           printf("error storing file: %s\n", out_file);
       }
	}

	if(out_file_fd > 0){
	   out_buffer = (unsigned char *) malloc(total_bytes + 4096);
	   if(out_buffer == NULL){
		   printf("can not alloc memory for rx, rx count too large\n");
		   return -1;
	   }
       out_ptr = out_buffer;
	}

	ifc_app_start(bdf, block_size);

    if(strstr(bdf, "0000:00:00.0") || bdf[0] == 0)
	   port = get_first_available_port();
	else
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
	ret = ifc_qdma_channel_get(qdev, &qchnl, ch, IFC_QDMA_DIRECTION_RX);
	if (ret < 0)
		goto out1;

	/* pre-fill RX the descriptor queue */
	dma_dir = IFC_QDMA_DIRECTION_RX;
	rxd = malloc(qdma_qdepth * sizeof(*rxd));
	if (rxd == NULL){
		printf("%s: can not alloc memory\n", argv[0]);
		ifc_qdma_channel_put(qchnl, IFC_QDMA_DIRECTION_RX);
		goto out1;
	}

    for (i = 0; i < qdma_qdepth; i++)
	    rxd[i] == NULL;
	for (i = 0; i < qdma_qdepth; i++) {
		rxd[i] = ifc_request_malloc(block_size);
		if (rxd[i] == NULL)
			goto out2;
	}
	for (i = 0; i < qdma_qdepth && current_submitted < count; i++) {
		rxd[i]->len = block_size;
		/* TODO: copy src data, for rx */
		current_submitted++;
		ret = ifc_qdma_request_start(qchnl, dma_dir, rxd[i]);
#ifdef DEBUG
		qdma_hex_dump(rxd[i]->buf, 64);
#endif
	}

	/* keep pushing same request */
	while (app_exit == 0 && current_done < count) {
		dma_dir = IFC_QDMA_DIRECTION_RX;
		nr = ifc_qdma_completion_poll(qchnl,
					      dma_dir,
					      rx_raw_pkt,
					      ARRAY_SIZE(rx_raw_pkt));
		current_done += nr > 0 ? nr : 0;
		if(nr > 0 && verbose)
		   printf("rx: completed %d block\n", nr);
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
            if(out_file_fd > 0 && out_ptr){
			   memcpy(out_ptr, d->buf, block_size);
			   out_ptr += block_size;
			}
			if(app_exit == 0 && current_submitted < count){
			   current_submitted++;
			   ret = ifc_qdma_request_start(qchnl, dma_dir, d);
			}
		}
		if (nr && 0) {
			printf("TEST: subitted %u RX descriptors\n", nr);
		}
		// usleep(1000);
	}

out2:
	/* release your allocated channels */
	ifc_qdma_channel_put(qchnl, IFC_QDMA_DIRECTION_RX);
	for (i = 0; i < qdma_qdepth; i++)
		if(rxd[i])ifc_request_free(rxd[i]);
	free(rxd);
out1:
	ifc_qdma_device_put(qdev);

out:
    if(out_buffer && out_file_fd > 0)
       write(out_file_fd, out_buffer, out_ptr - out_buffer);

    if(out_buffer)
	   free(out_buffer);
    if(out_file_fd > 0)
       close(out_file_fd);
	ifc_app_stop();

	printf("avmm rx: done\n");

	return 0;
}
