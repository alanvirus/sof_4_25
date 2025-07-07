#*  3-Clause BSD license
#* Copyright (C) 2019-2020 Intel Corporation
#* SPDX-License-Identifier: BSD-3-Clause

__objs= ${OBJ}/sapp.o
TARGET := sapp

include ../../common/mk/env.mk
include ${COMMON_DIR}/mk/common.mk

all: ${TARGET}
${LIBMQDMA_DIR}/libmqdmasoc.a:
	make -C ${LIBMQDMA_DIR} all

sapp: ${__objs} $(LIBMQDMA_DIR)/libmqdmasoc.a
	$(call cc_link, $@, $^)

clean:
	-@rm -f ${TARGET}
	rm -rf ${OBJ}

.PHONY: all clean
