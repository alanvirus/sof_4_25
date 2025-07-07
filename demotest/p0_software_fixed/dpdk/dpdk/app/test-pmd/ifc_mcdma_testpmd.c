/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020-2021 Intel Corporation
 */
#include "ifc_mcdma_testpmd.h"

#ifdef IFC_QDMA_DYN_CHAN
uint16_t mcdma_ph_chno[AVST_MAX_NUM_CHAN];
#endif

#ifndef IFC_BUF_REUSE
#ifndef IFC_AVST_MULTI_PORT
#ifdef IFC_AVST_PACKETGEN
static void pio_cleanup(uint16_t portno, uint32_t qid, uint32_t dir);

static uint64_t pio_queue_config_offset(uint32_t qid)
{
#ifndef IFC_QDMA_DYN_CHAN
	uint64_t pf_chnl_offset;
	uint64_t vf_chnl_offset;
#endif
	uint64_t offset;

#ifdef IFC_QDMA_DYN_CHAN
	offset = 0;
	return offset + (8 *  mcdma_ph_chno[qid]);
#else
	if (IFC_MCDMA_CUR_VF == 0) {
		pf_chnl_offset = (IFC_MCDMA_CUR_PF - 1) * IFC_MCDMA_PER_PF_CHNLS;
		vf_chnl_offset = 0;
	} else {
		pf_chnl_offset = IFC_MCDMA_PFS * IFC_MCDMA_PER_PF_CHNLS;
		vf_chnl_offset = (IFC_MCDMA_CUR_PF - 1) *
			(IFC_MCDMA_PER_PF_VFS * IFC_MCDMA_PER_VF_CHNLS);
		vf_chnl_offset += (IFC_MCDMA_CUR_VF - 1) * IFC_MCDMA_PER_VF_CHNLS;
	}
	offset = 8 * (pf_chnl_offset + vf_chnl_offset);
#endif
	return offset + (8 * qid);
}

static void ifc_verify_tx_pkt_count(uint32_t qid,
				    uint16_t portno)
{
	uint64_t addr;
	uint64_t status;
	int pckt_count;
	int pcktlen_err;
	int pcktdata_err;
	int pckt_error;
	uint64_t vf_offset = 0ULL;
	int bfile_counter = 0;
	int gfile_counter = 0;

	/* Read the good & bad packet counter from FPGA */
	vf_offset = pio_queue_config_offset(qid);
	rte_delay_us_sleep(5000);

	addr = PIO_REG_PORT_PKT_CONFIG_H2D_BASE + vf_offset;
	status =  ifc_mcdma_pio_read64(portno, addr);
	pckt_count = (status >> 32) & ((1 << 16) - 1);
	pcktdata_err = (status >> 31) & 1;
	pcktlen_err = (status >> 30) & 1;

	pckt_error = pcktlen_err | pcktdata_err;
	if (pckt_error) {
		bfile_counter = pckt_count;
	} else {
		gfile_counter = pckt_count;
	}
	printf("CH_id %u TX: Good Files %d Bad Files %d\n",
		qid, gfile_counter, bfile_counter);
}

static int pio_init(uint16_t portno, uint32_t qid, uint16_t dir)
{
	uint64_t base;
	uint64_t offset;
	uint64_t ed_config = 0ULL;
	uint64_t packet_size = 0ULL;
	unsigned long file_size = 1;
	uint64_t files = 0ULL;
	struct rte_eth_dev *dev;

	dev = &rte_eth_devices[portno];
	packet_size = dev->data->dev_conf.rxmode.max_rx_pkt_len;

	offset = pio_queue_config_offset(qid);

	if (dir == IFC_QDMA_DIRECTION_TX) {
		base = PIO_REG_PORT_PKT_CONFIG_H2D_BASE;
		ed_config = packet_size * file_size;
	}
	if (dir == IFC_QDMA_DIRECTION_RX) {
		base = PIO_REG_PORT_PKT_CONFIG_D2H_BASE;
		ed_config = packet_size * file_size;
		files = 64;
		ed_config |= (files << PIO_REG_PORT_PKT_CONFIG_FILES_SHIFT);
                ed_config |= (1UL << PIO_REG_PORT_PKT_CONFIG_ENABLE_SHIFT);
        }
	ifc_mcdma_pio_write64(portno, base + offset, ed_config);
	return 0;
}

void ifc_single_port_pio_init(uint16_t port_id)
{
	struct rte_eth_dev *dev;
	uint32_t qid;
	int ch_cnt;

	dev = &rte_eth_devices[port_id];
	ch_cnt = ifc_mcdma_pio_read64(port_id,
			PIO_REG_PORT_PKT_CH_CNT);
#ifdef IFC_QDMA_DYN_CHAN
	uint32_t num_chan;
	num_chan = RTE_MAX(dev->data->nb_rx_queues,
		dev->data->nb_tx_queues);
	if (ch_cnt == 0)
		ch_cnt = num_chan - 1;
	else
		ch_cnt += num_chan;
#else
#ifdef IFC_ENABLE_MAX_CH
	ch_cnt = IFC_MCDMA_QUEUES_NUM_MAX;
#else
	ch_cnt = RTE_MAX(dev->data->nb_rx_queues,
			dev->data->nb_tx_queues) - 1;
#endif
	ifc_mcdma_pio_write64(port_id,
			      PIO_REG_PORT_PKT_CH_CNT, ch_cnt);
	ifc_mcdma_pio_write64(port_id,
			      PIO_REG_PORT_PKT_RST, 1ULL);
#endif
	for (qid = 0; qid < dev->data->nb_rx_queues; qid++)
		pio_init(port_id, qid, IFC_QDMA_DIRECTION_RX);

	for (qid = 0; qid < dev->data->nb_tx_queues; qid++)
		pio_init(port_id, qid, IFC_QDMA_DIRECTION_TX);
}

#ifdef MCDMA_TESTPMD_DEBUG
static int pio_read(uint16_t portid)
{
	int counter = 8;
	uint64_t offset;
	uint64_t base;
	uint64_t rvalue;
	uint32_t *ptr;
	int i;

	base = PIO_REG_PORT_PKT_CONFIG_H2D_BASE;
	offset = 0x0;

	printf("Starting address: 0x%lx, counter: %d\n", base, counter);
	for (i = 0; i < 4; i++) {
		rvalue = ifc_mcdma_pio_read64(portid, base + offset);
		ptr = (uint32_t *)(&rvalue);
		printf("Addr: 0x%lx\tVal: 0x%x 0x%x\n",
			(unsigned long)(base + offset), *ptr,
			*(ptr + 1));
		offset += 8;
	}

	base = PIO_REG_PORT_PKT_CONFIG_D2H_BASE;
	offset = 0x00;

	printf("Starting address: 0x%lx, counter: %d\n", base, counter);
	for (i = 0; i < counter; i++) {
		rvalue = ifc_mcdma_pio_read64(portid, base + offset);
		ptr = (uint32_t *)(&rvalue);
		printf("Addr: 0x%lx\tVal: 0x%x 0x%x\n",
			(unsigned long)(base + offset), *ptr,
			*(ptr + 1));
		offset += 8;
	}
	return 0;
}
#endif

static void pio_cleanup(uint16_t portno, uint32_t qid, uint32_t dir)
{
	uint64_t base;
	uint64_t offset;
	int ch_cnt;

	offset = pio_queue_config_offset(qid);

	if (dir == IFC_QDMA_DIRECTION_TX)
		base = PIO_REG_PORT_PKT_CONFIG_H2D_BASE;
	else
		base = PIO_REG_PORT_PKT_CONFIG_D2H_BASE;

	ifc_mcdma_pio_write64(portno, base + offset, 0ULL);

	/* Update channel count */
	ch_cnt = ifc_mcdma_pio_read64(portno, PIO_REG_PORT_PKT_CH_CNT);
	ch_cnt--;
	if (ch_cnt <= 0)
		ch_cnt = 0;
	ifc_mcdma_pio_write64(portno, PIO_REG_PORT_PKT_CH_CNT, ch_cnt);
#ifdef IFC_DEBUG_STATS
	ifc_mcdma_print_stats(portno, qid, dir);
#endif
}

void ifc_mcdma_pio_cleanup(uint16_t port)
{
	struct rte_eth_dev *dev;
	uint32_t qid;

	dev = &rte_eth_devices[port];

	for (qid = 0; qid < dev->data->nb_rx_queues; qid++)
		pio_cleanup(port, qid, IFC_QDMA_DIRECTION_RX);

        for (qid = 0; qid < dev->data->nb_tx_queues; qid++) {
		ifc_verify_tx_pkt_count(qid, port);
		pio_cleanup(port, qid, IFC_QDMA_DIRECTION_TX);
	}
}
#endif
#endif
#endif

#ifndef IFC_BUF_REUSE
#ifdef IFC_AVST_MULTI_PORT

#ifdef IFC_AVST_PACKETGEN
static void pio_bar_bit_writer(uint16_t portid, uint64_t offset, int n, int set)
{
	uint64_t val;
	uint64_t addr = PIO_REG_PKT_GEN_EN + offset;

	val = ifc_mcdma_pio_read64(portid, addr);
	if (set)
		val |= (1ULL << n);
	else
		val &= ~(1ULL << n);

	ifc_mcdma_pio_write64(portid, addr, val);
}
#endif

void ifc_multi_port_pio_init(uint16_t portid)
{
	uint64_t file_size;
	uint64_t val;
	uint64_t addr;
	struct rte_eth_dev *dev;
	uint16_t num_chan;

	dev = &rte_eth_devices[portid];

	num_chan = RTE_MAX(dev->data->nb_rx_queues,
		dev->data->nb_tx_queues);

        /* case: loopback */
	addr = PIO_REG_PORT_PKT_RST;
	val = ifc_mcdma_pio_read64(portid, addr);

#ifndef IFC_AVST_PACKETGEN
	/* if loopback, set the required flags */
	val |= (1ULL << num_chan) - 1;
	ifc_mcdma_pio_write64(portid, addr, val);
#else
	/* else reset and clear the flags */
	/* Reset */
	val |= (1ULL << num_chan) - 1;
	ifc_mcdma_pio_write64(portid, addr, val);
        rte_delay_us_sleep(100000);

        /* clear */
        val &= ~((1ULL << num_chan) - 1);
        ifc_mcdma_pio_write64(portid, addr, val);
#endif

	file_size = IFC_DEF_FILE_SIZE *
		dev->data->dev_conf.rxmode.max_rx_pkt_len;
	/* case: tx */
	addr = PIO_REG_EXP_PKT_LEN;
	ifc_mcdma_pio_write64(portid, addr, file_size);

	/* case: rx */
	/* Setting the file size that will be generated */
	addr = PIO_REG_PKT_LEN;
	ifc_mcdma_pio_write64(portid, addr, file_size);

	/* Setting number of files that will be generated */
	/* Let's just put a very high number here */
	addr = PIO_REG_PKT_CNT_2_GEN;
	ifc_mcdma_pio_write64(portid, addr, 1 << 31);

#ifdef IFC_AVST_PACKETGEN
	int i = 0;
	for (i = 0; i < dev->data->nb_rx_queues; i++) {
		/* For Rx: clear & set the PIO flag */
		pio_bar_bit_writer(portid, 0x00, i, false);
		pio_bar_bit_writer(portid, 0x00, i, true);
	}
#endif
}
#endif
#endif

