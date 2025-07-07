#*  3-Clause BSD license
#* Copyright (C) 2019-2020 Intel Corporation
#* SPDX-License-Identifier: BSD-3-Clause
__objs= ${OBJ}/ref.o
__tx_objs= ${OBJ}/avmm_tx.o
__rx_objs= ${OBJ}/avmm_rx.o

TARGET := ref avmm_rx avmm_tx
include ../../common/mk/env.mk

all: ${TARGET}
include ${COMMON_DIR}/mk/common.mk

${LIBMQDMA_DIR}/libmqdmasoc.a:
	make -C ${LIBMQDMA_DIR} all

ref: ${__objs} $(LIBMQDMA_DIR)/libmqdmasoc.a
	$(call cc_link, $@, $^)

avmm_rx: ${__rx_objs} $(LIBMQDMA_DIR)/libmqdmasoc.a
	$(call cc_link, $@, $^)
avmm_tx: ${__tx_objs} $(LIBMQDMA_DIR)/libmqdmasoc.a
	$(call cc_link, $@, $^)
	
clean:
	-@rm -f ${TARGET}
	rm -rf ${OBJ} ${TARGET}

.PHONY: all clean

