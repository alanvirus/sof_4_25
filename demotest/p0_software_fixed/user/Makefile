#*  3-Clause BSD license
#* Copyright (C) 2019-2020 Intel Corporation
#* SPDX-License-Identifier: BSD-3-Clause

# MQDMA Top level makefile
include common/mk/env.mk

ifdef 32BIT
DIRS := libmqdma  cli/
else
DIRS := libmqdma  libmcmem cli/
endif

clean:
	$(foreach subdir,${DIRS},make -C ${subdir} clean;)
all:
	$(foreach subdir,${DIRS},make -C ${subdir} all;)
sdk:
	@$(MAKE) -f common/mk/release.mk all

.PHONY: clean all sdk 32BIT
