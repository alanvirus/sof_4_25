# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2020-21, Intel Corporation.

ROOT_DIR := $(abspath $(lastword $(MAKEFILE_LIST)/../../../../))

__cflags += -O2
__cflags += -Wformat=2 -Wmissing-prototypes -Wstrict-prototypes -Wno-format-nonliteral
__cflags += -MMD -MP -fstack-protector-strong -DFORTIFY_SOURCE=2 -DGENHDR_STRUCT

# Flags Prevent compiler from optimizing out security checks
# -fno-strict-overflow - Dont assume strict overflow does not occure
# -fno-delete-null-pointer - Dont delete NULL pointer checks
# -fwrapv - Signed integers wrapping may occure
__cflags += -fno-strict-overflow -fno-delete-null-pointer-checks -fwrapv
__cflags += -I $(ROOT_DIR)/common/include/
__cflags += -UDEBUG
__cflags += -UIFC_MCDMA_HEX_DUMP
__cflags += -DIFC_MCDMA_LB_CFG
__cflags += -UIFC_MCDMA_LB_DFLT_CFG
__cflags += -UIFC_MCDMA_HOL
__cflags += -UIFC_MCDMA_DEBUG
__cflags += -DIFC_QDMA_INTF_ST
__cflags += -DIFC_DATA_WIDTH=512
__cflags += -DMCDMA_ALIGN_ADDRESS
__cflags += -USKB_HEX_DUMP
__cflags += -DENABLE_DEBUGFS
__cflags += -DIFC_SELECT_QUEUE_ALGO
__cflags += -DIFC_MCDMA_X16
__cflags += -UIFC_MCDMA_BAS
__cflags += -DIFC_MCDMA_IOMMU_SUPPORT

CONFIG_MODULE_SIG=y
ifneq ($(KERNELRELEASE),)
# kbuild part of makefile
#
obj-m += ifc_mcdma_netdev.o
ifc_mcdma_netdev-y += ifc_mcdma_pci.o ifc_mcdma_net.o ifc_mcdma_ethtool.o ifc_mcdma_debugfs.o ifc_mcdma_ioctl.o ifc_mcdma_bas.o
else

DRIVER := ifc_mcdma_netdev
BUILD_KERNEL := $(shell uname -r)
# Kernel Search Path
# All the places we look for kernel source
KSP :=  /lib/modules/${BUILD_KERNEL}/source \
	/lib/modules/${BUILD_KERNEL}/build

test_dir = $(shell [ -e ${dir}/include/linux ] && echo ${dir})
KSP := $(foreach dir, ${KSP}, ${test_dir})

ifeq (,${KSRC})
KSRC := $(firstword ${KSP})
endif

kernelbuild = ${MAKE} CFLAGS_MODULE="${__cflags}" -C "${KSRC}" M=$(shell pwd) ${2} ${1}

all:
	$(call kernelbuild, modules)

clean:
	$(call kernelbuild, clean)

modules_install: all
	$(call kernelbuild, modules_install)

PATCH_FILE = check.patch
KDIR ?= /lib/modules/$(shell uname -r)/build
checkpatch:
	@echo "Checking latest commit patch"
	@git format-patch HEAD~1 --stdout > $(PATCH_FILE)
	@$(KDIR)/scripts/checkpatch.pl --strict \
	--ignore GERRIT_CHANGE_ID,FILE_PATH_CHANGES,LINUX_VERSION_CODE,\
	NOT_UNIFIED_DIFF,GIT_COMMIT_ID,EMAIL_SUBJECT,VOLATILE \
	--show-types --no-tree $(PATCH_FILE) > $(PATCH_FILE).conv || \
	(echo -e "\nERROR: Please check the temp patch analyzed: $(PATCH_FILE).conv\n\n" \
	&& cat $(PATCH_FILE).conv && exit 1)
	@rm -f $(PATCH_FILE) $(PATCH_FILE).conv
endif
