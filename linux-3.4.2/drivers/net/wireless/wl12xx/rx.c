/*
 * This file is part of wl1271
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/gfp.h>
#include <linux/sched.h>

#include "wl12xx.h"
#include "debug.h"
#include "acx.h"
#include "reg.h"
#include "rx.h"
#include "tx.h"
#include "io.h"

static u8 wl12xx_rx_get_mem_block(struct wl12xx_fw_status *status,
				  u32 drv_rx_counter)
{
	return le32_to_cpu(status->rx_pkt_descs[drv_rx_counter]) &
		RX_MEM_BLOCK_MASK;
}

static u32 wl12xx_rx_get_buf_size(struct wl12xx_fw_status *status,
				 u32 drv_rx_counter)
{
	return (le32_to_cpu(status->rx_pkt_descs[drv_rx_counter]) &
		RX_BUF_SIZE_MASK) >> RX_BUF_SIZE_SHIFT_DIV;
}

static bool wl12xx_rx_get_unaligned(struct wl12xx_fw_status *status,
				    u32 drv_rx_counter)
{
	/* Convert the value to bool */
	return !!(le32_to_cpu(status->rx_pkt_descs[drv_rx_counter]) &
		RX_BUF_UNALIGNED_PAYLOAD);
}

static void wl1271_rx_status(struct wl1271 *wl,
			     struct wl1271_rx_descriptor *desc,
			     struct ieee80211_rx_status *status,
			     u8 beacon)
{
	memset(status, 0, sizeof(struct ieee80211_rx_status));

	if ((desc->flags & WL1271_RX_DESC_BAND_MASK) == WL1271_RX_DESC_BAND_BG)
		status->band = IEEE80211_BAND_2GHZ;
	else
		status->band = IEEE80211_BAND_5GHZ;

	status->rate_idx = wl1271_rate_to_idx(desc->rate, status->band);

	/* 11n support */
	if (desc->rate <= CONF_HW_RXTX_RATE_MCS0)
		status->flag |= RX_FLAG_HT;

	status->signal = desc->rssi;

	/*
	 * FIXME: In wl1251, the SNR should be divided by two.  In wl1271 we
	 * need to divide by two for now, but TI has been discussing about
	 * changing it.  This needs to be rechecked.
	 */
	wl->noise = desc->rssi - (desc->snr >> 1);

	status->freq = ieee80211_channel_to_frequency(desc->channel,
						      status->band);

	if (desc->flags & WL1271_RX_DESC_ENCRYPT_MASK) {
		u8 desc_err_code = desc->status & WL1271_RX_DESC_STATUS_MASK;

		status->flag |= RX_FLAG_IV_STRIPPED | RX_FLAG_MMIC_STRIPPED |
				RX_FLAG_DECRYPTED;

		if (unlikely(desc_err_code == WL1271_RX_DESC_MIC_FAIL)) {
			status->flag |= RX_FLAG_MMIC_ERROR;
			wl1271_warning("Michael MIC error");
		}
	}
}

static int wl1271_rx_handle_data(struct wl1271 *wl, u8 *data, u32 length,
				 bool unaligned, u8 *hlid)
{
	struct wl1271_rx_descriptor *desc;
	struct sk_buff *skb;
	struct ieee80211_hdr *hdr;
	u8 *buf;
	u8 beacon = 0;
	u8 is_data = 0;
	u8 reserved = unaligned ? NET_IP_ALIGN : 0;
	u16 seq_num;

	/*
	 * In PLT mode we seem to get frames and mac80211 warns about them,
	 * workaround this by not retrieving them at all.
	 */
	if (unlikely(wl->plt))
		return -EINVAL;

	/* the data read starts with the descriptor */
	desc = (struct wl1271_rx_descriptor *) data;

	if (desc->packet_class == WL12XX_RX_CLASS_LOGGER) {
		size_t len = length - sizeof(*desc);
		wl12xx_copy_fwlog(wl, data + sizeof(*desc), len);
		wake_up_interruptible(&wl->fwlog_waitq);
		return 0;
	}

	switch (desc->status & WL1271_RX_DESC_STATUS_MASK) {
	/* discard corrupted packets */
	case WL1271_RX_DESC_DRIVER_RX_Q_FAIL:
	case WL1271_RX_DESC_DECRYPT_FAIL:
		wl1271_warning("corrupted packet in RX with status: 0x%x",
			       desc->status & WL1271_RX_DESC_STATUS_MASK);
		return -EINVAL;
	case WL1271_RX_DESC_SUCCESS:
	case WL1271_RX_DESC_MIC_FAIL:
		break;
	default:
		wl1271_error("invalid RX descriptor status: 0x%x",
			     desc->status & WL1271_RX_DESC_STATUS_MASK);
		return -EINVAL;
	}

	/* skb length not included rx descriptor */
	skb = __dev_alloc_skb(length + reserved - sizeof(*desc), GFP_KERNEL);
	if (!skb) {
		wl1271_error("Couldn't allocate RX frame");
		return -ENOMEM;
	}

	/* reserve the unaligned payload(if any) */
	skb_reserve(skb, reserved);

	buf = skb_put(skb, length - sizeof(*desc));

	/*
	 * Copy packets from aggregation buffer to the skbs without rx
	 * descriptor and with packet payload aligned care. In case of unaligned
	 * packets copy the packets in offset of 2 bytes guarantee IP header
	 * payload aligned to 4 bytes.
	 */
	memcpy(buf, data + sizeof(*desc), length - sizeof(*desc));
	*hlid = desc->hlid;

	hdr = (struct ieee80211_hdr *)skb->data;
	if (ieee80211_is_beacon(hdr->frame_control))
		beacon = 1;
	if (ieee80211_is_data_present(hdr->frame_control))
		is_data = 1;

	wl1271_rx_status(wl, desc, IEEE80211_SKB_RXCB(skb), beacon);

	seq_num = (le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_SEQ) >> 4;
	wl1271_debug(DEBUG_RX, "rx skb 0x%p: %d B %s seq %d hlid %d", skb,
		     skb->len - desc->pad_len,
		     beacon ? "beacon" : "",
		     seq_num, *hlid);

	skb_trim(skb, skb->len - desc->pad_len);

	skb_queue_tail(&wl->deferred_rx_queue, skb);
	queue_work(wl->freezable_wq, &wl->netstack_work);

	return is_data;
}

void wl12xx_rx(struct wl1271 *wl, struct wl12xx_fw_status *status)
{
	struct wl1271_acx_mem_map *wl_mem_map = wl->target_mem_map;
	unsigned long active_hlids[BITS_TO_LONGS(WL12XX_MAX_LINKS)] = {0};
	u32 buf_size;
	u32 fw_rx_counter  = status->fw_rx_counter & NUM_RX_PKT_DESC_MOD_MASK;
	u32 drv_rx_counter = wl->rx_counter & NUM_RX_PKT_DESC_MOD_MASK;
	u32 rx_counter;
	u32 mem_block;
	u32 pkt_length;
	u32 pkt_offset;
	u8 hlid;
	bool unaligned = false;

	while (drv_rx_counter != fw_rx_counter) {
		buf_size = 0;
		rx_counter = drv_rx_counter;
		while (rx_counter != fw_rx_counter) {
			pkt_length = wl12xx_rx_get_buf_size(status, rx_counter);
			if (buf_size + pkt_length > WL1271_AGGR_BUFFER_SIZE)
				break;
			buf_size += pkt_length;
			rx_counter++;
			rx_counter &= NUM_RX_PKT_DESC_MOD_MASK;
		}

		if (buf_size == 0) {
			wl1271_warning("received empty data");
			break;
		}

		if (wl->chip.id != CHIP_ID_1283_PG20) {
			/*
			 * Choose the block we want to read
			 * For aggregated packets, only the first memory block
			 * should be retrieved. The FW takes care of the rest.
			 */
			mem_block = wl12xx_rx_get_mem_block(status,
							    drv_rx_counter);

			wl->rx_mem_pool_addr.addr = (mem_block << 8) +
			   le32_to_cpu(wl_mem_map->packet_memory_pool_start);

			wl->rx_mem_pool_addr.addr_extra =
				wl->rx_mem_pool_addr.addr + 4;

			wl1271_write(wl, WL1271_SLV_REG_DATA,
				     &wl->rx_mem_pool_addr,
				     sizeof(wl->rx_mem_pool_addr), false);
		}

		/* Read all available packets at once */
		wl1271_read(wl, WL1271_SLV_MEM_DATA, wl->aggr_buf,
				buf_size, true);

		/* Split data into separate packets */
		pkt_offset = 0;
		while (pkt_offset < buf_size) {
			pkt_length = wl12xx_rx_get_buf_size(status,
					drv_rx_counter);

			unaligned = wl12xx_rx_get_unaligned(status,
					drv_rx_counter);

			/*
			 * the handle data call can only fail in memory-outage
			 * conditions, in that case the received frame will just
			 * be dropped.
			 */
			if (wl1271_rx_handle_data(wl,
						  wl->aggr_buf + pkt_offset,
						  pkt_length, unaligned,
						  &hlid) == 1) {
				if (hlid < WL12XX_MAX_LINKS)
					__set_bit(hlid, active_hlids);
				else
					WARN(1,
					     "hlid exceeded WL12XX_MAX_LINKS "
					     "(%d)\n", hlid);
			}

			wl->rx_counter++;
			drv_rx_counter++;
			drv_rx_counter &= NUM_RX_PKT_DESC_MOD_MASK;
			pkt_offset += pkt_length;
		}
	}

	/*
	 * Write the driver's packet counter to the FW. This is only required
	 * for older hardware revisions
	 */
	if (wl->quirks & WL12XX_QUIRK_END_OF_TRANSACTION)
		wl1271_write32(wl, RX_DRIVER_COUNTER_ADDRESS, wl->rx_counter);

	wl12xx_rearm_rx_streaming(wl, active_hlids);
}
