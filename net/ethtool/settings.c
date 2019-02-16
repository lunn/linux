// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note

#include "netlink.h"
#include "common.h"
#include "bitset.h"

struct settings_data {
	struct common_req_info		reqinfo_base;

	/* everything below here will be reset for each device in dumps */
	struct common_reply_data	repdata_base;
	struct ethtool_link_ksettings	ksettings;
	struct ethtool_link_settings	*lsettings;
	bool				lpm_empty;
};

static const struct nla_policy get_settings_policy[ETHA_SETTINGS_MAX + 1] = {
	[ETHA_SETTINGS_UNSPEC]		= { .type = NLA_REJECT },
	[ETHA_SETTINGS_DEV]		= { .type = NLA_NESTED },
	[ETHA_SETTINGS_INFOMASK]	= { .type = NLA_U32 },
	[ETHA_SETTINGS_COMPACT]		= { .type = NLA_FLAG },
	[ETHA_SETTINGS_LINK_INFO]	= { .type = NLA_REJECT },
	[ETHA_SETTINGS_LINK_MODES]	= { .type = NLA_REJECT },
};

static int parse_settings(struct common_req_info *req_info,
			  struct sk_buff *skb, struct genl_info *info,
			  const struct nlmsghdr *nlhdr)
{
	struct nlattr *tb[ETHA_SETTINGS_MAX + 1];
	int ret;

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

static int prepare_settings(struct common_req_info *req_info,
			    struct genl_info *info)
{
	struct settings_data *data =
		container_of(req_info, struct settings_data, reqinfo_base);
	struct net_device *dev = data->repdata_base.dev;
	u32 req_mask = req_info->req_mask;
	int ret;

	data->lsettings = &data->ksettings.base;
	data->lpm_empty = true;

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
