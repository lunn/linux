// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note

#include <linux/rtnetlink.h>
#include <linux/phy.h>
#include <linux/net_tstamp.h>
#include <net/devlink.h>
#include <net/xdp_sock.h>
#include "common.h"

const char netdev_features_strings[NETDEV_FEATURE_COUNT][ETH_GSTRING_LEN] = {
	[NETIF_F_SG_BIT] =               "tx-scatter-gather",
	[NETIF_F_IP_CSUM_BIT] =          "tx-checksum-ipv4",
	[NETIF_F_HW_CSUM_BIT] =          "tx-checksum-ip-generic",
	[NETIF_F_IPV6_CSUM_BIT] =        "tx-checksum-ipv6",
	[NETIF_F_HIGHDMA_BIT] =          "highdma",
	[NETIF_F_FRAGLIST_BIT] =         "tx-scatter-gather-fraglist",
	[NETIF_F_HW_VLAN_CTAG_TX_BIT] =  "tx-vlan-hw-insert",

	[NETIF_F_HW_VLAN_CTAG_RX_BIT] =  "rx-vlan-hw-parse",
	[NETIF_F_HW_VLAN_CTAG_FILTER_BIT] = "rx-vlan-filter",
	[NETIF_F_HW_VLAN_STAG_TX_BIT] =  "tx-vlan-stag-hw-insert",
	[NETIF_F_HW_VLAN_STAG_RX_BIT] =  "rx-vlan-stag-hw-parse",
	[NETIF_F_HW_VLAN_STAG_FILTER_BIT] = "rx-vlan-stag-filter",
	[NETIF_F_VLAN_CHALLENGED_BIT] =  "vlan-challenged",
	[NETIF_F_GSO_BIT] =              "tx-generic-segmentation",
	[NETIF_F_LLTX_BIT] =             "tx-lockless",
	[NETIF_F_NETNS_LOCAL_BIT] =      "netns-local",
	[NETIF_F_GRO_BIT] =              "rx-gro",
	[NETIF_F_GRO_HW_BIT] =           "rx-gro-hw",
	[NETIF_F_LRO_BIT] =              "rx-lro",

	[NETIF_F_TSO_BIT] =              "tx-tcp-segmentation",
	[NETIF_F_GSO_ROBUST_BIT] =       "tx-gso-robust",
	[NETIF_F_TSO_ECN_BIT] =          "tx-tcp-ecn-segmentation",
	[NETIF_F_TSO_MANGLEID_BIT] =	 "tx-tcp-mangleid-segmentation",
	[NETIF_F_TSO6_BIT] =             "tx-tcp6-segmentation",
	[NETIF_F_FSO_BIT] =              "tx-fcoe-segmentation",
	[NETIF_F_GSO_GRE_BIT] =		 "tx-gre-segmentation",
	[NETIF_F_GSO_GRE_CSUM_BIT] =	 "tx-gre-csum-segmentation",
	[NETIF_F_GSO_IPXIP4_BIT] =	 "tx-ipxip4-segmentation",
	[NETIF_F_GSO_IPXIP6_BIT] =	 "tx-ipxip6-segmentation",
	[NETIF_F_GSO_UDP_TUNNEL_BIT] =	 "tx-udp_tnl-segmentation",
	[NETIF_F_GSO_UDP_TUNNEL_CSUM_BIT] = "tx-udp_tnl-csum-segmentation",
	[NETIF_F_GSO_PARTIAL_BIT] =	 "tx-gso-partial",
	[NETIF_F_GSO_SCTP_BIT] =	 "tx-sctp-segmentation",
	[NETIF_F_GSO_ESP_BIT] =		 "tx-esp-segmentation",
	[NETIF_F_GSO_UDP_L4_BIT] =	 "tx-udp-segmentation",

	[NETIF_F_FCOE_CRC_BIT] =         "tx-checksum-fcoe-crc",
	[NETIF_F_SCTP_CRC_BIT] =        "tx-checksum-sctp",
	[NETIF_F_FCOE_MTU_BIT] =         "fcoe-mtu",
	[NETIF_F_NTUPLE_BIT] =           "rx-ntuple-filter",
	[NETIF_F_RXHASH_BIT] =           "rx-hashing",
	[NETIF_F_RXCSUM_BIT] =           "rx-checksum",
	[NETIF_F_NOCACHE_COPY_BIT] =     "tx-nocache-copy",
	[NETIF_F_LOOPBACK_BIT] =         "loopback",
	[NETIF_F_RXFCS_BIT] =            "rx-fcs",
	[NETIF_F_RXALL_BIT] =            "rx-all",
	[NETIF_F_HW_L2FW_DOFFLOAD_BIT] = "l2-fwd-offload",
	[NETIF_F_HW_TC_BIT] =		 "hw-tc-offload",
	[NETIF_F_HW_ESP_BIT] =		 "esp-hw-offload",
	[NETIF_F_HW_ESP_TX_CSUM_BIT] =	 "esp-tx-csum-hw-offload",
	[NETIF_F_RX_UDP_TUNNEL_PORT_BIT] =	 "rx-udp_tunnel-port-offload",
	[NETIF_F_HW_TLS_RECORD_BIT] =	"tls-hw-record",
	[NETIF_F_HW_TLS_TX_BIT] =	 "tls-hw-tx-offload",
	[NETIF_F_HW_TLS_RX_BIT] =	 "tls-hw-rx-offload",
};

const char
rss_hash_func_strings[ETH_RSS_HASH_FUNCS_COUNT][ETH_GSTRING_LEN] = {
	[ETH_RSS_HASH_TOP_BIT] =	"toeplitz",
	[ETH_RSS_HASH_XOR_BIT] =	"xor",
	[ETH_RSS_HASH_CRC32_BIT] =	"crc32",
};

const char
tunable_strings[__ETHTOOL_TUNABLE_COUNT][ETH_GSTRING_LEN] = {
	[ETHTOOL_ID_UNSPEC]     = "Unspec",
	[ETHTOOL_RX_COPYBREAK]	= "rx-copybreak",
	[ETHTOOL_TX_COPYBREAK]	= "tx-copybreak",
	[ETHTOOL_PFC_PREVENTION_TOUT] = "pfc-prevention-tout",
};

const char
phy_tunable_strings[__ETHTOOL_PHY_TUNABLE_COUNT][ETH_GSTRING_LEN] = {
	[ETHTOOL_ID_UNSPEC]     = "Unspec",
	[ETHTOOL_PHY_DOWNSHIFT]	= "phy-downshift",
};

int __ethtool_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;

	memset(info, 0, sizeof(*info));
	info->cmd = ETHTOOL_GDRVINFO;
	if (ops->get_drvinfo) {
		ops->get_drvinfo(dev, info);
	} else if (dev->dev.parent && dev->dev.parent->driver) {
		strlcpy(info->bus_info, dev_name(dev->dev.parent),
			sizeof(info->bus_info));
		strlcpy(info->driver, dev->dev.parent->driver->name,
			sizeof(info->driver));
	} else {
		return -EOPNOTSUPP;
	}

	/* this method of obtaining string set info is deprecated;
	 * Use ETHTOOL_GSSET_INFO instead.
	 */
	if (ops->get_sset_count) {
		int rc;

		rc = ops->get_sset_count(dev, ETH_SS_TEST);
		if (rc >= 0)
			info->testinfo_len = rc;
		rc = ops->get_sset_count(dev, ETH_SS_STATS);
		if (rc >= 0)
			info->n_stats = rc;
		rc = ops->get_sset_count(dev, ETH_SS_PRIV_FLAGS);
		if (rc >= 0)
			info->n_priv_flags = rc;
	}
	if (ops->get_regs_len) {
		int ret = ops->get_regs_len(dev);

		if (ret > 0)
			info->regdump_len = ret;
	}

	if (ops->get_eeprom_len)
		info->eedump_len = ops->get_eeprom_len(dev);

	if (!info->fw_version[0])
		devlink_compat_running_version(dev, info->fw_version,
					       sizeof(info->fw_version));

	return 0;
}

int __ethtool_get_ts_info(struct net_device *dev, struct ethtool_ts_info *info)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	struct phy_device *phydev = dev->phydev;
	int err = 0;

	memset(info, 0, sizeof(*info));
	info->cmd = ETHTOOL_GET_TS_INFO;

	if (phydev && phydev->drv && phydev->drv->ts_info) {
		err = phydev->drv->ts_info(phydev, info);
	} else if (ops->get_ts_info) {
		err = ops->get_ts_info(dev, info);
	} else {
		info->so_timestamping = SOF_TIMESTAMPING_RX_SOFTWARE |
					SOF_TIMESTAMPING_SOFTWARE;
		info->phc_index = -1;
	}

	return err;
}

/* return false if legacy contained non-0 deprecated fields
 * maxtxpkt/maxrxpkt. rest of ksettings always updated
 */
bool
convert_legacy_settings_to_link_ksettings(
	struct ethtool_link_ksettings *link_ksettings,
	const struct ethtool_cmd *legacy_settings)
{
	bool retval = true;

	memset(link_ksettings, 0, sizeof(*link_ksettings));

	/* This is used to tell users that driver is still using these
	 * deprecated legacy fields, and they should not use
	 * %ETHTOOL_GLINKSETTINGS/%ETHTOOL_SLINKSETTINGS
	 */
	if (legacy_settings->maxtxpkt ||
	    legacy_settings->maxrxpkt)
		retval = false;

	ethtool_convert_legacy_u32_to_link_mode(
		link_ksettings->link_modes.supported,
		legacy_settings->supported);
	ethtool_convert_legacy_u32_to_link_mode(
		link_ksettings->link_modes.advertising,
		legacy_settings->advertising);
	ethtool_convert_legacy_u32_to_link_mode(
		link_ksettings->link_modes.lp_advertising,
		legacy_settings->lp_advertising);
	link_ksettings->base.speed
		= ethtool_cmd_speed(legacy_settings);
	link_ksettings->base.duplex
		= legacy_settings->duplex;
	link_ksettings->base.port
		= legacy_settings->port;
	link_ksettings->base.phy_address
		= legacy_settings->phy_address;
	link_ksettings->base.autoneg
		= legacy_settings->autoneg;
	link_ksettings->base.mdio_support
		= legacy_settings->mdio_support;
	link_ksettings->base.eth_tp_mdix
		= legacy_settings->eth_tp_mdix;
	link_ksettings->base.eth_tp_mdix_ctrl
		= legacy_settings->eth_tp_mdix_ctrl;
	return retval;
}

int __ethtool_get_link(struct net_device *dev)
{
	if (!dev->ethtool_ops->get_link)
		return -EOPNOTSUPP;

	return netif_running(dev) && dev->ethtool_ops->get_link(dev);
}

int __ethtool_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	if (!dev->ethtool_ops->get_wol)
		return -EOPNOTSUPP;

	dev->ethtool_ops->get_wol(dev, wol);

	return 0;
}

static int ethtool_get_max_rxfh_channel(struct net_device *dev, u32 *max)
{
	u32 dev_size, current_max = 0;
	u32 *indir;
	int ret;

	if (!dev->ethtool_ops->get_rxfh_indir_size ||
	    !dev->ethtool_ops->get_rxfh)
		return -EOPNOTSUPP;
	dev_size = dev->ethtool_ops->get_rxfh_indir_size(dev);
	if (dev_size == 0)
		return -EOPNOTSUPP;

	indir = kcalloc(dev_size, sizeof(indir[0]), GFP_USER);
	if (!indir)
		return -ENOMEM;

	ret = dev->ethtool_ops->get_rxfh(dev, indir, NULL, NULL);
	if (ret)
		goto out;

	while (dev_size--)
		current_max = max(current_max, indir[dev_size]);

	*max = current_max;

out:
	kfree(indir);
	return ret;
}

int __ethtool_set_channels(struct net_device *dev,
			   const struct ethtool_channels *curr,
			   struct ethtool_channels *channels)
{
	u16 from_channel, to_channel;
	u32 max_rx_in_use = 0;
	unsigned int i;

	/* ensure new counts are within the maximums */
	if (channels->rx_count > curr->max_rx ||
	    channels->tx_count > curr->max_tx ||
	    channels->combined_count > curr->max_combined ||
	    channels->other_count > curr->max_other)
		return -EINVAL;

	/* ensure the new Rx count fits within the configured Rx flow
	 * indirection table settings */
	if (netif_is_rxfh_configured(dev) &&
	    !ethtool_get_max_rxfh_channel(dev, &max_rx_in_use) &&
	    (channels->combined_count + channels->rx_count) <= max_rx_in_use)
	    return -EINVAL;

	/* Disabling channels, query zero-copy AF_XDP sockets */
	from_channel = channels->combined_count +
		min(channels->rx_count, channels->tx_count);
	to_channel = curr->combined_count + max(curr->rx_count, curr->tx_count);
	for (i = from_channel; i < to_channel; i++)
		if (xdp_get_umem_from_qid(dev, i))
			return -EINVAL;

	return dev->ethtool_ops->set_channels(dev, channels);
}
