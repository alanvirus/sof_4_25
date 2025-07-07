// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __NETDEV_APP_
#define __NETDEV_APP_

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/if.h>
#include <inttypes.h>
#include <getopt.h>
#include <stdbool.h> 
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stddef.h>
#include <ifc_qdma_utils.h>
#include <ifc_libmcmem.h>

/* IOCTL Commands */
#define IFC_MCDMA_SET_VALUE_AT          (SIOCDEVPRIVATE)
#define IFC_MCDMA_GET_VALUE_AT          (SIOCDEVPRIVATE + 1)
#define IFC_MCDMA_BAS_TX		(SIOCDEVPRIVATE + 2)
#define IFC_MCDMA_BAS_RX		(SIOCDEVPRIVATE + 3)
#define IFC_MCDMA_SET_VALUE128_AT       (SIOCDEVPRIVATE + 4)
#define IFC_MCDMA_GET_VALUE128_AT       (SIOCDEVPRIVATE + 5)
#define IFC_MCDMA_SET_VALUE256_AT       (SIOCDEVPRIVATE + 6)
#define IFC_MCDMA_GET_VALUE256_AT       (SIOCDEVPRIVATE + 7)

/**
 * Payload Limit
 */
#define IFC_MCDMA_BUF_LIMIT             (1UL << 20)

#define PIO_ADDRESS 0x1000

/* Paramter Masks */
#define INTERFACE_NAME_PARAM_MASK	0x1
#define PIO_PARAM_MASK			0x2
#define TX_TRANSFER_PARAM_MASK		0x4
#define RX_TRANSFER_PARAM_MASK		0x8
#define TRANSFER_SIZE_PARAM_MASK	0x10
#define BAS_PARAM_MASK			0x20


#define REQUEST_RECEIVE			0
#define REQUEST_TRANSMIT		1
#define REQUEST_BY_SIZE 		0

#define BAS_EXPECTED_MASK1	(BAS_PARAM_MASK | TRANSFER_SIZE_PARAM_MASK |  INTERFACE_NAME_PARAM_MASK | TX_TRANSFER_PARAM_MASK)
#define BAS_EXPECTED_MASK2	(BAS_PARAM_MASK | TRANSFER_SIZE_PARAM_MASK |  INTERFACE_NAME_PARAM_MASK | RX_TRANSFER_PARAM_MASK)

/* BAS Addresses and offsets */
/* Read specific */
#define IFC_MCDMA_BAS_READ_ADDR                         0x00000000UL
#define IFC_MCDMA_BAS_READ_COUNT                        0x00000008UL
#define IFC_MCDMA_BAS_READ_ERR                          0x00000010UL
#define IFC_MCDMA_BAS_READ_CTRL                         0x00000018UL

#define IFC_MCDMA_BAS_READ_CTRL_TRANSFER_SIZE_SHIFT     0
#define IFC_MCDMA_BAS_READ_CTRL_TRANSFER_SIZE_WIDTH     8
#define IFC_MCDMA_BAS_READ_CTRL_TRANSFER_SIZE_MASK      0x0FFU

#define IFC_MCDMA_BAS_READ_CTRL_ENABLE_SHIFT            31
#define IFC_MCDMA_BAS_READ_CTRL_ENABLE_WIDTH            1
#define IFC_MCDMA_BAS_READ_CTRL_ENABLE_MASK             0x80000000UL


/* Write specific */
#define IFC_MCDMA_BAS_WRITE_ADDR                        0x00000020UL
#define IFC_MCDMA_BAS_WRITE_COUNT                       0x00000028UL
#define IFC_MCDMA_BAS_WRITE_ERR                         0x00000030UL
#define IFC_MCDMA_BAS_WRITE_CTRL                        0x00000038UL

#define IFC_MCDMA_BAS_WRITE_CTRL_TRANSFER_SIZE_SHIFT    0
#define IFC_MCDMA_BAS_WRITE_CTRL_TRANSFER_SIZE_WIDTH    8
#define IFC_MCDMA_BAS_WRITE_CTRL_TRANSFER_SIZE_MASK     0x0FFU

#define IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_SHIFT           31
#define IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_WIDTH           1
#define IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_MASK            0x80000000UL

#define IFC_MCDMA_BAS_READ_MAP_TABLE                    0x00000100UL
#define IFC_MCDMA_BAS_WRITE_MAP_TABLE                   0x00000200UL

#define IFC_MCDMA_BAS_X16_SHIFT_WIDTH                   0
#define IFC_MCDMA_BAS_X8_SHIFT_WIDTH                    0

#define IFC_MCDMA_BAS_X16_BURST_LENGTH                  8
#define IFC_MCDMA_BAS_X8_BURST_LENGTH                   16
#define IFC_MCDMA_BAS_BURST_BYTES                       512

#define IFC_MCDMA_BAS_TRANSFER_COUNT_MASK               0xFFFFFFFFUL
#define IFC_MCDMA_BAS_NON_STOP_TRANSFER_ENABLE_MASK     0x80000000UL
#define IFC_MCDMA_BAS_NON_STOP_TRANSFER_DISABLE_MASK    0x7FFFFFFFUL

struct ifc_mcdma_netdev_priv_data {
	uint64_t data;
	__uint128_t data128;
	uint64_t offset;
	uint64_t burst_size;
};

struct struct_flags {
	char ifname[IFNAMSIZ];/* Interface name */
	int fpio;
	int flimit;
	int fbas;
	int direction;
	size_t request_size;
	uint64_t params_mask;
};
#endif /* __NETDEV_APP_ */
