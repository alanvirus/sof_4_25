// SPDX-License-Identifier: GPL-2.0
/*-
 * Copyright(c) 2020-21 Intel Corporation. All rights reserved.
 */

#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/version.h>
#include <linux/list.h>
#include <linux/errno.h>

#include "ifc_mcdma_net.h"
#include "ifc_mcdma_debugfs.h"

static void use_release_message(volatile char *message);
static const char ifc_mcdma_driver_version[] = DRV_VERSION;
static const char ifc_mcdma_driver_string[] = DRV_SUMMARY;
static const char ifc_mcdma_copyright[] =
				"Copyright (c) 2020-21, Intel Corporation.";

MODULE_AUTHOR("Intel Corporation, <linux.nics@intel.com>");
MODULE_DESCRIPTION(DRV_SUMMARY);
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

static void use_release_message(volatile char *message) {
    (void)message;
}

#ifdef DEBUG
char *intel_dbg_message = "WARNING: this is a DEBUG driver";
#define __INTEL__USE_DBG_CHK() use_release_message(intel_dbg_message)

#else /* DEBUG */

char *intel_release_message = "Intel PRODUCTION driver";
#define __INTEL__USE_DBG_CHK() use_release_message(intel_release_message)

#endif /* DEBUG */

static int parse_long_int(const char *arg)
{
	unsigned long arg_val;
	const char *c = arg;

	while (*c != '\0') {
		if ((*c < 48 || *c > 57) && (*c != 10))
			return -1;
		c++;
	}
	sscanf(arg, "%lu", &arg_val);
	return arg_val;
}
#define NETDEV_GCSR_OFFSET(off) (off - 0x00200000U)
#define QDMA_REGS_2_PF0_IP_PARAM_2     0x00200084U
#define QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_SHIFT            16
#define QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_MASK             (0xFFFFU << QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_SHIFT)
#define QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_SHIFT            0
#define QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_MASK             (0xFFFFU << QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_SHIFT)




static ssize_t
mcdma_ipreset_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ifc_pci_dev *pctx = dev_get_drvdata(dev);
	struct ifc_mcdma_dev_ctx *dev_ctx;

	if (buf == NULL)
		return -1;

	if (unlikely(pctx == NULL)) {
		pr_err("Invalid PCI device Context\n");
		return -1;
	}

	dev_ctx = container_of(pctx, struct ifc_mcdma_dev_ctx, pctx);
	return snprintf(buf, 30, "%s\n", dev_ctx->bdf);
}

static ssize_t
mcdma_ipreset_store(struct device *dev, struct device_attribute *attr,
		    const char *buf, size_t count)
{
	struct ifc_pci_dev *pctx = dev_get_drvdata(dev);
	struct ifc_mcdma_dev_ctx *dev_ctx;
	int val;

	if (buf == NULL)
		return -1;

	val = parse_long_int(buf);
	if (val != 1)
		return -1;

	if (unlikely(pctx == NULL)) {
		pr_err("Invalid PCI device Context\n");
		return -1;
	}

	dev_ctx = container_of(pctx, struct ifc_mcdma_dev_ctx, pctx);
	if (ifc_mcdma_ip_reset(dev_ctx->qcsr) < 0) {
		pr_err("Failed to reset device\n");
		return -1;
	}

	return count;
}

static int ifc_mcdma_sriov_configure(struct pci_dev *dev, int vfs)
{
#if IS_ENABLED(CONFIG_PCI_IOV)
	int ret = 0;

	if (dev == NULL)
		return -EFAULT;

	if (!pci_sriov_get_totalvfs(dev) || vfs < 0)
		return -EINVAL;

	if (vfs == 0) {
		pci_disable_sriov(dev);
		return 0;
	}

	if (pci_num_vf(dev) == 0)
		ret = pci_enable_sriov(dev, vfs);
	else
		ret = -EINVAL;

	return ret;
#else
	return 0;
#endif
}

static
DEVICE_ATTR(mcdma_ipreset, S_IRUGO | S_IWUSR, mcdma_ipreset_show,
	    mcdma_ipreset_store);

static struct attribute *dev_attrs[] = {
	&dev_attr_mcdma_ipreset.attr,
	NULL,
};

static const struct attribute_group dev_attr_grp = {
	.attrs = dev_attrs,
};

#ifndef IFC_MCDMA_BAS
static int
ifc_pci_enable_interrupts(struct pci_dev *pdev, struct ifc_msix_info *msix_info)
{
	struct msix_entry *table;
	int err = 0;
	int vectors;
	int i;

	if (unlikely(pdev == NULL || msix_info == NULL)) {
		pr_err("Invalid PCI device Context\n");
		return -EFAULT;
	}

	vectors = pci_msix_vec_count(pdev);
	if (vectors < 0) {
		pr_err("Failed to receive MSI-X vector count\n");
		return -1;
	}

	table = kcalloc(vectors, sizeof(struct msix_entry), GFP_KERNEL);
	if (table == NULL) {
		pr_err("Failed to allocated memory\n");
		return -ENOMEM;
	}

	for (i = 0; i < vectors; i++)
		table[i].entry = i;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 8, 0)
	if (pci_enable_msix(pdev, table, vectors) != 0) {
#else
	if (pci_enable_msix_exact(pdev, table, vectors) != 0) {
#endif
		pr_err("failed while enabling interrupts %d\n", err);
		goto fail_cleanup;
	}
	pr_debug("Successfully enabled PCI MSI-X: vectors: %d", vectors);
	msix_info->nvectors = vectors;
	msix_info->table = table;
	return 0;

fail_cleanup:
	kfree(table);
	return -1;
}
#endif

static void
ifc_pci_disable_interrupts(struct pci_dev *pdev,
			   struct ifc_msix_info *msix_info)
{
	if (unlikely(pdev == NULL || msix_info == NULL)) {
		pr_err("Invalid PCI device Context\n");
		return;
	}
	pci_disable_msix(pdev);
	kfree(msix_info->table);
}

static int
ifc_pci_setup_iomem(struct pci_dev *dev, struct ifc_mcdma_bar_info *info,
		    int pci_bar, const char *name)
{
	unsigned long addr, len;
	void *internal_addr = NULL;

	if (unlikely(dev == NULL || info == NULL)) {
		pr_err("Invalid PCI device Context\n");
		return -EFAULT;
	}

	addr = pci_resource_start(dev, pci_bar);
	len = pci_resource_len(dev, pci_bar);
	if (addr == 0 || len == 0)
		return -EINVAL;

	internal_addr = ioremap(addr, len);
	if (IS_ERR(internal_addr))
		return PTR_ERR(internal_addr);

	info->name = name;
	info->iaddr = internal_addr;
	info->size = len;
	pr_debug("Mapping of %s successful\n", name);

	return 0;
}

static int
ifc_pci_setup_ioport(struct pci_dev *dev, struct ifc_mcdma_bar_info *info,
		     int pci_bar, const char *name)
{
	unsigned long addr, len;

	if (unlikely(dev == NULL || info == NULL)) {
		pr_err("Invalid PCI device Context\n");
		return -EFAULT;
	}
	addr = pci_resource_start(dev, pci_bar);
	len = pci_resource_len(dev, pci_bar);
	if (addr == 0 || len == 0)
		return -EINVAL;

	info->name = name;
	info->size = len;
	return 0;
}

/* Unmap previously ioremap'd resources */
static void
ifc_pci_release_iomem(struct ifc_mcdma_bar_info *info, int size)
{
	int i;

	if (unlikely(info == NULL)) {
		pr_err("Invalid BAR Context\n");
		return;
	}

	size = size ? size : PCI_STD_RESOURCE_END + 1;
	for (i = 0; i < size; i++) {
		if (info[i].iaddr) {
			iounmap(info[i].iaddr);
			pr_debug("Unmap of %s successful\n", info[i].name);
		}
	}
}

static int
ifc_setup_bars(struct pci_dev *dev, struct ifc_mcdma_bar_info **info)
{
	int i, iom, iop, ret;
	unsigned long flags;
	static const char *bar_names[PCI_STD_RESOURCE_END + 1]	= {
		"BAR0",
		"BAR1",
		"BAR2",
		"BAR3",
		"BAR4",
		"BAR5",
	};

	if (unlikely(dev == NULL)) {
		pr_err("Invalid PCI device Context\n");
		return -EFAULT;
	}

	iom = 0;
	iop = 0;

	*info = kzalloc(ARRAY_SIZE(bar_names) *
			sizeof(struct ifc_mcdma_bar_info), GFP_KERNEL);
	if (*info == NULL)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(bar_names); i++) {
		if (pci_resource_len(dev, i) != 0 &&
		    pci_resource_start(dev, i) != 0) {
			flags = pci_resource_flags(dev, i);
			if (flags & IORESOURCE_MEM) {
				ret = ifc_pci_setup_iomem(dev, *info + i, i,
						bar_names[i]);
				if (ret != 0)
					goto fail_cleanup;
				iom++;
			} else if (flags & IORESOURCE_IO) {
				ret = ifc_pci_setup_ioport(dev, *info + i,
						i, bar_names[i]);
				if (ret != 0)
					goto fail_cleanup;

				iop++;
			}
		}
	}
	return (iom != 0 || iop != 0) ? ret : -ENOENT;
fail_cleanup:
	ifc_pci_release_iomem(*info, i);
	kfree(*info);
	*info = NULL;
	return ret;
}

static int
ifc_mcdma_pci_init(struct ifc_pci_dev *pctx, struct pci_dev *dev,
		   const struct pci_device_id *id)
{
	int err;

	if (unlikely(dev == NULL || pctx == NULL)) {
		pr_err("Invalid PCI device Context\n");
		return -EFAULT;
	}

#ifdef HAVE_PCI_IS_BRIDGE_API
	if (pci_is_bridge(dev)) {
		dev_warn(&dev->dev, "Ignoring PCI bridge device\n");
		return -ENODEV;
	}
#endif
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
	err = ifc_setup_bars(dev, &pctx->info);
	if (err != 0) {
		pr_err("Bar Mapping Failed\n");
		goto fail_disable_pcidev;
	}
	pr_info("Bar Mapping Successful\n");

	if (dma_set_mask_and_coherent(&dev->dev, DMA_BIT_MASK(64))) {
		dev_err(&dev->dev, "Cannot set DMA mask\n");
		goto fail_release_barinfo;
	}

	/* create /sysfs entry */
	err = sysfs_create_group(&dev->dev.kobj, &dev_attr_grp);
	if (err != 0)
		goto fail_release_barinfo;
	pr_debug("Sysfs group creation successful\n");


	return 0;

fail_release_barinfo:
	ifc_pci_release_iomem(pctx->info, 0);
	kfree(pctx->info);
	pctx->info = NULL;
fail_disable_pcidev:
	pci_disable_device(dev);
fail_free:
	kfree(pctx);
	return err;
}

static void ifc_mcdma_pci_free(struct pci_dev *dev)
{
	struct ifc_pci_dev *pctx = pci_get_drvdata(dev);

	if (unlikely(pctx == NULL)) {
		pr_err("Invalid PCI device Context\n");
		return;
	}
	ifc_pci_release_iomem(pctx->info, 0);
	kfree(pctx->info);
	pctx->info = NULL;

	sysfs_remove_group(&dev->dev.kobj, &dev_attr_grp);
	pci_disable_device(dev);
	pci_set_drvdata(dev, NULL);
}

static u32 ifc_mcdma_get_chnl_number(struct ifc_pci_dev *pctx1,u8 is_pf)
{
    uint32_t reg = 0;
    uint32_t max_chcnt;
    void *gcsr = NULL;
    
    gcsr = (void *)((uint64_t)pctx1->info[0].iaddr + 0x200000);
    reg = readl(gcsr + NETDEV_GCSR_OFFSET(QDMA_REGS_2_PF0_IP_PARAM_2));

    if(is_pf)
    {
        max_chcnt = ((reg & QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_MASK) >>
        QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_PF_SHIFT);
    }
    else
    {
         max_chcnt = ((reg & QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_MASK) >>
        QDMA_REGS_2_PF0_IP_PARAM_2_NUM_DMA_CHAN_PER_VF_SHIFT);
    }

    return max_chcnt;
}

static int
ifc_mcdma_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct net_device *netdev;
	struct ifc_mcdma_dev_ctx *dev_ctx;
	int err;
    u32 tot_chn = 0;
    struct ifc_mcdma_dev_ctx *dev_ctx_temp = NULL;

    dev_ctx_temp = kzalloc(sizeof(struct ifc_mcdma_dev_ctx), GFP_KERNEL);

    if(dev_ctx_temp == NULL)
    {
        pr_err("mem alloc failed for dev_ctx_temp:probe failed ");
        return -ENOMEM;
    }

 /* Complete PCI configurations */
    err = ifc_mcdma_pci_init(&dev_ctx_temp->pctx, pdev, id);

    if (err < 0){
     pr_err("pci init of bars failed:probe failed");
     kfree(dev_ctx_temp);
     return -1;
    }
    tot_chn = ifc_mcdma_get_chnl_number(&dev_ctx_temp->pctx,!pdev->is_virtfn);

	/* Complete NetDev configurations */
	netdev = alloc_etherdev_mq(sizeof(struct ifc_mcdma_dev_ctx),tot_chn);

	if (!netdev){
        pr_err("netdev no memory:probe failed\n");
        kfree(dev_ctx_temp);
        ifc_mcdma_pci_free(pdev);
        return -ENOMEM;
    }

	ifc_mcdma_netdev_setup(netdev,tot_chn);

    dev_ctx = netdev_priv(netdev);

    dev_ctx->pctx.info = dev_ctx_temp->pctx.info;
    dev_ctx->netdev = netdev;
	dev_ctx->tot_chnl_avl = tot_chn;

    /* freeing the temp dev context as we don't require it now */
    kfree(dev_ctx_temp);

    dev_ctx->pctx.pdev = pdev;
	pci_set_drvdata(pdev, &dev_ctx->pctx);

	dev_ctx->qcsr = dev_ctx->pctx.info[0].iaddr;
	strncpy(dev_ctx->bdf, dev_name(&pdev->dev), 18);
	pr_debug("%s:%d: QCSR:0x%lx, BDF:%s\n", __func__, __LINE__,
		 (unsigned long)dev_ctx->qcsr, dev_ctx->bdf);
#ifndef IFC_MCDMA_BAS
	/* Setup MSI-X support */
	err = ifc_pci_enable_interrupts(pdev, &dev_ctx->msix_info);
	if (err < 0) {
		pr_err("Failed to enable interrupt\n");
		goto err_out_free;
	}
	pr_debug("Successfully Enabled Interrupts\n");
#endif
	pr_info("PCIe Initialization Successful\n");

	/* Reset the IP if PF0 */
	if (PCI_SLOT(pdev->devfn) == 0 && PCI_FUNC(pdev->devfn) == 0) {
		if (ifc_mcdma_ip_reset(dev_ctx->qcsr) < 0) {
			pr_err("Failed to reset device\n");
			goto err_out_disable_intr;
		}
		pr_debug("%s: Creating LB DebugFS\n", netdev->name);
		ifc_mcdma_lb_config.dev_ctx = dev_ctx;
#ifdef ENABLE_DEBUGFS
		ifc_mcdma_lb_debugfs_setup();
#endif
	}

	/* Register the NetDev */
	err = register_netdev(netdev);
	if (err) {
		netif_err(dev_ctx, probe, netdev, "Cannot register net device, aborting\n");
		goto err_out_disable_intr;
	}
	pr_info("%s: Probe Successful\n", netdev->name);
	return 0;

err_out_disable_intr:
	ifc_pci_disable_interrupts(pdev, &dev_ctx->msix_info);
#ifndef IFC_MCDMA_BAS
err_out_free:
	ifc_mcdma_pci_free(pdev);
#endif
	free_netdev(netdev);
	return err;
}

static void
ifc_mcdma_remove(struct pci_dev *dev)
{
	struct ifc_pci_dev *pctx = pci_get_drvdata(dev);
	struct ifc_mcdma_dev_ctx *dev_ctx;
	struct net_device *netdev;
	char name[20];

	if (unlikely(pctx == NULL)) {
		pr_err("Invalid PCI device Context\n");
		return;
	}

	dev_ctx = container_of(pctx, struct ifc_mcdma_dev_ctx, pctx);
#ifdef ENABLE_DEBUGFS
	if (PCI_SLOT(dev_ctx->pctx.pdev->devfn) == 0 &&
	    PCI_FUNC(dev_ctx->pctx.pdev->devfn) == 0) {
		debugfs_remove_recursive(ifc_mcdma_lb_config.d);
	}
#endif

	/* Release PCI resources */
	netdev = dev_ctx->netdev;
	if (netdev) {
		snprintf(name, 20, "%s", netdev->name);
		unregister_netdev(netdev);

		/* Release core info context */
		kfree(dev_ctx->cores);
		dev_ctx->cores = NULL;

		free_netdev(netdev);
	}

	ifc_pci_disable_interrupts(dev, &dev_ctx->msix_info);
	ifc_mcdma_pci_free(dev);
	pr_info("%s: Driver Removal Successful\n", name);
}

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

static const struct pci_error_handlers ifc_mcdma_err_handler = {
	.error_detected = ifc_io_error_detected,
	.slot_reset = ifc_io_slot_reset,
	.resume = ifc_io_resume
};

const struct pci_device_id ifc_mcdma_netdev_pci_tbl[] = {
	{ PCI_DEVICE(VENDOR_ID, DEVICE_ID) },
	{0, }
};

static struct pci_driver ifc_mcdma_netdev_pci_driver = {
	.name = "ifc_mcdma_netdev",
	.id_table = ifc_mcdma_netdev_pci_tbl,
	.probe = ifc_mcdma_probe,
	.sriov_configure = ifc_mcdma_sriov_configure,
	.remove = ifc_mcdma_remove,
	.err_handler = &ifc_mcdma_err_handler
};

static int __init
ifc_mcdma_netdev_init_module(void)
{
	pr_info("%s - version %s\n", ifc_mcdma_driver_string,
		ifc_mcdma_driver_version);
	pr_info("%s\n", ifc_mcdma_copyright);
	__INTEL__USE_DBG_CHK();

	return pci_register_driver(&ifc_mcdma_netdev_pci_driver);
}

static void __exit
ifc_mcdma_netdev_exit_module(void)
{
	pci_unregister_driver(&ifc_mcdma_netdev_pci_driver);
	pr_info("%s: Driver Exit Successful\n", IFC_MCDMA_DRIVER_NAME);
}

module_init(ifc_mcdma_netdev_init_module);
module_exit(ifc_mcdma_netdev_exit_module);
