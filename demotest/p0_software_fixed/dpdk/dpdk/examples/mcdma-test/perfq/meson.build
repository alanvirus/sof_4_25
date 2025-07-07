# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2020 Intel Corporation

# meson file, for building this example as part of a main DPDK build.
#
# To build this example as a standalone application with an already-installed
# DPDK instance, use 'make'

if dpdk_conf.has('RTE_LIBRTE_MCDMA_PMD')
        deps += 'pmd_mcdma'
endif

cflags += ['-DPERFQ_PERF',
        '-UPERFQ_DATA',
        '-UPERFQ_LOAD_DATA',
        '-UVERIFY_FUNC',
        '-UIFC_ED_CONFIG_TID_UPDATE',
        '-DIFC_MCDMA_SINGLE_FUNC',
        '-UNO_ALIGN',
        '-UDUMP_FAIL_DATA',
        '-UCID_PAT',
        '-UIFC_PROG_DATA_EN',
        '-UCID_PFVF_PAT',
        '-UIFC_MCDMA_RANDOMIZATION',
        '-UBUFFER_FORWARD_D2H_TO_H2D',
        '-UIFC_MCDMA_FUNC_VER',
        '-DDPDK_21_11_RC2',
	'-UIFC_MCDMA_MAILBOX']


sources = files(
	'perfq_app.c',
        'perfq_threads.c',
        'perfq_stat.c',
        'ifc_qdma_utils.c',
	'perfq_bas.c',
	'perfq_random.c'
)

includes += include_directories('../../../install/usr/local/include')

