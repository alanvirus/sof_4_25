/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#ifndef __PERFQ_APP_
#define __PERFQ_APP_

#ifdef __cplusplus
extern "C" {
#endif

#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include<string.h>
#ifdef DPDK_21_11_RC2
#include <ethdev_driver.h>
#endif
#include <rte_pmd_mcdma.h>
#include <rte_malloc.h>
#include <mcdma_ip_params.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include <pio_reg_registers.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_eal.h>

#ifdef IFC_MCDMA_MAILBOX
#include <rte_kni_fifo.h>
#include <rte_kni_common.h>
#include "mcdma_mailbox.h"
#endif

#define MBUF_CACHE_SIZE 256
#define MBUF_PRIVATE_DATA_SIZE 0

#define IFC_QDMA_NUM_CHUNKS_IN_POOL 266240
//#define IFC_QDMA_NUM_CHUNKS_IN_POOL 8192
#define IFC_QDMA_NUM_CHUNKS_IN_EACH_POOL 1024

#define THR_EXEC_TIME_MSEC 128
#define REQUEST_RECEIVE 0
#define REQUEST_TRANSMIT 1
#define REQUEST_LOOPBACK 2
#define REQUEST_BOTH 3
#define REQUEST_BY_SIZE 0
#define REQUEST_BY_TIME 1
#define true 1
#define false 0
#ifdef IFC_QDMA_INTF_ST
#define PIO_ADDRESS 0x1000
#else
#define PIO_ADDRESS 0x00
#endif
#define BAS_ADDRESS 0x00
#define PIO_BASE 0x10000
#define REG_READ 0
#define WRITE_BACK 1
#define MSIX 2
#define AVST_PATTERN_DATA               0xa
#define PATTERN_CHANNEL_MULTIPLIER      8
#define PATTERN_CHANNEL_BASE            0x00010000
#define PORT_TOTAL_WIDTH                16
#define CHANNEL_TOTAL_WIDTH             12

/* Alignement offset */
#define PERFQ_ALIGN_OFF	1

#define FILE_PATH_SIZE 30

#define PERFQ_TIMEOUT_PCKTGEN    1ULL

#define PERFQ_FILE "perfq_data.csv"
#define DUMP_FILE "dump_data.csv"

#define check_payload(a, b)	((a == b) ? \
				(PERFQ_SUCCESS) : (PERFQ_DATA_PAYLD_FAILURE))

#define not_powerof2(x) (x & (x - 1))

#define QUEUE_SIZE (IFC_CONFIG_QDMA_NUM_DESC_PAGES_PER_QUEUE * 128)
#define HOL_REG_CONFIG_INTERVAL 5

/* Timeout values in micro seconds */
#define PERFQ_TIMEOUT_LOOPBACK	1000000
#define PERFQ_TIMEOUT_SMALL	2
#define PERFQ_TIMEOUT_BIG	5

#define FORCE_ENQUEUE		1
#define NO_FORCE_ENQUEUE	0

#define IFC_MCDMA_DATA_WIDTH	16

#ifdef IFC_QDMA_INTF_ST
#ifdef IFC_QDMA_ST_PACKETGEN
/* ED Defaults */
#define DEFAULT_PKT_GEN_FILES	512
#ifdef ED_VER0
#define DEFAULT_BATCH_SIZE	(QDEPTH)
#elif defined IFC_ED_CONFIG_TID_UPDATE
#define DEFAULT_BATCH_SIZE	(QDEPTH)
#else
#define DEFAULT_BATCH_SIZE	(QDEPTH/4)
#endif

#define PREFILL_QDEPTH		(QDEPTH)
#define DEFAULT_FILE_SIZE	1
#define DIDF_ALLOWED_BATCH_SIZE	64
#define DIDF_ALLOWED_FILE_SIZE	64

#else
/* Loopback Defaults */
#define DEFAULT_BATCH_SIZE	(QDEPTH/2)
#define PREFILL_QDEPTH		(QDEPTH)
#define DEFAULT_FILE_SIZE	1
#define DIDF_ALLOWED_BATCH_SIZE	64
#define DIDF_ALLOWED_FILE_SIZE	1
#endif

#else
/* AVMM Defaults */
#define DEFAULT_BATCH_SIZE	(QDEPTH)
#define PREFILL_QDEPTH		(QDEPTH)
#define DEFAULT_FILE_SIZE	1
#endif

#define AVMM_CHANNELS		512
#define AVST_CHANNELS		2048

#define AVST_MAX_NUM_CHAN	AVST_CHANNELS

#if PCIe_SLOT == 0
/* AVMM memory is 2048 deep and 512bit wide */
#define PERFQ_AVMM_BUF_LIMIT	131072
#elif PCIe_SLOT == 1
/* AVMM memory is 2048 deep and 256bit wide for x8 */
#define PERFQ_AVMM_BUF_LIMIT	65536
/* AVMM memory is 2048 deep and 128bit wide for x4 */
#elif PCIe_SLOT == 2
#define PERFQ_AVMM_BUF_LIMIT	32768
#endif
/* Meta data size in bytes */
#define METADATA_SIZE		8

#if 0
/* PF & VF channels */
/* PF count starts from 1 */
#define IFC_QDMA_CUR_PF         1
/* VF count starts from 1. Zero implies PF was used instead of VF */
#define IFC_QDMA_CUR_VF         0
#endif
/* Number of PFs */
#define IFC_QDMA_PFS            4
/* Channels available per PF */
#define IFC_QDMA_PER_PF_CHNLS   512
/* Channels available per VF */
#define IFC_QDMA_PER_VF_CHNLS   0
/* Number of VFs per PF */
#define IFC_QDMA_PER_PF_VFS     0

#define FORCE_ENQUEUE           1
#define NO_FORCE_ENQUEUE        0

/* performance mode */
#ifdef PERFQ_PERF

#ifdef PERFQ_LOAD_DATA
#undef PERFQ_LOAD_DATA
#endif

#ifdef DUMP_DATA
#undef DUMP_DATA
#endif

#ifdef VERIFY_FUNC
#undef VERIFY_FUNC
#endif

#ifdef VERIFY_HOL
#undef VERIFY_HOL
#endif

#endif


/* BAS Addresses and offsets */
/* Read specific */
#define IFC_MCDMA_BAS_READ_ADDR		0x00000000UL
#define IFC_MCDMA_BAS_READ_COUNT	0x00000008UL
#define IFC_MCDMA_BAS_READ_ERR		0x00000010UL
#define IFC_MCDMA_BAS_READ_CTRL		0x00000018UL

#define IFC_MCDMA_BAS_READ_CTRL_TRANSFER_SIZE_SHIFT	0
#define IFC_MCDMA_BAS_READ_CTRL_TRANSFER_SIZE_WIDTH	8
#define IFC_MCDMA_BAS_READ_CTRL_TRANSFER_SIZE_MASK	0x0FFU

#define IFC_MCDMA_BAS_READ_CTRL_ENABLE_SHIFT	31
#define IFC_MCDMA_BAS_READ_CTRL_ENABLE_WIDTH	1
#define IFC_MCDMA_BAS_READ_CTRL_ENABLE_MASK	0x80000000UL

/* Write specific */
#define IFC_MCDMA_BAS_WRITE_ADDR	0x00000020UL
#define IFC_MCDMA_BAS_WRITE_COUNT	0x00000028UL
#define IFC_MCDMA_BAS_WRITE_ERR		0x00000030UL
#define IFC_MCDMA_BAS_WRITE_CTRL	0x00000038UL

#define IFC_MCDMA_BAS_WRITE_CTRL_TRANSFER_SIZE_SHIFT	0
#define IFC_MCDMA_BAS_WRITE_CTRL_TRANSFER_SIZE_WIDTH	8
#define IFC_MCDMA_BAS_WRITE_CTRL_TRANSFER_SIZE_MASK	0x0FFU

#define IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_SHIFT	31
#define IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_WIDTH	1
#define IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_MASK	0x80000000UL

#define IFC_MCDMA_BAS_READ_MAP_TABLE	0x00000100UL
#define IFC_MCDMA_BAS_WRITE_MAP_TABLE	0x00000200UL

#define IFC_MCDMA_BAS_X16_SHIFT_WIDTH	0
#define IFC_MCDMA_BAS_X8_SHIFT_WIDTH	0
#define IFC_MCDMA_BAS_X4_SHIFT_WIDTH	0

#define IFC_MCDMA_BAS_X16_BURST_LENGTH	8
#define IFC_MCDMA_BAS_X8_BURST_LENGTH	16
/* In X4 configuration, avst data width is 128,
 * burst count = Max_payload_size/avst_data_width,
 *  512bytes/128 bits= 32. 
 *  */
#define IFC_MCDMA_BAS_X4_BURST_LENGTH	32
#define IFC_MCDMA_BAS_BURST_BYTES	512

#define IFC_MCDMA_BAM_BURST_BYTES	32

#define IFC_MCDMA_BAS_TRANSFER_COUNT_MASK		0xFFFFFFFFUL
#define IFC_MCDMA_BAS_NON_STOP_TRANSFER_ENABLE_MASK	0x80000000UL
#define IFC_MCDMA_BAS_NON_STOP_TRANSFER_DISABLE_MASK	0x7FFFFFFFUL

#define PERFQ_ARG_IP_RESET	0x1
#define PERFQ_ARG_NUM_THREADS	0x2
#define PERFQ_ARG_CHNL_CNT	0x4
#define PERFQ_ARG_SINGLE_BDF	0x8
#define PERFQ_ARG_NUM_CHNLS	0x10
#define PERFQ_ARG_DEBUG		0x20
#define PERFQ_ARG_FILE_SIZE	0x40
#define PERFQ_ARG_PKT_SIZE	0x80
#define PERFQ_ARG_QUEUE_SIZE	0x100
#define PERFQ_ARG_TRANSFER_SIZE	0x200
#define PERFQ_ARG_TRANSFER_TX	0x400
#define PERFQ_ARG_TRANSFER_RX	0x800
#define PERFQ_ARG_TRANSFER_U	0x1000
#define PERFQ_ARG_TRANSFER_Z	0x2000
#define PERFQ_ARG_TRANSFER_TIME	0x4000
#define PERFQ_ARG_TRANSFER_V	0x8000
#define PERFQ_ARG_DATA_VAL	0x10000
#define PERFQ_ARG_PIO		0x20000
#define PERFQ_ARG_BAS		0x40000
#define PERFQ_ARG_BATCH		0x80000
#define PERFQ_ARG_TRANSFER_I	0x100000
#define	PERFQ_ARG_BAS_PERF	0x200000
#define	PERFQ_ARG_PKT_GEN_FILES	0x800000
#define	PERFQ_ARG_PF		0x1000000
#define	PERFQ_ARG_VF		0x2000000

#define BAS_EXPECTED_MASK1	(PERFQ_ARG_BAS | PERFQ_ARG_TRANSFER_SIZE |  PERFQ_ARG_SINGLE_BDF | PERFQ_ARG_TRANSFER_TX)
#define BAS_EXPECTED_MASK2	(PERFQ_ARG_BAS | PERFQ_ARG_TRANSFER_SIZE |  PERFQ_ARG_SINGLE_BDF | PERFQ_ARG_TRANSFER_RX)
#define BAS_EXPECTED_MASK3	(PERFQ_ARG_BAS | PERFQ_ARG_TRANSFER_SIZE |  PERFQ_ARG_SINGLE_BDF | PERFQ_ARG_TRANSFER_Z)
#define BAS_PERF_EXPECTED_MASK	(PERFQ_ARG_BAS_PERF | PERFQ_ARG_TRANSFER_SIZE |  PERFQ_ARG_SINGLE_BDF | PERFQ_ARG_TRANSFER_Z)

extern struct ifc_mcdma_stats ifc_mcdma_stats_g;

enum perfq_status {
	PERFQ_CORE_ASSGN_FAILURE				= -1,
	PERFQ_MALLOC_FAILURE					= -2,
	PERFQ_CMPLTN_NTFN_FAILURE				= -3,
	PERFQ_DATA_VALDN_FAILURE				= -4,
	PERFQ_FILE_VALDN_FAILURE				= -5,
	PERFQ_DATA_PAYLD_FAILURE				= -6,
	PERFQ_TASK_FAILURE					= -7,
	PERFQ_CH_INIT_FAILURE					= -8,
	PERFQ_SUCCESS						= 0,
	PERFQ_SOF						= 1,
	PERFQ_EOF						= 2,
	PERFQ_BOTH						= 3
};

enum thread_status {
	THREAD_ERROR_STATE					= -1,
	THREAD_DEAD						= 0,
	THREAD_RUNNING						= 1,
	THREAD_STOP						= 2,
	THREAD_NEW						= 3,
	THREAD_READY						= 4,
	THREAD_WAITING						= 5
};

enum error_event {
	TID_ERROR						= (1u << 0),
	COMPLETION_TIME_OUT_ERROR				= (1u << 1),
	DESC_FETCH_EVENT					= (1u << 2),
	DATA_FETCH_EVENT					= (1u << 3),
	OTHER_ERROR						= (1u << 4)
};

#ifdef IFC_MCDMA_RANDOMIZATION
#define IFC_RAND_HIFUN_P	(1)
struct rand_params_st {
	size_t hifun_p;
};
#endif

struct loop_lock {
	sem_t tx_lock;
	sem_t rx_lock;
};

struct task_stat {
	int completion_status;
	int gfile_status;
	int gpckt_status;
};

#ifdef VERIFY_HOL
struct hol_stat {
	char *reg_config;
	unsigned int size;
	unsigned int index;
	unsigned int interval;
};
#endif

struct struct_flags {
	int flimit; //1:by time 0: by request size
	int fpf;
	int fvf;
	int num_threads;
	int comp_policy;
	int fpio;
	int fbas;
	int fbas_perf;
	int fbam_perf;
	int fpkt_gen_files;
	int fvalidation;
	int fmpps;
	int fshow_stuck;
	char bdf[256];
	int chnls;
	int start_ch;
	size_t request_size;
	size_t  packet_size;
	unsigned long packets;
	unsigned long rx_packets;
	unsigned long batch_size;
	unsigned long tx_batch_size;
	unsigned long rx_batch_size;
	unsigned long file_size;
	unsigned long rx_packet_size;
	unsigned long rx_file_size;
	unsigned long rx_file_pyld_size;
	unsigned long qdepth_per_page;
	int direction;
	unsigned int interval;
	unsigned int time_limit;
	unsigned int file_pyld_size;
	pthread_mutex_t *locks;
	struct loop_lock *loop_locks;
	int hol;
	char fname[FILE_PATH_SIZE];
	int ip_reset;
	int ready;
	int cleanup;
	uint64_t params_mask;
	bool chnl_cnt;
	int mbuf_pool_fail;
	int pkt_gen_files;
	int pf;
	int vf;
	int bar;
	uint32_t desc_pyld[QDEPTH];
	uint16_t num_pylds;
	uint16_t h2d_pyld_sum;
	uint32_t h2d_max_pyld;
#ifdef DUMP_FAIL_DATA
	FILE* dump_fd;
	char dump_file_name[FILE_PATH_SIZE];
#endif
	uint32_t portid;
        int is_pf;
        int is_vf;
#ifdef IFC_MCDMA_RANDOMIZATION
	int frand;
	struct rand_params_st *rand_param_list;
	int	rand_enteries;
#endif
};

struct ifc_mcdma_mbuf_buffer {
       uint16_t size;
       uint16_t length;
       struct rte_mbuf *pkts[];
};

struct queue_context {
	char bdf[256];
	int phy_channel_id;
	int channel_id;
	int direction;
	uint16_t port_id;
	unsigned long batch_size;
	unsigned long file_size;
	struct rte_mempool *mp;
	struct struct_flags *flags;
	pthread_t pthread_id;
	void *completion_buf;
	void *poll_ctx;
	struct timespec start_time; /* start time of test */
	struct timespec end_time; /* end time of test */
	struct timespec start_time_epoch; /* epoch start time */
	int fstart;

	unsigned long request_counter;
	unsigned long prev_rqst_cntr;
	unsigned long prep_counter;
	unsigned long completion_counter;
	unsigned long cur_comp_counter;
	unsigned long prev_comp_counter;
	unsigned long failed_attempts;
	struct ifc_mcdma_mbuf_buffer *dma_buffer;
	struct rte_mempool *tx_mbuf_pool;
	unsigned long tout_err_cnt;
	unsigned long umsix_err_cnt;

	/* Stuck detection */
	unsigned long ovrall_tid;
	unsigned long prev_tid;
	unsigned long ovrall_nonb_tid;
	unsigned long prev_nonb_tid;
	unsigned long stuck_count;
	unsigned long vpckt_counter;
	unsigned long prev_vpckt;
	uint32_t switch_count;
	uint32_t prev_switch_count;
	unsigned long pr_count;
	unsigned long prev_pr_count;

	uint32_t mdata_cur_pattern;
	uint32_t mdata_expected_pattern;
	unsigned long mdata_gpckt_counter;
	unsigned long mdata_bpckt_counter;

	uint32_t cur_pattern;
	uint32_t expected_pattern;
	unsigned long gpckt_counter;
	unsigned long bpckt_counter;

	int cur_file_status;
	unsigned long gfile_counter;
	unsigned long bfile_counter;
	enum thread_status status;
	enum error_event err_event;
	int rx_data_drop;
	int tx_data_drop;
	int transfer_status;
#ifndef IFC_QDMA_INTF_ST
	uint64_t base;
	uint64_t limit;
	uint64_t src;
	uint64_t dest;
#endif

	struct thread_context *thrctx;
	int init_done;
	int prefill_done;
	uint32_t valid:1;
	uint32_t epoch_done;
	uint32_t state;
	unsigned long accumulator_index;

	long int last_topup;
	uint32_t tid_count;
	uint32_t nonb_tid_count;
	uint32_t desc_requested;
	uint32_t files_requested;
	int backlog;
	int comp_buf_offset;
	uint32_t extra_bytes;
	uint32_t data_tag;
	uint32_t prev_timediff_sec;
	uint32_t tid_err;
	uint32_t comp_err;
	uint32_t unknown_err;

#ifdef IFC_MCDMA_RANDOMIZATION
	int	rand_f_idx;
#endif
};

enum queue_state {
	IFC_QDMA_QUE_INIT,
	IFC_QDMA_QUE_WAIT_FOR_COMP,
	IFC_QDMA_QUE_MAX,
};

#ifdef IFC_MCDMA_MAILBOX
/*max number of cmds supported*/
#define MB_MAX_NUM_CMDS    8

struct mbox_node {
    int is_pf_vf;
    int cmd_type;
};

/*mailbox context data structure*/
struct mailbox_thread_context {
        struct mbox_node cmd_list[MB_MAX_NUM_CMDS];
        struct rte_kni_fifo *cmd_fifo;
        uint16_t portid;
};

#endif

struct thread_context {
	int tid;
	pthread_t pthread_id;
	struct struct_flags *flags;
	enum thread_status status;
	struct queue_context qctx[4096];
	int qcnt;
};

struct struct_time {
	double ovrall_bw;
	double intrvl_bw;
	double ovrall_mpps;
	double intrvl_mpps;
	double cpercentage;
	long timediff_msec;
	long timediff_sec;
	long timediff_min;
};

#ifdef VERIFY_HOL
extern struct hol_stat global_hol_stat;
#endif

int cmdline_option_parser(int argc, char *argv[], struct struct_flags *flags);
void show_help(void);
void cleanup(struct struct_flags *flags,
	     struct thread_context *tctx);
int thread_cleanup(struct thread_context *tctx);
int show_progress(struct thread_context *tctx, struct struct_flags *flags,
		  struct timespec *last_checked,
		  struct timespec *start_time);
enum perfq_status
show_summary(struct thread_context *tctx, struct struct_flags *flags,
		  struct timespec *last_checked,
		  struct timespec *start_time);

void time_computations(struct struct_time *t, struct queue_context *tctx,
		       struct struct_flags *flags,
		       struct timespec *last_checked,
		       struct timespec *cur_time);

uint32_t load_data(void *buf, size_t size, uint32_t pattern, struct queue_context *qctx);
uint64_t avmm_addr_manager(struct thread_context *tctx, uint64_t *addr,
			   unsigned long payload);
enum perfq_status pattern_checker(void *buf, size_t size,
				   uint32_t expected_pattern, uint32_t *, struct queue_context *);
enum perfq_status tag_checker(void *buf, size_t size,
				   uint32_t expected_pattern, uint32_t *, struct queue_context *);
 
void *transfer_handler(void *ptr);
void *transfer_handler_util(void *ptr);
void *transfer_handler_loopback(void *ptr);
int should_thread_stop(struct queue_context *tctx);
void sig_handler(int sig);
void memory_sig_handler(int sig);
void signal_mask(void);
int time_diff(struct timespec *a, struct timespec *b, struct timespec *result);
void* queues_schedule(void *ptr);
#ifdef IFC_MCDMA_MAILBOX
int thread_creator(struct struct_flags *flags,
                struct thread_context *tctx,
                struct mailbox_thread_context *mctx);
#else
int thread_creator(struct struct_flags *flags,
                struct thread_context *tctx);
#endif
int context_init(struct struct_flags *flags, struct thread_context **ptr);
enum perfq_status request_completion_poll(struct queue_context *tctx);
enum perfq_status queue_init(struct queue_context *tctx);
enum perfq_status enqueue_request(struct queue_context *tctx,
				  struct rte_mbuf **req_buf,
				  int nr, int do_submit, int force,
				  bool check_thread_status);
int count_queues(struct struct_flags *flags);
int pio_util(uint16_t portid);
int pio_perf_util(uint16_t portid);
int count_threads(struct struct_flags *flags);
enum perfq_status post_processing(struct queue_context *tctx, struct rte_mbuf **buf);
enum perfq_status set_sof_eof(struct struct_flags *flags, struct rte_mbuf *req,
			      unsigned long req_counter);
enum perfq_status check_sof_eof(struct queue_context *tctx,
				struct rte_mbuf *req,
				unsigned long request_number);
int hw_eof_handler(struct queue_context *tctx);
int pio_bit_writer(uint16_t port_id,
		   uint64_t offset, int n, int set);
int non_msi_init(struct queue_context *tctx);
int msix_init(struct queue_context *tctx);
int show_header(struct struct_flags *flags);
int append_to_file(char *file_name, char *append_data);
int batch_enqueue_wrapper(struct queue_context *tctx, struct rte_mbuf **buf,
			  unsigned long nr, bool check_thread_status);
unsigned long get_payload_size(struct queue_context *tctx,
			       unsigned long req_no);
unsigned long get_file_size(struct queue_context *qctx);
void dump_flags(struct struct_flags *flags);
int hol_stat_init(void);
int hol_config(struct struct_flags *flags, struct thread_context *tctx,
	       unsigned long elapsed_sec);
int should_app_exit(struct timespec *timeout_timer);
int test_evaluation(struct queue_context *tctx, struct struct_flags *flags,
		    struct task_stat *stat);
void sem_wait_wrapper(struct queue_context *tctx, sem_t *lock);
int ifc_mcdma_request_buffer_flush(struct queue_context *tctx);
int ifc_mcdma_request_buffer(struct queue_context *tctx,
			     struct rte_mbuf *req);
int ifc_mcdma_request_submit(struct queue_context *tctx, int dir,
			     struct rte_mbuf **bufs, uint32_t nr);
uint64_t pio_queue_config_offset(struct queue_context *tctx);
int batch_load_wrapper(struct queue_context *tctx, struct rte_mbuf
                          **buf, unsigned long nr);
int ifc_mcdma_umsix_irq_handler(int port_id, int qid,
				int dir, int *errinfo);
int ifc_mcdma_queue_reset(int port_id, int qid, int dir);
void pio_reset(uint16_t port_id);
struct queue_context *ifc_get_que_ctx(int qid, int dir);
int pio_update_files(uint16_t portid, struct queue_context *tctx,
			    uint32_t fcount);
void perfq_submit_requests(struct queue_context *qctx, uint32_t diff);
int bas_test(uint16_t port_id, struct struct_flags *flags);
void compute_bw(struct struct_time *t,
				  struct timespec *start_time,
				  struct timespec *cur_time,
				  uint64_t pkts, uint32_t size);

void set_avst_pattern_data(struct queue_context *tctx);
#ifdef IFC_MCDMA_MAILBOX
int mailbox_context_init(struct mailbox_thread_context **mbctx, uint16_t portid);
void* mailbox_schedule_msg_send(void *mbctx);
void* mailbox_schedule_recv_msg(void *mbctx);
#endif

#ifdef __cplusplus
}
#endif
#endif
