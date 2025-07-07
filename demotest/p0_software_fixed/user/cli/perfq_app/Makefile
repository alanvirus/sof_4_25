#*  3-Clause BSD license
#* Copyright (C) 2019-2020 Intel Corporation
#* SPDX-License-Identifier: BSD-3-Clause
__objs= ${COMMON_SRC_DIR}/ifc_qdma_utils.o  ${OBJ}/perfq_threads.o $\
	${OBJ}/perfq_stat.o ${OBJ}/perfq_app.o ${OBJ}/perfq_bas.o
TARGET=perfq_app

all: ${TARGET}

include ../../common/mk/common.mk
__ldflags += -L${LIBMQDMA_DIR}  -pthread -lmqdmasoc

${TARGET}: ${__objs}
#	$(call cc_link,$@,$<)
#	$(call cc_link,$@,$?)
	$(call cc_link,$@,$^)
clean:
	rm -rf ${OBJ} ${TARGET}

.PHONY: all clean 
