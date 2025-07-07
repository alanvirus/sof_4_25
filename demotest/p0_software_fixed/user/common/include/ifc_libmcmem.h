// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _IFC_LIBMCMEM_H_
#define _IFC_LIBMCMEM_H_

/*
 * Number of Huge pages to be allocated.
 * Tune this according to DMA buffer requirement
 */
#define NUM_HUGE_PAGES		10

/**
 * MCDMA I/O DMA buffer structure
 *
 * To allcate memory from the mcmem library, application must use
 * ifc_dma_alloc(). This API retuns ifc_mem_struct structure.
 * Application should use the physical address provided in the structure
 * to submit the buffer to the Kernel Driver..
 *
 * Once usage of the request structure is completed or over the application
 * must release the buffer by ifc_dma_free().
 **/
struct ifc_mem_struct {
	void *virt_addr;	/* Virtual address of start of the buffer */
	uint64_t phy_addr;	/* Physical address of start of the buffer */
	size_t len;		/* Length of the buffer */
	int ctx;		/* For library, not for application */
};

/**
 * ifc_env_init - Allocate & initialize the huge page memory
 * @bdf: BDF of the device
 * @payload: Size of each memory chunk 
 *
 * @Returns 0, on success. Negative otherwise
 */
int ifc_env_init(char *bdf, size_t payload);

/**
 * ifc_env_exit - Free all huge page memory 
 */
void ifc_env_exit(void);

/**
 * ifc_dma_alloc - allocate buffer for I/O request
 *
 * Application should allocate buffer and request structure
 * with this API. Please note, returned buffer would be DMA-able.
 *
 * @Returns pointer to ifc_mem_struct on success. NULL incase of fails
 */
struct ifc_mem_struct *ifc_dma_alloc(void);

/**
 * ifc_dma_free - release the passed buffer and add in free pool
 * @r - pointer to the ifc_mem_struct start provided with the buffer
 *
 * Once, DMA transaction is completed, user must release the
 * buffer by this API
 */
void ifc_dma_free(struct ifc_mem_struct *r);

/**
 * ifc_allocated_chunks - Get total number of DMA buffer chunks allocated
 * @Returns total allocated chunks
 */
int ifc_allocated_chunks(void);
#endif /* _IFC_LIBMCMEM_H_ */
