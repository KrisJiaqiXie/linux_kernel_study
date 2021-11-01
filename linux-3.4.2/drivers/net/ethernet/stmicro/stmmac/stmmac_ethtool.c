/*******************************************************************************
  STMMAC Ethtool support

  Copyright (C) 2007-2009  STMicroelectronics Ltd

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/interrupt.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <asm/io.h>

#include "stmmac.h"
#include "dwmac_dma.h"

#define REG_SPACE_SIZE	0x1054
#define MAC100_ETHTOOL_NAME	"st_mac100"
#define GMAC_ETHTOOL_NAME	"st_gmac"

struct stmmac_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define STMMAC_STAT(m)	\
	{ #m, FIELD_SIZEOF(struct stmmac_extra_stats, m),	\
	offsetof(struct stmmac_priv, xstats.m)}

static const struct stmmac_stats stmmac_gstrings_stats[] = {
	/* Transmit errors */
	STMMAC_STAT(tx_underflow),
	STMMAC_STAT(tx_carrier),
	STMMAC_STAT(tx_losscarrier),
	STMMAC_STAT(vlan_tag),
	STMMAC_STAT(tx_deferred),
	STMMAC_STAT(tx_vlan),
	STMMAC_STAT(tx_jabber),
	STMMAC_STAT(tx_frame_flushed),
	STMMAC_STAT(tx_payload_error),
	STMMAC_STAT(tx_ip_header_error),
	/* Receive errors */
	STMMAC_STAT(rx_desc),
	STMMAC_STAT(sa_filter_fail),
	STMMAC_STAT(overflow_error),
	STMMAC_STAT(ipc_csum_error),
	STMMAC_STAT(rx_collision),
	STMMAC_STAT(rx_crc),
	STMMAC_STAT(dribbling_bit),
	STMMAC_STAT(rx_length),
	STMMAC_STAT(rx_mii),
	STMMAC_STAT(rx_multicast),
	STMMAC_STAT(rx_gmac_overflow),
	STMMAC_STAT(rx_watchdog),
	STMMAC_STAT(da_rx_filter_fail),
	STMMAC_STAT(sa_rx_filter_fail),
	STMMAC_STAT(rx_missed_cntr),
	STMMAC_STAT(rx_overflow_cntr),
	STMMAC_STAT(rx_vlan),
	/* Tx/Rx IRQ errors */
	STMMAC_STAT(tx_undeflow_irq),
	STMMAC_STAT(tx_process_stopped_irq),
	STMMAC_STAT(tx_jabber_irq),
	STMMAC_STAT(rx_overflow_irq),
	STMMAC_STAT(rx_buf_unav_irq),
	STMMAC_STAT(rx_process_stopped_irq),
	STMMAC_STAT(rx_watchdog_irq),
	STMMAC_STAT(tx_early_irq),
	STMMAC_STAT(fatal_bus_error_irq),
	/* Extra info */
	STMMAC_STAT(threshold),
	STMMAC_STAT(tx_pkt_n),
	STMMAC_STAT(rx_pkt_n),
	STMMAC_STAT(poll_n),
	STMMAC_STAT(sched_timer_n),
	STMMAC_STAT(normal_irq_n),
};
#define STMMAC_STATS_LEN ARRAY_SIZE(stmmac_gstrings_stats)

/* HW MAC Management counters (if supported) */
#define STMMAC_MMC_STAT(m)	\
	{ #m, FIELD_SIZEOF(struct stmmac_counters, m),	\
	offsetof(struct stmmac_priv, mmc.m)}

static const struct stmmac_stats stmmac_mmc[] = {
	STMMAC_MMC_STAT(mmc_tx_octetcount_gb),
	STMMAC_MMC_STAT(mmc_tx_framecount_gb),
	STMMAC_MMC_STAT(mmc_tx_broadcastframe_g),
	STMMAC_MMC_STAT(mmc_tx_multicastframe_g),
	STMMAC_MMC_STAT(mmc_tx_64_octets_gb),
	STMMAC_MMC_STAT(mmc_tx_65_to_127_octets_gb),
	STMMAC_MMC_STAT(mmc_tx_128_to_255_octets_gb),
	STMMAC_MMC_STAT(mmc_tx_256_to_511_octets_gb),
	STMMAC_MMC_STAT(mmc_tx_512_to_1023_octets_gb),
	STMMAC_MMC_STAT(mmc_tx_1024_to_max_octets_gb),
	STMMAC_MMC_STAT(mmc_tx_unicast_gb),
	STMMAC_MMC_STAT(mmc_tx_multicast_gb),
	STMMAC_MMC_STAT(mmc_tx_broadcast_gb),
	STMMAC_MMC_STAT(mmc_tx_underflow_error),
	STMMAC_MMC_STAT(mmc_tx_singlecol_g),
	STMMAC_MMC_STAT(mmc_tx_multicol_g),
	STMMAC_MMC_STAT(mmc_tx_deferred),
	STMMAC_MMC_STAT(mmc_tx_latecol),
	STMMAC_MMC_STAT(mmc_tx_exesscol),
	STMMAC_MMC_STAT(mmc_tx_carrier_error),
	STMMAC_MMC_STAT(mmc_tx_octetcount_g),
	STMMAC_MMC_STAT(mmc_tx_framecount_g),
	STMMAC_MMC_STAT(mmc_tx_excessdef),
	STMMAC_MMC_STAT(mmc_tx_pause_frame),
	STMMAC_MMC_STAT(mmc_tx_vlan_frame_g),
	STMMAC_MMC_STAT(mmc_rx_framecount_gb),
	STMMAC_MMC_STAT(mmc_rx_octetcount_gb),
	STMMAC_MMC_STAT(mmc_rx_octetcount_g),
	STMMAC_MMC_STAT(mmc_rx_broadcastframe_g),
	STMMAC_MMC_STAT(mmc_rx_multicastframe_g),
	STMMAC_MMC_STAT(mmc_rx_crc_errror),
	STMMAC_MMC_STAT(mmc_rx_align_error),
	STMMAC_MMC_STAT(mmc_rx_run_error),
	STMMAC_MMC_STAT(mmc_rx_jabber_error),
	STMMAC_MMC_STAT(mmc_rx_undersize_g),
	STMMAC_MMC_STAT(mmc_rx_oversize_g),
	STMMAC_MMC_STAT(mmc_rx_64_octets_gb),
	STMMAC_MMC_STAT(mmc_rx_65_to_127_octets_gb),
	STMMAC_MMC_STAT(mmc_rx_128_to_255_octets_gb),
	STMMAC_MMC_STAT(mmc_rx_256_to_511_octets_gb),
	STMMAC_MMC_STAT(mmc_rx_512_to_1023_octets_gb),
	STMMAC_MMC_STAT(mmc_rx_1024_to_max_octets_gb),
	STMMAC_MMC_STAT(mmc_rx_unicast_g),
	STMMAC_MMC_STAT(mmc_rx_length_error),
	STMMAC_MMC_STAT(mmc_rx_autofrangetype),
	STMMAC_MMC_STAT(mmc_rx_pause_frames),
	STMMAC_MMC_STAT(mmc_rx_fifo_overflow),
	STMMAC_MMC_STAT(mmc_rx_vlan_frames_gb),
	STMMAC_MMC_STAT(mmc_rx_watchdog_error),
	STMMAC_MMC_STAT(mmc_rx_ipc_intr_mask),
	STMMAC_MMC_STAT(mmc_rx_ipc_intr),
	STMMAC_MMC_STAT(mmc_rx_ipv4_gd),
	STMMAC_MMC_STAT(mmc_rx_ipv4_hderr),
	STMMAC_MMC_STAT(mmc_rx_ipv4_nopay),
	STMMAC_MMC_STAT(mmc_rx_ipv4_frag),
	STMMAC_MMC_STAT(mmc_rx_ipv4_udsbl),
	STMMAC_MMC_STAT(mmc_rx_ipv4_gd_octets),
	STMMAC_MMC_STAT(mmc_rx_ipv4_hderr_octets),
	STMMAC_MMC_STAT(mmc_rx_ipv4_nopay_octets),
	STMMAC_MMC_STAT(mmc_rx_ipv4_frag_octets),
	STMMAC_MMC_STAT(mmc_rx_ipv4_udsbl_octets),
	STMMAC_MMC_STAT(mmc_rx_ipv6_gd_octets),
	STMMAC_MMC_STAT(mmc_rx_ipv6_hderr_octets),
	STMMAC_MMC_STAT(mmc_rx_ipv6_nopay_octets),
	STMMAC_MMC_STAT(mmc_rx_ipv6_gd),
	STMMAC_MMC_STAT(mmc_rx_ipv6_hderr),
	STMMAC_MMC_STAT(mmc_rx_ipv6_nopay),
	STMMAC_MMC_STAT(mmc_rx_udp_gd),
	STMMAC_MMC_STAT(mmc_rx_udp_err),
	STMMAC_MMC_STAT(mmc_rx_tcp_gd),
	STMMAC_MMC_STAT(mmc_rx_tcp_err),
	STMMAC_MMC_STAT(mmc_rx_icmp_gd),
	STMMAC_MMC_STAT(mmc_rx_icmp_err),
	STMMAC_MMC_STAT(mmc_rx_udp_gd_octets),
	STMMAC_MMC_STAT(mmc_rx_udp_err_octets),
	STMMAC_MMC_STAT(mmc_rx_tcp_gd_octets),
	STMMAC_MMC_STAT(mmc_rx_tcp_err_octets),
	STMMAC_MMC_STAT(mmc_rx_icmp_gd_octets),
	STMMAC_MMC_STAT(mmc_rx_icmp_err_octets),
};
#define STMMAC_MMC_STATS_LEN ARRAY_SIZE(stmmac_mmc)

static void stmmac_ethtool_getdrvinfo(struct net_device *dev,
				      struct ethtool_drvinfo *info)
{
	struct stmmac_priv *priv = netdev_priv(dev);

	if (priv->plat->has_gmac)
		strlcpy(info->driver, GMAC_ETHTOOL_NAME, sizeof(info->driver));
	else
		strlcpy(info->driver, MAC100_ETHTOOL_NAME,
			sizeof(info->driver));

	strcpy(info->version, DRV_MODULE_VERSION);
	info->fw_version[0] = '\0';
}

static int stmmac_ethtool_getsettings(struct net_device *dev,
				      struct ethtool_cmd *cmd)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	struct phy_device *phy = priv->phydev;
	int rc;
	if (phy == NULL) {
		pr_err("%s: %s: PHY is not registered\n",
		       __func__, dev->name);
		return -ENODEV;
	}
	if (!netif_running(dev)) {
		pr_err("%s: interface is disabled: we cannot track "
		"link speed / duplex setting\n", dev->name);
		return -EBUSY;
	}
	cmd->transceiver = XCVR_INTERNAL;
	spin_lock_irq(&priv->lock);
	rc = phy_ethtool_gset(phy, cmd);
	spin_unlock_irq(&priv->lock);
	return rc;
}

static int stmmac_ethtool_setsettings(struct net_device *dev,
				      struct ethtool_cmd *cmd)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	struct phy_device *phy = priv->phydev;
	int rc;

	spin_lock(&priv->lock);
	rc = phy_ethtool_sset(phy, cmd);
	spin_unlock(&priv->lock);

	return rc;
}

static u32 stmmac_ethtool_getmsglevel(struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	return priv->msg_enable;
}

static void stmmac_ethtool_setmsglevel(struct net_device *dev, u32 level)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	priv->msg_enable = level;

}

static int stmmac_check_if_running(struct net_device *dev)
{
	if (!netif_running(dev))
		return -EBUSY;
	return 0;
}

static int stmmac_ethtool_get_regs_len(struct net_device *dev)
{
	return REG_SPACE_SIZE;
}

static void stmmac_ethtool_gregs(struct net_device *dev,
			  struct ethtool_regs *regs, void *space)
{
	int i;
	u32 *reg_space = (u32 *) space;

	struct stmmac_priv *priv = netdev_priv(dev);

	memset(reg_space, 0x0, REG_SPACE_SIZE);

	if (!priv->plat->has_gmac) {
		/* MAC registers */
		for (i = 0; i < 12; i++)
			reg_space[i] = readl(priv->ioaddr + (i * 4));
		/* DMA registers */
		for (i = 0; i < 9; i++)
			reg_space[i + 12] =
			    readl(priv->ioaddr + (DMA_BUS_MODE + (i * 4)));
		reg_space[22] = readl(priv->ioaddr + DMA_CUR_TX_BUF_ADDR);
		reg_space[23] = readl(priv->ioaddr + DMA_CUR_RX_BUF_ADDR);
	} else {
		/* MAC registers */
		for (i = 0; i < 55; i++)
			reg_space[i] = readl(priv->ioaddr + (i * 4));
		/* DMA registers */
		for (i = 0; i < 22; i++)
			reg_space[i + 55] =
			    readl(priv->ioaddr + (DMA_BUS_MODE + (i * 4)));
	}
}

static void
stmmac_get_pauseparam(struct net_device *netdev,
		      struct ethtool_pauseparam *pause)
{
	struct stmmac_priv *priv = netdev_priv(netdev);

	spin_lock(&priv->lock);

	pause->rx_pause = 0;
	pause->tx_pause = 0;
	pause->autoneg = priv->phydev->autoneg;

	if (priv->flow_ctrl & FLOW_RX)
		pause->rx_pause = 1;
	if (priv->flow_ctrl & FLOW_TX)
		pause->tx_pause = 1;

	spin_unlock(&priv->lock);
}

static int
stmmac_set_pauseparam(struct net_device *netdev,
		      struct ethtool_pauseparam *pause)
{
	struct stmmac_priv *priv = netdev_priv(netdev);
	struct phy_device *phy = priv->phydev;
	int new_pause = FLOW_OFF;
	int ret = 0;

	spin_lock(&priv->lock);

	if (pause->rx_pause)
		new_pause |= FLOW_RX;
	if (pause->tx_pause)
		new_pause |= FLOW_TX;

	priv->flow_ctrl = new_pause;
	phy->autoneg = pause->autoneg;

	if (phy->autoneg) {
		if (netif_running(netdev))
			ret = phy_start_aneg(phy);
	} else
		priv->hw->mac->flow_ctrl(priv->ioaddr, phy->duplex,
					 priv->flow_ctrl, priv->pause);
	spin_unlock(&priv->lock);
	return ret;
}

static void stmmac_get_ethtool_stats(struct net_device *dev,
				 struct ethtool_stats *dummy, u64 *data)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	int i, j = 0;

	/* Update the DMA HW counters for dwmac10/100 */
	if (!priv->plat->has_gmac)
		priv->hw->dma->dma_diagnostic_fr(&dev->stats,
						 (void *) &priv->xstats,
						 priv->ioaddr);
	else {
		/* If supported, for new GMAC chips expose the MMC counters */
		if (priv->dma_cap.rmon) {
			dwmac_mmc_read(priv->ioaddr, &priv->mmc);

			for (i = 0; i < STMMAC_MMC_STATS_LEN; i++) {
				char *p;
				p = (char *)priv + stmmac_mmc[i].stat_offset;

				data[j++] = (stmmac_mmc[i].sizeof_stat ==
					     sizeof(u64)) ? (*(u64 *)p) :
					     (*(u32 *)p);
			}
		}
	}
	for (i = 0; i < STMMAC_STATS_LEN; i++) {
		char *p = (char *)priv + stmmac_gstrings_stats[i].stat_offset;
		data[j++] = (stmmac_gstrings_stats[i].sizeof_stat ==
			     sizeof(u64)) ? (*(u64 *)p) : (*(u32 *)p);
	}
}

static int stmmac_get_sset_count(struct net_device *netdev, int sset)
{
	struct stmmac_priv *priv = netdev_priv(netdev);
	int len;

	switch (sset) {
	case ETH_SS_STATS:
		len = STMMAC_STATS_LEN;

		if (priv->dma_cap.rmon)
			len += STMMAC_MMC_STATS_LEN;

		return len;
	default:
		return -EOPNOTSUPP;
	}
}

static void stmmac_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	int i;
	u8 *p = data;
	struct stmmac_priv *priv = netdev_priv(dev);

	switch (stringset) {
	case ETH_SS_STATS:
		if (priv->dma_cap.rmon)
			for (i = 0; i < STMMAC_MMC_STATS_LEN; i++) {
				memcpy(p, stmmac_mmc[i].stat_string,
				       ETH_GSTRING_LEN);
				p += ETH_GSTRING_LEN;
			}
		for (i = 0; i < STMMAC_STATS_LEN; i++) {
			memcpy(p, stmmac_gstrings_stats[i].stat_string,
				ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	default:
		WARN_ON(1);
		break;
	}
}

/* Currently only support WOL through Magic packet. */
static void stmmac_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct stmmac_priv *priv = netdev_priv(dev);

	spin_lock_irq(&priv->lock);
	if (device_can_wakeup(priv->device)) {
		wol->supported = WAKE_MAGIC | WAKE_UCAST;
		wol->wolopts = priv->wolopts;
	}
	spin_unlock_irq(&priv->lock);
}

static int stmmac_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	u32 support = WAKE_MAGIC | WAKE_UCAST;

	/* By default almost all GMAC devices support the WoL via
	 * magic frame but we can disable it if the HW capability
	 * register shows no support for pmt_magic_frame. */
	if ((priv->hw_cap_support) && (!priv->dma_cap.pmt_magic_frame))
		wol->wolopts &= ~WAKE_MAGIC;

	if (!device_can_wakeup(priv->device))
		return -EINVAL;

	if (wol->wolopts & ~support)
		return -EINVAL;

	if (wol->wolopts) {
		pr_info("stmmac: wakeup enable\n");
		device_set_wakeup_enable(priv->device, 1);
		enable_irq_wake(priv->wol_irq);
	} else {
		device_set_wakeup_enable(priv->device, 0);
		disable_irq_wake(priv->wol_irq);
	}

	spin_lock_irq(&priv->lock);
	priv->wolopts = wol->wolopts;
	spin_unlock_irq(&priv->lock);

	return 0;
}

static const struct ethtool_ops stmmac_ethtool_ops = {
	.begin = stmmac_check_if_running,
	.get_drvinfo = stmmac_ethtool_getdrvinfo,
	.get_settings = stmmac_ethtool_getsettings,
	.set_settings = stmmac_ethtool_setsettings,
	.get_msglevel = stmmac_ethtool_getmsglevel,
	.set_msglevel = stmmac_ethtool_setmsglevel,
	.get_regs = stmmac_ethtool_gregs,
	.get_regs_len = stmmac_ethtool_get_regs_len,
	.get_link = ethtool_op_get_link,
	.get_pauseparam = stmmac_get_pauseparam,
	.set_pauseparam = stmmac_set_pauseparam,
	.get_ethtool_stats = stmmac_get_ethtool_stats,
	.get_strings = stmmac_get_strings,
	.get_wol = stmmac_get_wol,
	.set_wol = stmmac_set_wol,
	.get_sset_count	= stmmac_get_sset_count,
};

void stmmac_set_ethtool_ops(struct net_device *netdev)
{
	SET_ETHTOOL_OPS(netdev, &stmmac_ethtool_ops);
}
