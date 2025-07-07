#*  3-Clause BSD license
#* Copyright (C) 2019-2020 Intel Corporation
#* SPDX-License-Identifier: BSD-3-Clause

__objs = ${COMMON_SRC_DIR}/ifc_qdma_utils.o
__objs += ${OBJ}/ifc_mcdma_mem.o

TARGETS:=libmcmemsoc.so libmcmemsoc.a

all: ${TARGETS}

include ../common/mk/common.mk

libmcmemsoc.so: ${__objs}
	$(call lib_shared,$@, ${__objs})

libmcmemsoc.a: ${__objs}
	$(call lib_static,$@, ${__objs})

clean:
	rm -rf ${OBJ} ${COMMON_SRC_DIR}/ifc_qdma_utils.o ${TARGETS}
	@echo ${COMMON_SRC_DIR}
	@echo ${__objs}

.PHONY: all clean
