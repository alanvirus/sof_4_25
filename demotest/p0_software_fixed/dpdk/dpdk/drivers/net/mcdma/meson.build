# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2021 Intel Corporation

cflags += ['-DGENHDR_STRUCT',
        '-MMD',
        '-MP',
        '-DFORTIFY_SOURCE=2',
        '-Wall',
        '-Werror',
        '-Wextra',
        '-Wcast-align',
        '-Wcast-qual',
        '-Wformat=2',
        '-Wmissing-prototypes',
        '-Wstrict-prototypes',
        '-Wformat=2',
        '-Wshadow',
        '-Wwrite-strings',
        '-fpic',
        '-fstack-protector-strong',
        '-fno-strict-overflow',
        '-fno-delete-null-pointer-checks',
        '-fwrapv',
	'-DDPDK_21_11_RC2']

sources += files('base/mcdma_access.c',
        'mcdma_ethdev.c',
        'mcdma_debug.c',
        'mcdma_common.c',
        'mcdma_devops.c',
        'mcdma_platform.c',
        'mcdma_rxtx.c',
        'mcdma_dca.c',
	'mcdma_mailbox.c')

deps += ['eal', 'ethdev']

install_headers('base/mcdma_ip_params.h',
        'base/pio_reg_registers.h',
        'base/dynamic_channel_params.h',
        'rte_pmd_mcdma.h')

includes += include_directories('base')
