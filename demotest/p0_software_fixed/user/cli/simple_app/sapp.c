// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <ifc_libmqdma.h>
#include <ifc_qdma_utils.h>
#include <regs/pio_reg_registers.h>
#include <ifc_reglib_osdep.h>
#include <inttypes.h>
#include <getopt.h>

#define true	1
#define false	0
#define REQUEST_BY_SIZE 0
#define REQUEST_BY_TIME 1
#define REQUEST_RECEIVE 0
#define REQUEST_TRANSMIT 1
#define DEFAULT_PKT_GEN_FILES 64

#undef DEBUG_APP

#if IFC_CONFIG_QDMA_NUM_DESC_PAGES_PER_QUEUE > 1
#define QDEPTH ((IFC_CONFIG_QDMA_NUM_DESC_PAGES_PER_QUEUE/2) * IFC_MCDMA_CONFIG_NUM_DESC_PER_PAGE)
#else
#define QDEPTH ((IFC_MCDMA_CONFIG_NUM_DESC_PER_PAGE/2) + 1)
#endif

/* Set the config to use */
int qdma_dir = IFC_QDMA_DIRECTION_RX;
char bdf[256];
int qdepth = QDEPTH;
int batch_size = 63;
int payload = 64;


struct ifc_qdma_device *qdev;
struct queue_ctx *global_qctx;
struct struct_flags *global_flags;

struct struct_flags {
	int flimit; //1:by time 0: by request size
	char bdf[256];
	size_t request_size;
	size_t  packet_size;
	unsigned long packets;
	unsigned long file_size;
	int direction;
	unsigned int interval;
	unsigned int time_limit;
};

struct queue_ctx {
	int channel_id;
	struct ifc_qdma_channel *qchnl;
	struct ifc_qdma_request **req_buf;
	int head;
	int tail;
	int isempty;
	struct timespec start_time;
	struct timespec end_time;
	unsigned long request_counter;
	unsigned long completion_counter;
	int prep_counter;
	unsigned long cur_comp_counter;
	int fstart;
	struct struct_flags *flags;
};

static int pio_cleanup(void)
{
	uint64_t ed_config;
	uint64_t offset;
	uint64_t base;

	/* Channel config */
	for (offset = 0x00; offset < 0x800; offset += 8) {
		/* H2D config */
		base = 0x20000;
		ed_config = 0ULL;
		ifc_qdma_pio_write64(qdev, base + offset, ed_config);

		/* D2H config */
		base = 0x30000;
		ed_config = 0ULL;
		ifc_qdma_pio_write64(qdev, base + offset, ed_config);
	}
	return 0;
}

static void cleanup(struct queue_ctx *qctx)
{
	struct ifc_qdma_request **req_buf;
	int i;

	if (qctx->req_buf) {
		req_buf = qctx->req_buf;
		for (i = 0; i < qdepth; i++)
			ifc_request_free(req_buf[i]);
		free(req_buf);
		qctx->req_buf = NULL;
	}

	pio_cleanup();
	if (qctx->qchnl)
		ifc_qdma_channel_put(qctx->qchnl, qdma_dir);
	if (qdev)
		ifc_qdma_device_put(qdev);
	ifc_app_stop();
	free(qctx);
	qctx = NULL;
}

static int
time_diff(struct timespec *a, struct timespec *b, struct timespec *result)
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

static int show_summary(struct queue_ctx *qctx)
{
	struct timespec result;
	unsigned long tl_pyld;
	double bw = 0.0;
	double tm_diff;
	long tm_diff_min, tm_diff_sec, tm_diff_msec;

	memset(&result, 0, sizeof(struct timespec));
	time_diff(&(qctx->end_time), &(qctx->start_time), &result);

	tm_diff = result.tv_sec + ((double)result.tv_nsec / 1e9);
	tl_pyld = qctx->completion_counter * payload;
	bw = (qctx->completion_counter / tm_diff) * payload;

	tm_diff_sec = result.tv_sec;
	tm_diff_min = tm_diff_sec / 60;
	tm_diff_sec = tm_diff_sec - (tm_diff_min * 60);
	tm_diff_msec = result.tv_nsec / 1e6;

	printf("---------------------------------------Summary"
	       "--------------------------------------\n");
	printf("Ch_D: %d%s,", qctx->channel_id, qdma_dir == 1 ? "Tx" : "Rx");
	printf("Req: %lu,", qctx->request_counter);
	printf("Rsp: %lu,", qctx->completion_counter);
	printf("Bytes Transferred: %zu,", tl_pyld);
	printf("Time: %02ld:%02ld:%03ld\n",
			tm_diff_min, tm_diff_sec, tm_diff_msec);
	printf("Total Bandwidth: %.2fGBPS\n", bw / (1UL << 30));
	printf("-----------------------------------------------"
	       "-------------------------------------\n");
	return 0;
}

/* SIGINT received */
static void sig_handler(int sig)
{
	printf("You have pressed ctrl+c\n");
	printf("Calling Handler for Signal %d\n", sig);
	printf("Calling cleanup crew\n");

	/* Step 6: Print the stats */
	show_summary(global_qctx);

	/* Step 7: Cleanup */
	cleanup(global_qctx);

	/* Step 8: Leave now */
	exit(0);
}

static int pio_init(void)
{
	uint64_t base;
	uint64_t offset = 0ULL;
	uint64_t ed_config = 0ULL;
	uint64_t files = 0ULL;
	unsigned long file_size = global_flags->file_size;

	/* write number of channels */
	ifc_qdma_pio_write64(qdev, 0x10008, 0);

	/* configure infinite mode */
	ifc_qdma_pio_write64(qdev, 0x10010, 1ULL);

	if (qdma_dir == IFC_QDMA_DIRECTION_TX) {
		base = 0x20000;
		ed_config = payload;
		ifc_qdma_pio_write64(qdev, base + offset, ed_config);
	}

	if (qdma_dir == IFC_QDMA_DIRECTION_RX) {
		base = 0x30000;
		ed_config = payload;
		if (global_flags->flimit == REQUEST_BY_SIZE)
			files = global_flags->packets / file_size;
		else
			files = DEFAULT_PKT_GEN_FILES;

		ed_config |= (files << PIO_REG_PORT_PKT_CONFIG_FILES_SHIFT);
		ed_config |= (1UL << PIO_REG_PORT_PKT_CONFIG_ENABLE_SHIFT);
		ifc_qdma_pio_write64(qdev, base + offset, ed_config);
	}

	return 0;
}

static int init(struct queue_ctx **qctx)
{
	struct queue_ctx *q;
	int ret;
	int port;

	/* need memory for the queue context */
	q = calloc(1, sizeof(struct queue_ctx));
	if (!q)
		return -1;

	/* start the engine */
	ifc_app_start(bdf, payload);
        port = ifc_mcdma_port_by_name(bdf);
        if (port < 0) {
		cleanup(q);
                return 1;
	}

	/* get DMA device */
	ret = ifc_qdma_device_get(port, &qdev, 128, IFC_CONFIG_QDMA_COMPL_PROC);
	if (ret)
		goto out;

	/* get number of channel */
	ret = ifc_num_channels_get(qdev);
	if (ret <= 0)
		goto out;

	/* allocate a channel */
	ret = ifc_qdma_channel_get(qdev, &q->qchnl, 0, qdma_dir);
	if (ret < 0)
		goto out;

	*qctx = q;
	pio_init();
	return 0;

out:
	cleanup(q);
	return -1;
}

static int prefill(struct queue_ctx *qctx)
{
	struct ifc_qdma_request **req_buf;
	int i;

	/* allocate memory to store the requests */
	req_buf = calloc(qdepth, sizeof(struct ifc_qdma_request *));
	if (!req_buf)
		return -1;

	/* get requests from the driver */
	for (i = 0; i < qdepth; i++) {
		req_buf[i] = ifc_request_malloc(payload);
		if (req_buf[i] == NULL)
			goto out;

		req_buf[i]->len = payload;
	}

	qctx->req_buf = req_buf;
	qctx->isempty = true;
	return 0;

out:
	while(--i)
		ifc_request_free(req_buf[i]);
	free(req_buf);
	req_buf = NULL;
	return -1;
}

static inline int should_app_stop(struct queue_ctx *qctx,
				  struct struct_flags *flags)
{
	struct timespec cur_time;
	long timediff_sec;
	int timeout_sec = flags->time_limit;

	if (qctx->fstart == false && flags->flimit == REQUEST_BY_TIME)
		return false;

	clock_gettime(CLOCK_MONOTONIC, &cur_time);

	/* is the transfer by time or request */
	if (flags->flimit == REQUEST_BY_TIME) {
		timediff_sec = difftime(cur_time.tv_sec,
				(qctx->start_time).tv_sec);
		if (timediff_sec >= timeout_sec) {
			clock_gettime(CLOCK_MONOTONIC, &(qctx->end_time));
			return true;
		}
	} else {
		if (flags->packets <=
		    (qctx->request_counter + qctx->prep_counter)) {
			clock_gettime(CLOCK_MONOTONIC, &(qctx->end_time));
			return true;
		}
	}
	return false;
}

static void show_help(void)
{
	printf("-h\t\tShow Help\n");
	printf("-b <bdf>\tBDF of Device\n");
	printf("-p <bytes>\tPayload Size of each descriptor\n");
	printf("-t\t\tTransmit Operation\n");
	printf("-r\t\tReceive Operation\n");
	printf("-l <seconds>\tTime limit in Seconds\n");
	printf("-s <bytes>\tRequest Size in Bytes\n");
	printf("Required parameters: b & p & (t | r) & (s | l)\n");
}

static void show_header(struct struct_flags *flags)
{
	printf("-----------------------------------------------"
	       "-------------------------------------\n");
        printf("BDF: %s\n", flags->bdf);
        printf("Payload Size per descriptor: %zu Bytes\n", flags->packet_size);
        if (flags->flimit == REQUEST_BY_SIZE)
                printf("Descriptors on channel: %lu\n", flags->packets);
        printf("SOF on descriptor: %d\n", 1);
        printf("EOF on descriptor: %lu\n", flags->file_size);
        printf("File Size: %lu Bytes\n", flags->file_size * flags->packet_size);
        printf("PKG Gen Files: %u\n", DEFAULT_PKT_GEN_FILES);
	printf("-----------------------------------------------"
	       "-------------------------------------\n");
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

static int cmdline_option_parser(int argc, char *argv[],
				 struct struct_flags *flags)
{
	int opt;
	int arg_val;
	int fbdf = false;
	int transmit_counter = -1;
	int fpacket_size = false;

	flags->request_size = 0;
	flags->file_size = 1;
	flags->flimit = -1;
	flags->packet_size = 0;
	flags->time_limit = 0;
	flags->direction = -1;

	while ((opt = getopt(argc, argv, "b:hl:p:rs:t")) != -1) {
		switch (opt) {
		case 'b':
			if (fbdf == true) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			ifc_qdma_strncpy(flags->bdf, sizeof(flags->bdf),
					 optarg, 20);
			fbdf = true;
			break;
		case 'h':
			if (optind != argc) {
				printf("ERR: Invalid parameter: %s\n",
					argv[optind]);
				printf("Try help: -h\n");
				return -1;
			}
			show_help();
                        return -1;
		case 'l':
			if (flags->time_limit != 0UL) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			arg_val = parse_long_int(optarg);
			if (arg_val <= 0) {
				printf("Invalid -l value\n");
				return -1;
			}
			if (flags->flimit != -1) {
				printf("ERR: Both -s & -l not allowed\n");
				printf("Try -h for help\n");
				return -1;
			}
			flags->time_limit = (unsigned int)arg_val;
			flags->flimit = REQUEST_BY_TIME;
                        break;
		case 'p':
			if (flags->packet_size != 0UL) {
				printf("ERR: Flags can not be repeated --p\n");
				return -1;
			}
			arg_val = parse_long_int(optarg);
			if (arg_val <= 0) {
				printf("Invalid -p value\n");
				return -1;
			}
			flags->packet_size = (unsigned long)arg_val;
			fpacket_size = true;
			break;
                case 'r':
                        if (flags->direction == REQUEST_RECEIVE) {
                                printf("ERR: Flags can not be repeated\n");
                                return -1;
                        }
                        flags->direction = REQUEST_RECEIVE;
                        transmit_counter++;
                        break;
                case 's':
                        if (flags->request_size != 0UL) {
                                printf("ERR: Flags can not be repeated\n");
                                return -1;
                        }
			arg_val = parse_long_int(optarg);
			if (arg_val <= 0) {
				printf("Invalid -s value\n");
				return -1;
			}
			if (flags->flimit != -1) {
				printf("ERR: Both -s & -l not allowed\n");
				printf("Try -h for help\n");
				return -1;
			}
			flags->request_size = (unsigned long)arg_val;
			flags->flimit = REQUEST_BY_SIZE;
			break;
		case 't':
			if (flags->direction == REQUEST_TRANSMIT) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			flags->direction = REQUEST_TRANSMIT;
			transmit_counter++;
			break;
		case '?':
			printf("Invalid option\n");
			printf("Try -h for help\n");
			return -1;
		default:
			printf("Try -h for help\n");
			return -1;
		}
	}

	if (optind != argc) {
		printf("ERR: Invalid parameter: %s\n", argv[optind]);
		printf("Try -h for help\n");
		return -1;
	}

	if (!fbdf) {
		printf("ERR: BDF value not specified\n");
		printf("Try -h for help\n");
		return -1;
        }

	/* Check for if -t or -r is set */
	if (transmit_counter) {
		printf("ERR: Direction not specified\n");
		printf("Try -h for help\n");
		return -1;
	}

	/* check for only one of -s and -l are set */
	if (flags->flimit == -1) {
		printf("Try -h for help\n");
		return -1;
	}

	/* check for required flags */
	if (!fpacket_size) {
		printf("ERR: Payload not specified\n");
		printf("Try -h for help\n");
		return -1;
	}

	if (flags->packet_size > 1048576) {
		printf("Payload Size must be less than 1MB\n");
		return -1;
	}

	if (flags->packet_size % 64 != 0) {
		if (flags->packet_size != IFC_MTU_LEN) {
			printf("Payload Size must be a multiple of 64"
				" or MTU size in AVST Packet Gen\n");
			return -1;
		}
	}

	if (flags->flimit == REQUEST_BY_SIZE) {
		if (flags->request_size < flags->packet_size) {
			printf("ERR: Request Size is smaller than Packet "
				"Size\n");
			printf("\tRequest_size: %lu < Packet_size: %lu\n",
				flags->request_size, flags->packet_size);
			return -1;
		}
		flags->packets = flags->request_size / flags->packet_size;
		/* request_size is not divisible by packet_size */
		if (flags->request_size % flags->packet_size) {
			printf("ERR: Request Size is not modulus"
				" of Packet size\n");
			printf("\tRequest_size: %lu  Packet_size: %lu\n",
				flags->request_size, flags->packet_size);
			return -1;
		}
        }

	memcpy(bdf, flags->bdf, 256);
	qdma_dir = flags->direction;
	payload = flags->packet_size;

	return 0;
}

static void poll_completions(struct queue_ctx *qctx)
{
	int ret = -1;

	ret = ifc_qdma_completion_poll(qctx->qchnl, qdma_dir,
			(void *)(qctx->req_buf + qctx->head),
			qdepth - qctx->head);
	if (ret <= 0) {
#ifdef DEBUG_APP
		fprintf(stderr, "Nothing completed: RC: %lu, Prep: %d, CC: %lu\n",
			qctx->request_counter, qctx->prep_counter,
			qctx->completion_counter);
#endif
	} else {
		qctx->head = (qctx->head + ret) % qdepth;
		qctx->completion_counter += ret;
		if (qctx->head == qctx->tail)
			qctx->isempty = true;
#ifdef DEBUG_APP
			fprintf(stderr, "Completed request: RC: %lu, Prep: %d, CC: %lu\n",
				qctx->request_counter, qctx->prep_counter,
				qctx->completion_counter);
#endif
	}
}

static void submit_reuqests(struct queue_ctx *qctx)
{
	int ret = -1;

	ret = ifc_qdma_request_submit(qctx->qchnl, qdma_dir);
	if (ret < 0) {
		printf("Failed to submit\n");
		return;
	}
	qctx->request_counter += qctx->prep_counter;
#ifdef DEBUG_APP
	fprintf(stderr, "Submitted request: RC: %lu, Prep: %d, CC: %lu\n",
		qctx->request_counter, qctx->prep_counter,
		qctx->completion_counter);
	if ((qctx->request_counter - qctx->completion_counter) >
	    (unsigned long)qdepth)
		fprintf(stderr, "Something went wrong, RC: %lu, Prep: %d, CC: %lu\n",
			qctx->request_counter, qctx->prep_counter,
			qctx->completion_counter);
#endif
	qctx->prep_counter = 0;
}

/**
 * Steps:
 *  Init
 *  Prefill: Get all the descriptors
 *  Start the clock
 *  Keep submitting requests in uni-direction
 *  When SIGINT is received, stop the clock
 *  Print the stats
 *  Cleanup & leave
 **/
int main(int argc, char *argv[])
{
	struct queue_ctx *qctx = NULL;
	int ret;

	/* allocate memory to struct_flags */
	global_flags = (struct struct_flags *)
		malloc(sizeof(struct struct_flags));
	if (!global_flags) {
		printf("Failed to allocate memory for flags\n");
		return 1;
	}

	ret = cmdline_option_parser(argc, argv, global_flags);
	if (ret)
		return 1;

	show_header(global_flags);

	qdev = NULL;
	global_qctx = NULL;

	/* Step 1: Init */
	ret = init(&qctx);
	if (ret < 0) {
		printf("Initialization Failed\n");
		return 1;
	}

	global_qctx = qctx;
	signal(SIGINT, sig_handler);

	/* Step 2: Prefill */
	ret = prefill(qctx);
	if (ret < 0) {
		printf("Prefill Failed\n");
		cleanup(qctx);
		return 1;
	}

	/* Step 3: Start the clock */
	clock_gettime(CLOCK_MONOTONIC, &qctx->start_time);
	qctx->fstart = true;

	/* Step 4: Submit requests */
	while (1) {
		if (should_app_stop(qctx, global_flags)) {
			/* Wait for completions */
			while (qctx->completion_counter != qctx->request_counter)
				poll_completions(qctx);
			show_summary(qctx);
			break;
		}
		/* prep only if we have requests not currently in use */
		if (qctx->head != qctx->tail || qctx->isempty) {
			ret = ifc_qdma_request_prepare(qctx->qchnl,
						       qdma_dir,
						       qctx->req_buf[qctx->tail]);
			if (ret < 0) {
				printf("Prepare API failed\n");
				continue;
			}
			qctx->prep_counter++;
			qctx->tail = (qctx->tail + 1) % qdepth;

			/* Submit the batch */
			if (qctx->prep_counter == batch_size ||
				((global_flags->flimit == REQUEST_BY_SIZE) &&
					(qctx->request_counter !=
					 global_flags->packets))) {
				submit_reuqests(qctx);
				qctx->isempty = false;
			}
		}

		/* poll */
		if (qctx->isempty == false)
			poll_completions(qctx);
	}
	cleanup(qctx);
	return 0;
}
