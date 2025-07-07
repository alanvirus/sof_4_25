// 3-Clause BSD license
/*-
*Copyright (C) 2019-220 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _IFC_LIBMQDMA_H_
#define _IFC_LIBMQDMA_H_

/**
 *  * Payload Limit
 *   */
#define IFC_MCDMA_BUF_LIMIT             (1UL << 20)

/**
 * DMA direction from device
 */
#define IFC_QDMA_DIRECTION_RX   0
/**
 * DMA direction to device
 */
#define IFC_QDMA_DIRECTION_TX   1

/**
 * Bi-directional DMA - first TX and then RX
 */
#define IFC_QDMA_DIRECTION_BOTH 2



/**
 * parameter to be passed to ifc_qdma_channel_get
 * to select the available channel
 */
#define IFC_QDMA_AVAIL_CHNL_ARG  -1

/**
 * SOF - Flag can be passed to driver
 */
#define IFC_QDMA_SOF_MASK		0x1

/**
 * EOF - Flag can be passed to driver
 */
#define IFC_QDMA_EOF_MASK		0x2

/**
 * Flag can be passed to driver
 */
#define IFC_QDMA_Q_BATCH_DELAY		1

/**
 * number of descriptors per page
 */
#define IFC_MCDMA_CONFIG_NUM_DESC_PER_PAGE             127

/*
 * QDepth: Max number of descriptors we can put on queue at prefill * time
 */
#ifdef IFC_MCDMA_DIDF
#define IFC_CONFIG_QDMA_NUM_DESC_PAGES_PER_QUEUE	1
#else
#ifdef IFC_32BIT_SUPPORT
#define IFC_CONFIG_QDMA_NUM_DESC_PAGES_PER_QUEUE	8
#else
#define IFC_CONFIG_QDMA_NUM_DESC_PAGES_PER_QUEUE	32
#endif
#endif

/*
 * Number of huge pages to be allocated
 * Tune this number based on Number of descriptor pages
 */
#ifdef IFC_32BIT_SUPPORT
#define NUM_HUGE_PAGES	40
#else
#define NUM_HUGE_PAGES  4	
#endif
/*
 * HW Loopback enablement in Loopback design
 */
#define LOOPBACK

/*
 * PCLe_SLOT
 * 0 - x16
 * 1 - x8
 * 2 - x4
 */
#define PCIe_SLOT	0

/*
 * Allowed log levels
 */
enum ifc_qdma_debug_level {
	IFC_QDMA_DEBUG,
	IFC_QDMA_INFO,
	IFC_QDMA_WARNING,
	IFC_QDMA_ERROR,
	IFC_QDMA_EMERG,
};

/*
 * Allowed log areas
 */
enum ifc_qdma_debug_param {
	IFC_QDMA_INIT,
	IFC_QDMA_TXRX,
	IFC_QDMA_STATS,
	IFC_QDMA_MAIN,
	IFC_QDMA_ALL = 0xFFFFFFFF,
};

/*
 * Default log level
 */
#define IFC_QDMA_DEFAULT_LOG_LEVEL	IFC_QDMA_ERROR

/*
 * Default log area
 */
#define IFC_QDMA_DEFAULT_LOG_AREA	IFC_QDMA_ALL

/**
 * Write back value in descriptor during loading time
 */
#define IFC_QDMA_RX_DESC_CMPL_PROC	0

#ifdef IFC_MCDMA_DIDF
#ifndef IFC_MCDMA_FUNC_VER
#define IFC_MCDMA_FUNC_VER
#endif
#endif

enum ifc_qdma_queue_param {
	IFC_QDMA_CHNL_QUEUE_CMPL,
	IFC_QDMA_CHNL_WEIGHTAGE,
};

/**
 * QDMA Descriptor completion reporting procedure enum
 *
 * While initialiing the queue specific parameters, driver uses
 * this parameter to set the descriptor completion reporting mechanism
 * in FPGA.
 * This values would be stored in queue context and based on this
 * parameter, driver reads the DMA completion status
 *
 */
enum ifc_config_qdma_cmpl_proc {
	/*
 	 * write back mechanism
 	 * hardware writes the completion status at the
 	 * configured host address.
 	 * configuration done at the time of queue initilization
	 */
	CONFIG_QDMA_QUEUE_WB,
	/*
 	 * driver responsible to read the status from
 	 * PCIe registers available in register spec
 	 */
	CONFIG_QDMA_QUEUE_REG,
	/*
 	 * during queue initilization, driver registers the eventfds
 	 * and notify to PCIe end point module.
 	 * When interrupt received, kernel notify the event to user space
 	 */
	CONFIG_QDMA_QUEUE_MSIX,
};

/*
 * Set default descriptor completion procedure
 */
#define IFC_CONFIG_QDMA_COMPL_PROC CONFIG_QDMA_QUEUE_WB


/*
 * Max Transferrable unit length
 * 1500 payload + 14 bytes ethernet header + 4 bytes CRC
 */
#define IFC_MTU_LEN	1518


/*
 * Using this parameter to add error handling at
 * channel acquisition if we set IFC_MCDMA_ERR_CHAN
 * from makefile
 *
 * 1 - This device is PF
 * 0 - This device is VF
 */
#define IFC_MCDMA_IS_PF

/**
 * QDMA I/O request struct
 *
 * To make an I/O request to library application must use this structure.
 *
 * Allocate this request structure by ifc_qdma_request_malloc().
 * Application may change some of the optional fields (like 'offset').
 *
 * Queue this I/O request to library by ifc_qdma_request_start(). To know
 * progress/completion of processing application should call
 * ifc_qdma_completions_poll(). If this I/O structure is returned by the
 * library as part of completions_poll(), then request can be free'd or
 * re-used for further I/O request.
 *
 * Once usage of the request structure is completed or over the application
 * must release the buffer by ifc_qdma_request_free().
 **/
struct ifc_qdma_request {
	void *buf;              /* src/dst buffer */
	uint64_t phy_addr;      /* physical address */
	uint32_t len;           /* number of bytes */
	/* In case of D2H, driver updates the data size in pyld_cnt */
	uint64_t pyld_cnt;
	/*
	 * values in flags
	 * IFC_QDMA_EOF_MASK
	 * IFC_QDMA_SOF_MASK
	 */
	uint32_t flags;		/* SOF and EOF */
	void *ctx;              /* driver context, NOT for application */

	/* for avmm */
	uint64_t src;
	uint64_t dest;

	/* In case of H2D user need to update metadata,
 	 * but in case of D2H driver updates back the metadata
 	 */
	uint64_t metadata;
        /*To get the current processed descriptor*/
        uint32_t *cur_desc;
};

struct ifc_qdma_desc {
        /* word 1,2 */
        uint64_t src;
        /* word 3,4 */
        uint64_t dest;
        /* word 5 */
        uint32_t len:20;
        uint32_t rsvd:12;
        /* word 6 */
        uint32_t didx:16;
        uint32_t msix_en:1;
        uint32_t wb_en:1;
        uint32_t rsvd2:14;
        /* word 7 */
        uint32_t rx_pyld_cnt:20;
        uint32_t rsvd3:10;
#ifdef IFC_QDMA_INTF_ST
        uint32_t sof:1;
        uint32_t eof:1;
#else
        uint32_t rsvd4:2;
#endif
        /* word 8 */
        uint32_t rsvd5:28;
        uint32_t pad_len:2;
        uint32_t desc_invalid:1;
        uint32_t link:1;
};

/* statistics */
struct ifc_qdma_stats {
	uint64_t ifc_qdma_bad_ds;
	uint64_t ifc_qdma_zero_fifo_len;
	uint64_t ifc_qdma_no_queue_space;
	uint64_t ifc_qdma_tx_load_fail;
	uint64_t ifc_qdma_rx_load_fail;
	uint64_t ifc_qdma_inv_len;
};

/**
 * QDMA device
 *
 * This structure contains QDMA context. Application needs to get
 * this structure by using API ifc_qdma_device_get
 * From application context, QDMA device would be differentiated by
 * BDF. User needs to pass the BDF to above specified API to get
 * the context.
 *
 * Once usage of device is completed, application must release the
 * context by ifc_qdma_device_put
 */
struct ifc_qdma_device;

/**
 * QDMA channel
 *
 * This structure contains QDMA channel context.
 * User need to pass device context to driver to get the available channel
 * driver will maintain the channels usage in device context and
 * returns the available channel based on the demand.
 * Application need to ass this context to send/receive the data to MQDMA IP
 *
 * Once usage of device is completed, application must release the
 * context by ifc_qdma_channel_put
 */
struct ifc_qdma_channel;

/**
 * ifc_app_start - start application and prepare environment
 *
 * Returns 0, on success. negative otherwise
 */
int ifc_app_start(const char *bdf, uint32_t buf_size);

/**
 * ifc_qdma_device_get - Initilizes the device context
 * @port: port number of corresponding device
 * @qdev: Pointer to update device context
 * @page_desc: #descriptors per page
 *
 * Application should use this device context for all channel
 * management operation like channel get/release and device
 * specific operations
 * Once the usage is completed, Application must release the
 * device context by using ifc_qdma_device_put API
 *
 * Returns 0, on success. negative otherwise
 *
 */
int
ifc_qdma_device_get(int port, struct ifc_qdma_device **qdev,
		    uint32_t page_desc, int completion_mode);


int ifc_mcdma_dev_start(struct ifc_qdma_device  *qdev);


/**
 * ifc_num_channels_get - get number of channels supported
 * @qdev: QDMA device
 *
 * Returns number of channels supported in the device, 0 if none.
 */
int ifc_num_channels_get(struct ifc_qdma_device *qdev);

/**
 * ifc_qdma_release_all_channels - Release all channels
 * @qdev: QDMA device
 *
 * Application must make sure to stop the traffic before
 * calling this API
 *
 * Returns number of channels acquired in the device, 0 if none.
 */
int ifc_qdma_release_all_channels(struct ifc_qdma_device *qdev);


/**
 * ifc_qdma_acquire_channels - Acquires number of channels from HW
 * @qdev: QDMA device
 * @num_chnls: channels requesting
 *
 * Returns number of channels acquired in the device, 0 if none.
 */
int ifc_qdma_acquire_channels(struct ifc_qdma_device *qdev,
                              int num_chnls);

/**
 * ifc_num_channels_get - get the available channel context
 * @qdev: QDMA device
 * @chnl: Pointer to update channel context
 * @chno: Channel no if user wants specific channel. -1 if no specific
 * @dir : Direction
 * 	  Allowed values:
 *		IFC_QDMA_DIRECTION_RX
 *		IFC_QDMA_DIRECTION_TX
 *		IFC_QDMA_DIRECTION_BOTH
 *
 * Application should use the channel context for further DMA
 * operations. Once the usage is completed, Application must
 * release the channel context by using ifc_qdma_channel_put API
 *
 * Returns 0, on success. negative otherwise.
 */
int ifc_qdma_channel_get(struct ifc_qdma_device *qdev,
			struct ifc_qdma_channel **chnl,
			int chno, int dir);


/*
 * ifc_qdma_completion_poll - poll for request processing completions
 * @qchnl: channel context
 * @dir: DMA direction, one of IFC_QDMA_DIRECTION_*
 * @pkts: address where completed requests to be copied
 * @quota: maximum number of requests to search
 *
 * Poll and check if there is any previously queued and pending
 * request got completed. Based on configured descriptor completion
 * mechanis, this API will polls PCIe register or host memory
 * based on coniguration at init time
 *
 * Returns number of completed requests, on success.
 * 	   0, if none
 * 	   negative otherwise.
 */
int ifc_qdma_completion_poll(struct ifc_qdma_channel *qchnl, int dir,
			    void **pkts, uint32_t quota);

/*
 * ifc_qdma_request_start - queue a DMA request for processing
 * @qchnl: channel context received on ifc_qchannel_get()
 * @dir: DMA direction, one of IFC_QDMA_DIRECTION_*
 * @r: request struct that needs to be processed
 *
 * Depending on data direction, one of the descriptor ring pushes
 * the request to DMA engine to be processed.
 *
 * @returns 0, on success. negative otherwise.
 */
int ifc_qdma_request_start(struct ifc_qdma_channel *qchnl, int dir,
			  struct ifc_qdma_request *r);

/**
 * ifc_qdma_pio_read64 - Read the value from BAR2 address
 * @qdev: Device context returned by ifc_qdma_device_get
 * @addr: Address to read
 *
 * @returns value at address in BAR2
 */
uint64_t ifc_qdma_pio_read64(struct ifc_qdma_device *qdev, uint64_t addr);
uint32_t ifc_qdma_pio_read32(struct ifc_qdma_device *qdev, uint32_t addr);
uint64_t ifc_qdma_read64(struct ifc_qdma_device *qdev,
			 uint64_t addr, int bar_num);
uint32_t ifc_qdma_read32(struct ifc_qdma_device *qdev,
			 uint32_t addr, int bar_num);
#ifndef IFC_32BIT_SUPPORT
__uint128_t ifc_qdma_pio_read128(struct ifc_qdma_device *qdev,
				 uint64_t addr, int bar_num);
#endif
/**
 * ifc_qdma_pio_write64 - Read the value from BAR2 address
 * @qdev: Device context returned by ifc_qdma_device_get
 * @addr: Address to write
 * @val: Value to write
 *
 * @Returns: void
 */
void ifc_qdma_pio_write32(struct ifc_qdma_device *qdev, uint32_t addr,
			 uint32_t val);
void ifc_qdma_pio_write64(struct ifc_qdma_device *qdev, uint64_t addr,
			 uint64_t val);
void ifc_qdma_write32(struct ifc_qdma_device *qdev, uint32_t addr,
			 uint32_t val, int bar_num);
void ifc_qdma_write64(struct ifc_qdma_device *qdev, uint64_t addr,
			 uint64_t val, int bar_num);
#ifndef IFC_32BIT_SUPPORT
void ifc_qdma_pio_write128(struct ifc_qdma_device *qdev, uint64_t addr,
                         __uint128_t val, int bar_num);

#endif
int
ifc_qdma_pio_read256(struct ifc_qdma_device *qdev, uint64_t offset,
		     uint64_t *buf, int bar_num);
int ifc_qdma_pio_write256(struct ifc_qdma_device *qdev, uint64_t offset,
			  uint64_t *value, int bar_num);
/*
 * ifc_qdma_request_prepare - prepare a DMA request for processing
 * @qchnl: channel context received on ifc_qchannel_get()
 * @dir: DMA direction, one of IFC_QDMA_DIRECTION_*
 * @r: request struct that needs to be processed
 *
 * Depending on data direction, one of the descriptor ring loads
 * the request to DMA engine to be processed.
 * Please note, this do not submits the transactions. It populates
 * descriptor and update the tail in queue context
 *
 * @returns 0, on success. negative otherwise.
 * 	   -1, if no space in queue
 * 	   -2, invalid length
 */
int ifc_qdma_request_prepare(struct ifc_qdma_channel *qchnl, int dir,
			  struct ifc_qdma_request *r);

/*
 * ifc_qdma_request_submit - submit a DMA request for processing
 * @qchnl: channel context received on ifc_qchannel_get()
 * @dir: DMA direction, one of IFC_QDMA_DIRECTION_*
 *
 * Depending on data direction, DMA transactions would get submitted
 * to DMA engine. Application must use ifc_qdma_request_prepare to load
 * the descriptor before calling this function
 * Please note, this is not blocking request. completion of DMA transaction
 * would be notified by configured mechanism in descriptor
 *
 * @returns 0, on success. negative otherwise.
 */
int ifc_qdma_request_submit(struct ifc_qdma_channel *qchnl, int dir);

/**
 * ifc_num_channels_put - release the acquired channel
 * @qchnl: QDMA channel context returned by
 * 	   ifc_qdma_channel_get
 * @dir:   Direction
 * 	     Allowed values:
 *		IFC_QDMA_DIRECTION_RX
 *		IFC_QDMA_DIRECTION_TX
 *		IFC_QDMA_DIRECTION_BOTH
 */
void ifc_qdma_channel_put(struct ifc_qdma_channel *qchnl, int dir);

/**
 * ifc_num_channels_reset - release the acquired channel
 * @qchnl: QDMA channel context returned by
 * 	   ifc_qdma_channel_get
 * @dir:   Direction
 * 	     Allowed values:
 *		IFC_QDMA_DIRECTION_RX
 *		IFC_QDMA_DIRECTION_TX
 *		IFC_QDMA_DIRECTION_BOTH
 */
int ifc_qdma_channel_reset(struct ifc_qdma_device *qdev,
			    struct ifc_qdma_channel *qchnl,
			    int dir);

/**
 * ifc_qdma_device_put - release the acquired device
 * @qdev: QDMA device context returned by
 * 	  ifc_qdma_device_get
 */
void ifc_qdma_device_put(struct ifc_qdma_device *qdev);

/**
 * ifc_request_malloc - allocate buffer for I/O request
 * @len - size of data buffer for I/O request
 *
 * Application should allocate buffer and request structure
 * with this API or similar variants. Please note, returned
 * buffer would be DMA-able.
 *
 * @Returns pointer to ifc_qdma_request on success.
 * 	    NULL incase of fails
 */
struct ifc_qdma_request *ifc_request_malloc(size_t len);

/**
 * ifc_request_free - release the passed buffer and add
 * in free pool
 * @req - start address of allocation buffer
 *
 * Once, DMA transaction is completed, user must release the
 * buffer by this API
 */
void ifc_request_free(void *req);

/**
 * ifc_app_stop - stop application and release allocated resources
 */
void ifc_app_stop(void);


int ifc_qdma_get_avail_channel_count(struct ifc_qdma_device *qdev);

/**
 * ifc_qdma_poll_init - initilizes the polling instance
 * to be used in case MSIX interrupts are enabled
 * @qdev - device context returned by ifc_qdma_device_get
 *
 * Application should use the returned context to add the
 * 	queues to get monitored
 *
 * Returns, pointer to polling context on success
 * 	    NULL, in case of failes
 */
void* ifc_qdma_poll_init(struct ifc_qdma_device *qdev);

/**
 * ifc_qdma_poll_add - adds event descriptor to polling context
 * @chnl: channel context returned by ifc_qdma_channel_get
 * @dir: Direction to monitor for interrupts
 * @ctx: polling context returned by ifc_qdma_poll_init
 *
 * @returns 0, on success. negative otherwise.
 */
int ifc_qdma_poll_add( struct ifc_qdma_channel *chnl,
		      int dir,
		      void *ctx);

int ifc_mcdma_poll_add_event(struct ifc_qdma_device *qdev,
			     int dir,
			     void *ctx);

/**
 * ifc_qdma_poll_add - wait for interrupt for specied number
 * of micro seconds in blocking mode
 * @ctx: polling context returned by ifc_qdma_poll_init
 * @timeout: blocking time to wait for interrupts
 * @chnl: Pointer where interrupted chnl
 * 	  would be returned
 * @dir: Pointer where interrupted direction
 * 	 would be returned
 *
 * @returns 0, on success. negative otherwise
 */
int ifc_qdma_poll_wait(void *ctx,
		       int timeout,
		       struct ifc_qdma_channel **chnl,
		       int *dir);

int ifc_mcdma_poll_wait_for_intr(void *ctx,
		       int timeout,
		       int *dir);

/**
 * umsix_irq_handler - adds event descriptor to polling context
 * @ch_id: channel ID on which interrupt occured
 * @dir: Direction of queue
 * @user_irq_handler: pointer to data_handler
 * @errinfo: Error reported by hardware
 *
 * @returns 0, on success. negative otherwise.
 */
typedef int (*umsix_irq_handler)(struct ifc_qdma_device *qdev,
				 struct ifc_qdma_channel *chnl,
				 int dir,
				 void *data,
				 int *errinfo);

/**
 * ifc_qdma_add_irq_handler - adds event descriptor to polling context
 * @chnl: channel context returned by ifc_qdma_channel_get
 * @dir: Direction to monitor for interrupts
 * @user_irq_handler: pointer to user msix interrupt handler function.
 * @data : private data for the irq_handler
 *
 */
void ifc_qdma_add_irq_handler( struct ifc_qdma_channel *chnl,
		      int dir,
		      umsix_irq_handler irq_handler,
		      void *data);

/**
 * ifc_mcdma_port_by_name - Return the port number corresponing BDF
 */
int ifc_mcdma_port_by_name(const char* bdf);

/**
 * ifc_qdma_dump_stats - Dump QDMA statistics
 * @qdev - device context returned by ifc_qdma_device_get
 */
#ifdef IFC_DEBUG_STATS
void ifc_qdma_dump_stats(struct ifc_qdma_device *qdev);
void ifc_qdma_print_stats(struct ifc_qdma_channel *c, int dir);
#else
#define ifc_qdma_dump_stats(a)                  do {} while (0)
#define ifc_qdma_print_stats(a, b)                  do {} while (0)
#endif

/**
 * ifc_mcdma_get_drop_count - return data drop count for queue.
 * @chnl: channel context returned by ifc_qdma_channel_get
 * @dir: Direction of queue
 */
int ifc_mcdma_get_drop_count(struct ifc_qdma_channel *c, int dir);

/*
 * ifc_smp_barrier - Adds CPU level barrier
 * Instructs the CPU to ensure true ordering so that CPU 
 * will wait till read/write IO before using the
 * data
 */
void ifc_smp_barrier(void);

/**
 * ifc_allocated_chunks - Get total number of DMA buffer chunks allocated
 * @Returns total allocated chunks
 */
int ifc_allocated_chunks(void);

void ifc_qdma_channel_block(struct ifc_qdma_channel *qchnl);
void ifc_qdma_channel_unblock(struct ifc_qdma_channel *qchnl);
int ifc_qdma_descq_queue_batch_load(struct ifc_qdma_channel *qchnl,
				   void *req_buf,
				   int dir,
				   int n);
void qdma_hexdump(FILE *f, unsigned char *base, int len);
#ifdef IFC_MCDMA_EXTERNL_DESC
int ifc_qdma_dbg_ext_fetch_chnl_qcsr(struct ifc_qdma_device *qdev,
			    int dir, int qid);
#endif

int get_num_ports(void);

#ifndef UIO_SUPPORT
int set_num_channels(int channel_count);
#endif

#endif /* _IFC_LIBMQDMA_H_ */
