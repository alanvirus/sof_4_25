#*  3-Clause BSD license
#* Copyright (C) 2019-2020 Intel Corporation
#* SPDX-License-Identifier: BSD-3-Clause
__objs= ${COMMON_SRC_DIR}/ifc_qdma_utils.o ${OBJ}/ifc_mcdma_debug.o
ifdef 32BIT
__objs += ${OBJ}/ifc_mcdma.o ${OBJ}/ifc_env.o 
else
__objs += ${OBJ}/ifc_mcdma.o ${OBJ}/ifc_env.o ${OBJ}/ifc_vfio.o
endif
TARGETS:=libmqdmasoc.so libmqdmasoc.a

all: ${TARGETS}

include ../common/mk/common.mk

libmqdmasoc.so: ${__objs}
	$(call lib_shared,$@, ${__objs})

libmqdmasoc.a: ${__objs}
	$(call lib_static,$@, ${__objs})

clean:
	rm -rf ${OBJ} ${COMMON_SRC_DIR}/ifc_qdma_utils.o ${TARGETS} 

.PHONY: all clean 
