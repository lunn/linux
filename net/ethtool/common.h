/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef _ETHTOOL_COMMON_H
#define _ETHTOOL_COMMON_H

#include <linux/netdevice.h>
#include <linux/ethtool.h>

extern const char
netdev_features_strings[NETDEV_FEATURE_COUNT][ETH_GSTRING_LEN];
extern const char
rss_hash_func_strings[ETH_RSS_HASH_FUNCS_COUNT][ETH_GSTRING_LEN];
extern const char
tunable_strings[__ETHTOOL_TUNABLE_COUNT][ETH_GSTRING_LEN];
extern const char
phy_tunable_strings[__ETHTOOL_PHY_TUNABLE_COUNT][ETH_GSTRING_LEN];

int __ethtool_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info);
int __ethtool_get_ts_info(struct net_device *dev, struct ethtool_ts_info *info);

bool convert_legacy_settings_to_link_ksettings(
	struct ethtool_link_ksettings *link_ksettings,
	const struct ethtool_cmd *legacy_settings);

#endif /* _ETHTOOL_COMMON_H */
