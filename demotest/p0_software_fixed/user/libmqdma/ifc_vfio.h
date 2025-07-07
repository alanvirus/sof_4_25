// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _IFC_VFIO_H_
#define _IFC_VFIO_H_

#include <linux/limits.h>
#include <linux/vfio.h>
#include <ifc_reglib_osdep.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include "ifc_env.h"
#include "ifc_mcdma.h"
#include "ifc_qdma_utils.h"
#include "ifc_libmqdma.h"
#include "stdint.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define VFIO_PATH_FMT    "/sys/class/vfio"
#define SYSFS_PCI_DIR "/sys/bus/pci/devices"
#define SYSFS_VFIO_GROUP_DIR_FMT    "/sys/kernel/iommu_groups/%s/devices/"
#define SYSFS_VFIO_DEVICE_FMT    "/sys/kernel/iommu_groups/%s/devices/%s"

#define IFC_VFIO_MAX_GROUPS		16

#define IFC_MAX_RXTX_INTR_VEC_ID	2048
#define IFC_QDMA_IRQ_SET_BUF_LEN (sizeof(struct vfio_irq_set) + \
                              sizeof(int) * (IFC_MAX_RXTX_INTR_VEC_ID + 1))
#define IRQ_SET_BUF_LEN  (sizeof(struct vfio_irq_set) + sizeof(int))

struct ifc_intr_handle {
	int vfio_dev_fd;
	int fd;
	uint32_t max_intr;
	uint32_t nb_efd;               /**< number of available efd(event fd) */
	uint8_t efd_counter_size;
	int efds[IFC_MAX_RXTX_INTR_VEC_ID];
};

struct ifc_vfio_group {
        int group_num;
        int group_fd;
        int num_devices;
};

struct ifc_vfio_config {
        int container_fd;
	int active_group_cnt;
        struct ifc_vfio_group vfio_groups[IFC_VFIO_MAX_GROUPS];
};

int
ifc_vfio_get_group_num(const char *dev_addr, int *iommu_group_num);

int
ifc_vfio_setup_device(const char *dev_addr, struct ifc_pci_device *pdev, int chnls);

int ifc_vfio_release_device(const char *dev_addr, struct ifc_pci_device *pdev);

int ifc_vfio_init(const char *bdf, int chnls);

#endif
