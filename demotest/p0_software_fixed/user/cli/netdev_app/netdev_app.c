// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#include "netdev_app.h"

#pragma GCC push_options
#pragma GCC optimize ("O0")
static void  load_data(void *buf, size_t size, uint32_t pattern)
{
	unsigned int i;

	for (i = 0; i < (size / sizeof(uint32_t)); i++)
		*(((uint32_t *)buf) + i) = pattern++;

}
#pragma GCC pop_options

static int pattern_checker(void *buf, size_t size, uint32_t expected_pattern)
{
	uint32_t *data_ptr = (uint32_t *)buf;
	uint32_t actual_pattern;
	unsigned int i;

	for (i = 0; i < size / sizeof(uint32_t); i++) {
		actual_pattern = data_ptr[i];
		if (actual_pattern != expected_pattern) {
			return -1;
		}
		expected_pattern++;
	}
	return 0;
}

static void show_help(void)
{
	printf("-h\t\tShow Help\n");
	printf("--ifname\tInterface name\n");
	printf("--pio\t\tPIO Test\n");
	printf("--bas\t\tBAS Test\n");
	printf("-r\t\tReceive Operation\n");
	printf("-t\t\tTransmit Operation\n");
	printf("-s <bytes>\tRequest Size in Bytes\n");
	printf("Required parameters: --ifname & (--pio | (--bas (-t | -r) & -s))\n");
}

static int parse_long_int(char *arg)
{
	unsigned long arg_val;
	char *c = arg;

	while(*c != '\0') {
		if (*c < 48 || *c > 57)
			return -1;
		c++;
	}
	sscanf(arg, "%lu", &arg_val);
	return arg_val;
}

static int cmdline_option_parser(int argc, char *argv[],
				 struct struct_flags *flags)
{
	int opt;
	int opt_idx;
	int fname = false;
	int transmit_counter = -1;
	int ret = -1;

	flags->direction = -1;
	flags->flimit = -1;

        static struct option lgopts[] = {
			{ "ifname", 	1, 0, 0 },
			{ "pio", 	0, 0, 0 },
			{ "bas", 	0, 0, 0 },
		};

	while ((opt = getopt_long(argc, argv, "s:hrt",
				  lgopts, &opt_idx)) != -1) {
		switch (opt) {
		case 0: /* long options */
			if (!strcmp(lgopts[opt_idx].name, "ifname")) {
				if (fname != false) {
					printf("ERR: Flags can not be repeated\n");
					return -1;
				}
				ifc_qdma_strncpy(flags->ifname,
						 sizeof(flags->ifname),
						 optarg, IFNAMSIZ);
				fname = true;
				flags->params_mask |= INTERFACE_NAME_PARAM_MASK;
			}
			if (!strcmp(lgopts[opt_idx].name, "pio")) {
				if (flags->fpio != false) {
					printf("ERR: Flags can not be repeated\n");
					return -1;
				}
				flags->fpio = true;
				flags->params_mask |= PIO_PARAM_MASK;
			}
			if (!strcmp(lgopts[opt_idx].name, "bas")) {
				if (flags->fbas != false) {
					printf("ERR: Flags can not be repeated\n");
					return -1;
				}
				flags->fbas = true;
				flags->params_mask |= BAS_PARAM_MASK;
			}
			break;
		case 'h':
			if (optind != argc) {
				printf("ERR: Invalid parameter: %s\n",
					argv[optind]);
				printf("Try help: -h\n");
				return -1;
			}
			show_help();
                        return -1;
		case 's':
			if (flags->request_size != 0UL) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			ret = parse_long_int(optarg);
			if (ret <= 0) {
				printf("ERR: Invalid -s value\n");
				return -1;
			}
			flags->request_size = ret;
			flags->flimit = REQUEST_BY_SIZE;
			flags->params_mask |= TRANSFER_SIZE_PARAM_MASK;
			break;
		case 't':
			if (flags->direction == REQUEST_TRANSMIT) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			flags->direction = REQUEST_TRANSMIT;
			transmit_counter++;
			flags->params_mask |= TX_TRANSFER_PARAM_MASK;
			break;
		case 'r':
			if (flags->direction == REQUEST_RECEIVE) {
				printf("ERR: Flags can not be repeated\n");
				return -1;
			}
			flags->direction = REQUEST_RECEIVE;
			transmit_counter++;
			flags->params_mask |= RX_TRANSFER_PARAM_MASK;
			break;
		case '?':
			printf("Invalid option\n");
			printf("Try -h for help\n");
			return -1;
		default:
			printf("Try -h for help\n");
			return -1;
		}
	}

	if (optind != argc) {
		printf("ERR: Invalid parameter: %s\n", argv[optind]);
		printf("Try -h for help\n");
		return -1;
	}

	if (!fname) {
		printf("ERR: Interface name not specified\n");
		printf("Try -h for help\n");
		return -1;
        }

	if (flags->fpio)
		return 0;

	if (flags->fbas) {
		if ((flags->params_mask == BAS_EXPECTED_MASK1) ||
		    (flags->params_mask == BAS_EXPECTED_MASK2)) {
			if ((flags->request_size %
			     IFC_MCDMA_BAS_BURST_BYTES) == 0)
				return 0;
			else {
				printf("ERR: Request size needs to be"
					" multiple of %d\n",
					IFC_MCDMA_BAS_BURST_BYTES);
				return -1;
			}
		}
	}
	printf("Try -h for help\n");
	return -1;
}

#ifdef IFC_PIO_128
static int pio_test128(int skfd, struct struct_flags *flags)
{
	uint64_t offset = PIO_ADDRESS;
	int i = 0;
	__uint128_t wvalue = 0ULL ;
	__uint128_t rvalue = 0ULL ;
	struct ifc_mcdma_netdev_priv_data write;
	struct ifc_mcdma_netdev_priv_data *read = NULL;
	struct ifreq ifr;

	/* Populate Interface name to Request struct */
	ifc_qdma_strncpy(ifr.ifr_name, sizeof(ifr.ifr_name),
			 flags->ifname, IFNAMSIZ);

	for (i = 0; i < 100; i++) {
		/* Populate private data */
		write.data128 = wvalue;
		write.offset = offset;
		ifr.ifr_data = (void *)&write;

		/* Write */
		if (ioctl(skfd, IFC_MCDMA_SET_VALUE128_AT, &ifr) < 0) {
			printf("ERR: IFC_MCDMA_SET_VALUE128_AT Failed errno %d, %s\n",
				errno, strerror(errno));
			return -1;
		}

		/* READ */
		if (ioctl(skfd, IFC_MCDMA_GET_VALUE128_AT, &ifr) < 0) {
			printf("ERR: IFC_MCDMA_GET_VALUE128_AT Failed errno %d, %s\n",
				errno, strerror(errno));
			return -1;
		}

		/* Decode data */
		read = (struct ifc_mcdma_netdev_priv_data *) ifr.ifr_data; 
		rvalue = read->data128;
		if (rvalue != wvalue) {
			printf("PIO Test Failed %u %lu %lu\n", i,
				(uint64_t)rvalue, (uint64_t)wvalue);
			return -1;
		}
		offset += 8;
		wvalue++;
	}
	printf("PIO 128b Test Successful\n");
	return 0;
}
#endif

#ifdef IFC_PIO_64
static int pio_test(int skfd, struct struct_flags *flags)
{
	uint64_t offset = PIO_ADDRESS;
	int i = 0;
	uint64_t wvalue = 0ULL;
	uint64_t rvalue = 0ULL;
	struct ifc_mcdma_netdev_priv_data write;
	struct ifc_mcdma_netdev_priv_data *read = NULL;
	struct ifreq ifr;

	/* Populate Interface name to Request struct */
	ifc_qdma_strncpy(ifr.ifr_name, sizeof(ifr.ifr_name),
			 flags->ifname, IFNAMSIZ);

	for (i = 0; i < 100; i++) {
		/* Populate private data */
		write.data = wvalue;
		write.offset = offset;
		ifr.ifr_data = (void *)&write;

		/* Write */
		if (ioctl(skfd, IFC_MCDMA_SET_VALUE_AT, &ifr) < 0) {
			printf("ERR: IFC_MCDMA_SET_VALUE_AT Failed errno %d, %s\n",
				errno, strerror(errno));
			return -1;
		}

		/* READ */
		if (ioctl(skfd, IFC_MCDMA_GET_VALUE_AT, &ifr) < 0) {
			printf("ERR: IFC_MCDMA_GET_VALUE_AT Failed errno %d, %s\n",
				errno, strerror(errno));
			return -1;
		}

		/* Decode data */
		read = (struct ifc_mcdma_netdev_priv_data *) ifr.ifr_data; 
		rvalue = read->data;
		if (rvalue != wvalue) {
			printf("PIO Test Failed %u\n", i);
			return -1;
		}
		offset += 8;
		wvalue++;
	}
	printf("PIO Test Successful\n");
	return 0;
}
#endif

static void bas_wait_ctrl_deassert(int skfd, uint64_t ctrl_offset,
				   uint64_t enable_mask,
				   struct struct_flags *flags)
{
	struct ifc_mcdma_netdev_priv_data req;
	struct ifc_mcdma_netdev_priv_data *resp = NULL;
	struct ifreq ifr;
        int shift_off;
        uint64_t val = 0;
        uint64_t ctrl_val;

#ifdef IFC_MCDMA_X16
        shift_off = IFC_MCDMA_BAS_X16_SHIFT_WIDTH;
        val = IFC_MCDMA_BAS_X16_BURST_LENGTH;
#else
        shift_off = IFC_MCDMA_BAS_X16_SHIFT_WIDTH;
        val = IFC_MCDMA_BAS_X8_BURST_LENGTH;
#endif
        val = ((val & ~enable_mask) | enable_mask);
	req.offset = ctrl_offset << shift_off;

	/* Populate Interface name to Request struct */
	ifc_qdma_strncpy(ifr.ifr_name, sizeof(ifr.ifr_name),
			 flags->ifname, IFNAMSIZ);

	ifr.ifr_data = (void *)&req;

	while (true) {
		/* Read */
		if (ioctl(skfd, IFC_MCDMA_GET_VALUE_AT, &ifr) < 0) {
			printf("ERR: IFC_MCDMA_GET_VALUE_AT Failed errno %d, %s\n",
				errno, strerror(errno));
			return;
		}

		/* Decode data */
		resp = (struct ifc_mcdma_netdev_priv_data *) ifr.ifr_data;
		ctrl_val = resp->data;
                if (!(ctrl_val & enable_mask))
                        break;
	}
}

static
uint64_t bas_check_err_count(int skfd,
			    __attribute__ ((unused)) struct ifc_mem_struct *buf,
			    uint64_t err_offset, struct struct_flags *flags)
{
	struct ifc_mcdma_netdev_priv_data req;
	struct ifc_mcdma_netdev_priv_data *resp = NULL;
	struct ifreq ifr;
        int shift_off;
        uint64_t val = 0;

#ifdef IFC_MCDMA_X16
        shift_off = IFC_MCDMA_BAS_X16_SHIFT_WIDTH;
#else
        shift_off = IFC_MCDMA_BAS_X16_SHIFT_WIDTH;
#endif
	/* Populate Interface name to Request struct */
	ifc_qdma_strncpy(ifr.ifr_name, sizeof(ifr.ifr_name),
			 flags->ifname, IFNAMSIZ);

	/* Populate Read/Write Error Register offset */
	req.offset = err_offset << shift_off;
	ifr.ifr_data = (void *)&req;

	/* Read Error count */
	if (ioctl(skfd, IFC_MCDMA_GET_VALUE_AT, &ifr) < 0) {
		printf("ERR: IFC_MCDMA_GET_VALUE_AT Failed errno %d, %s\n",
			errno, strerror(errno));
		return 1;
	}

	/* Decode data */
	resp = (struct ifc_mcdma_netdev_priv_data *) ifr.ifr_data;
	val = resp->data;

	if (flags->direction == REQUEST_TRANSMIT)
		val = pattern_checker(buf->virt_addr, flags->request_size, 0);

	return val;
}

static void bas_rx_configure(int skfd, struct struct_flags *flags)
{
	struct ifc_mcdma_netdev_priv_data rx;
	struct ifreq ifr;
	struct ifc_mem_struct *buf;
	uint64_t ret;

	buf = ifc_dma_alloc();
	if (buf == NULL) {
		printf("Failed to allocate DMA memory\n");
		return;
	}

	/* Pooulate Pattern */
	load_data(buf->virt_addr, flags->request_size, 0);

	/* Populate Physical address of start of the buffer */
	rx.data = buf->phy_addr;
	/* Populate Burst size */
	rx.burst_size = flags->request_size;

	ifr.ifr_data = (void *)&rx;

	/* Populate Interface name to Request struct */
	ifc_qdma_strncpy(ifr.ifr_name, sizeof(ifr.ifr_name),
			 flags->ifname, IFNAMSIZ);

	/* Configure BAS */
	if (ioctl(skfd, IFC_MCDMA_BAS_RX, &ifr) < 0) {
		printf("ERR: IFC_MCDMA_SET_VALUE_AT Failed errno %d, %s\n",
			errno, strerror(errno));
		ifc_dma_free(buf);
		return;
	}

	/* wait for ctrl to deassert */
	bas_wait_ctrl_deassert(skfd, IFC_MCDMA_BAS_READ_CTRL,
			       IFC_MCDMA_BAS_READ_CTRL_ENABLE_MASK, flags);


	/* Check read error count */
	ret = bas_check_err_count(skfd, buf, IFC_MCDMA_BAS_READ_ERR, flags);
	if (ret == 0)
		printf("%s:\t\t\tPass\n", flags->direction ? "Tx" : "Rx");
	else
		printf("%s:\t\t\tFailed error:%lu\n",
		       flags->direction ? "Tx" : "Rx", ret);

	ifc_dma_free(buf);
}

static void bas_tx_configure(int skfd, struct struct_flags *flags)
{
	struct ifc_mcdma_netdev_priv_data tx;
	struct ifreq ifr;
	struct ifc_mem_struct *buf;
	uint64_t ret;

	buf = ifc_dma_alloc();
	if (buf == NULL) {
		printf("Failed to allocate DMA memory\n");
		return;
	}

	/* Populate Physical address of start of the buffer */
	tx.data = buf->phy_addr;
	/* Populate Burst size */
	tx.burst_size = flags->request_size;

	ifr.ifr_data = (void *)&tx;

	/* Populate Interface name to Request struct */
	ifc_qdma_strncpy(ifr.ifr_name, sizeof(ifr.ifr_name),
			 flags->ifname, IFNAMSIZ);

	/* Configure BAS */
	if (ioctl(skfd, IFC_MCDMA_BAS_TX, &ifr) < 0) {
		printf("ERR: IFC_MCDMA_SET_VALUE_AT Failed errno %d, %s\n",
			errno, strerror(errno));
		ifc_dma_free(buf);
		return;
	}

	/* wait for ctrl to deassert */
	bas_wait_ctrl_deassert(skfd, IFC_MCDMA_BAS_WRITE_CTRL,
			       IFC_MCDMA_BAS_WRITE_CTRL_ENABLE_MASK, flags);


	/* Check write error count */
	ret = bas_check_err_count(skfd, buf, IFC_MCDMA_BAS_WRITE_ERR, flags);
	if (ret == 0)
		printf("Tx:\t\t\tPass\n");
	else
		printf("TX:\t\t\tFailed error:%lu\n", ret);

	ifc_dma_free(buf);
}

static void bas_test(int skfd, struct struct_flags *flags)
{
	if (flags == NULL) {
		printf("ERR: struct_flags is NULL\n");
		return;
	}

	if (flags->direction == REQUEST_TRANSMIT)
		bas_tx_configure(skfd, flags);
	else
		bas_rx_configure(skfd, flags);

}

int main(int argc, char *argv[])
{
	int ret = 0;
	int skfd = -1;
	struct struct_flags *flags = NULL;

	/* Allocate memory to struct_flags */
	flags = (struct struct_flags *)
		malloc(sizeof(struct struct_flags));
	if (!flags) {
		printf("Failed to allocate memory for flags\n");
		return 1;
	}

	memset(flags, 0, sizeof(struct struct_flags));

	/* Parse command line options */
	ret = cmdline_option_parser(argc, argv, flags);
	if (flags == NULL || ret) {
		free(flags);
		return ret;
	}

	/* Open a socket */
	if ((skfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		printf("ERR: Failed to open a socket");
		free(flags);
		return -1;
	}

	/* Perform PIO Read Write Test */
	if (flags->fpio) {
#ifdef IFC_PIO_128
		ret = pio_test128(skfd, flags);
#endif
#ifdef IFC_PIO_64
		ret = pio_test(skfd, flags);
#endif
	}

	if (flags->fbas) {
		ifc_env_init(flags->ifname, IFC_MCDMA_BUF_LIMIT);
		bas_test(skfd, flags);
		ifc_env_exit();
	}

	/* Close socket FD */
	if (skfd >= 0)
		close(skfd);

	free(flags);
	return ret;
}
