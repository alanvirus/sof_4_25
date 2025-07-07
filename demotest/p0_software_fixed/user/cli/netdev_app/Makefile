#*  3-Clause BSD license
#* Copyright (C) 2019-2020 Intel Corporation
#* SPDX-License-Identifier: BSD-3-Clause
__objs= ${COMMON_SRC_DIR}/ifc_qdma_utils.o
__objs+= ${OBJ}/netdev_app.o
TARGET=netdev_app

all: ${TARGET}

include ../../common/mk/common.mk
__ldflags += -L${LIBMCMEM_DIR} -lmcmemsoc
__cflags += -DIFC_MCDMA_X16

${TARGET}: ${__objs}
	$(call cc_link,$@,$^)
clean:
	rm -rf ${OBJ} ${TARGET}

.PHONY: all clean
