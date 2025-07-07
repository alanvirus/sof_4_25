#*  3-Clause BSD license
#* Copyright (C) 2019-2020 Intel Corporation
#* SPDX-License-Identifier: BSD-3-Clause
__objs= ${OBJ}/devmem.o
TARGET=devmem

all: ${TARGET}
32BIT: ${TARGET}

include ../../common/mk/common.mk
__ldflags = -L${LIBMQDMA_DIR} -lmqdmasoc

${TARGET}: ${__objs}
	$(call cc_link,$@,$<)

clean:
	rm -rf ${OBJ} ${TARGET}
clean32:
	rm -rf ${OBJ} ${TARGET}

.PHONY: all clean
