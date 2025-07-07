# SPDX-License-Identifier: BSD-3-Clause
# # Copyright(c) 2020 Intel Corporation

include $(RTE_SDK)/mk/rte.vars.mk

#
# library name
#
LIB = librte_pmd_mcdma.a

# library version
LIBABIVER := 1

#Include Directories
CFLAGS += -I$(RTE_SDK)/drivers/net/mcdma/base/
CFLAGS += -I$(RTE_SDK)/drivers/net/mcdma/

# Compilation flags
CFLAGS += -O3
CFLAGS += -DGENHDR_STRUCT
CFLAGS += -Wall -Werror -Wextra -Wcast-align -Wcast-qual
CFLAGS += -Wformat=2 -Wmissing-prototypes -Wstrict-prototypes
CFLAGS += -Wformat=2 -Wshadow -Wwrite-strings
CFLAGS += -MMD -MP -fpic -fstack-protector-strong -DFORTIFY_SOURCE=2
CFLAGS += -fno-strict-overflow -fno-delete-null-pointer-checks -fwrapv
CFLAGS += ${CFLAGS}
CFLAGS += ${EXTRA_CFLAGS}

# this lib depends upon:
LDLIBS += -lrte_eal -lrte_mbuf -lrte_mempool -lrte_ring
LDLIBS += -lrte_ethdev -lrte_net -lrte_kvargs
LDLIBS += -lrte_bus_pci

# versioning export map
EXPORT_MAP := rte_pmd_mcdma_version.map

# Source files
SRCS-$(CONFIG_RTE_LIBRTE_MCDMA_PMD) += mcdma_ethdev.c
SRCS-$(CONFIG_RTE_LIBRTE_MCDMA_PMD) += mcdma_debug.c
SRCS-$(CONFIG_RTE_LIBRTE_MCDMA_PMD) += mcdma_common.c
SRCS-$(CONFIG_RTE_LIBRTE_MCDMA_PMD) += mcdma_devops.c
SRCS-$(CONFIG_RTE_LIBRTE_MCDMA_PMD) += base/mcdma_access.c
SRCS-$(CONFIG_RTE_LIBRTE_MCDMA_PMD) += mcdma_platform.c
SRCS-$(CONFIG_RTE_LIBRTE_MCDMA_PMD) += mcdma_rxtx.c
SRCS-$(CONFIG_RTE_LIBRTE_MCDMA_PMD) += mcdma_dca.c


SYMLINK-y-include += rte_pmd_mcdma.h
SYMLINK-y-include += base/mcdma_ip_params.h
SYMLINK-y-include += base/pio_reg_registers.h
SYMLINK-y-include += base/dynamic_channel_params.h


include $(RTE_SDK)/mk/rte.lib.mk
