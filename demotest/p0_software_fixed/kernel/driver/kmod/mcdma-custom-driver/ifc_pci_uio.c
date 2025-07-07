// SPDX-License-Identifier: GPL-2.0
/*-
 * Copyright(c) 2019-20 Intel Corporation. All rights reserved.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/uio_driver.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/msi.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/eventfd.h>
#include <linux/rcupdate.h>
#include "./ifc_pci_uio.h"
#include "../../../common/include/mcdma_ip_params.h"


#ifndef PCI_VENDOR_ID_REDHAT_QUMRANET
#define PCI_VENDOR_ID_REDHAT_QUMRANET	0x1af4
#endif

#define MSIX_CAPACITY 2048
#define MSIX_INTR_CTX_BAR 2
#define MSIX_INTR_CTX_ADDR 0x0000
#define MSIX_CH_NO_MASK 0xFFF00000
#define MSIX_IRQFD_MASK 0xFFFFF
#define MSIX_DISABLE_INTR 0xFFFFE
#define MSIX_IRQFD_BITS 20

/* MSI related info */
#define MSI_CAPACITY 32
#define MSI_VEC_BITS 8
#define MSI_VEC_MASK 0xFF000000
#define MSI_IRQFD_MASK 0xFFFFFF
#define MSI_IRQFD_BITS 24 

#define DRV_VERSION     "1.0.0.2"
#define DRV_SUMMARY     "ifc_uio Intel(R) PCIe end point driver"
static const char ifc_uio_driver_version[] = DRV_VERSION;
static const char ifc_uio_driver_string[] = DRV_SUMMARY;
static const char ifc_uio_copyright[] = "Copyright (c) 2019-20, Intel Corporation.";

MODULE_AUTHOR("Intel Corporation, <linux.nics@intel.com>");
MODULE_DESCRIPTION(DRV_SUMMARY);
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

#ifdef IFC_QDMA_MSI_ENABLE
#define HAVE_ALLOC_IRQ_VECTORS
#endif

struct msix_intr_info {
        uint32_t valid;
        uint32_t efd;
        uint32_t msix_allocated;
};

struct msi_intr_info {
        uint32_t valid;
        uint32_t efd;
        uint32_t msi_allocated;
};

struct msix_info {
	u32 nvectors;
	u32 evectors; // Enabled vectors
	struct msix_entry *table;
	struct msix_intr_info msix_info[MSIX_CAPACITY];
	struct uio_msix_irq_ctx {
		struct eventfd_ctx *trigger;
		char *name;
	} *ctx;
};

struct msi_info {
	u32 nvectors;
	u32 evectors; // Enabled vectors
	struct msi_intr_info msi_info[MSI_CAPACITY];
	struct uio_msi_irq_ctx {
		struct eventfd_ctx *trigger;
		char *name;
	} *ctx;
};

/**
 * A structure describing the private information for a uio device.
 */
struct ifc_uio_pci_dev {
	struct uio_info info;
	struct pci_dev *pdev;
	atomic_t refcnt;
	struct msix_info msix;
	struct mutex msix_state_lock;
	u16    msix_en;
	u16    msi_en;


	/* MSI Info */
	struct msi_info msi;
};

#ifdef IFC_QDMA_MSIX_ENABLE
#ifdef IFC_QDMA_DYN_CHAN
static int ifc_uio_pci_reset_msix(struct ifc_uio_pci_dev *udev, int vectors);
#endif
static void ifc_uio_pci_disable_msix_interrupts(struct ifc_uio_pci_dev *udev);
#endif

#if defined(IFC_QDMA_MSI_ENABLE) || defined(IFC_QDMA_MSIX_ENABLE)
/*
 * Disable the IRQ once received the interrupt
 * user space responsible to acknowledge
 */
static irqreturn_t ifc_uio_irq_handler(int irq, void *arg)
{
	struct eventfd_ctx *trigger = arg;
	if (trigger)
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,7,12)
		eventfd_signal(trigger);
	#else
		eventfd_signal(trigger, 1);
	#endif
	return IRQ_HANDLED;
}
#endif

#ifdef IFC_QDMA_MSIX_ENABLE
/* set the mapping between vector # and existing eventfd. */
static int set_irq_eventfd(struct ifc_uio_pci_dev *udev, u32 vec, int efd)
{
	struct uio_msix_irq_ctx *ctx;
	struct eventfd_ctx *trigger;
	int irq, err;

	if (udev == NULL) {
		pr_err("udev is null\n");
		return -1;
	}

	if (vec >= udev->msix.nvectors) {
		pr_err("vec %u >= num_vec %u\n",
			vec, udev->msix.nvectors);
		return -ERANGE;
	}

	irq = udev->msix.table[vec].vector;

	ctx = &udev->msix.ctx[vec];

	if (ctx && ctx->trigger && udev->msix.msix_info[vec].msix_allocated) {
		free_irq(irq, ctx->trigger);
		eventfd_ctx_put(ctx->trigger);
	        udev->msix.msix_info[vec].msix_allocated = false;
		ctx->trigger = NULL;
	}

	if (efd < 0)
		return 0;

	trigger = eventfd_ctx_fdget(efd);
	if (IS_ERR(trigger)) {
		err = PTR_ERR(trigger);
		pr_err("eventfd ctx get failed: err:%d efd:%u\n", err, efd);
		return err;
	}

	err = request_irq(irq, ifc_uio_irq_handler, udev->info.irq_flags, ctx->name, trigger);
	if (err) {
		eventfd_ctx_put(trigger);
		return err;
	}
#ifdef __INTEL__DEBUG_CHK
	pr_debug("eventfd ctx registration done: efd:%u vec:%u irq:%u\n", efd, vec, irq);
#endif

	udev->msix.msix_info[vec].msix_allocated = true;
	ctx->trigger = trigger;

	return 0;
}
#endif

#ifdef IFC_QDMA_MSIX_ENABLE
#ifdef IFC_QDMA_DYN_CHAN
static int
ifc_uio_pci_dca_irqcontrol(struct uio_info *info, s32 irq_state)
{
        struct ifc_uio_pci_dev *udev = info->priv;
        uint32_t msix_num;
	int err = 0;
        int irqfd;
        int num_vec;

        msix_num = ((irq_state & MSIX_CH_NO_MASK) >> MSIX_IRQFD_BITS);
        irqfd = (irq_state & MSIX_IRQFD_MASK);

	if (irqfd == MSIX_DISABLE_INTR) {
		ifc_uio_pci_disable_msix_interrupts(udev);
	} else if (irqfd == MSIX_IRQFD_MASK) {
        	num_vec = ((irq_state >> 20) * 4);
                err = ifc_uio_pci_reset_msix(udev, num_vec);
                if (err < 0)
                        pr_err("msix enablement failed %u\n", err);
        } else {
                udev->msix.msix_info[msix_num].valid = 1;
                udev->msix.msix_info[msix_num].efd = irqfd;
                udev->msix.evectors++;
        }
        return err;
}
#endif
#endif

#ifdef IFC_QDMA_MSIX_ENABLE
#ifndef IFC_QDMA_DYN_CHAN
/* This function is used here for registering EVENTFD against
 * each MSIX vector.
 * When application acquiring channel, driver on
 * behalf of application creates the irqfd and shares channel number and
 * irqfd by writing to /dev/uio0 file. This write operation to /dev/uio0,
 * triggers this callback and it registers the msix number with
 * the passed irqfd.
 * UIO framework allows to write only 32 bits to /dev/uio0 file
 * So, most significant 12 bits, we are using for channel number
 * least significant 20 bits, we are using for irqfd
 */
static int ifc_uio_irq_irqcontrol(struct uio_info *dev_info, s32 irq_data)
{
        struct ifc_uio_pci_dev *udev;
        struct pci_dev *pdev;
	uint32_t msix_num;
	int irqfd;
	int err;

	if (dev_info == NULL) {
		pr_err("dev_info is NULL. Failed to register eventfd against vector\n");
		return -1;
	}

	udev = dev_info->priv;
	if (!udev->msix_en)
		return -1;
	pdev = udev->pdev;
	msix_num = ((irq_data & MSIX_CH_NO_MASK) >> 20);
	irqfd = (irq_data & MSIX_IRQFD_MASK);

	mutex_lock(&udev->msix_state_lock);
	err = set_irq_eventfd(udev, msix_num, (int)irqfd);
	if (err < 0)
		pr_err("msix registration failed %u\n",msix_num);
	mutex_unlock(&udev->msix_state_lock);

        return 0;
}
#endif

static int
ifc_uio_pci_enable_msix_interrupts(struct ifc_uio_pci_dev *udev, int vectors)
{
	int err = 0;
	int i = 0, nvectors;
	struct pci_dev *pdev = udev->pdev;
	udev->info.irq_flags = (IRQF_NO_THREAD | IRQF_NOBALANCING | IRQF_ONESHOT | IRQF_IRQPOLL);

	nvectors = pci_msix_vec_count(udev->pdev);
	if(nvectors < 0){
		pr_err("failed while enabling getting vectors\n");
		return -1;
	}

	udev->msix.nvectors = nvectors;
	udev->msix.table = kcalloc(vectors, sizeof(struct msix_entry),
				   GFP_KERNEL);
	if (!udev->msix.table) {
		pr_err("failed to allocate memory for MSI-X table");
		goto err_ctx_alloc;
	}

	udev->msix.ctx = kcalloc(vectors, sizeof(struct uio_msix_irq_ctx),
				 GFP_KERNEL);
	if (!udev->msix.ctx) {
		pr_err("failed to allocate memory for MSI-X contexts");
		goto err_ctx_alloc;
	}

	for (i = 0; i < vectors; i++) {
		udev->msix.table[i].entry = i;
		udev->msix.ctx[i].name = kasprintf(GFP_KERNEL,
						   KBUILD_MODNAME "[%d](%s)",
						   i, pci_name(pdev));
		if (!udev->msix.ctx[i].name)
			goto err_name_alloc;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
	if (pci_enable_msix(udev->pdev, udev->msix.table, vectors) != 0) {
#else
	if (pci_enable_msix_exact(udev->pdev, udev->msix.table, vectors) != 0) {
#endif
		udev->msix_en = 0;
		pr_err("failed while enabling interrupts %d\n",err);
		goto err_name_alloc;
	}
	udev->msix_en = 1;
	return 0;

err_name_alloc:
	for (i = 0; i < vectors; i++)
		kfree(udev->msix.ctx[i].name);

	kfree(udev->msix.ctx);
err_ctx_alloc:
	kfree(udev->msix.table);
	return -1;
}
#endif

#ifdef IFC_QDMA_MSI_ENABLE
/* set the mapping between vector # and existing eventfd. */
int static set_msi_irq_eventfd(struct ifc_uio_pci_dev *udev, u32 vec, int efd)
{
	struct uio_msi_irq_ctx *ctx;
	struct eventfd_ctx *trigger;
	int irq, err;

	if (udev == NULL) {
		pr_err("udev is null\n");
		return -1;
	}

	if (vec >= udev->msi.nvectors) {
		pr_err("vec %u >= num_vec %u\n",
			vec, udev->msix.nvectors);
		return -ERANGE;
	}

	irq = pci_irq_vector(udev->pdev, vec);

	ctx = &udev->msi.ctx[vec];
	if (ctx == NULL)
		return 0;

	if (ctx && ctx->trigger && udev->msi.msi_info[vec].msi_allocated) {
		free_irq(irq, ctx->trigger);
		eventfd_ctx_put(ctx->trigger);
	        udev->msi.msi_info[vec].msi_allocated = false;
		ctx->trigger = NULL;
	}

	if (efd < 0)
		return 0;

	trigger = eventfd_ctx_fdget(efd);
	if (IS_ERR(trigger)) {
		err = PTR_ERR(trigger);
		pr_err("eventfd ctx get failed: err:%d efd:%u\n", err, efd);
		return err;
	}

	err = request_irq(irq, ifc_uio_irq_handler, udev->info.irq_flags,
			  udev->info.name, trigger);
	if (err) {
		eventfd_ctx_put(trigger);
		return err;
	}
#ifdef __INTEL__DEBUG_CHK
	pr_debug("eventfd ctx registration done: efd:%u vec:%u irq:%u\n", efd, vec, irq);
#endif

	udev->msi.msi_info[vec].msi_allocated = true;
	ctx->trigger = trigger;

	return 0;
}

/* Regiser eventfd with MSI interrupts */
static int ifc_uio_pci_msi_irq_irqcontrol(struct uio_info *dev_info, s32 irq_data)
{
	struct ifc_uio_pci_dev *udev;
	struct pci_dev *pdev;
	u32 msi_num;
	int irqfd;
	int err;

	if (dev_info == NULL) {
		pr_err("dev_info is NULL. Failed to register eventfd against vector\n");
		return -1;
	}

	/* Retrieve private context  check for enablement */
	udev = dev_info->priv;
	if (!udev->msi_en)
		return -1;
	pdev = udev->pdev;

	/*  Retrieve vector and eventfd */
	msi_num = ((irq_data & MSI_VEC_MASK) >> MSI_IRQFD_BITS);

	irqfd = (irq_data & MSI_IRQFD_MASK);
	mutex_lock(&udev->msix_state_lock);
	err = set_msi_irq_eventfd(( struct ifc_uio_pci_dev *)udev, msi_num, (int)irqfd);
	if (err < 0)
		pr_err("msi registration failed %u\n",msi_num);
	mutex_unlock(&udev->msix_state_lock);

        return 0;
}

/* Enable MSI interrupts */
static int
ifc_uio_pci_enable_msi_interrupts(struct ifc_uio_pci_dev *udev)
{
	int i = 0, nvectors;
	struct pci_dev *pdev = udev->pdev;
	udev->info.irq_flags = (IRQF_NO_THREAD | IRQF_NOBALANCING | IRQF_ONESHOT | IRQF_IRQPOLL);

	nvectors = pci_msi_vec_count(udev->pdev);

	nvectors = (nvectors>0)?(nvectors+1):0;
	if(nvectors <= 0){
		pr_err("failed while enabling getting vectors\n");
		return -1;
	}

	udev->msi.nvectors = nvectors;
	udev->msi.ctx = kcalloc(nvectors, sizeof(struct uio_msi_irq_ctx),
				 GFP_KERNEL);
	if (!(udev->msi.ctx)) {
		pr_err("failed to allocate memory for MSI contexts");
		goto err_msi_ctx_alloc;
	}

	for (i = 0; i < nvectors; i++) {
		udev->msi.ctx[i].name = kasprintf(GFP_KERNEL,
						   KBUILD_MODNAME "[%d](%s)",
						   i, pci_name(pdev));
		if (!udev->msi.ctx[i].name)
			goto err_msi_name_alloc;
	}


#ifndef HAVE_ALLOC_IRQ_VECTORS                                                  
	if (pci_enable_msi(udev->pdev) == 0) {                          
		dev_dbg(&udev->pdev->dev, "using MSI");                 
		udev->info.irq_flags = IRQF_NO_THREAD;                  
		udev->info.irq = udev->pdev->irq;                       
		udev->msi_en = 1;
	}                                                               
#else                                                                           
	if (pci_alloc_irq_vectors(udev->pdev, 2, 3, PCI_IRQ_MSI) >= 1) {
		dev_dbg(&udev->pdev->dev, "using MSI");                 
		udev->info.irq_flags = IRQF_NO_THREAD;                  
		udev->info.irq = pci_irq_vector(udev->pdev, 0);         
		udev->msi_en = 1;
	}                                                               
#endif 

	return 0;

err_msi_name_alloc:
		for (i = 0; i < nvectors; i++)
			kfree(udev->msi.ctx[i].name);
		kfree(udev->msi.ctx);
err_msi_ctx_alloc:
		return -1;
}

/* Disable MSI interrupts */
static void
ifc_uio_pci_disable_msi_interrupts(struct ifc_uio_pci_dev *udev)
{
	int i;
	int ret;

	if (udev == NULL)
		return;
	if (udev->msi_en == 0)
		return;

	if(udev->msi.nvectors < 0){
		pr_err("Failed while getting vectors\n");
		return;
	}

	for (i = 0; i < udev->msi.nvectors; i++) {
		if (udev->msi.ctx[i].trigger) {
			ret = set_msi_irq_eventfd(udev, i, -1);
			if (ret < 0) {
				pr_err("Failed to set irq eventfds\n");
				return;
			}
		}
		kfree(udev->msi.ctx[i].name);
                udev->msi.msi_info[i].valid = 0;
                udev->msi.msi_info[i].efd = 0;
	}


#ifndef HAVE_ALLOC_IRQ_VECTORS                                                  
	if(udev->msi_en){
		pci_disable_msi(udev->pdev);
	}
#else
	if(udev->msi_en){
		pci_free_irq_vectors(udev->pdev);
	}
#endif

	udev->msi_en = 0;
	kfree(udev->msi.ctx);
}
#endif

static int ifc_uio_pci_open(struct uio_info *info, struct inode *inode)
{
	int err = -EFAULT;
#ifndef IFC_QDMA_DYN_CHAN
#ifdef IFC_QDMA_MSIX_ENABLE
	int nvectors;
#endif
#endif
	struct ifc_uio_pci_dev *udev = info->priv;

	if (udev == NULL)
		return err;

	if (atomic_inc_return(&udev->refcnt) != 1)
		return err;

#ifndef IFC_QDMA_DYN_CHAN
#ifdef IFC_QDMA_MSIX_ENABLE
	/* enable interrupts */
	nvectors = pci_msix_vec_count(udev->pdev);
	if(nvectors < 0){
		pr_err("No inerrupts supported\n");
		return err;
	}

	err = ifc_uio_pci_enable_msix_interrupts(udev, nvectors);
	if (err) {
		pr_err("Failed to enable interrupt\n");
		return err;
	}
	pr_debug("successfully enabled interrupts\n");
#elif defined(IFC_QDMA_MSI_ENABLE)
	/* MSI Support */
	err = ifc_uio_pci_enable_msi_interrupts(udev);
	if (err) {
		pr_err("Failed to enable MSI interrupt\n");
		return -EFAULT;
	}
	pr_debug("successfully enabled MSI interrupts\n");
#endif
#endif
        return err;
}

#ifdef IFC_QDMA_MSIX_ENABLE
void
ifc_uio_pci_disable_msix_interrupts(struct ifc_uio_pci_dev *udev)
{
	int i;
	int ret;
	int vectors;

	if (udev == NULL)
		return;
	if (udev->msix_en == 0)
		return;

	vectors = pci_msix_vec_count(udev->pdev);
	if(vectors < 0){
		pr_err("Failed while getting vectors\n");
		return;
	}
#ifdef IFC_QDMA_DYN_CHAN
        vectors = udev->msix.evectors;
#endif
	for (i = 0; i < vectors; i++) {
		if (udev->msix.ctx[i].trigger) {
			ret = set_irq_eventfd(udev, i, -1);
			if (ret < 0) {
				pr_err("Failed to set irq eventfds\n");
				return;
			}
		}
		kfree(udev->msix.ctx[i].name);
                udev->msix.msix_info[i].valid = 0;
                udev->msix.msix_info[i].efd = 0;
	}

	pci_disable_msix(udev->pdev);
#ifdef IFC_QDMA_DYN_CHAN
        udev->msix.evectors = 0;
#endif
	udev->msix_en = 0;
	kfree(udev->msix.ctx);
	kfree(udev->msix.table);
}
#endif

#ifdef IFC_QDMA_MSIX_ENABLE
#ifdef IFC_QDMA_DYN_CHAN
static int
ifc_uio_pci_reset_msix(struct ifc_uio_pci_dev *udev, int vectors)
{
        int err;
        int i = 0;
        struct pci_dev *dev;

	if (udev == NULL)
		return -EFAULT;

        dev = udev->pdev;
        udev->msix.evectors = vectors;
        err = ifc_uio_pci_enable_msix_interrupts(udev, vectors);
        if (err) {
                dev_err(&dev->dev, "Enable interrupt fails\n");
		return -EFAULT;
        }

        for (i = 0; i < MSIX_CAPACITY; i++) {
                if (udev->msix.msix_info[i].valid) {
                        mutex_lock(&udev->msix_state_lock);
                        set_irq_eventfd(udev, i, udev->msix.msix_info[i].efd);
			if (err < 0)
				pr_err("msix registration failed %d\n", i);
                        mutex_unlock(&udev->msix_state_lock);
                }
        }
        return 0;
}
#endif	//IFC_QDMA_DYN_CHAN
#endif	//IFC_QDMA_MSIX_ENABLE

static int
ifc_uio_pci_release(struct uio_info *info, struct inode *inode)
{
	struct ifc_uio_pci_dev *udev = info->priv;

	if (udev == NULL)
		return -1;

#ifdef IFC_QDMA_MSIX_ENABLE
	ifc_uio_pci_disable_msix_interrupts(udev);
#elif defined(IFC_QDMA_MSI_ENABLE)
	ifc_uio_pci_disable_msi_interrupts(udev);
#endif

	atomic_dec_and_test(&udev->refcnt);
    return 0;
}

static int ifcuio_pci_sriov_configure(struct pci_dev *dev, int vfs)
{
#if IS_ENABLED(CONFIG_PCI_IOV)
	int rc = 0;

	if (dev == NULL)
		return -EFAULT;

	if (!pci_sriov_get_totalvfs(dev))
		return -EINVAL;

	if (!vfs)
		pci_disable_sriov(dev);
	else if (!pci_num_vf(dev))
		rc = pci_enable_sriov(dev, vfs);
	else /* do nothing if change vfs number */
		rc = -EINVAL;
	return rc;
#else
	(void)dev;
	(void)vfs;
	return 0;
#endif
}

/* sriov sysfs */
static ssize_t
max_vfs_show(struct device *dev, struct device_attribute *attr,
	     char *buf)
{
	if (buf == NULL)
		return 0;

	return snprintf(buf, 10, "%u\n", dev_num_vf(dev));
}

static ssize_t
max_vfs_store(struct device *dev, struct device_attribute *attr,
	      const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	unsigned long max_vfs;
	int err;

	if (buf == NULL)
		return -EINVAL;

	if (!sscanf(buf, "%lu", &max_vfs))
		return -EINVAL;

	err = ifcuio_pci_sriov_configure(pdev, max_vfs);
	return err ? err : count;
}

static DEVICE_ATTR(max_vfs, S_IRUGO | S_IWUSR, max_vfs_show, max_vfs_store);
static struct attribute *dev_attrs[] = {
	&dev_attr_max_vfs.attr,
	NULL,
};

static const struct attribute_group dev_attr_grp = {
	.attrs = dev_attrs,
};

/**
 * ifc_io_error_detected - called when PCI error is detected
 * @dev: Pointer to PCI device
 * @state: The current pci connection state
 *
 * This function is called after a PCI bus error affecting
 * this device has been detected.
 */

static pci_ers_result_t ifc_io_error_detected(struct pci_dev *dev,
                                             pci_channel_state_t state)
{
	pr_err("PCI error occured: channel state: %u\n", state);
	switch (state) {
		case pci_channel_io_normal:
			/* Non Correctable Non-Fatal errors */
			return PCI_ERS_RESULT_CAN_RECOVER;
		case pci_channel_io_perm_failure:
			return PCI_ERS_RESULT_DISCONNECT;
		case pci_channel_io_frozen:
			pci_disable_device(dev);
			return PCI_ERS_RESULT_NEED_RESET;
		default:
			pr_err("default error\n");

	}

	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * ifc_io_slot_reset - called after the pci bus has been reset.
 * @dev: Pointer to PCI device
 *
 * Restart the card from scratch, as if from a cold-boot. Implementation
 * may resemble the first half of the ifc_resume routine.
 */
static pci_ers_result_t ifc_io_slot_reset(struct pci_dev *dev)
{
	int err;

	err = pci_enable_device_mem(dev);
	if (err) {
		pr_err("Cannot re-enable PCI device after reset.\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	pci_set_master(dev);

	pci_restore_state(dev);

	return PCI_ERS_RESULT_RECOVERED;
}

/**
 * ifc_io_resume - called when traffic can start flowing again.
 * @dev: Pointer to PCI device
 *
 * This callback is called when the error recovery drivers tells us that
 * its OK to resume normal operation. Implementation resembles the
 * second-half of the ifc_resume function.
 */
static void ifc_io_resume(struct pci_dev *dev)
{
	if (dev == NULL)
		return;

	pci_set_master(dev);
	pci_restore_state(dev);
}


/* Remap pci resources described by bar #pci_bar in uio resource n. */
static int
ifcuio_pci_setup_iomem(struct pci_dev *dev, struct uio_info *info,
		       int n, int pci_bar, const char *name)
{
	unsigned long addr, len;
	void *internal_addr;

	if (info == NULL)
		return -EINVAL;

	if (n >= ARRAY_SIZE(info->mem))
		return -EINVAL;

	addr = pci_resource_start(dev, pci_bar);
	len = pci_resource_len(dev, pci_bar);
	if (addr == 0 || len == 0)
		return -1;
	internal_addr = ioremap(addr, len);
	if (!internal_addr)
		return -1;
	info->mem[n].name = name;
	info->mem[n].addr = addr;
	info->mem[n].internal_addr = internal_addr;
	info->mem[n].size = len;
	info->mem[n].memtype = UIO_MEM_PHYS;
	return 0;
}

/* Get pci port io resources described by bar #pci_bar in uio resource n. */
static int
ifcuio_pci_setup_ioport(struct pci_dev *dev, struct uio_info *info,
			int n, int pci_bar, const char *name)
{
	unsigned long addr, len;

	if (info == NULL)
		return -EINVAL;

	if (n >= ARRAY_SIZE(info->port))
		return -EINVAL;

	addr = pci_resource_start(dev, pci_bar);
	len = pci_resource_len(dev, pci_bar);
	if (addr == 0 || len == 0)
		return -EINVAL;

	info->port[n].name = name;
	info->port[n].start = addr;
	info->port[n].size = len;
	info->port[n].porttype = UIO_PORT_X86;

	return 0;
}

/* Unmap previously ioremap'd resources */
static void
ifcuio_pci_release_iomem(struct uio_info *info)
{
	int i;

	for (i = 0; i < MAX_UIO_MAPS; i++) {
		if (info->mem[i].internal_addr)
			iounmap(info->mem[i].internal_addr);
	}
	return;
}

static int
ifcuio_setup_bars(struct pci_dev *dev, struct uio_info *info)
{
	int i, iom, iop, ret;
	unsigned long flags;
	static const char *bar_names[PCI_STD_RESOURCE_END + 1]  = {
		"BAR0",
		"BAR1",
		"BAR2",
		"BAR3",
		"BAR4",
		"BAR5",
	};

	iom = 0;
	iop = 0;

	for (i = 0; i < ARRAY_SIZE(bar_names); i++) {
		if (pci_resource_len(dev, i) != 0 &&
		    pci_resource_start(dev, i) != 0) {
			flags = pci_resource_flags(dev, i);
			if (flags & IORESOURCE_MEM) {
				ret = ifcuio_pci_setup_iomem(dev, info, iom,
							     i, bar_names[i]);
				if (ret != 0)
					return ret;
				iom++;
			} else if (flags & IORESOURCE_IO) {
				ret = ifcuio_pci_setup_ioport(dev, info, iop,
							      i, bar_names[i]);
				if (ret != 0)
					return ret;
				iop++;
			}
		}
	}

	return (iom != 0 || iop != 0) ? ret : -ENOENT;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
static int __devinit
#else
static int
#endif
ifcuio_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct ifc_uio_pci_dev *udev;
	dma_addr_t map_dma_addr;
	void *map_addr;
	int err;

#ifdef HAVE_PCI_IS_BRIDGE_API
	if (pci_is_bridge(dev)) {
		dev_warn(&dev->dev, "Ignoring PCI bridge device\n");
		return -ENODEV;
	}
#endif

	udev = kzalloc(sizeof(*udev), GFP_KERNEL);
	if (!udev)
		return -ENOMEM;

	/*
	 * enable device: ask low-level code to enable I/O and
	 * memory
	 */
	err = pci_enable_device(dev);
	if (err != 0) {
		dev_err(&dev->dev, "Cannot enable PCI device\n");
		goto fail_free;
	}

	/* enable bus mastering on the device */
	pci_set_master(dev);

	/* remap IO memory */
	err = ifcuio_setup_bars(dev, &udev->info);
	if (err != 0)
		goto fail_release_iomem;

	/* set 64-bit DMA mask */
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,18,0)
	err = dma_set_mask_and_coherent(&dev->dev,  DMA_BIT_MASK(64));
	if (err != 0) {
		dev_err(&dev->dev, "Cannot set DMA mask and coherent\n");
		goto fail_release_iomem;
	}
	#else
	err = pci_set_dma_mask(dev,  DMA_BIT_MASK(64));
	if (err != 0) {
		dev_err(&dev->dev, "Cannot set DMA mask\n");
		goto fail_release_iomem;
	}

	err = pci_set_consistent_dma_mask(dev, DMA_BIT_MASK(64));
	if (err != 0) {
		dev_err(&dev->dev, "Cannot set consistent DMA mask\n");
		goto fail_release_iomem;
	}
	#endif
	/* fill uio infos */
	udev->info.name = "ifc_uio";
	udev->info.version = "0.1";
	udev->info.priv = udev;
	udev->pdev = dev;
	udev->info.irq = -1;
	mutex_init(&udev->msix_state_lock);

	/* Setup the interrupt handler to disable the interrupts
	 * user space driver responsible to poll for interrupts
	 * and acknowledge
	 */
#ifdef IFC_QDMA_MSIX_ENABLE
#ifdef IFC_QDMA_DYN_CHAN
	udev->info.irqcontrol = ifc_uio_pci_dca_irqcontrol;
#else
	udev->info.irqcontrol = ifc_uio_irq_irqcontrol;
#endif
#else
#ifdef IFC_QDMA_MSI_ENABLE
	udev->info.irqcontrol = ifc_uio_pci_msi_irq_irqcontrol;
#endif
#endif
	udev->info.open = ifc_uio_pci_open;
	udev->info.release = ifc_uio_pci_release;

	/* create /sysfs entry */
	if (pci_sriov_get_totalvfs(dev)) {
		err = sysfs_create_group(&dev->dev.kobj, &dev_attr_grp);
		if (err != 0)
			goto fail_release_iomem;
	}

	/* register uio driver */
	err = uio_register_device(&dev->dev, &udev->info);
	if (err != 0)
		goto fail_remove_group;

	pci_set_drvdata(dev, udev);

	/*
	 * Doing a harmless dma mapping for attaching the device to
	 * the iommu identity mapping if kernel boots with iommu=pt.
	 * Note this is not a problem if no IOMMU at all.
	 */
	map_addr = dma_alloc_coherent(&dev->dev, 1024, &map_dma_addr,
				      GFP_KERNEL);
	if (map_addr)
		memset(map_addr, 0, 1024);

	if (!map_addr) {
		dev_info(&dev->dev, "dma mapping failed\n");
		goto fail_remove_group;
	} else {
		pr_debug("DMA mapping Successful\n");
		dma_free_coherent(&dev->dev, 1024, map_addr, map_dma_addr);
	}

	return 0;

fail_remove_group:
	sysfs_remove_group(&dev->dev.kobj, &dev_attr_grp);
fail_release_iomem:
	ifcuio_pci_release_iomem(&udev->info);
	pci_disable_device(dev);
fail_free:
	kfree(udev);

	return err;
}

static void
ifcuio_pci_remove(struct pci_dev *dev)
{
	struct ifc_uio_pci_dev *udev = pci_get_drvdata(dev);

	sysfs_remove_group(&dev->dev.kobj, &dev_attr_grp);
	uio_unregister_device(&udev->info);
	ifcuio_pci_release_iomem(&udev->info);
	if (!pci_num_vf(dev))
		pci_disable_device(dev);
	pci_set_drvdata(dev, NULL);
	kfree(udev);
}

const struct pci_device_id ifc_pci_tbl[] = {
	{ PCI_DEVICE(0x1172, 0x0000) },
	{0, }
};

static const struct pci_error_handlers ifc_err_handler = {
	.error_detected = ifc_io_error_detected,
	.slot_reset = ifc_io_slot_reset,
	.resume = ifc_io_resume
};

static struct pci_driver ifcuio_pci_driver = {
	.name = "ifc_uio",
	.id_table = ifc_pci_tbl,
	.probe = ifcuio_pci_probe,
	.remove = ifcuio_pci_remove,
	.sriov_configure = ifcuio_pci_sriov_configure,
	.err_handler = &ifc_err_handler
};

static int __init
ifcuio_pci_init_module(void)
{
        pr_info("%s - version %s\n", ifc_uio_driver_string,
                ifc_uio_driver_version);
        pr_info("%s\n", ifc_uio_copyright);
	__INTEL__USE_DBG_CHK();

	return pci_register_driver(&ifcuio_pci_driver);
}

static void __exit
ifcuio_pci_exit_module(void)
{
	pci_unregister_driver(&ifcuio_pci_driver);
}

module_init(ifcuio_pci_init_module);
module_exit(ifcuio_pci_exit_module);
