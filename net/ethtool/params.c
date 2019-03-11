/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#include "netlink.h"
#include "common.h"
#include "bitset.h"

static const struct nla_policy get_params_policy[ETHA_PARAMS_MAX + 1] = {
	[ETHA_PARAMS_UNSPEC]		= { .type = NLA_REJECT },
	[ETHA_PARAMS_DEV]		= { .type = NLA_NESTED },
	[ETHA_PARAMS_INFOMASK]		= { .type = NLA_U32 },
	[ETHA_PARAMS_COMPACT]		= { .type = NLA_FLAG },
	[ETHA_PARAMS_COALESCE]		= { .type = NLA_REJECT },
	[ETHA_PARAMS_RING]		= { .type = NLA_REJECT },
	[ETHA_PARAMS_PAUSE]		= { .type = NLA_REJECT },
	[ETHA_PARAMS_CHANNELS]		= { .type = NLA_REJECT },
	[ETHA_PARAMS_EEE]		= { .type = NLA_REJECT },
};

struct params_data {
	struct common_req_info		reqinfo_base;

	/* everything below here will be reset for each device in dumps */
	struct common_reply_data	repdata_base;
	struct ethtool_coalesce		coalesce;
	struct ethtool_ringparam	ring;
	struct ethtool_pauseparam	pause;
	struct ethtool_channels		channels;
	struct ethtool_eee		eee;
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

static int ethnl_get_ring(struct net_device *dev,
			  struct ethtool_ringparam *data)
{
	if (!dev->ethtool_ops->get_ringparam)
		return -EOPNOTSUPP;
	dev->ethtool_ops->get_ringparam(dev, data);
	return 0;
}

static int ethnl_get_pause(struct net_device *dev,
			   struct ethtool_pauseparam *data)
{
	if (!dev->ethtool_ops->get_pauseparam)
		return -EOPNOTSUPP;
	dev->ethtool_ops->get_pauseparam(dev, data);
	return 0;
}

static int ethnl_get_channels(struct net_device *dev,
			      struct ethtool_channels *data)
{
	if (!dev->ethtool_ops->get_channels)
		return -EOPNOTSUPP;
	dev->ethtool_ops->get_channels(dev, data);
	return 0;
}

static int ethnl_get_eee(struct net_device *dev, struct ethtool_eee *data)
{
	if (!dev->ethtool_ops->get_eee)
		return -EOPNOTSUPP;
	return dev->ethtool_ops->get_eee(dev, data);
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
	if (req_mask & ETH_PARAMS_IM_RING) {
		ret = ethnl_get_ring(dev, &data->ring);
		if (ret < 0)
			req_mask &= ~ETH_PARAMS_IM_RING;
	}
	if (req_mask & ETH_PARAMS_IM_PAUSE) {
		ret = ethnl_get_pause(dev, &data->pause);
		if (ret < 0)
			req_mask &= ~ETH_PARAMS_IM_PAUSE;
	}
	if (req_mask & ETH_PARAMS_IM_CHANNELS) {
		ret = ethnl_get_channels(dev, &data->channels);
		if (ret < 0)
			req_mask &= ~ETH_PARAMS_IM_CHANNELS;
	}
	if (req_mask & ETH_PARAMS_IM_EEE) {
		ret = ethnl_get_eee(dev, &data->eee);
		if (ret < 0)
			req_mask &= ~ETH_PARAMS_IM_EEE;
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

static int ring_size(void)
{
	return nla_total_size(8 * nla_total_size(sizeof(u32)));
}

static int pause_size(void)
{
	return nla_total_size(3 * nla_total_size(sizeof(u8)));
}

static int channels_size(void)
{
	return nla_total_size(8 * nla_total_size(sizeof(u32)));
}

static int eee_size(const struct ethtool_eee *eee, bool compact)
{
	const unsigned int flags = compact ? ETHNL_BITSET_COMPACT : 0;
	int len = 0;
	int ret;

	/* link_modes */
	ret = ethnl_bitset32_size(sizeof(eee->advertised) * BITS_PER_BYTE,
				  &eee->advertised, &eee->supported,
				  link_mode_names, flags);
	if (ret < 0)
		return ret;
	len += ret;
	/* peer_modes */
	ret = ethnl_bitset32_size(sizeof(eee->lp_advertised) * BITS_PER_BYTE,
				  &eee->lp_advertised, NULL, link_mode_names,
				  flags | ETHNL_BITSET_LIST);
	if (ret < 0)
		return ret;
	len += ret;
	/* active, enabled, tx_lpi_enabled */
	len += 3 * nla_total_size(sizeof(u8));
	/* tx_lpi_timer */
	len += nla_total_size(sizeof(u32));

	/* nest */
	return nla_total_size(len);
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
	if (info_mask & ETH_PARAMS_IM_RING)
		len += ring_size();
	if (info_mask & ETH_PARAMS_IM_PAUSE)
		len += pause_size();
	if (info_mask & ETH_PARAMS_IM_CHANNELS)
		len += channels_size();
	if (info_mask & ETH_PARAMS_IM_EEE) {
		int ret = eee_size(&data->eee, req_info->compact);

		if (ret < 0)
			return ret;
		len += ret;
	}

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

static int fill_ring(struct sk_buff *skb, struct ethtool_ringparam *data)
{
	struct nlattr *nest = ethnl_nest_start(skb, ETHA_PARAMS_RING);

	if (!nest)
		return -EMSGSIZE;
	if (nla_put_u32(skb, ETHA_RING_RX_MAX_PENDING,
			data->rx_max_pending) ||
	    nla_put_u32(skb, ETHA_RING_RX_MINI_MAX_PENDING,
			data->rx_mini_max_pending) ||
	    nla_put_u32(skb, ETHA_RING_RX_JUMBO_MAX_PENDING,
			data->rx_jumbo_max_pending) ||
	    nla_put_u32(skb, ETHA_RING_TX_MAX_PENDING,
			data->tx_max_pending) ||
	    nla_put_u32(skb, ETHA_RING_RX_PENDING,
			data->rx_pending) ||
	    nla_put_u32(skb, ETHA_RING_RX_MINI_PENDING,
			data->rx_mini_pending) ||
	    nla_put_u32(skb, ETHA_RING_RX_JUMBO_PENDING,
			data->rx_jumbo_pending) ||
	    nla_put_u32(skb, ETHA_RING_TX_PENDING,
			data->tx_pending)) {
		nla_nest_cancel(skb, nest);
		return -EMSGSIZE;
	}

	nla_nest_end(skb, nest);
	return 0;
}

static int fill_pause(struct sk_buff *skb, struct ethtool_pauseparam *data)
{
	struct nlattr *nest = ethnl_nest_start(skb, ETHA_PARAMS_PAUSE);

	if (!nest)
		return -EMSGSIZE;
	if (nla_put_u8(skb, ETHA_PAUSE_AUTONEG, !!data->autoneg) ||
	    nla_put_u8(skb, ETHA_PAUSE_RX, !!data->rx_pause) ||
	    nla_put_u8(skb, ETHA_PAUSE_TX, !!data->tx_pause)) {
		nla_nest_cancel(skb, nest);
		return -EMSGSIZE;
	}

	nla_nest_end(skb, nest);
	return 0;
}

static int fill_channels(struct sk_buff *skb, struct ethtool_channels *data)
{
	struct nlattr *nest = ethnl_nest_start(skb, ETHA_PARAMS_CHANNELS);

	if (!nest)
		return -EMSGSIZE;
	if (nla_put_u32(skb, ETHA_CHANNELS_MAX_RX, data->max_rx) ||
	    nla_put_u32(skb, ETHA_CHANNELS_MAX_TX, data->max_tx) ||
	    nla_put_u32(skb, ETHA_CHANNELS_MAX_OTHER, data->max_other) ||
	    nla_put_u32(skb, ETHA_CHANNELS_MAX_COMBINED, data->max_combined) ||
	    nla_put_u32(skb, ETHA_CHANNELS_RX_COUNT, data->rx_count) ||
	    nla_put_u32(skb, ETHA_CHANNELS_TX_COUNT, data->tx_count) ||
	    nla_put_u32(skb, ETHA_CHANNELS_OTHER_COUNT, data->other_count) ||
	    nla_put_u32(skb, ETHA_CHANNELS_COMBINED_COUNT,
			data->combined_count)) {
		return -EMSGSIZE;
	}

	nla_nest_end(skb, nest);
	return 0;
}

static int fill_eee(struct sk_buff *skb, struct ethtool_eee *data, bool compact)
{
	const unsigned int flags = compact ? ETHNL_BITSET_COMPACT : 0;
	struct nlattr *nest = ethnl_nest_start(skb, ETHA_PARAMS_EEE);
	int ret;

	if (!nest)
		return -EMSGSIZE;
	ret = ethnl_put_bitset32(skb, ETHA_EEE_LINK_MODES,
				 sizeof(data->advertised) * BITS_PER_BYTE,
				 &data->advertised, &data->supported,
				 link_mode_names, flags);
	if (ret < 0)
		goto err;
	ret = ethnl_put_bitset32(skb, ETHA_EEE_PEER_MODES,
				 sizeof(data->lp_advertised) * BITS_PER_BYTE,
				 &data->lp_advertised, &data->lp_advertised,
				 link_mode_names, flags | ETHNL_BITSET_LIST);
	if (ret < 0)
		goto err;

	if (nla_put_u8(skb, ETHA_EEE_ACTIVE, !!data->eee_active) ||
	    nla_put_u8(skb, ETHA_EEE_ENABLED, !!data->eee_enabled) ||
	    nla_put_u8(skb, ETHA_EEE_TX_LPI_ENABLED, !!data->tx_lpi_enabled) ||
	    nla_put_u32(skb, ETHA_EEE_TX_LPI_TIMER, data->tx_lpi_timer)) {
		ret = -EMSGSIZE;
		goto err;
	}

	nla_nest_end(skb, nest);
	return 0;
err:
	nla_nest_cancel(skb, nest);
	return ret;
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
	if (info_mask & ETH_PARAMS_IM_RING) {
		ret = fill_ring(skb, &data->ring);
		if (ret < 0)
			return ret;
	}
	if (info_mask & ETH_PARAMS_IM_PAUSE) {
		ret = fill_pause(skb, &data->pause);
		if (ret < 0)
			return ret;
	}
	if (info_mask & ETH_PARAMS_IM_CHANNELS) {
		ret = fill_channels(skb, &data->channels);
		if (ret < 0)
			return ret;
	}
	if (info_mask & ETH_PARAMS_IM_EEE) {
		ret = fill_eee(skb, &data->eee, req_info->compact);
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

/* SET_PARAMS */

static const struct nla_policy set_params_policy[ETHA_PARAMS_MAX + 1] = {
	[ETHA_PARAMS_UNSPEC]		= { .type = NLA_REJECT },
	[ETHA_PARAMS_DEV]		= { .type = NLA_NESTED },
	[ETHA_PARAMS_INFOMASK]		= { .type = NLA_REJECT },
	[ETHA_PARAMS_COMPACT]		= { .type = NLA_FLAG },
	[ETHA_PARAMS_COALESCE]		= { .type = NLA_NESTED },
	[ETHA_PARAMS_RING]		= { .type = NLA_NESTED },
	[ETHA_PARAMS_PAUSE]		= { .type = NLA_NESTED },
	[ETHA_PARAMS_CHANNELS]		= { .type = NLA_NESTED },
	[ETHA_PARAMS_EEE]		= { .type = NLA_REJECT },
};

static const struct nla_policy coalesce_policy[ETHA_COALESCE_MAX + 1] = {
	[ETHA_COALESCE_UNSPEC]			= { .type = NLA_REJECT },
	[ETHA_COALESCE_RX_USECS]		= { .type = NLA_U32 },
	[ETHA_COALESCE_RX_MAXFRM]		= { .type = NLA_U32 },
	[ETHA_COALESCE_RX_USECS_IRQ]		= { .type = NLA_U32 },
	[ETHA_COALESCE_RX_MAXFRM_IRQ]		= { .type = NLA_U32 },
	[ETHA_COALESCE_RX_USECS_LOW]		= { .type = NLA_U32 },
	[ETHA_COALESCE_RX_MAXFRM_LOW]		= { .type = NLA_U32 },
	[ETHA_COALESCE_RX_USECS_HIGH]		= { .type = NLA_U32 },
	[ETHA_COALESCE_RX_MAXFRM_HIGH]		= { .type = NLA_U32 },
	[ETHA_COALESCE_TX_USECS]		= { .type = NLA_U32 },
	[ETHA_COALESCE_TX_MAXFRM]		= { .type = NLA_U32 },
	[ETHA_COALESCE_TX_USECS_IRQ]		= { .type = NLA_U32 },
	[ETHA_COALESCE_TX_MAXFRM_IRQ]		= { .type = NLA_U32 },
	[ETHA_COALESCE_TX_USECS_LOW]		= { .type = NLA_U32 },
	[ETHA_COALESCE_TX_MAXFRM_LOW]		= { .type = NLA_U32 },
	[ETHA_COALESCE_TX_USECS_HIGH]		= { .type = NLA_U32 },
	[ETHA_COALESCE_TX_MAXFRM_HIGH]		= { .type = NLA_U32 },
	[ETHA_COALESCE_PKT_RATE_LOW]		= { .type = NLA_U32 },
	[ETHA_COALESCE_PKT_RATE_HIGH]		= { .type = NLA_U32 },
	[ETHA_COALESCE_RX_USE_ADAPTIVE]		= { .type = NLA_U8 },
	[ETHA_COALESCE_TX_USE_ADAPTIVE]		= { .type = NLA_U8 },
	[ETHA_COALESCE_RATE_SAMPLE_INTERVAL]	= { .type = NLA_U32 },
	[ETHA_COALESCE_STATS_BLOCK_USECS]	= { .type = NLA_U32 },
};

static int update_coalesce(struct genl_info *info, struct net_device *dev,
			   struct nlattr *nest)
{
	struct nlattr *tb[ETHA_COALESCE_MAX + 1];
	struct ethtool_coalesce data = {};
	bool mod = false;
	int ret;

	if (!nest)
		return 0;
	if (!dev->ethtool_ops->get_coalesce || !dev->ethtool_ops->set_coalesce)
		return -EOPNOTSUPP;
	ret = dev->ethtool_ops->get_coalesce(dev, &data);
	if (ret < 0)
		return ret;

	ret = nla_parse_nested_strict(tb, ETHA_COALESCE_MAX, nest,
				      coalesce_policy, info->extack);
	if (ret < 0)
		return ret;

	if (ethnl_update_u32(&data.rx_coalesce_usecs,
			     tb[ETHA_COALESCE_RX_USECS]))
		mod = true;
	if (ethnl_update_u32(&data.rx_max_coalesced_frames,
			     tb[ETHA_COALESCE_RX_MAXFRM]))
		mod = true;
	if (ethnl_update_u32(&data.rx_coalesce_usecs_irq,
			     tb[ETHA_COALESCE_RX_USECS_IRQ]))
		mod = true;
	if (ethnl_update_u32(&data.rx_max_coalesced_frames_irq,
			     tb[ETHA_COALESCE_RX_MAXFRM_IRQ]))
		mod = true;
	if (ethnl_update_u32(&data.rx_coalesce_usecs_low,
			     tb[ETHA_COALESCE_RX_USECS_LOW]))
		mod = true;
	if (ethnl_update_u32(&data.rx_max_coalesced_frames_low,
			     tb[ETHA_COALESCE_RX_MAXFRM_LOW]))
		mod = true;
	if (ethnl_update_u32(&data.rx_coalesce_usecs_high,
			     tb[ETHA_COALESCE_RX_USECS_HIGH]))
		mod = true;
	if (ethnl_update_u32(&data.rx_max_coalesced_frames_high,
			     tb[ETHA_COALESCE_RX_MAXFRM_HIGH]))
		mod = true;
	if (ethnl_update_u32(&data.tx_coalesce_usecs,
			     tb[ETHA_COALESCE_TX_USECS]))
		mod = true;
	if (ethnl_update_u32(&data.tx_max_coalesced_frames,
			     tb[ETHA_COALESCE_TX_MAXFRM]))
		mod = true;
	if (ethnl_update_u32(&data.tx_coalesce_usecs_irq,
			     tb[ETHA_COALESCE_TX_USECS_IRQ]))
		mod = true;
	if (ethnl_update_u32(&data.tx_max_coalesced_frames_irq,
			     tb[ETHA_COALESCE_TX_MAXFRM_IRQ]))
		mod = true;
	if (ethnl_update_u32(&data.tx_coalesce_usecs_low,
			     tb[ETHA_COALESCE_TX_USECS_LOW]))
		mod = true;
	if (ethnl_update_u32(&data.tx_max_coalesced_frames_low,
			     tb[ETHA_COALESCE_TX_MAXFRM_LOW]))
		mod = true;
	if (ethnl_update_u32(&data.tx_coalesce_usecs_high,
			     tb[ETHA_COALESCE_TX_USECS_HIGH]))
		mod = true;
	if (ethnl_update_u32(&data.tx_max_coalesced_frames_high,
			     tb[ETHA_COALESCE_TX_MAXFRM_HIGH]))
		mod = true;
	if (ethnl_update_u32(&data.pkt_rate_low,
			     tb[ETHA_COALESCE_PKT_RATE_LOW]))
		mod = true;
	if (ethnl_update_u32(&data.pkt_rate_high,
			     tb[ETHA_COALESCE_PKT_RATE_HIGH]))
		mod = true;
	if (ethnl_update_bool32(&data.use_adaptive_rx_coalesce,
				tb[ETHA_COALESCE_RX_USE_ADAPTIVE]))
		mod = true;
	if (ethnl_update_bool32(&data.use_adaptive_tx_coalesce,
				tb[ETHA_COALESCE_TX_USE_ADAPTIVE]))
		mod = true;
	if (ethnl_update_u32(&data.rate_sample_interval,
			     tb[ETHA_COALESCE_RATE_SAMPLE_INTERVAL]))
		mod = true;
	if (ethnl_update_u32(&data.stats_block_coalesce_usecs,
			     tb[ETHA_COALESCE_STATS_BLOCK_USECS]))
		mod = true;

	if (!mod)
		return 0;
	ret = dev->ethtool_ops->set_coalesce(dev, &data);
	return (ret < 0) ? ret : 1;
}

static const struct nla_policy ring_policy[ETHA_RING_MAX + 1] = {
	[ETHA_RING_UNSPEC]			= { .type = NLA_REJECT },
	[ETHA_RING_RX_MAX_PENDING]		= { .type = NLA_U32 },
	[ETHA_RING_RX_MINI_MAX_PENDING]		= { .type = NLA_U32 },
	[ETHA_RING_RX_JUMBO_MAX_PENDING]	= { .type = NLA_U32 },
	[ETHA_RING_TX_MAX_PENDING]		= { .type = NLA_U32 },
	[ETHA_RING_RX_PENDING]			= { .type = NLA_U32 },
	[ETHA_RING_RX_MINI_PENDING]		= { .type = NLA_U32 },
	[ETHA_RING_RX_JUMBO_PENDING]		= { .type = NLA_U32 },
	[ETHA_RING_TX_PENDING]			= { .type = NLA_U32 },
};

static int update_ring(struct genl_info *info, struct net_device *dev,
		       struct nlattr *nest)
{
	struct nlattr *tb[ETHA_RING_MAX + 1];
	struct ethtool_ringparam data = {};
	bool mod = false;
	int ret;

	if (!nest)
		return 0;
	if (!dev->ethtool_ops->get_ringparam ||
	    !dev->ethtool_ops->set_ringparam)
		return -EOPNOTSUPP;
	dev->ethtool_ops->get_ringparam(dev, &data);

	ret = nla_parse_nested_strict(tb, ETHA_RING_MAX, nest, ring_policy,
				      info->extack);
	if (ret < 0)
		return ret;
	/* read only attributes */
	if (tb[ETHA_RING_RX_MAX_PENDING] || tb[ETHA_RING_RX_MINI_MAX_PENDING] ||
	    tb[ETHA_RING_RX_JUMBO_MAX_PENDING] ||
	    tb[ETHA_RING_TX_MAX_PENDING]) {
		ETHNL_SET_ERRMSG(info, "attempt to set a read only attribute");
		return -EINVAL;
	}

	if (ethnl_update_u32(&data.rx_pending, tb[ETHA_RING_RX_PENDING]))
		mod = true;
	if (ethnl_update_u32(&data.rx_mini_pending,
			     tb[ETHA_RING_RX_MINI_PENDING]))
		mod = true;
	if (ethnl_update_u32(&data.rx_jumbo_pending,
			     tb[ETHA_RING_RX_JUMBO_PENDING]))
		mod = true;
	if (ethnl_update_u32(&data.tx_pending, tb[ETHA_RING_TX_PENDING]))
		mod = true;
	if (!mod)
		return 0;

	/* ensure new ring parameters are within the maximums */
	if (data.rx_pending > data.rx_max_pending ||
	    data.rx_mini_pending > data.rx_mini_max_pending ||
	    data.rx_jumbo_pending > data.rx_jumbo_max_pending ||
	    data.tx_pending > data.tx_max_pending) {
		ETHNL_SET_ERRMSG(info,
				 "requested ring param value exceeeds maximum");
		return -EINVAL;
	}

	ret = dev->ethtool_ops->set_ringparam(dev, &data);
	return (ret < 0) ? ret : 1;
}

static const struct nla_policy pause_policy[ETHA_PAUSE_MAX + 1] = {
	[ETHA_PAUSE_UNSPEC]	= { .type = NLA_REJECT },
	[ETHA_PAUSE_AUTONEG]	= { .type = NLA_U8 },
	[ETHA_PAUSE_RX]		= { .type = NLA_U8 },
	[ETHA_PAUSE_TX]		= { .type = NLA_U8 },
};

static int update_pause(struct genl_info *info, struct net_device *dev,
			struct nlattr *nest)
{
	struct nlattr *tb[ETHA_RING_MAX + 1];
	struct ethtool_pauseparam data = {};
	bool mod = false;
	int ret;

	if (!nest)
		return 0;
	if (!dev->ethtool_ops->get_pauseparam ||
	    !dev->ethtool_ops->set_pauseparam)
		return -EOPNOTSUPP;
	dev->ethtool_ops->get_pauseparam(dev, &data);

	ret = nla_parse_nested_strict(tb, ETHA_PAUSE_MAX, nest, pause_policy,
				      info->extack);
	if (ret < 0)
		return ret;

	if (ethnl_update_u32(&data.autoneg, tb[ETHA_PAUSE_AUTONEG]))
		mod = true;
	if (ethnl_update_u32(&data.rx_pause, tb[ETHA_PAUSE_RX]))
		mod = true;
	if (ethnl_update_u32(&data.tx_pause, tb[ETHA_PAUSE_TX]))
		mod = true;

	if (!mod)
		return 0;
	ret = dev->ethtool_ops->set_pauseparam(dev, &data);
	return (ret < 0) ? ret : 1;
}

static const struct nla_policy channels_policy[ETHA_CHANNELS_MAX + 1] = {
	[ETHA_CHANNELS_UNSPEC]		= { .type = NLA_REJECT },
	[ETHA_CHANNELS_MAX_RX]		= { .type = NLA_REJECT },
	[ETHA_CHANNELS_MAX_TX]		= { .type = NLA_REJECT },
	[ETHA_CHANNELS_MAX_OTHER]	= { .type = NLA_REJECT },
	[ETHA_CHANNELS_MAX_COMBINED]	= { .type = NLA_REJECT },
	[ETHA_CHANNELS_RX_COUNT]	= { .type = NLA_U32 },
	[ETHA_CHANNELS_TX_COUNT]	= { .type = NLA_U32 },
	[ETHA_CHANNELS_OTHER_COUNT]	= { .type = NLA_U32 },
	[ETHA_CHANNELS_COMBINED_COUNT]	= { .type = NLA_U32 },
};

static int update_channels(struct genl_info *info, struct net_device *dev,
			   struct nlattr *nest)
{
	struct ethtool_channels old = { .cmd = ETHTOOL_GCHANNELS };
	struct ethtool_channels new = { .cmd = ETHTOOL_SCHANNELS };
	struct nlattr *tb[ETHA_CHANNELS_MAX + 1];
	bool mod = false;
	int ret;

	if (!nest)
		return 0;
	if (!dev->ethtool_ops->get_channels ||
	    !dev->ethtool_ops->set_channels)
		return -EOPNOTSUPP;
	dev->ethtool_ops->get_channels(dev, &old);
	new = old;
	new.cmd = ETHTOOL_SCHANNELS;

	ret = nla_parse_nested_strict(tb, ETHA_CHANNELS_MAX, nest,
				      channels_policy, info->extack);
	if (ret < 0)
		return ret;

	if (ethnl_update_u32(&new.rx_count, tb[ETHA_CHANNELS_RX_COUNT]))
		mod = true;
	if (ethnl_update_u32(&new.tx_count, tb[ETHA_CHANNELS_TX_COUNT]))
		mod = true;
	if (ethnl_update_u32(&new.other_count,tb[ETHA_CHANNELS_OTHER_COUNT]))
		mod = true;
	if (ethnl_update_u32(&new.combined_count,
			     tb[ETHA_CHANNELS_COMBINED_COUNT]))
		mod = true;
	if (!mod)
		return 0;

	ret = __ethtool_set_channels(dev, &old, &new);
	return (ret < 0) ? ret : 1;
}

int ethnl_set_params(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *tb[ETHA_PARAMS_MAX + 1];
	struct net_device *dev;
	u32 req_mask = 0;
	int ret;

	ret = ethnlmsg_parse(info->nlhdr, tb, ETHA_PARAMS_MAX,
			     set_params_policy, info);
	if (ret < 0)
		return ret;
	dev = ethnl_dev_get(info, tb[ETHA_PARAMS_DEV]);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	rtnl_lock();
	ret = ethnl_before_ops(dev);
	if (ret < 0)
		goto out_rtnl;
	ret = update_coalesce(info, dev, tb[ETHA_PARAMS_COALESCE]);
	if (ret < 0)
		goto out_ops;
	if (ret)
		req_mask |= ETH_PARAMS_IM_COALESCE;
	ret = update_ring(info, dev, tb[ETHA_PARAMS_RING]);
	if (ret < 0)
		goto out_ops;
	if (ret)
		req_mask |= ETH_PARAMS_IM_RING;
	ret = update_pause(info, dev, tb[ETHA_PARAMS_PAUSE]);
	if (ret < 0)
		goto out_ops;
	if (ret)
		req_mask |= ETH_PARAMS_IM_PAUSE;
	ret = update_channels(info, dev, tb[ETHA_PARAMS_CHANNELS]);
	if (ret < 0)
		goto out_ops;
	if (ret)
		req_mask |= ETH_PARAMS_IM_CHANNELS;

	ret = 0;
out_ops:
	if (req_mask)
		ethtool_notify(dev, NULL, ETHNL_CMD_SET_PARAMS, req_mask, NULL);
	ethnl_after_ops(dev);
out_rtnl:
	rtnl_unlock();
	dev_put(dev);
	return ret;
}
