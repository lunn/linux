// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note

#include <linux/rtnetlink.h>
#include <linux/phy.h>
#include <linux/net_tstamp.h>
#include <net/devlink.h>
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
