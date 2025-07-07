
THIS_DIR := $(abspath $(lastword $(MAKEFILE_LIST)/../))
include ${THIS_DIR}/env.mk

OBJ ?= obj
CC := gcc
AR ?= ar

#__cflags += -O2 -Wall -Werror -Wextra -Wcast-align -Wcast-qual -g -Wl,-stack=1048576
#__cflags += -Wformat=2 -Wmissing-prototypes -Wstrict-prototypes -Wshadow -Wwrite-strings
#__cflags += -Wformat=2 -Wshadow -Wwrite-strings
__cflags +=  -g -fpic -DFORTIFY_SOURCE=2 -DGENHDR_STRUCT
#-MMD -MP -fstack-protector-strong 
# Flags Prevent compiler from optimizing out security checks
# -fno-strict-overflow - Dont assume strict overflow does not occure
# -fno-delete-null-pointer - Dont delete NULL pointer checks
# -fwrapv - Signed integers wrapping may occure
__cflags += -fno-strict-overflow -fno-delete-null-pointer-checks -fwrapv

__ldflags += -lm -g 
__cflags += -UIFC_PIO_256
__cflags += -UIFC_PIO_128
__cflags += -DIFC_PIO_64
__cflags += -UIFC_PIO_32
__cflags += -UIFC_PIO_MIX
__cflags += -mavx
__cflags += -DPERFQ_PERF
__cflags += -UIFC_DEBUG_STATS
__cflags += -UDEBUG
__cflags += -UDEBUG_DCA
__cflags += -UPERFQ_DATA
__cflags += -UPERFQ_LOAD_DATA
__cflags += -UIFC_SET_WB_ALL
__cflags += -UDUMP_DATA
__cflags += -UVERIFY_FUNC
__cflags += -UVERIFY_HOL
__cflags += -UIFC_QDMA_DW_LEN
__cflags += -UIFC_QDMA_DYN_CHAN
__cflags += -UIFC_QDMA_META_DATA
__cflags += -UGCSR_ENABLED
__cflags += -UIFC_MCDMA_ERR_CHAN
__cflags += -UIFC_ED_CONFIG_TID_UPDATE
__cflags += -UIFC_QDMA_IP_RESET

__cflags += -UIFC_64B_DESC_FETCH
__cflags += -DUIO_SUPPORT
__cflags += -UIFC_32BIT_SUPPORT
__cflags += -UNO_IOMMU_MODE
__cflags += -UIFC_USER_MSIX
__cflags += -DIFC_ED_1MB_SUPPORT

__cflags += -UTID_FIFO_ENABLED
__cflags += -UHW_FIFO_ENABLED
__cflags += -UWB_CHK_TID_UPDATE

__cflags += -DRESTRICTED_BATCH_SIZE
__cflags += -UIFC_MCDMA_DIDF
__cflags += -DIFC_MCDMA_SINGLE_FUNC
__cflags += -UIFC_MCDMA_BAS_EN
__cflags += -UIFC_MCDMA_FUNC_VER

__cflags += -UCID_PAT
__cflags += -UIFC_PROG_DATA_EN

__cflags += -I ${COMMON_DIR}/include -I ${LIBMQDMA_DIR}/
__cflags += ${CFLAGS}
__cflags += ${EXTRA_CFLAGS} -g

define cc_link
	${CC} -o ${1} ${2} ${__ldflags} -static -lrt -lpthread -lm
endef

define cc_comp
	${CC} ${__cflags} -o ${1} -c ${2}
endef

define lib_shared
        ${CC} -shared -Wl,-soname,${1} -o ${1} ${2} ${__ldflags}
endef

define lib_static
        ${AR} r ${1} ${2}
endef

${COMMON_SRC_DIR}/%.o: ${COMMON_SRC_DIR}/%.c
	$(call cc_comp, $@, $<)

${OBJ}/%.o: ${SRC}/%.c | ${OBJ}
	$(call cc_comp, $@, $<)

${OBJ}/%.o: %.c | ${OBJ}
	$(call cc_comp, $@, $<)

${OBJ}/%: | ${OBJ}

${OBJ}:
	mkdir -p $@

-include $(wildcard ${OBJ}/*.d)

.PHONY: all
.DEFAULT_GOAL := all

