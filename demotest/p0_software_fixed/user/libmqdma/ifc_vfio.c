// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#include "ifc_vfio.h"
#include <dirent.h>
#include <unistd.h>
#include <ifc_mqdma.h>
#include <linux/pci_regs.h>
#include <sys/eventfd.h>

struct ifc_vfio_config vfio_cfg;
struct ifc_intr_handle ifc_intr_handler;
static enum ifc_config_qdma_cmpl_proc config_qdma_cmpl_proc =
		IFC_CONFIG_QDMA_COMPL_PROC;

static int
ifc_pci_vfio_get_num_intr(int vfio_dev_fd)
{
	int rc;
	int msix_irq_info_count;
	struct vfio_irq_info msix_irq_info = { .argsz = sizeof(msix_irq_info) };

	/* populate IRQ vector index */
	msix_irq_info.index = VFIO_PCI_MSIX_IRQ_INDEX;

	if (vfio_dev_fd < 0) {
                printf("Invalid vfio dev fd = %d\n",vfio_dev_fd);
                return vfio_dev_fd;
        }
	/* Get MSIX info */
        rc = ioctl(vfio_dev_fd, VFIO_DEVICE_GET_IRQ_INFO, &msix_irq_info);
	if (rc < 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
				" could not  retrieve MSIX IRQ info, "
				"error %i (%s)\n", errno, strerror(errno));
		return -1;
	}
	/*Update the msix irq num support from EP device*/
	msix_irq_info_count = msix_irq_info.count;
        
	if (msix_irq_info_count > IFC_MAX_RXTX_INTR_VEC_ID) {
                ifc_intr_handler.max_intr = IFC_MAX_RXTX_INTR_VEC_ID;
                return -ERANGE;
        } else {
                ifc_intr_handler.max_intr = msix_irq_info_count;
        }
        return msix_irq_info_count;
}
/*Initialize the msix interrupt*/
static int irq_init(int vfio_dev_fd)
{
        char irq_set_buf[IFC_QDMA_IRQ_SET_BUF_LEN];
        struct vfio_irq_set *irq_set;
        int len, rc, max_intr;
        int32_t *fd_ptr;
        uint32_t i;
        
        struct vfio_irq_info msix_irq_info = { .argsz = sizeof(msix_irq_info) };
        
        /*Get the max interrupt support */
        max_intr = ifc_intr_handler.max_intr;
        if (max_intr > IFC_MAX_RXTX_INTR_VEC_ID) {
        	return -ERANGE;
        }
        len = sizeof(struct vfio_irq_set) + ((sizeof(int32_t)) * (max_intr));
       	irq_set = (struct vfio_irq_set *)irq_set_buf;
        irq_set->argsz = len;
        irq_set->start = 0;
        irq_set->count = max_intr;
        irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
        irq_set->index = VFIO_PCI_MSIX_IRQ_INDEX;

        fd_ptr = (int32_t *)&irq_set->data[0];
        if (fd_ptr != NULL) {
                for (i = 0; i < irq_set->count; i++) {
                        fd_ptr[i] = -1;
                }
        }
        if (vfio_dev_fd < 0) {
                printf("Invalid vfio_dev_fd= %d\n",vfio_dev_fd);
                return vfio_dev_fd;
        }
        rc = ioctl(vfio_dev_fd, VFIO_DEVICE_SET_IRQS, irq_set);
        return rc;
}

#if 0
static int
ifc_intr_efd_enable(struct ifc_intr_handle *intr_handle, int vfio_msix_count)
{
	int i;
	int fd;

	for (i = 0; i < vfio_msix_count; i++) {
		fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
		if (fd < 0) {
			return -errno;
		}
		intr_handle->efds[i] = fd;
	}
	return 0;
}
#endif

static int
ifc_vfio_enable_msix(int vfio_fd, int num_intr)
{
	int len, ret = 0;
        char irq_set_buf[IFC_QDMA_IRQ_SET_BUF_LEN];
        struct vfio_irq_set *irq_set;
        int *fd_ptr;
        int  vfio_msix_max_cnt;
        int fd, vector;

	 /*Get the irq info from device.*/
	 ifc_pci_vfio_get_num_intr(vfio_fd);
	 /*Init the msix irq */
	 ret = irq_init(vfio_fd);
	 if (ret != 0) {
	        IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,"vfio msix irq init failed %d\n",ret);
	        return -1;
         }
         /*Get the max interrupt support */
	 vfio_msix_max_cnt = ifc_intr_handler.max_intr;
	 /*check the num of interrupt based on channel input and  device supported interrupt */
	 if ( num_intr > vfio_msix_max_cnt) {
                printf("input num_intr is greater than supported vfio_msix_count\n");
                return -ERANGE;
         }

	for (vector = 0; vector < num_intr; vector++) {
                /* Create new eventfd for vector*/
                fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
                if (fd == -1) {
                        IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR, "Error while enablig eventfds ret:%d\n",ret);
                        return -1;
                }
		/*Set the  event fd*/
		ifc_intr_handler.fd = fd;
		/*set the intr efd index*/
                ifc_intr_handler.efds[vector] = fd;

                /*number of available efd(event fd)*/
                ifc_intr_handler.nb_efd = vector + 1;

                /* populate IRQ info*/
                len = sizeof(struct vfio_irq_set) + sizeof(int32_t);
                irq_set = (struct vfio_irq_set *) irq_set_buf;
                irq_set->argsz = len;
                irq_set->start = vector;
                irq_set->count = 1;
                irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
                irq_set->index = VFIO_PCI_MSIX_IRQ_INDEX;

                fd_ptr = (int32_t *) &irq_set->data[0];
                if (fd_ptr != NULL) {
                        fd_ptr[0] = ifc_intr_handler.efds[vector];
                }
		ret = ioctl(vfio_fd, VFIO_DEVICE_SET_IRQS, irq_set);
                if (ret) {
                     IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
                          "Error enabling interrupts %s %d\n",
                            strerror(errno), ret);
                     return -1;
                 }		 
	} 
	return 0;       
}

#if 0
static int
ifc_pci_vfio_set_bus_master(int dev_fd, int op)
{
	struct vfio_region_info reg_info = { .argsz = sizeof(reg_info) };
	uint16_t reg;
	int ret;

	reg_info.index = VFIO_PCI_CONFIG_REGION_INDEX;

	if (ioctl(dev_fd, VFIO_DEVICE_GET_REGION_INFO, &reg_info) == -1) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			    "Failed to retrieve region info %u\n",
			    VFIO_DEVICE_GET_REGION_INFO);
		return -1;
	}

	ret = pread(dev_fd, &reg, sizeof(reg),
			reg_info.offset + PCI_COMMAND);
	if (ret != sizeof(reg)) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
				"Cannot read command from PCI config space!\n");
		return -1;
	}

	reg = op ? (reg | PCI_COMMAND_MASTER) : (reg & ~(PCI_COMMAND_MASTER));

	ret = pwrite(dev_fd, &reg, sizeof(reg),
		    reg_info.offset + PCI_COMMAND);

	if (ret != sizeof(reg)) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
				"Cannot write command to PCI config space!\n");
		return -1;
	}
	return 0;
}
#endif

int
ifc_vfio_get_group_num(__attribute__((unused)) const char *dev_addr,
		      int *iommu_group_num)
{
	char linkname[PATH_MAX];
	char filename[PATH_MAX];
	char *tok[16], *group_tok, *end;
	int ret;

	memset(linkname, 0, sizeof(linkname));
	memset(filename, 0, sizeof(filename));

	/* try to find out IOMMU group for this device */
	snprintf(linkname, sizeof(linkname),
			 "%s/%s/iommu_group", SYSFS_PCI_DIR, dev_addr);

	ret = readlink(linkname, filename, sizeof(filename));
	if (ret < 0)
		return 0;

	ret = ifc_qdma_strsplit(filename, sizeof(filename),
			tok, ARRAY_SIZE(tok), '/');

	if (ret <= 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    "  %s cannot get IOMMU group\n", dev_addr);
		return -1;
	}

	/* IOMMU group is always the last token */
	errno = 0;
	group_tok = tok[ret - 1];
	end = group_tok;
	*iommu_group_num = strtol(group_tok, &end, 10);
	if ((end != group_tok && *end != '\0') || errno != 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    "  %s error parsing IOMMU number!\n", dev_addr);
		return -1;
	}
	return 1;
}

static int
ifc_get_new_group_idx(struct ifc_vfio_config *vfiocfg)
{
	int i;

	for (i = 0; i < IFC_VFIO_MAX_GROUPS; i++) {
		if (vfiocfg->vfio_groups[i].group_num == 0)
			return i;
	}
	return -1;
}

static int
ifc_get_vfio_group_idx(int group_id)
{
	int i;

	for (i = 0; i < IFC_VFIO_MAX_GROUPS; i++) {
		if (vfio_cfg.vfio_groups[i].group_num == group_id)
			return i;
	}
	return -1;
}

static int
ifc_vfio_open_group_fd(int iommu_group_num)
{
	int group_fd;
	char filename[PATH_MAX];

	/* Open the group */
	snprintf(filename, sizeof(filename), "/dev/vfio/%u", iommu_group_num);
	group_fd = ifc_qdma_open(filename, O_RDWR);
	if (group_fd < 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    "open failed:iommu_groupno:%u error:%s\n",
			    iommu_group_num, strerror(errno));
		snprintf(filename, sizeof(filename),
				"/dev/vfio/noiommu-%u",
				iommu_group_num);
		group_fd = ifc_qdma_open(filename, O_RDWR);
		if (group_fd < 0) {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
				    "open failed:group_num:%u error:%s\n",
				    iommu_group_num, strerror(errno));
			return -1;
		}
	}
	return group_fd;
}

static int
ifc_vfio_get_group_fd(int iommu_group_num)
{
	int group_fd;
	int idx;

	idx = ifc_get_vfio_group_idx(iommu_group_num);
	if (idx >= 0)
		/* group already present */
		return vfio_cfg.vfio_groups[idx].group_fd;

	/* group not present. create new group */
	idx = ifc_get_new_group_idx(&vfio_cfg);
	if(idx < 0)
		return -1;

	group_fd = ifc_vfio_open_group_fd(iommu_group_num);
	if (group_fd < 0)
		return -1;

	vfio_cfg.vfio_groups[idx].group_num = iommu_group_num;
	vfio_cfg.vfio_groups[idx].group_fd = group_fd;
	vfio_cfg.active_group_cnt++;
	return group_fd;
}

static int
ifc_vfio_create_container(struct ifc_vfio_config *vfio_config)
{
	int container;

	if (vfio_config->container_fd) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			    "container already created. id:%u\n",
			    vfio_config->container_fd);
		return vfio_config->container_fd;
	}

	/* Create a new container */
	container = ifc_qdma_open("/dev/vfio/vfio", O_RDWR);
	if(container < 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    "Unable to open vfio device\n");
		return -1;
	}

	if (ioctl(container, VFIO_GET_API_VERSION) != VFIO_API_VERSION) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    "Unknown VFIO API version\n");
		close(container);
		return -1;
	}

	if (!ioctl(container, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU)) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    "Doesn't support the IOMMU driver we want\n");
		close(container);
		return -1;
	}

	vfio_config->container_fd = container;
	return container;
}

static int
ifc_vfio_dma_map_to_iommu(struct ifc_vfio_config *vfio_config)
{
	struct vfio_iommu_type1_dma_map dma_map = {
				.argsz = sizeof(dma_map) };
	int i = 0;

	for (i = 0; i < env_ctx.nr_hugepages; i++) {
		/* Allocate some space and setup a DMA mapping */
		dma_map.vaddr = (uint64_t)env_ctx.hp[i].virt;
		dma_map.size = env_ctx.hp[i].size;
		dma_map.iova = (uint64_t)env_ctx.hp[i].iova;
		dma_map.flags = VFIO_DMA_MAP_FLAG_READ |
				VFIO_DMA_MAP_FLAG_WRITE;

		if (ioctl(vfio_config->container_fd, VFIO_IOMMU_MAP_DMA,
			 &dma_map) == -1) {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_INFO,
				    "mmap vaddr:0x%lx iova:0x%lx size:0x%lx\n",
				    (uint64_t)dma_map.vaddr,
				    (uint64_t)dma_map.iova,
				    (uint64_t)dma_map.size);
		} else {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
				    "mmap vaddr:0x%lx iova:0x%lx size:0x%lx\n",
				    (uint64_t)dma_map.vaddr,
				    (uint64_t)dma_map.iova,
				    (uint64_t)dma_map.size);
		}
	}
	return 0;
}


static int
ifc_vfio_map_regions(int device, struct ifc_pci_device *pdev,
		    struct vfio_device_info *device_info)
{
	uint32_t i = 0;
	void *bar_addr = NULL;

	/* Test and setup the device */
	if (ioctl(device, VFIO_DEVICE_GET_INFO, device_info) == -1) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    " Getting device_info Failed %s\n",
			    strerror(errno));
		return -1;
	}

	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			"Number of regions: %u\n", device_info->num_regions);
	for (i = 0; i < device_info->num_regions; i++) {
		struct vfio_region_info reg = { .argsz = sizeof(reg) };

		reg.index = i;

		if (ioctl(device, VFIO_DEVICE_GET_REGION_INFO, &reg) == -1) {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
				    "Failed to retrieve region info %u\n", i);
			continue;
		}

		/* skip non-mmapable BARs */
		if ((reg.flags & VFIO_REGION_INFO_FLAG_MMAP) == 0)
			continue;

		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
				"Bar:%u size:0x%lx offset:0x%lx device:0x%x\n",
				i, (uint64_t)reg.size,
				(uint64_t)reg.offset, device);

		bar_addr = NULL;
		if (i == 0)
			reg.size = 0x100000;
		bar_addr = mmap(NULL, reg.size, PROT_READ | PROT_WRITE,
				MAP_SHARED, device, reg.offset);

		if (bar_addr == MAP_FAILED) {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
					"Mapping failed bar:%u errno:%s\n",
					i, strerror(errno));
		} else {
			pdev->r[i].len = reg.size;
			pdev->r[i].map = bar_addr;
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
					"Maping success bar:%u len:0x%lx va:%lu\n",
					i, (uint64_t)pdev->r[i].len,
					(uint64_t)pdev->r[i].map);
			if (i == 0) {
				reg.size = 0x200000;
				bar_addr = mmap(NULL, reg.size,
					       PROT_READ | PROT_WRITE,
					       MAP_SHARED, device, 0x200000);
				if (bar_addr == MAP_FAILED) {
					IFC_QDMA_LOG(IFC_QDMA_INIT,
						    IFC_QDMA_ERROR,
						    "mmap fail bar:%u err:%s\n",
						    i, strerror(errno));
				} else {
					IFC_QDMA_LOG(IFC_QDMA_INIT,
						    IFC_QDMA_DEBUG,
						    "GCSR mmap success bar:%u len:0x%lx va:%lu\n",
						    i,
						    (uint64_t)pdev->r[i].len,
						    (uint64_t)pdev->r[i].map);
				}
				pdev->region[IFC_GCSR_REGION].len = reg.size;
				pdev->region[IFC_GCSR_REGION].map = bar_addr;
			}
			pdev->num_bars++; 
		}
	}
	return 0;
}

int
ifc_vfio_setup_device(const char *dev_addr, struct ifc_pci_device *pdev, int chnls)
{
	int container, group_id, device, group_fd, ret;
	uint32_t group_idx;
	struct vfio_group_status group_status = {
				.argsz = sizeof(group_status) };
	struct vfio_iommu_type1_info iommu_info = {
				.argsz = sizeof(iommu_info) };
	struct vfio_iommu_type1_dma_map dma_map = {
				.argsz = sizeof(dma_map) };
	struct vfio_device_info device_info = {
				.argsz = sizeof(device_info) };

	/* get group number */
	ret = ifc_vfio_get_group_num(dev_addr, &group_id);
	if (ret == 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    "  %s not managed by VFIO driver, skipping\n",
			    dev_addr);
		return 1;
	}


	/* if negative, something failed */
	if (ret < 0)
		return -1;

	container = ifc_vfio_create_container(&vfio_cfg);

	group_fd = ifc_vfio_get_group_fd(group_id);
	if (group_fd <= 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    "opening group fd failed %s\n", dev_addr);
		return -1;
	}

	group_idx = ifc_get_vfio_group_idx(group_id);
	if (group_idx >=  IFC_VFIO_MAX_GROUPS) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    "invalid group index %s\n", dev_addr);
		return -1;
	}


	/* Test the group is viable and available */
	if (ioctl(group_fd, VFIO_GROUP_GET_STATUS, &group_status) == -1) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    "Getting Group status failed %s %s\n",
			    dev_addr, strerror(errno));
		return -1;
	}

	if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    "Group is not viable %s\n",
			    dev_addr);
		return -1;
	}

	if (!(group_status.flags & VFIO_GROUP_FLAGS_CONTAINER_SET)) {

		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			    "processing new group %s %u %u\n",
			    dev_addr, group_fd, errno);

		/* Add the group to the container */
		if (ioctl(group_fd, VFIO_GROUP_SET_CONTAINER,
		   	 &container) == -1) {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
				    "cant add group to containder %s %s %u %u\n",
				    dev_addr, strerror(errno), group_fd, errno);
			return -1;
		}
		vfio_cfg.vfio_groups[group_idx].num_devices++;

		if (vfio_cfg.active_group_cnt == 1) {
			/* Enable the IOMMU model we want */
			if (ioctl(container, VFIO_SET_IOMMU,
						VFIO_TYPE1_IOMMU) == -1) {
				IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
						"Enable the IOMMU model failed %s %s",
						dev_addr, strerror(errno));
				return -1;
			}
		}

		/* Get addition IOMMU info */
		if (ioctl(container, VFIO_IOMMU_GET_INFO, &iommu_info) == -1) {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
				    " Get addition IOMMU info Failed %s\n",
				    dev_addr);
			return -1;
		}

		ifc_vfio_dma_map_to_iommu(&vfio_cfg);
	} else {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			    "Group existing: not setting to container %u\n",
			    group_id);
	}

	/* Get a file descriptor for the device */
	device = ioctl(group_fd, VFIO_GROUP_GET_DEVICE_FD, dev_addr);
	if (device < 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_INFO,
			    "Failed to get descriptor info %s %s\n",
			    dev_addr, strerror(errno));
		return -1;
	}

	/* updated device FD in config */
	pdev->uio_fd = device;
	vfio_cfg.vfio_groups[group_idx].num_devices++;

	/* Map regions */
	if (ifc_vfio_map_regions(device, pdev, &device_info) < 0)
		return -1;

	/* Enable MSIx */
	if (config_qdma_cmpl_proc == CONFIG_QDMA_QUEUE_MSIX) {
		/*Each channel supports 4 irqs*/
		if (ifc_vfio_enable_msix(device, chnls*4)) {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
					"Enabling MSIX failed\n");
			return -1;
		}
	}

        char top_dev[32];
        char cmd[512];
        sprintf(top_dev, "%s", dev_addr);
        top_dev[strlen(dev_addr)-1] = '0';
        sprintf(cmd, "setpci -s %s 4.w=$(printf %%x $((0x$(setpci -s %s 4.w)|6)))", top_dev, top_dev);
        if (system(cmd) == -1) {
		return -1;
	}
#if 0
	/* set bus mastering for the device */
	if (ifc_pci_vfio_set_bus_master(device, 1)) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    "Cannot set up device as bus maste\n");
		return -1;
	}
	//The below checker is removed as its support is removed from the latest BS 
	/* Gratuitous device reset and go... */
	if(ioctl(device, VFIO_DEVICE_RESET) < 0){
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,"Failed to reset VFIO device\n");
		return -1;
	}
#endif

	return 0;
}

int ifc_vfio_release_device(const char *dev_addr,
			    struct ifc_pci_device *pdev)
{
	struct vfio_group_status group_status = {
			.argsz = sizeof(group_status)
	};
	int vfio_group_fd, group_idx;
	int iommu_group_num;
	int ret;

	/* get group number */
	ret = ifc_vfio_get_group_num(dev_addr, &iommu_group_num);
	if (ret <= 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    "  %s not managed by VFIO driver, skipping\n",
			    dev_addr);
		ret = -1;
		goto out;
	}

	group_idx = ifc_get_vfio_group_idx(iommu_group_num);
	if (group_idx < 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    "  %s Retrieving group idx failed\n",
			    dev_addr);
		ret = -1;
		goto out;
	}

	/* get the actual group fd */
	vfio_group_fd = vfio_cfg.vfio_groups[group_idx].group_fd;
	if (vfio_group_fd <= 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    "ifc_vfio_get_group_fd failed for %s\n",
			    dev_addr);
		ret = -1;
		goto out;
	}

	/* Closing a device */
	if (close(pdev->uio_fd) < 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    "Error when closing vfio_dev_fd for %s\n",
			    dev_addr);
		ret = -1;
		goto out;
	}

	vfio_cfg.vfio_groups[group_idx].num_devices--;
	if (vfio_cfg.vfio_groups[group_idx].num_devices == 0) {
		if (close(vfio_group_fd) < 0) {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
				    "Error when closing vfio_group_fd for %s\n",
				    dev_addr);
			ret = -1;
			goto out;
		}
	}
	ret = 0;

out:
	return ret;
}

int ifc_vfio_init(const char *bdf, int chnls)
{
	struct dirent *vfio_e, *group_e;
	char	group_sysfs_path[PATH_MAX];
	char	device_sysfs_path[PATH_MAX];
	DIR	*vfio_dir, *group_dir;
	int 	group_id, dev_group_id;
	uint32_t ret;
	int	didx,i;

	/* Initilize env lock */
	if (pthread_mutex_init(&env_ctx.env_lock, NULL) != 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    "Mutex init failed\n");
		return -ENOLCK;
	}

	/* get group number */
	ret = ifc_vfio_get_group_num(bdf, &group_id);
	if (ret == 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			    "  %s not managed by VFIO driver, skipping\n",
			    bdf);
		return 1;
	}

	if (probe_and_map_hugepage()) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "can't map hugepage\n");
		return -EBUSY;
	}
	env_ctx.ctx_base = env_ctx.hugepage + IFC_DESC_MEM;
	env_ctx.last_page = env_ctx.hugepage + IFC_DESC_MEM;

	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_INFO,
		"allocated hugepage of sz %luG and mapped to %p va (%lx pa)\n",
		env_ctx.hugepage_sz >> 30,
		env_ctx.hugepage,
		mem_virt2phys(env_ctx.hugepage));

	vfio_dir = ifc_qdma_opendir(VFIO_PATH_FMT);
	if (!vfio_dir)
		return -EBADF;

	while ((vfio_e = readdir(vfio_dir))) {
		snprintf(group_sysfs_path, sizeof(group_sysfs_path),
			SYSFS_VFIO_GROUP_DIR_FMT, vfio_e->d_name);
		group_dir = ifc_qdma_opendir(group_sysfs_path);
		if (!group_dir) {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
				    "%s not found: skipping\n",
				    vfio_e->d_name);
			continue;
		}
		while ((group_e = (readdir(group_dir)))) {
			snprintf(device_sysfs_path, sizeof(device_sysfs_path),
				SYSFS_VFIO_DEVICE_FMT,
				vfio_e->d_name, group_e->d_name);

			/* get group number */
			ret = ifc_vfio_get_group_num(group_e->d_name, &dev_group_id);
			if (ret == 0) {
				IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_INFO,
						"  %s failed to get groupid , skipping\n",
						bdf);
				continue;
			}
			if (dev_group_id != group_id) {
				IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
						"  %s device id not matched: skipping\n",
						bdf);
				continue;
			}
			didx = env_ctx.nr_device;
			ret = ifc_vfio_setup_device(group_e->d_name,
						   &env_ctx.uio_devices[didx], chnls);
			if (ret) {
				IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_INFO,
					    "%s setup failed: continuing\n",
					    group_e->d_name);
				continue;
			}
			snprintf(env_ctx.uio_devices[didx].pci_slot_name,
				 sizeof(env_ctx.uio_devices[didx].pci_slot_name), "%s",
				  (char *)group_e->d_name);

			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
				    "%s setup success: continuing\n",
				    group_e->d_name);
			env_ctx.nr_device++;
		}
	}
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
		    "Number of devices identified are %u\n",
		    env_ctx.nr_device);
	if (env_ctx.nr_device == 0)
		return -1;
	/*This loop add all the available devices to total number of recognised device*/
	for(i = 0; i < env_ctx.nr_device; i++)
		env_ctx.uio_devices[i].uio_id = i;

	return 0;
}
