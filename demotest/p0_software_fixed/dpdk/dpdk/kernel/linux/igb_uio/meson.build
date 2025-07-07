# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2017 Intel Corporation

mkfile = custom_target('igb_uio_makefile',
	output: 'Makefile',
	command: ['touch', '@OUTPUT@'])

custom_target('igb_uio',
	input: ['igb_uio.c', 'Kbuild'],
	output: 'igb_uio.ko',
	command: ['make', '-C', kernel_build_dir,
		'M=' + meson.current_build_dir(),
		'src=' + meson.current_source_dir(),
                ' '.join(['MODULE_CFLAGS=', kmod_cflags,'-include '])
                + dpdk_source_root + '/config/rte_config.h' +
                ' -I' + dpdk_source_root + '/lib/eal/include' +
                ' -I' + dpdk_build_root +
                ' -I' + meson.current_source_dir() +
                ' -Wno-implicit-fallthrough',
		'modules'],
	depends: mkfile,
	install: true,
	install_dir: kernel_install_dir + '/extra/dpdk',
	build_by_default: get_option('enable_kmods'))
