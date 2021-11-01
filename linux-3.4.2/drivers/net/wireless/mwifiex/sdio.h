/*
 * Marvell Wireless LAN device driver: SDIO specific definitions
 *
 * Copyright (C) 2011, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#ifndef	_MWIFIEX_SDIO_H
#define	_MWIFIEX_SDIO_H


#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/card.h>

#include "main.h"

#define SD8787_DEFAULT_FW_NAME "mrvl/sd8787_uapsta.bin"
#define SD8797_DEFAULT_FW_NAME "mrvl/sd8797_uapsta.bin"

#define BLOCK_MODE	1
#define BYTE_MODE	0

#define REG_PORT			0
#define RD_BITMAP_L			0x04
#define RD_BITMAP_U			0x05
#define WR_BITMAP_L			0x06
#define WR_BITMAP_U			0x07
#define RD_LEN_P0_L			0x08
#define RD_LEN_P0_U			0x09

#define MWIFIEX_SDIO_IO_PORT_MASK		0xfffff

#define MWIFIEX_SDIO_BYTE_MODE_MASK	0x80000000

#define CTRL_PORT			0
#define CTRL_PORT_MASK			0x0001
#define DATA_PORT_MASK			0xfffe

#define MAX_MP_REGS			64
#define MAX_PORT			16

#define SDIO_MP_AGGR_DEF_PKT_LIMIT	8

#define SDIO_MP_TX_AGGR_DEF_BUF_SIZE        (8192)	/* 8K */

/* Multi port RX aggregation buffer size */
#define SDIO_MP_RX_AGGR_DEF_BUF_SIZE        (16384)	/* 16K */

/* Misc. Config Register : Auto Re-enable interrupts */
#define AUTO_RE_ENABLE_INT              BIT(4)

/* Host Control Registers */
/* Host Control Registers : I/O port 0 */
#define IO_PORT_0_REG			0x78
/* Host Control Registers : I/O port 1 */
#define IO_PORT_1_REG			0x79
/* Host Control Registers : I/O port 2 */
#define IO_PORT_2_REG			0x7A

/* Host Control Registers : Configuration */
#define CONFIGURATION_REG		0x00
/* Host Control Registers : Host without Command 53 finish host*/
#define HOST_TO_CARD_EVENT       (0x1U << 3)
/* Host Control Registers : Host without Command 53 finish host */
#define HOST_WO_CMD53_FINISH_HOST	(0x1U << 2)
/* Host Control Registers : Host power up */
#define HOST_POWER_UP			(0x1U << 1)
/* Host Control Registers : Host power down */
#define HOST_POWER_DOWN			(0x1U << 0)

/* Host Control Registers : Host interrupt mask */
#define HOST_INT_MASK_REG		0x02
/* Host Control Registers : Upload host interrupt mask */
#define UP_LD_HOST_INT_MASK		(0x1U)
/* Host Control Registers : Download host interrupt mask */
#define DN_LD_HOST_INT_MASK		(0x2U)
/* Enable Host interrupt mask */
#define HOST_INT_ENABLE	(UP_LD_HOST_INT_MASK | DN_LD_HOST_INT_MASK)
/* Disable Host interrupt mask */
#define	HOST_INT_DISABLE		0xff

/* Host Control Registers : Host interrupt status */
#define HOST_INTSTATUS_REG		0x03
/* Host Control Registers : Upload host interrupt status */
#define UP_LD_HOST_INT_STATUS		(0x1U)
/* Host Control Registers : Download host interrupt status */
#define DN_LD_HOST_INT_STATUS		(0x2U)

/* Host Control Registers : Host interrupt RSR */
#define HOST_INT_RSR_REG		0x01
/* Host Control Registers : Upload host interrupt RSR */
#define UP_LD_HOST_INT_RSR		(0x1U)
#define SDIO_INT_MASK			0x3F

/* Host Control Registers : Host interrupt status */
#define HOST_INT_STATUS_REG		0x28
/* Host Control Registers : Upload CRC error */
#define UP_LD_CRC_ERR			(0x1U << 2)
/* Host Control Registers : Upload restart */
#define UP_LD_RESTART                   (0x1U << 1)
/* Host Control Registers : Download restart */
#define DN_LD_RESTART                   (0x1U << 0)

/* Card Control Registers : Card status register */
#define CARD_STATUS_REG                 0x30
/* Card Control Registers : Card I/O ready */
#define CARD_IO_READY                   (0x1U << 3)
/* Card Control Registers : CIS card ready */
#define CIS_CARD_RDY                    (0x1U << 2)
/* Card Control Registers : Upload card ready */
#define UP_LD_CARD_RDY                  (0x1U << 1)
/* Card Control Registers : Download card ready */
#define DN_LD_CARD_RDY                  (0x1U << 0)

/* Card Control Registers : Host interrupt mask register */
#define HOST_INTERRUPT_MASK_REG         0x34
/* Card Control Registers : Host power interrupt mask */
#define HOST_POWER_INT_MASK             (0x1U << 3)
/* Card Control Registers : Abort card interrupt mask */
#define ABORT_CARD_INT_MASK             (0x1U << 2)
/* Card Control Registers : Upload card interrupt mask */
#define UP_LD_CARD_INT_MASK             (0x1U << 1)
/* Card Control Registers : Download card interrupt mask */
#define DN_LD_CARD_INT_MASK             (0x1U << 0)

/* Card Control Registers : Card interrupt status register */
#define CARD_INTERRUPT_STATUS_REG       0x38
/* Card Control Registers : Power up interrupt */
#define POWER_UP_INT                    (0x1U << 4)
/* Card Control Registers : Power down interrupt */
#define POWER_DOWN_INT                  (0x1U << 3)

/* Card Control Registers : Card interrupt RSR register */
#define CARD_INTERRUPT_RSR_REG          0x3c
/* Card Control Registers : Power up RSR */
#define POWER_UP_RSR                    (0x1U << 4)
/* Card Control Registers : Power down RSR */
#define POWER_DOWN_RSR                  (0x1U << 3)

/* Card Control Registers : Miscellaneous Configuration Register */
#define CARD_MISC_CFG_REG               0x6C

/* Host F1 read base 0 */
#define HOST_F1_RD_BASE_0		0x0040
/* Host F1 read base 1 */
#define HOST_F1_RD_BASE_1		0x0041
/* Host F1 card ready */
#define HOST_F1_CARD_RDY		0x0020

/* Firmware status 0 register */
#define CARD_FW_STATUS0_REG		0x60
/* Firmware status 1 register */
#define CARD_FW_STATUS1_REG		0x61
/* Rx length register */
#define CARD_RX_LEN_REG			0x62
/* Rx unit register */
#define CARD_RX_UNIT_REG		0x63

/* Max retry number of CMD53 write */
#define MAX_WRITE_IOMEM_RETRY		2

/* SDIO Tx aggregation in progress ? */
#define MP_TX_AGGR_IN_PROGRESS(a) (a->mpa_tx.pkt_cnt > 0)

/* SDIO Tx aggregation buffer room for next packet ? */
#define MP_TX_AGGR_BUF_HAS_ROOM(a, len) ((a->mpa_tx.buf_len+len)	\
						<= a->mpa_tx.buf_size)

/* Copy current packet (SDIO Tx aggregation buffer) to SDIO buffer */
#define MP_TX_AGGR_BUF_PUT(a, payload, pkt_len, port) do {		\
	memmove(&a->mpa_tx.buf[a->mpa_tx.buf_len],			\
			payload, pkt_len);				\
	a->mpa_tx.buf_len += pkt_len;					\
	if (!a->mpa_tx.pkt_cnt)						\
		a->mpa_tx.start_port = port;				\
	if (a->mpa_tx.start_port <= port)				\
		a->mpa_tx.ports |= (1<<(a->mpa_tx.pkt_cnt));		\
	else								\
		a->mpa_tx.ports |= (1<<(a->mpa_tx.pkt_cnt+1+(MAX_PORT -	\
						a->mp_end_port)));	\
	a->mpa_tx.pkt_cnt++;						\
} while (0);

/* SDIO Tx aggregation limit ? */
#define MP_TX_AGGR_PKT_LIMIT_REACHED(a)					\
			(a->mpa_tx.pkt_cnt == a->mpa_tx.pkt_aggr_limit)

/* SDIO Tx aggregation port limit ? */
#define MP_TX_AGGR_PORT_LIMIT_REACHED(a) ((a->curr_wr_port <		\
			a->mpa_tx.start_port) && (((MAX_PORT -		\
			a->mpa_tx.start_port) + a->curr_wr_port) >=	\
				SDIO_MP_AGGR_DEF_PKT_LIMIT))

/* Reset SDIO Tx aggregation buffer parameters */
#define MP_TX_AGGR_BUF_RESET(a) do {					\
	a->mpa_tx.pkt_cnt = 0;						\
	a->mpa_tx.buf_len = 0;						\
	a->mpa_tx.ports = 0;						\
	a->mpa_tx.start_port = 0;					\
} while (0);

/* SDIO Rx aggregation limit ? */
#define MP_RX_AGGR_PKT_LIMIT_REACHED(a)					\
			(a->mpa_rx.pkt_cnt == a->mpa_rx.pkt_aggr_limit)

/* SDIO Tx aggregation port limit ? */
#define MP_RX_AGGR_PORT_LIMIT_REACHED(a) ((a->curr_rd_port <		\
			a->mpa_rx.start_port) && (((MAX_PORT -		\
			a->mpa_rx.start_port) + a->curr_rd_port) >=	\
			SDIO_MP_AGGR_DEF_PKT_LIMIT))

/* SDIO Rx aggregation in progress ? */
#define MP_RX_AGGR_IN_PROGRESS(a) (a->mpa_rx.pkt_cnt > 0)

/* SDIO Rx aggregation buffer room for next packet ? */
#define MP_RX_AGGR_BUF_HAS_ROOM(a, rx_len)				\
			((a->mpa_rx.buf_len+rx_len) <= a->mpa_rx.buf_size)

/* Prepare to copy current packet from card to SDIO Rx aggregation buffer */
#define MP_RX_AGGR_SETUP(a, skb, port) do {				\
	a->mpa_rx.buf_len += skb->len;					\
	if (!a->mpa_rx.pkt_cnt)						\
		a->mpa_rx.start_port = port;				\
	if (a->mpa_rx.start_port <= port)				\
		a->mpa_rx.ports |= (1<<(a->mpa_rx.pkt_cnt));		\
	else								\
		a->mpa_rx.ports |= (1<<(a->mpa_rx.pkt_cnt+1));		\
	a->mpa_rx.skb_arr[a->mpa_rx.pkt_cnt] = skb;			\
	a->mpa_rx.len_arr[a->mpa_rx.pkt_cnt] = skb->len;		\
	a->mpa_rx.pkt_cnt++;						\
} while (0);

/* Reset SDIO Rx aggregation buffer parameters */
#define MP_RX_AGGR_BUF_RESET(a) do {					\
	a->mpa_rx.pkt_cnt = 0;						\
	a->mpa_rx.buf_len = 0;						\
	a->mpa_rx.ports = 0;						\
	a->mpa_rx.start_port = 0;					\
} while (0);


/* data structure for SDIO MPA TX */
struct mwifiex_sdio_mpa_tx {
	/* multiport tx aggregation buffer pointer */
	u8 *buf;
	u32 buf_len;
	u32 pkt_cnt;
	u16 ports;
	u16 start_port;
	u8 enabled;
	u32 buf_size;
	u32 pkt_aggr_limit;
};

struct mwifiex_sdio_mpa_rx {
	u8 *buf;
	u32 buf_len;
	u32 pkt_cnt;
	u16 ports;
	u16 start_port;

	struct sk_buff *skb_arr[SDIO_MP_AGGR_DEF_PKT_LIMIT];
	u32 len_arr[SDIO_MP_AGGR_DEF_PKT_LIMIT];

	u8 enabled;
	u32 buf_size;
	u32 pkt_aggr_limit;
};

int mwifiex_bus_register(void);
void mwifiex_bus_unregister(void);

struct sdio_mmc_card {
	struct sdio_func *func;
	struct mwifiex_adapter *adapter;

	u16 mp_rd_bitmap;
	u16 mp_wr_bitmap;

	u16 mp_end_port;
	u16 mp_data_port_mask;

	u8 curr_rd_port;
	u8 curr_wr_port;

	u8 *mp_regs;

	struct mwifiex_sdio_mpa_tx mpa_tx;
	struct mwifiex_sdio_mpa_rx mpa_rx;
};

/*
 * .cmdrsp_complete handler
 */
static inline int mwifiex_sdio_cmdrsp_complete(struct mwifiex_adapter *adapter,
					       struct sk_buff *skb)
{
	dev_kfree_skb_any(skb);
	return 0;
}

/*
 * .event_complete handler
 */
static inline int mwifiex_sdio_event_complete(struct mwifiex_adapter *adapter,
					      struct sk_buff *skb)
{
	dev_kfree_skb_any(skb);
	return 0;
}

#endif /* _MWIFIEX_SDIO_H */
