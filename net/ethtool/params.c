/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#include "netlink.h"

static const struct nla_policy get_params_policy[ETHA_PARAMS_MAX + 1] = {
	[ETHA_PARAMS_UNSPEC]		= { .type = NLA_REJECT },
	[ETHA_PARAMS_DEV]		= { .type = NLA_NESTED },
	[ETHA_PARAMS_INFOMASK]		= { .type = NLA_U32 },
	[ETHA_PARAMS_COMPACT]		= { .type = NLA_FLAG },
	[ETHA_PARAMS_COALESCE]		= { .type = NLA_REJECT },
};

struct params_data {
	struct common_req_info		reqinfo_base;

	/* everything below here will be reset for each device in dumps */
	struct common_reply_data	repdata_base;
	struct ethtool_coalesce		coalesce;
};

static int parse_params(struct common_req_info *req_info, struct sk_buff *skb,
			struct genl_info *info, const struct nlmsghdr *nlhdr)
{
	struct nlattr *tb[ETHA_PARAMS_MAX + 1];
	int ret;

	ret = ethnlmsg_parse(nlhdr, tb, ETHA_PARAMS_MAX, get_params_policy,
			     info);
	if (ret < 0)
		return ret;

	if (tb[ETHA_PARAMS_DEV]) {
		req_info->dev = ethnl_dev_get(info, tb[ETHA_PARAMS_DEV]);
		if (IS_ERR(req_info->dev)) {
			ret = PTR_ERR(req_info->dev);
			req_info->dev = NULL;
			return ret;
		}
	}
	if (tb[ETHA_PARAMS_INFOMASK])
		req_info->req_mask = nla_get_u32(tb[ETHA_PARAMS_INFOMASK]);
	if (tb[ETHA_PARAMS_COMPACT])
		req_info->compact = true;
	if (req_info->req_mask == 0)
		req_info->req_mask = ETH_PARAMS_IM_ALL;

	return 0;
}

static int ethnl_get_coalesce(struct net_device *dev,
			      struct ethtool_coalesce *data)
{
	if (!dev->ethtool_ops->get_coalesce)
		return -EOPNOTSUPP;
	return dev->ethtool_ops->get_coalesce(dev, data);
}

static int prepare_params(struct common_req_info *req_info,
			  struct genl_info *info)
{
	struct params_data *data =
		container_of(req_info, struct params_data, reqinfo_base);
	struct net_device *dev = data->repdata_base.dev;
	u32 req_mask = req_info->req_mask;
	int ret;

	ret = ethnl_before_ops(dev);
	if (ret < 0)
		return ret;
	if (req_mask & ETH_PARAMS_IM_COALESCE) {
		ret = ethnl_get_coalesce(dev, &data->coalesce);
		if (ret < 0)
			req_mask &= ~ETH_PARAMS_IM_COALESCE;
	}
	ethnl_after_ops(dev);

	data->repdata_base.info_mask = req_mask;
	if (req_info->req_mask & ~req_mask)
		warn_partial_info(info);
	return 0;
}

static int coalesce_size(void)
{
	return nla_total_size(20 * nla_total_size(sizeof(u32)) +
			      2 * nla_total_size(sizeof(u8)));
}

static int params_size(const struct common_req_info *req_info)
{
	struct params_data *data =
		container_of(req_info, struct params_data, reqinfo_base);
	u32 info_mask = data->repdata_base.info_mask;
	int len = 0;

	len += dev_ident_size();
	if (info_mask & ETH_PARAMS_IM_COALESCE)
		len += coalesce_size();

	return len;
}

static int fill_coalesce(struct sk_buff *skb, struct ethtool_coalesce *data)
{
	struct nlattr *nest = ethnl_nest_start(skb, ETHA_PARAMS_COALESCE);

	if (!nest)
		return -EMSGSIZE;
	if (nla_put_u32(skb, ETHA_COALESCE_RX_USECS,
			data->rx_coalesce_usecs) ||
	    nla_put_u32(skb, ETHA_COALESCE_RX_MAXFRM,
			data->rx_max_coalesced_frames) ||
	    nla_put_u32(skb, ETHA_COALESCE_RX_USECS_IRQ,
			data->rx_coalesce_usecs_irq) ||
	    nla_put_u32(skb, ETHA_COALESCE_RX_MAXFRM_IRQ,
			data->rx_max_coalesced_frames_irq) ||
	    nla_put_u32(skb, ETHA_COALESCE_RX_USECS_LOW,
			data->rx_coalesce_usecs_low) ||
	    nla_put_u32(skb, ETHA_COALESCE_RX_MAXFRM_LOW,
			data->rx_max_coalesced_frames_low) ||
	    nla_put_u32(skb, ETHA_COALESCE_RX_USECS_HIGH,
			data->rx_coalesce_usecs_high) ||
	    nla_put_u32(skb, ETHA_COALESCE_RX_MAXFRM_HIGH,
			data->rx_max_coalesced_frames_high) ||
	    nla_put_u32(skb, ETHA_COALESCE_TX_USECS,
			data->tx_coalesce_usecs) ||
	    nla_put_u32(skb, ETHA_COALESCE_TX_MAXFRM,
			data->tx_max_coalesced_frames) ||
	    nla_put_u32(skb, ETHA_COALESCE_TX_USECS_IRQ,
			data->tx_coalesce_usecs_irq) ||
	    nla_put_u32(skb, ETHA_COALESCE_TX_MAXFRM_IRQ,
			data->tx_max_coalesced_frames_irq) ||
	    nla_put_u32(skb, ETHA_COALESCE_TX_USECS_LOW,
			data->tx_coalesce_usecs_low) ||
	    nla_put_u32(skb, ETHA_COALESCE_TX_MAXFRM_LOW,
			data->tx_max_coalesced_frames_low) ||
	    nla_put_u32(skb, ETHA_COALESCE_TX_USECS_HIGH,
			data->tx_coalesce_usecs_high) ||
	    nla_put_u32(skb, ETHA_COALESCE_TX_MAXFRM_HIGH,
			data->tx_max_coalesced_frames_high) ||
	    nla_put_u32(skb, ETHA_COALESCE_PKT_RATE_LOW,
			data->pkt_rate_low) ||
	    nla_put_u32(skb, ETHA_COALESCE_PKT_RATE_HIGH,
			data->pkt_rate_high) ||
	    nla_put_u8(skb, ETHA_COALESCE_RX_USE_ADAPTIVE,
		       !!data->use_adaptive_rx_coalesce) ||
	    nla_put_u8(skb, ETHA_COALESCE_TX_USE_ADAPTIVE,
		       !!data->use_adaptive_tx_coalesce) ||
	    nla_put_u32(skb, ETHA_COALESCE_RATE_SAMPLE_INTERVAL,
			data->rate_sample_interval) ||
	    nla_put_u32(skb, ETHA_COALESCE_STATS_BLOCK_USECS,
			data->stats_block_coalesce_usecs)) {
		nla_nest_cancel(skb, nest);
		return -EMSGSIZE;
	}

	nla_nest_end(skb, nest);
	return 0;
}

static int fill_params(struct sk_buff *skb,
		       const struct common_req_info *req_info)
{
	struct params_data *data =
		container_of(req_info, struct params_data, reqinfo_base);
	u32 info_mask = data->repdata_base.info_mask;
	int ret;

	if (info_mask & ETH_PARAMS_IM_COALESCE) {
		ret = fill_coalesce(skb, &data->coalesce);
		if (ret < 0)
			return ret;
	}

	return 0;
}

const struct get_request_ops params_request_ops = {
	.request_cmd		= ETHNL_CMD_GET_PARAMS,
	.reply_cmd		= ETHNL_CMD_SET_PARAMS,
	.dev_attrtype		= ETHA_PARAMS_DEV,
	.data_size		= sizeof(struct params_data),
	.repdata_offset		= offsetof(struct params_data, repdata_base),

	.parse_request		= parse_params,
	.prepare_data		= prepare_params,
	.reply_size		= params_size,
	.fill_reply		= fill_params,
};
