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
 #include <pthread.h>
#include <ifc_qdma_utils.h>
#include <ifc_reglib_osdep.h>
#include <ifc_libmqdma.h>
#include <signal.h>
#include <fcntl.h>         

static char *in_file = "";
static char *out_file = "";
const char *bdf = "0000:00:00.0";
static int tx_buf_len = 1024;
static int app_exit = 0;
static int verbose = 0;
static long count = 64;
static long current_submitted = 0, current_done = 0;

/* Block size */
#define MAXBUFLEN  tx_buf_len

/* Enable to load pattern */
#define LOAD_DATA
/* Disable to load data from input file */
#define DEFUALT_DATA
/* Enable to dump data */
#undef DEBUG

#if IFC_CONFIG_QDMA_NUM_DESC_PAGES_PER_QUEUE > 1
#define DEFAULT_QDEPTH ((IFC_CONFIG_QDMA_NUM_DESC_PAGES_PER_QUEUE/2) * IFC_MCDMA_CONFIG_NUM_DESC_PER_PAGE)
#else
#define DEFAULT_QDEPTH ((IFC_MCDMA_CONFIG_NUM_DESC_PER_PAGE/2) + 1)
#endif
static int queue_deep = DEFAULT_QDEPTH;

#define QDEPTH queue_deep

int get_first_available_port(void);

void print_usage(char *app_name)
{
	printf("usage:\n");
	printf("   %s bdf=0000:22:00.0 in=in.bin blksize=1024 qdeep=128 count=64\n", app_name);
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
		   printf("%s:take arg out_file: %s\n", argv[0],out_file);
		} else if(strstr(current_arg, "blksize=") == current_arg){
		   current_arg = current_arg + strlen("blksize=");
		   tx_buf_len = strtoul(current_arg, NULL, 0);
		   if(tx_buf_len < 1)
		      tx_buf_len = 4096;
		   printf("%s:take arg blksize: %d\n", argv[0], tx_buf_len);
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
#ifdef DEFUALT_DATA
#pragma GCC push_options
#pragma GCC optimize ("O0")
static uint32_t load_data(void *buf, size_t size, uint32_t pattern)
{
        unsigned int i;

        for (i = 0; i < (size / sizeof(uint32_t)); i++)
                *(((uint32_t *)buf) + i) = pattern++;

        return pattern;
}
#pragma GCC pop_options
#endif

static int load_data_frm_file(int in_file_fd, void *buf)
{
	/* Read data from file */
	int count = read(in_file_fd, buf, MAXBUFLEN);
	if (count < 0) {
		printf("Error reading file\n");
		return -1;
	}
	return 0;
}

#endif

static int process_default_data(struct ifc_qdma_channel *qchnl,
				struct ifc_qdma_request **txd,
				uint32_t qdma_qdepth)
{
	struct ifc_qdma_request *d = NULL;
	void *tx_raw_pkt[64];
	uint32_t i = 0;
	uint32_t nr = 0;
	int ret = 0;

	for (i = 0; i < qdma_qdepth; i++)
	    txd[i] = NULL;
	for (i = 0; i < qdma_qdepth; i++) {
		txd[i] = ifc_request_malloc(MAXBUFLEN);
		if (txd[i] == NULL)
			goto out;
	}

	/* pre-filling */
	for (i = 0; i < qdma_qdepth && current_submitted < count ; i++) {
		txd[i]->len = MAXBUFLEN;
#ifdef LOAD_DATA
#ifdef DEFUALT_DATA
		load_data(txd[i]->buf, MAXBUFLEN, 0x00);
#endif
#endif
		/* TODO: copy src data, for tx */
		current_submitted++;
		//txd[i]->flags |= (IFC_QDMA_SOF_MASK | IFC_QDMA_EOF_MASK); 
		ret = ifc_qdma_request_start(qchnl, IFC_QDMA_DIRECTION_TX, txd[i]);
		if (ret < 0)
			goto out;
#ifdef DEBUG
		qdma_hex_dump(txd[i]->buf, MAXBUFLEN);
#endif
	}
	printf("Pre-filling done for %ld desc\n", current_submitted);

	/* keep pushing same request */
	while (app_exit == 0 &&  current_done < count) {
		nr = ifc_qdma_completion_poll(qchnl,
					      IFC_QDMA_DIRECTION_TX,
					      tx_raw_pkt,
					      ARRAY_SIZE(tx_raw_pkt));
		if(nr > 0 && verbose)
		   printf("tx: completed %d block\n", nr);
		current_done += nr > 0 ? nr : 0;
		for (i = 0; i < nr; i++) {
			d = (struct ifc_qdma_request *)tx_raw_pkt[i];
#ifdef LOAD_DATA
			load_data(d->buf, MAXBUFLEN, 0x00);
#endif
            if(app_exit == 0 && current_submitted < count){
			   ret = ifc_qdma_request_start(qchnl, IFC_QDMA_DIRECTION_TX, d);
			   if (ret < 0)
				goto out;
			   current_submitted++;
			} 
#ifdef DEBUG
			qdma_hex_dump(txd[i]->buf, MAXBUFLEN);
#endif
		}
	}
out:
	for (i = 0; i < qdma_qdepth; i++)
		if(txd[i])ifc_request_free(txd[i]);
	return 0;
}

static int process_file_data(struct ifc_qdma_channel *qchnl,
			     struct ifc_qdma_request **txd, uint32_t qdma_qdepth)
{
	struct ifc_qdma_request *d = NULL;
	void *tx_raw_pkt[64];
	uint32_t i = 0;
	uint32_t nr = 0;
	int ret = 0;
	const char *file_name = in_file;
	long sleep_byte_record = 0;
	int in_file_fd = -1;

	/* Read from file */
	in_file_fd = open(file_name, O_RDONLY, 0);
	if (in_file_fd < 0) {
		printf("ERR: Cannot open binary file with name: %s\n",file_name);
		return -1;
	}

	for (i = 0; i < qdma_qdepth; i++)
	    txd[i] = NULL;
	for (i = 0; i < qdma_qdepth; i++) {
		txd[i] = ifc_request_malloc(MAXBUFLEN);
		if (txd[i] == NULL)
			goto out;
	}

	/* pre-filling */
	for (i = 0; i < qdma_qdepth && current_submitted < count; i++) {
		txd[i]->len = MAXBUFLEN;
		ret = load_data_frm_file(in_file_fd, txd[i]->buf);
		if (ret < 0) {
			printf("load file < 0\n");
			break;
		}
		/* TODO: copy src data, for tx */
		//txd[i]->flags |= (IFC_QDMA_SOF_MASK | IFC_QDMA_EOF_MASK); 
		ret = ifc_qdma_request_start(qchnl, IFC_QDMA_DIRECTION_TX, txd[i]);
		if (ret < 0){
			printf("start req error\n");
			break;
		}
		current_submitted++;

#ifdef DEBUG
		qdma_hex_dump(txd[i]->buf, MAXBUFLEN);
#endif
	}
	
	printf("Pre-filling done for %ld desc\n", current_submitted);

	/* keep pushing same request till eof not reached */
	do {
		nr = ifc_qdma_completion_poll(qchnl,
					      IFC_QDMA_DIRECTION_TX,
					      tx_raw_pkt,
					      ARRAY_SIZE(tx_raw_pkt));
		if(nr > 0 && verbose)
		   printf("tx: completed %d block\n", nr);
		current_done += nr > 0 ? nr : 0;
		for (i = 0; i < nr; i++) {
			d = (struct ifc_qdma_request *)tx_raw_pkt[i];
			sleep_byte_record += d->len;
            if(app_exit == 0 && current_submitted < count){ 
			   ret = load_data_frm_file(in_file_fd, d->buf);
			   if (ret < 0) 
				  goto out;
			   ret = ifc_qdma_request_start(qchnl, IFC_QDMA_DIRECTION_TX, d);
			   if (ret < 0)
				 goto out;
			   current_submitted++;
			} 
#ifdef DEBUG
			qdma_hex_dump(txd[i]->buf, MAXBUFLEN);
#endif
		}
		if(sleep_byte_record >= 0x4000000){
		   // usleep(1000000);
		   sleep_byte_record = 0;
		}
	} while (app_exit == 0 && current_done < count);

out:
    if(in_file_fd > 0) 
	   close(in_file_fd);
	for (i = 0; i < qdma_qdepth; i++)
	   	if(txd[i])ifc_request_free(txd[i]); 

	return 0;
}

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
	struct ifc_qdma_request **txd;
	struct ifc_qdma_device *qdev;
	struct ifc_qdma_channel *qchnl;
	/* initialize with correct BDF value */
	uint32_t block_size = MAXBUFLEN;
	uint32_t qdma_qdepth = QDEPTH;
	int ch = IFC_QDMA_AVAIL_CHNL_ARG;
	int ret;
	int port;

	signal(SIGINT, signal_handler);
	take_args(argc, argv);
	block_size = MAXBUFLEN;
	qdma_qdepth = QDEPTH;

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
	ret = ifc_qdma_channel_get(qdev, &qchnl, ch, IFC_QDMA_DIRECTION_TX);
	if (ret < 0)
		goto out1;

	/* pre-fill TX the descriptor queue */
	txd = malloc(qdma_qdepth * sizeof(*txd));
	if (txd == NULL){
		printf("%s: can not alloc memory\n", argv[0]);
		ifc_qdma_channel_put(qchnl, IFC_QDMA_DIRECTION_TX);
		goto out1;
	}

    if(strlen(in_file) > 0 && access(in_file, F_OK) == 0) {
	   ret = process_file_data(qchnl, txd, qdma_qdepth);
    } else {
	   ret = process_default_data(qchnl, txd, qdma_qdepth);
	}

out2:
	/* release your allocated channels */
	ifc_qdma_channel_put(qchnl, IFC_QDMA_DIRECTION_TX);
	free(txd);
out1:
	ifc_qdma_device_put(qdev);
out:
	ifc_app_stop();

	printf("avmm tx: done\n");

	return 0;
}
