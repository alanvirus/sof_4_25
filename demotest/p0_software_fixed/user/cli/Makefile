# 3-Clause BSD license
#Copyright (C) 2019-2020 Intel Corporation
#SPDX-License-Identifier: BSD-3-Clause

ifdef 32BIT
DIRS := perfq_app/
else
DIRS := perfq_app/ devmem/ netdev_app/
endif

clean:
	$(foreach subdir,${DIRS},make -C ${subdir} clean;)
all:
	$(foreach subdir,${DIRS},make -C ${subdir} all;)

.PHONY: clean all
