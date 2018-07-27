// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note

#include "netlink.h"
#include "common.h"
#include "bitset.h"

struct settings_data {
	struct common_req_info		reqinfo_base;
	bool				privileged;

	/* everything below here will be reset for each device in dumps */
	struct common_reply_data	repdata_base;
	struct ethtool_link_ksettings	ksettings;
	struct ethtool_wolinfo		wolinfo;
	struct ethtool_link_settings	*lsettings;
	int				link;
	u32				msglevel;
	struct {
		u32	hw[ETHTOOL_DEV_FEATURE_WORDS];
		u32	wanted[ETHTOOL_DEV_FEATURE_WORDS];
		u32	active[ETHTOOL_DEV_FEATURE_WORDS];
		u32	nochange[ETHTOOL_DEV_FEATURE_WORDS];
	} features;
	bool				lpm_empty;
};

struct link_mode_info {
	int				speed;
	u8				duplex;
};

#define __DEFINE_LINK_MODE_PARAMS(_speed, _type, _duplex) \
	[ETHTOOL_LINK_MODE(_speed, _type, _duplex)] = { \
		.speed	= SPEED_ ## _speed, \
		.duplex	= __DUPLEX_ ## _duplex \
	}
#define __DUPLEX_Half DUPLEX_HALF
#define __DUPLEX_Full DUPLEX_FULL
#define __DEFINE_SPECIAL_MODE_PARAMS(_mode) \
	[ETHTOOL_LINK_MODE_ ## _mode ## _BIT] = { \
		.speed	= SPEED_UNKNOWN, \
		.duplex	= DUPLEX_UNKNOWN, \
	}

static const struct link_mode_info link_mode_params[] = {
	__DEFINE_LINK_MODE_PARAMS(10, T, Half),
	__DEFINE_LINK_MODE_PARAMS(10, T, Full),
	__DEFINE_LINK_MODE_PARAMS(100, T, Half),
	__DEFINE_LINK_MODE_PARAMS(100, T, Full),
	__DEFINE_LINK_MODE_PARAMS(1000, T, Half),
	__DEFINE_LINK_MODE_PARAMS(1000, T, Full),
	__DEFINE_SPECIAL_MODE_PARAMS(Autoneg),
	__DEFINE_SPECIAL_MODE_PARAMS(TP),
	__DEFINE_SPECIAL_MODE_PARAMS(AUI),
	__DEFINE_SPECIAL_MODE_PARAMS(MII),
	__DEFINE_SPECIAL_MODE_PARAMS(FIBRE),
	__DEFINE_SPECIAL_MODE_PARAMS(BNC),
	__DEFINE_LINK_MODE_PARAMS(10000, T, Full),
	__DEFINE_SPECIAL_MODE_PARAMS(Pause),
	__DEFINE_SPECIAL_MODE_PARAMS(Asym_Pause),
	__DEFINE_LINK_MODE_PARAMS(2500, X, Full),
	__DEFINE_SPECIAL_MODE_PARAMS(Backplane),
	__DEFINE_LINK_MODE_PARAMS(1000, KX, Full),
	__DEFINE_LINK_MODE_PARAMS(10000, KX4, Full),
	__DEFINE_LINK_MODE_PARAMS(10000, KR, Full),
	[ETHTOOL_LINK_MODE_10000baseR_FEC_BIT] = {
		.speed	= SPEED_10000,
		.duplex = DUPLEX_FULL,
	},
	__DEFINE_LINK_MODE_PARAMS(20000, MLD2, Full),
	__DEFINE_LINK_MODE_PARAMS(20000, KR2, Full),
	__DEFINE_LINK_MODE_PARAMS(40000, KR4, Full),
	__DEFINE_LINK_MODE_PARAMS(40000, CR4, Full),
	__DEFINE_LINK_MODE_PARAMS(40000, SR4, Full),
	__DEFINE_LINK_MODE_PARAMS(40000, LR4, Full),
	__DEFINE_LINK_MODE_PARAMS(56000, KR4, Full),
	__DEFINE_LINK_MODE_PARAMS(56000, CR4, Full),
	__DEFINE_LINK_MODE_PARAMS(56000, SR4, Full),
	__DEFINE_LINK_MODE_PARAMS(56000, LR4, Full),
	__DEFINE_LINK_MODE_PARAMS(25000, CR, Full),
	__DEFINE_LINK_MODE_PARAMS(25000, KR, Full),
	__DEFINE_LINK_MODE_PARAMS(25000, SR, Full),
	__DEFINE_LINK_MODE_PARAMS(50000, CR2, Full),
	__DEFINE_LINK_MODE_PARAMS(50000, KR2, Full),
	__DEFINE_LINK_MODE_PARAMS(100000, KR4, Full),
	__DEFINE_LINK_MODE_PARAMS(100000, SR4, Full),
	__DEFINE_LINK_MODE_PARAMS(100000, CR4, Full),
	__DEFINE_LINK_MODE_PARAMS(100000, LR4_ER4, Full),
	__DEFINE_LINK_MODE_PARAMS(50000, SR2, Full),
	__DEFINE_LINK_MODE_PARAMS(1000, X, Full),
	__DEFINE_LINK_MODE_PARAMS(10000, CR, Full),
	__DEFINE_LINK_MODE_PARAMS(10000, SR, Full),
	__DEFINE_LINK_MODE_PARAMS(10000, LR, Full),
	__DEFINE_LINK_MODE_PARAMS(10000, LRM, Full),
	__DEFINE_LINK_MODE_PARAMS(10000, ER, Full),
	__DEFINE_LINK_MODE_PARAMS(2500, T, Full),
	__DEFINE_LINK_MODE_PARAMS(5000, T, Full),
	__DEFINE_SPECIAL_MODE_PARAMS(FEC_NONE),
	__DEFINE_SPECIAL_MODE_PARAMS(FEC_RS),
	__DEFINE_SPECIAL_MODE_PARAMS(FEC_BASER),
	__DEFINE_LINK_MODE_PARAMS(50000, KR, Full),
	__DEFINE_LINK_MODE_PARAMS(50000, SR, Full),
	__DEFINE_LINK_MODE_PARAMS(50000, CR, Full),
	__DEFINE_LINK_MODE_PARAMS(50000, LR_ER_FR, Full),
	__DEFINE_LINK_MODE_PARAMS(50000, DR, Full),
	__DEFINE_LINK_MODE_PARAMS(100000, KR2, Full),
	__DEFINE_LINK_MODE_PARAMS(100000, SR2, Full),
	__DEFINE_LINK_MODE_PARAMS(100000, CR2, Full),
	__DEFINE_LINK_MODE_PARAMS(100000, LR2_ER2_FR2, Full),
	__DEFINE_LINK_MODE_PARAMS(100000, DR2, Full),
	__DEFINE_LINK_MODE_PARAMS(200000, KR4, Full),
	__DEFINE_LINK_MODE_PARAMS(200000, SR4, Full),
	__DEFINE_LINK_MODE_PARAMS(200000, LR4_ER4_FR4, Full),
	__DEFINE_LINK_MODE_PARAMS(200000, DR4, Full),
	__DEFINE_LINK_MODE_PARAMS(200000, CR4, Full),
};

/* We want to allow ~0 as selector for backward compatibility (to just set
 * given set of modes, whatever kernel supports) so that we allow all bits
 * on validation and do our own sanity check later.
 */
static u32 all_bits = ~(u32)0;

static const struct nla_policy get_settings_policy[ETHA_SETTINGS_MAX + 1] = {
	[ETHA_SETTINGS_UNSPEC]		= { .type = NLA_REJECT },
	[ETHA_SETTINGS_DEV]		= { .type = NLA_NESTED },
	[ETHA_SETTINGS_INFOMASK]	= { .type = NLA_U32 },
	[ETHA_SETTINGS_COMPACT]		= { .type = NLA_FLAG },
	[ETHA_SETTINGS_LINK_INFO]	= { .type = NLA_REJECT },
	[ETHA_SETTINGS_LINK_MODES]	= { .type = NLA_REJECT },
	[ETHA_SETTINGS_LINK_STATE]	= { .type = NLA_REJECT },
	[ETHA_SETTINGS_WOL]		= { .type = NLA_REJECT },
	[ETHA_SETTINGS_DEBUG]		= { .type = NLA_REJECT },
	[ETHA_SETTINGS_FEATURES]	= { .type = NLA_REJECT },
};

static int parse_settings(struct common_req_info *req_info,
			  struct sk_buff *skb, struct genl_info *info,
			  const struct nlmsghdr *nlhdr)
{
	struct settings_data *data =
		container_of(req_info, struct settings_data, reqinfo_base);
	struct nlattr *tb[ETHA_SETTINGS_MAX + 1];
	int ret;

	data->privileged = ethnl_is_privileged(skb);

	ret = ethnlmsg_parse(nlhdr, tb, ETHA_SETTINGS_MAX, get_settings_policy,
			     info);
	if (ret < 0)
		return ret;

	if (tb[ETHA_SETTINGS_DEV]) {
		req_info->dev = ethnl_dev_get(info, tb[ETHA_SETTINGS_DEV]);
		if (IS_ERR(req_info->dev)) {
			ret = PTR_ERR(req_info->dev);
			req_info->dev = NULL;
			return ret;
		}
	}
	if (tb[ETHA_SETTINGS_INFOMASK])
		req_info->req_mask = nla_get_u32(tb[ETHA_SETTINGS_INFOMASK]);
	if (tb[ETHA_SETTINGS_COMPACT])
		req_info->compact = true;
	if (req_info->req_mask == 0)
		req_info->req_mask = ETH_SETTINGS_IM_ALL;

	return 0;
}

static int ethnl_get_link_ksettings(struct genl_info *info,
				    struct net_device *dev,
				    struct ethtool_link_ksettings *ksettings)
{
	int ret;

	ret = __ethtool_get_link_ksettings(dev, ksettings);

	if (ret < 0)
		ETHNL_SET_ERRMSG(info, "failed to retrieve link settings");
	return ret;
}

static int ethnl_get_wol(struct genl_info *info, struct net_device *dev,
			 struct ethtool_wolinfo *wolinfo)
{
	int ret = __ethtool_get_wol(dev, wolinfo);

	if (ret < 0)
		ETHNL_SET_ERRMSG(info, "failed to retrieve wol info");
	return ret;
}

static void features_to_bitmap(u32 *dest, netdev_features_t src)
{
	unsigned int i;

	for (i = 0; i < ETHTOOL_DEV_FEATURE_WORDS; i++)
		dest[i] = (u32)(src >> (32 * i));
}

static int ethnl_get_features(struct net_device *dev,
			      struct settings_data *data)
{
	features_to_bitmap(data->features.hw, dev->hw_features);
	features_to_bitmap(data->features.wanted, dev->wanted_features);
	features_to_bitmap(data->features.active, dev->features);
	features_to_bitmap(data->features.nochange, NETIF_F_NEVER_CHANGE);
	return 0;
}

static int prepare_settings(struct common_req_info *req_info,
			    struct genl_info *info)
{
	struct settings_data *data =
		container_of(req_info, struct settings_data, reqinfo_base);
	struct net_device *dev = data->repdata_base.dev;
	const struct ethtool_ops *eops = dev->ethtool_ops;
	u32 req_mask = req_info->req_mask;
	int ret;

	data->lsettings = &data->ksettings.base;
	data->lpm_empty = true;
	data->link = -EOPNOTSUPP;

	ret = ethnl_before_ops(dev);
	if (ret < 0)
		return ret;
	if (req_mask & (ETH_SETTINGS_IM_LINKINFO | ETH_SETTINGS_IM_LINKMODES)) {
		ret = ethnl_get_link_ksettings(info, dev, &data->ksettings);
		if (ret < 0)
			req_mask &= ~(ETH_SETTINGS_IM_LINKINFO |
				      ETH_SETTINGS_IM_LINKMODES);
	}
	if (req_mask & ETH_SETTINGS_IM_LINKMODES) {
		data->lpm_empty =
			bitmap_empty(data->ksettings.link_modes.lp_advertising,
				     __ETHTOOL_LINK_MODE_MASK_NBITS);
		ethnl_bitmap_to_u32(data->ksettings.link_modes.supported,
				    __ETHTOOL_LINK_MODE_MASK_NWORDS);
		ethnl_bitmap_to_u32(data->ksettings.link_modes.advertising,
				    __ETHTOOL_LINK_MODE_MASK_NWORDS);
		ethnl_bitmap_to_u32(data->ksettings.link_modes.lp_advertising,
				    __ETHTOOL_LINK_MODE_MASK_NWORDS);
	}
	if (req_mask & ETH_SETTINGS_IM_LINKSTATE)
		data->link = __ethtool_get_link(dev);
	if (req_mask & ETH_SETTINGS_IM_WOL) {
		ret = ethnl_get_wol(info, dev, &data->wolinfo);
		if (ret < 0)
			req_mask &= ~ETH_SETTINGS_IM_WOL;
	}
	if (req_mask & ETH_SETTINGS_IM_DEBUG) {
		if (eops->get_msglevel)
			data->msglevel = eops->get_msglevel(dev);
		else
			req_mask &= ~ETH_SETTINGS_IM_DEBUG;
	}
	if (req_mask & ETH_SETTINGS_IM_FEATURES)
		ethnl_get_features(dev, data);
	ethnl_after_ops(dev);

	data->repdata_base.info_mask = req_mask;
	if (req_info->req_mask & ~req_mask)
		warn_partial_info(info);
	return 0;
}

static int link_info_size(void)
{
	int len = 0;

	/* port, phyaddr, mdix, mdixctrl, transcvr */
	len += 5 * nla_total_size(sizeof(u8));
	/* mdio_support */
	len += nla_total_size(sizeof(struct nla_bitfield32));

	/* nest */
	return nla_total_size(len);
}

static int link_modes_size(const struct ethtool_link_ksettings *ksettings,
			   bool compact)
{
	unsigned int flags = compact ? ETHNL_BITSET_COMPACT : 0;
	u32 *supported = (u32 *)ksettings->link_modes.supported;
	u32 *advertising = (u32 *)ksettings->link_modes.advertising;
	u32 *lp_advertising = (u32 *)ksettings->link_modes.lp_advertising;
	int len = 0, ret;

	/* speed, duplex, autoneg */
	len += nla_total_size(sizeof(u32)) + 2 * nla_total_size(sizeof(u8));
	ret = ethnl_bitset32_size(__ETHTOOL_LINK_MODE_MASK_NBITS, advertising,
				  supported, link_mode_names, flags);
	if (ret < 0)
		return ret;
	len += ret;
	ret = ethnl_bitset32_size(__ETHTOOL_LINK_MODE_MASK_NBITS,
				  lp_advertising, NULL, link_mode_names,
				  flags & ETHNL_BITSET_LIST);
	if (ret < 0)
		return ret;
	len += ret;

	/* nest */
	return nla_total_size(len);
}

static int link_state_size(int link)
{
	if (link < 0)
		return nla_total_size(0);
	return nla_total_size(nla_total_size(sizeof(u8)));
}

static int wol_size(void)
{
	return nla_total_size(nla_total_size(sizeof(struct nla_bitfield32)) +
			      nla_total_size(SOPASS_MAX));
}

static int debug_size(void)
{
	return nla_total_size(nla_total_size(sizeof(struct nla_bitfield32)));
}

static int features_size(const struct settings_data *data)
{
	unsigned int flags =
		(data->reqinfo_base.compact ? ETHNL_BITSET_COMPACT : 0) |
		ETHNL_BITSET_LEGACY_NAMES;
	int len = 0, ret;

	ret = ethnl_bitset32_size(NETDEV_FEATURE_COUNT, data->features.hw,
				  NULL, netdev_features_strings, flags);
	if (ret < 0)
		return ret;
	len += ret;
	flags |= ETHNL_BITSET_LIST;
	ret = ethnl_bitset32_size(NETDEV_FEATURE_COUNT, data->features.wanted,
				  NULL, netdev_features_strings, flags);
	if (ret < 0)
		return ret;
	len += ret;
	ret = ethnl_bitset32_size(NETDEV_FEATURE_COUNT, data->features.active,
				  NULL, netdev_features_strings, flags);
	if (ret < 0)
		return ret;
	len += ret;
	ret = ethnl_bitset32_size(NETDEV_FEATURE_COUNT, data->features.nochange,
				  NULL, netdev_features_strings, flags);
	if (ret < 0)
		return ret;
	len += ret;

	return len;
}

/* To keep things simple, reserve space for some attributes which may not
 * be added to the message (e.g. ETHA_SETTINGS_SOPASS); therefore the length
 * returned may be bigger than the actual length of the message sent
 */
static int settings_size(const struct common_req_info *req_info)
{
	struct settings_data *data =
		container_of(req_info, struct settings_data, reqinfo_base);
	u32 info_mask = data->repdata_base.info_mask;
	bool compact = req_info->compact;
	int len = 0, ret;

	len += dev_ident_size();
	if (info_mask & ETH_SETTINGS_IM_LINKINFO)
		len += link_info_size();
	if (info_mask & ETH_SETTINGS_IM_LINKMODES) {
		ret = link_modes_size(&data->ksettings, compact);
		if (ret < 0)
			return ret;
		len += ret;
	}
	if (info_mask & ETH_SETTINGS_IM_LINKSTATE)
		len += link_state_size(data->link);
	if (info_mask & ETH_SETTINGS_IM_WOL)
		len += wol_size();
	if (info_mask & ETH_SETTINGS_IM_DEBUG)
		len += debug_size();
	if (info_mask & ETH_SETTINGS_IM_FEATURES) {
		ret = features_size(data);
		if (ret < 0)
			return ret;
		len += ret;
	}

	return len;
}

static int fill_link_info(struct sk_buff *skb,
			  const struct ethtool_link_settings *lsettings)
{
	struct nlattr *nest = ethnl_nest_start(skb, ETHA_SETTINGS_LINK_INFO);

	if (!nest)
		return -EMSGSIZE;
	if (nla_put_u8(skb, ETHA_LINKINFO_PORT, lsettings->port) ||
	    nla_put_u8(skb, ETHA_LINKINFO_PHYADDR,
		       lsettings->phy_address) ||
	    nla_put_u8(skb, ETHA_LINKINFO_TP_MDIX,
		       lsettings->eth_tp_mdix) ||
	    nla_put_u8(skb, ETHA_LINKINFO_TP_MDIX_CTRL,
		       lsettings->eth_tp_mdix_ctrl) ||
	    nla_put_u8(skb, ETHA_LINKINFO_TRANSCEIVER,
		       lsettings->transceiver)) {
		nla_nest_cancel(skb, nest);
		return -EMSGSIZE;
	}

	nla_nest_end(skb, nest);
	return 0;
}

static int fill_link_modes(struct sk_buff *skb,
			   const struct ethtool_link_ksettings *ksettings,
			   bool lpm_empty, bool compact)
{
	const u32 *supported = (const u32 *)ksettings->link_modes.supported;
	const u32 *advertising = (const u32 *)ksettings->link_modes.advertising;
	const u32 *lp_adv = (const u32 *)ksettings->link_modes.lp_advertising;
	const unsigned int flags = compact ? ETHNL_BITSET_COMPACT : 0;
	const struct ethtool_link_settings *lsettings = &ksettings->base;
	struct nlattr *nest;
	int ret;

	nest = ethnl_nest_start(skb, ETHA_SETTINGS_LINK_MODES);
	if (!nest)
		return -EMSGSIZE;
	if (nla_put_u8(skb, ETHA_LINKMODES_AUTONEG, lsettings->autoneg))
		goto err;

	ret = ethnl_put_bitset32(skb, ETHA_LINKMODES_OURS,
				 __ETHTOOL_LINK_MODE_MASK_NBITS, advertising,
				 supported, link_mode_names, flags);
	if (ret < 0)
		goto err;
	if (!lpm_empty) {
		ret = ethnl_put_bitset32(skb, ETHA_LINKMODES_PEER,
					 __ETHTOOL_LINK_MODE_MASK_NBITS,
					 lp_adv, NULL, link_mode_names,
					 flags | ETHNL_BITSET_LIST);
		if (ret < 0)
			goto err;
	}

	if (nla_put_u32(skb, ETHA_LINKMODES_SPEED, lsettings->speed) ||
	    nla_put_u8(skb, ETHA_LINKMODES_DUPLEX, lsettings->duplex))
		goto err;

	nla_nest_end(skb, nest);
	return 0;

err:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static int fill_link_state(struct sk_buff *skb, u8 link)
{
	struct nlattr *nest;

	nest = ethnl_nest_start(skb, ETHA_SETTINGS_LINK_STATE);
	if (!nest)
		return -EMSGSIZE;
	if (link >=0 && nla_put_u8(skb, ETHA_LINKSTATE_LINK, link))
		goto err;
	nla_nest_end(skb, nest);
	return 0;

err:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static int fill_wolinfo(struct sk_buff *skb,
			const struct ethtool_wolinfo *wolinfo, bool privileged)
{
	struct nlattr *nest;

	nest = ethnl_nest_start(skb, ETHA_SETTINGS_WOL);
	if (!nest)
		return -EMSGSIZE;
	if (nla_put_bitfield32(skb, ETHA_WOL_MODES, wolinfo->wolopts,
			       wolinfo->supported))
		goto err;
	/* ioctl() restricts read access to wolinfo but the actual
	 * reason is to hide sopass from unprivileged users; netlink
	 * can show wol modes without sopass
	 */
	if (privileged &&
	    nla_put(skb, ETHA_WOL_SOPASS, sizeof(wolinfo->sopass),
		    wolinfo->sopass))
		goto err;
	nla_nest_end(skb, nest);
	return 0;

err:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static int fill_debug(struct sk_buff *skb, u32 msglevel)
{
	struct nlattr *nest;

	nest = ethnl_nest_start(skb, ETHA_SETTINGS_DEBUG);
	if (!nest)
		return -EMSGSIZE;
	if (nla_put_bitfield32(skb, ETHA_DEBUG_MSG_MASK, msglevel,
			       NETIF_MSG_ALL))
		goto err;
	nla_nest_end(skb, nest);
	return 0;

err:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static int fill_features(struct sk_buff *skb, const struct settings_data *data)
{
	unsigned int flags =
		(data->reqinfo_base.compact ? ETHNL_BITSET_COMPACT : 0) |
		ETHNL_BITSET_LEGACY_NAMES;
	struct nlattr *feat_attr;
	int ret;

	feat_attr = ethnl_nest_start(skb, ETHA_SETTINGS_FEATURES);
	if (!feat_attr)
		return -EMSGSIZE;

	ret = ethnl_put_bitset32(skb, ETHA_FEATURES_HW, NETDEV_FEATURE_COUNT,
				 data->features.hw, NULL,
				 netdev_features_strings, flags);
	if (ret < 0)
		return ret;
	flags |= ETHNL_BITSET_LIST;
	ret = ethnl_put_bitset32(skb, ETHA_FEATURES_WANTED,
				 NETDEV_FEATURE_COUNT, data->features.wanted,
				 NULL, netdev_features_strings, flags);
	if (ret < 0)
		return ret;
	ret = ethnl_put_bitset32(skb, ETHA_FEATURES_ACTIVE,
				 NETDEV_FEATURE_COUNT, data->features.active,
				 NULL, netdev_features_strings, flags);
	if (ret < 0)
		return ret;
	ret = ethnl_put_bitset32(skb, ETHA_FEATURES_NOCHANGE,
				 NETDEV_FEATURE_COUNT, data->features.nochange,
				 NULL, netdev_features_strings, flags);
	if (ret < 0)
		return ret;

	nla_nest_end(skb, feat_attr);
	return 0;
}

static int fill_settings(struct sk_buff *skb,
			 const struct common_req_info *req_info)
{
	const struct settings_data *data =
		container_of(req_info, struct settings_data, reqinfo_base);
	u32 info_mask = data->repdata_base.info_mask;
	bool compact = req_info->compact;
	int ret;

	if (info_mask & ETH_SETTINGS_IM_LINKINFO) {
		ret = fill_link_info(skb, data->lsettings);
		if (ret < 0)
			return ret;
	}
	if (info_mask & ETH_SETTINGS_IM_LINKMODES) {
		ret = fill_link_modes(skb, &data->ksettings, data->lpm_empty,
				      compact);
		if (ret < 0)
			return ret;
	}
	if (info_mask & ETH_SETTINGS_IM_LINKSTATE) {
		ret = fill_link_state(skb, data->link);
		if (ret < 0)
			return ret;
	}
	if (info_mask & ETH_SETTINGS_IM_WOL) {
		ret = fill_wolinfo(skb, &data->wolinfo, data->privileged);
		if (ret < 0)
			return ret;
	}
	if (info_mask & ETH_SETTINGS_IM_DEBUG) {
		ret = fill_debug(skb, data->msglevel);
		if (ret < 0)
			return ret;
	}
	if (info_mask & ETH_SETTINGS_IM_FEATURES) {
		ret = fill_features(skb, data);
		if (ret < 0)
			return ret;
	}

	return 0;
}

const struct get_request_ops settings_request_ops = {
	.request_cmd		= ETHNL_CMD_GET_SETTINGS,
	.reply_cmd		= ETHNL_CMD_SET_SETTINGS,
	.dev_attrtype		= ETHA_SETTINGS_DEV,
	.data_size		= sizeof(struct settings_data),
	.repdata_offset		= offsetof(struct settings_data, repdata_base),

	.parse_request		= parse_settings,
	.prepare_data		= prepare_settings,
	.reply_size		= settings_size,
	.fill_reply		= fill_settings,
};

/* SET_SETTINGS */

static const struct nla_policy set_linkinfo_policy[ETHA_LINKINFO_MAX + 1] = {
	[ETHA_LINKINFO_UNSPEC]		= { .type = NLA_REJECT },
	[ETHA_LINKINFO_PORT]		= { .type = NLA_U8 },
	[ETHA_LINKINFO_PHYADDR]		= { .type = NLA_U8 },
	[ETHA_LINKINFO_TP_MDIX]		= { .type = NLA_REJECT },
	[ETHA_LINKINFO_TP_MDIX_CTRL]	= { .type = NLA_U8 },
	[ETHA_LINKINFO_TRANSCEIVER]	= { .type = NLA_REJECT },
};

static const struct nla_policy set_linkmodes_policy[ETHA_LINKMODES_MAX + 1] = {
	[ETHA_LINKMODES_UNSPEC]		= { .type = NLA_REJECT },
	[ETHA_LINKMODES_AUTONEG]	= { .type = NLA_U8 },
	[ETHA_LINKMODES_OURS]		= { .type = NLA_NESTED },
	[ETHA_LINKMODES_PEER]		= { .type = NLA_REJECT },
	[ETHA_LINKMODES_SPEED]		= { .type = NLA_U32 },
	[ETHA_LINKMODES_DUPLEX]		= { .type = NLA_U8 },
};

static const struct nla_policy set_wol_policy[ETHA_LINKINFO_MAX + 1] = {
	[ETHA_WOL_UNSPEC]		= { .type = NLA_REJECT },
	[ETHA_WOL_MODES]		= { .type = NLA_BITFIELD32,
					    .validation_data = &all_bits },
	[ETHA_WOL_SOPASS]		= { .type = NLA_BINARY,
					    .len = SOPASS_MAX },
};

static const struct nla_policy set_debug_policy[ETHA_DEBUG_MAX + 1] = {
	[ETHA_DEBUG_UNSPEC]		= { .type = NLA_REJECT },
	[ETHA_DEBUG_MSG_MASK]		= { .type = NLA_BITFIELD32,
					    .validation_data = &all_bits },
};

static const struct nla_policy set_settings_policy[ETHA_SETTINGS_MAX + 1] = {
	[ETHA_SETTINGS_UNSPEC]		= { .type = NLA_REJECT },
	[ETHA_SETTINGS_DEV]		= { .type = NLA_NESTED },
	[ETHA_SETTINGS_INFOMASK]	= { .type = NLA_REJECT },
	[ETHA_SETTINGS_COMPACT]		= { .type = NLA_FLAG },
	[ETHA_SETTINGS_LINK_INFO]	= { .type = NLA_NESTED },
	[ETHA_SETTINGS_LINK_MODES]	= { .type = NLA_NESTED },
	[ETHA_SETTINGS_LINK_STATE]	= { .type = NLA_REJECT },
	[ETHA_SETTINGS_WOL]		= { .type = NLA_NESTED },
	[ETHA_SETTINGS_DEBUG]		= { .type = NLA_NESTED },
	[ETHA_SETTINGS_FEATURES]	= { .type = NLA_REJECT },
};

static int ethnl_set_link_ksettings(struct genl_info *info,
				    struct net_device *dev,
				    struct ethtool_link_ksettings *ksettings)
{
	int ret = dev->ethtool_ops->set_link_ksettings(dev, ksettings);

	if (ret < 0)
		ETHNL_SET_ERRMSG(info, "link settings update failed");
	return ret;
}

/* Set advertised link modes to all supported modes matching requested speed
 * and duplex values. Called when autonegotiation is on, speed or duplex is
 * requested but no link mode change. This is done in userspace with ioctl()
 * interface, move it into kernel for netlink.
 * Returns true if advertised modes bitmap was modified.
 */
static bool auto_link_modes(struct ethtool_link_ksettings *ksettings,
			    bool req_speed, bool req_duplex)
{
	unsigned long *advertising = ksettings->link_modes.advertising;
	unsigned long *supported = ksettings->link_modes.supported;
	DECLARE_BITMAP(old_adv, __ETHTOOL_LINK_MODE_MASK_NBITS);
	unsigned int i;

	bitmap_copy(old_adv, advertising, __ETHTOOL_LINK_MODE_MASK_NBITS);

	for (i = 0; i < __ETHTOOL_LINK_MODE_MASK_NBITS; i++) {
		const struct link_mode_info *info = &link_mode_params[i];

		if (info->speed == SPEED_UNKNOWN)
			continue;
		if (test_bit(i, supported) &&
		    (!req_speed || info->speed == ksettings->base.speed) &&
		    (!req_duplex || info->duplex == ksettings->base.duplex))
			set_bit(i, advertising);
		else
			clear_bit(i, advertising);
	}

	return !bitmap_equal(old_adv, advertising,
			     __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static int update_linkinfo(struct genl_info *info, struct nlattr *nest,
			   struct ethtool_link_settings *lsettings)
{
	struct nlattr *tb[ETHA_LINKINFO_MAX + 1];
	int ret;

	if (!nest)
		return 0;
	ret = nla_parse_nested_strict(tb, ETHA_LINKINFO_MAX, nest,
				      set_linkinfo_policy, info->extack);
	if (ret < 0)
		return ret;

	ret = 0;
	if (ethnl_update_u8(&lsettings->port, tb[ETHA_LINKINFO_PORT]))
		ret = 1;
	if (ethnl_update_u8(&lsettings->phy_address, tb[ETHA_LINKINFO_PHYADDR]))
		ret = 1;
	if (ethnl_update_u8(&lsettings->eth_tp_mdix_ctrl,
			    tb[ETHA_LINKINFO_TP_MDIX_CTRL]))
		ret = 1;

	return ret;
}

static int update_link_modes(struct genl_info *info, const struct nlattr *nest,
			     struct ethtool_link_ksettings *ksettings)
{
	struct ethtool_link_settings *lsettings = &ksettings->base;
	struct nlattr *tb[ETHA_LINKMODES_MAX + 1];
	bool req_speed, req_duplex;
	bool mod = false;
	int ret;

	if (!nest)
		return 0;
	ret = nla_parse_nested_strict(tb, ETHA_LINKMODES_MAX, nest,
				      set_linkmodes_policy, info->extack);
	if (ret < 0)
		return ret;
	req_speed = tb[ETHA_LINKMODES_SPEED];
	req_duplex = tb[ETHA_LINKMODES_DUPLEX];

	if (ethnl_update_u8(&lsettings->autoneg, tb[ETHA_LINKMODES_AUTONEG]))
		mod = true;
	if (ethnl_update_bitset(ksettings->link_modes.advertising, NULL,
				__ETHTOOL_LINK_MODE_MASK_NBITS,
				tb[ETHA_LINKMODES_OURS],
				&ret, link_mode_names, false, info))
		mod = true;
	if (ret < 0)
		return ret;
	if (ethnl_update_u32(&lsettings->speed, tb[ETHA_LINKMODES_SPEED]))
		mod = true;
	if (ethnl_update_u8(&lsettings->duplex, tb[ETHA_LINKMODES_DUPLEX]))
		mod = true;

	if (!tb[ETHA_LINKMODES_OURS] && lsettings->autoneg &&
	    (req_speed || req_duplex) &&
	    auto_link_modes(ksettings, req_speed, req_duplex))
		mod = true;

	return mod;
}

/* Update device settings using ->set_link_ksettings() callback */
static int ethnl_update_ksettings(struct genl_info *info, struct nlattr **tb,
				  struct net_device *dev, u32 *req_mask)
{
	struct ethtool_link_ksettings ksettings = {};
	struct ethtool_link_settings *lsettings;
	u32 mod_mask = 0;
	int ret;

	ret = ethnl_get_link_ksettings(info, dev, &ksettings);
	if (ret < 0)
		return ret;
	lsettings = &ksettings.base;

	ret = update_linkinfo(info, tb[ETHA_SETTINGS_LINK_INFO], lsettings);
	if (ret < 0)
		return ret;
	if (ret)
		mod_mask |= ETH_SETTINGS_IM_LINKINFO;

	ret = update_link_modes(info, tb[ETHA_SETTINGS_LINK_MODES], &ksettings);
	if (ret < 0)
		return ret;
	if (ret)
		mod_mask |= ETH_SETTINGS_IM_LINKMODES;

	if (mod_mask) {
		ret = ethnl_set_link_ksettings(info, dev, &ksettings);
		if (ret < 0)
			return ret;
		*req_mask |= mod_mask;
	}

	return 0;
}

static int update_wol(struct genl_info *info, struct nlattr *nest,
		      struct net_device *dev)
{
	struct nlattr *tb[ETHA_WOL_MAX + 1];
	struct ethtool_wolinfo wolinfo = {};
	int ret;

	if (!nest)
		return 0;
	ret = nla_parse_nested_strict(tb, ETHA_WOL_MAX, nest, set_wol_policy,
				      info->extack);
	if (ret < 0)
		return ret;

	ret = ethnl_get_wol(info, dev, &wolinfo);
	if (ret < 0) {
		ETHNL_SET_ERRMSG(info, "failed to get wol settings");
		return ret;
	}

	ret = 0;
	if (ethnl_update_bitfield32(&wolinfo.wolopts, tb[ETHA_WOL_MODES]))
		ret = 1;
	if (ethnl_update_binary(wolinfo.sopass, SOPASS_MAX,
				tb[ETHA_WOL_SOPASS]))
		ret = 1;
	if (ret) {
		int ret2 = dev->ethtool_ops->set_wol(dev, &wolinfo);
		if (ret2 < 0) {
			ETHNL_SET_ERRMSG(info, "wol info update failed");
			ret = ret2;
		}
	}

	return ret;
}

static int update_debug(struct genl_info *info, struct nlattr *nest,
			struct net_device *dev)
{
	struct nlattr *tb[ETHA_DEBUG_MAX + 1];
	u32 msglevel;
	int ret;

	if (!nest)
		return 0;
	ret = nla_parse_nested_strict(tb, ETHA_DEBUG_MAX, nest,
				      set_debug_policy, info->extack);
	if (ret < 0)
		return ret;

	if (!dev->ethtool_ops->get_msglevel ||
	    !dev->ethtool_ops->set_msglevel) {
		ETHNL_SET_ERRMSG(info,
				 "device does not provide msglvl access");
		return -EOPNOTSUPP;
	}
	ret = 0;
	msglevel = dev->ethtool_ops->get_msglevel(dev);
	if (ethnl_update_bitfield32(&msglevel, tb[ETHA_DEBUG_MSG_MASK])) {
		dev->ethtool_ops->set_msglevel(dev, msglevel);
		ret = 1;
	}

	return ret;
}

int ethnl_set_settings(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *tb[ETHA_SETTINGS_MAX + 1];
	struct net_device *dev;
	u32 req_mask = 0;
	int ret;

	ret = ethnlmsg_parse(info->nlhdr, tb, ETHA_SETTINGS_MAX,
			     set_settings_policy, info);
	if (ret < 0)
		return ret;
	dev = ethnl_dev_get(info, tb[ETHA_SETTINGS_DEV]);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	rtnl_lock();
	ret = ethnl_before_ops(dev);
	if (ret < 0)
		goto out_rtnl;
	if (tb[ETHA_SETTINGS_LINK_INFO] || tb[ETHA_SETTINGS_LINK_MODES]) {
		ret = -EOPNOTSUPP;
		if (!dev->ethtool_ops->get_link_ksettings)
			goto out_ops;
		ret = ethnl_update_ksettings(info, tb, dev, &req_mask);
		if (ret < 0)
			goto out_ops;
	}
	if (tb[ETHA_SETTINGS_WOL]) {
		ret = update_wol(info, tb[ETHA_SETTINGS_WOL], dev);
		if (ret < 0)
			goto out_ops;
		if (ret)
			req_mask |= ETH_SETTINGS_IM_WOL;
	}
	if (tb[ETHA_SETTINGS_DEBUG]) {
		ret = update_debug(info, tb[ETHA_SETTINGS_DEBUG], dev);
		if (ret < 0)
			goto out_ops;
		if (ret)
			req_mask |= ETH_SETTINGS_IM_DEBUG;
	}
	ret = 0;

out_ops:
	if (req_mask)
		ethtool_notify(dev, NULL, ETHNL_CMD_SET_SETTINGS, req_mask,
			       NULL);
	ethnl_after_ops(dev);
out_rtnl:
	rtnl_unlock();
	dev_put(dev);
	return ret;
}
