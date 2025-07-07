
THIS_DIR := $(abspath $(lastword $(MAKEFILE_LIST)/../))
include ${THIS_DIR}/env.mk

OBJ ?= obj
CC := /usr/bin/gcc
AR ?= ar

__cflags += -O2 -Wall -Werror -Wextra -Waggregate-return -Wcast-align -Wcast-qual
__cflags += -Wformat=2 -Wmissing-prototypes -Wstrict-prototypes -Wshadow -Wwrite-strings
__cflags += -Wformat=2 -Wshadow -Wwrite-strings
__cflags += -MMD -MP -fpic -fstack-protector-strong -DFORTIFY_SOURCE=2 -DGENHDR_STRUCT

# Flags Prevent compiler from optimizing out security checks
# -fno-strict-overflow - Dont assume strict overflow does not occure
# -fno-delete-null-pointer - Dont delete NULL pointer checks
# -fwrapv - Signed integers wrapping may occure
__cflags += -fno-strict-overflow -fno-delete-null-pointer-checks -fwrapv

__cflags += -I ${COMMON_DIR}/include
__cflags += ${CFLAGS}
__cflags += ${EXTRA_CFLAGS}

define cc_link
	${CC} -o ${1} ${2} ${__ldflags}
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

