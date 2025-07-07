# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2020 Intel Corporation

# binary name
APP = mcdma-test

SRCS-y += perfq_app.c
SRCS-y += perfq_threads.c
SRCS-y += perfq_stat.c
SRCS-y += ifc_qdma_utils.c
SRCS-y += perfq_bas.c
SRCS-y += perfq_random.c

# Build using pkg-config variables if possible
$(shell pkg-config --exists libdpdk)
ifeq ($(.SHELLSTATUS),0)

all: shared
.PHONY: shared static
shared: build/$(APP)-shared
	ln -sf $(APP)-shared build/$(APP)
static: build/$(APP)-static
	ln -sf $(APP)-static build/$(APP)

PC_FILE := $(shell pkg-config --path libdpdk)
CFLAGS += -g $(shell pkg-config --cflags libdpdk)

LDFLAGS_SHARED = $(shell pkg-config --libs libdpdk)
LDFLAGS_STATIC = -Wl,-Bstatic $(shell pkg-config --static --libs libdpdk)

build/$(APP)-shared: $(SRCS-y) Makefile $(PC_FILE) | build
	$(CC) $(CFLAGS) $(SRCS-y) -o $@ $(LDFLAGS) $(LDFLAGS_SHARED)

build/$(APP)-static: $(SRCS-y) Makefile $(PC_FILE) | build
	$(CC) $(CFLAGS) $(SRCS-y) -o $@ $(LDFLAGS) $(LDFLAGS_STATIC)

build:
	@mkdir -p $@

.PHONY: clean
clean:
	rm -f build/$(APP) build/$(APP)-static build/$(APP)-shared
	rmdir --ignore-fail-on-non-empty build

else

ifeq ($(RTE_SDK),)
$(error "Please define RTE_SDK environment variable")
endif

# Default target, can be overridden by command line or environment
RTE_TARGET ?= x86_64-native-linuxapp-gcc

include $(RTE_SDK)/mk/rte.vars.mk

CFLAGS += -O3
CFLAGS += -DPERFQ_PERF
CFLAGS += -UPERFQ_DATA
CFLAGS += -UPERFQ_LOAD_DATA
CFLAGS += -UVERIFY_FUNC
CFLAGS += -UIFC_ED_CONFIG_TID_UPDATE
CFLAGS += -DIFC_MCDMA_SINGLE_FUNC
CFLAGS += -UNO_ALIGN
CFLAGS += -UDUMP_FAIL_DATA
CFLAGS += -UCID_PAT
CFLAGS += -UIFC_PROG_DATA_EN
CFLAGS += -UCID_PFVF_PAT
CFLAGS += -UIFC_MCDMA_RANDOMIZATION
CFLAGS += -UBUFFER_FORWARD_D2H_TO_H2D
CFLAGS += -UIFC_MCDMA_FUNC_VER
CFLAGS += -UIFC_MCDMA_MAILBOX
CFLAGS += $(WERROR_FLAGS)

include $(RTE_SDK)/mk/rte.extapp.mk

endif
