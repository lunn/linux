// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note

#include <linux/net_tstamp.h>

#include "netlink.h"
#include "common.h"
#include "bitset.h"

const char *const so_timestamping_labels[] = {
	"hardware-transmit",		/* SOF_TIMESTAMPING_TX_HARDWARE */
	"software-transmit",		/* SOF_TIMESTAMPING_TX_SOFTWARE */
	"hardware-receive",		/* SOF_TIMESTAMPING_RX_HARDWARE */
	"software-receive",		/* SOF_TIMESTAMPING_RX_SOFTWARE */
	"software-system-clock",	/* SOF_TIMESTAMPING_SOFTWARE */
	"hardware-legacy-clock",	/* SOF_TIMESTAMPING_SYS_HARDWARE */
	"hardware-raw-clock",		/* SOF_TIMESTAMPING_RAW_HARDWARE */
	"option-id",			/* SOF_TIMESTAMPING_OPT_ID */
	"sched-transmit",		/* SOF_TIMESTAMPING_TX_SCHED */
	"ack-transmit",			/* SOF_TIMESTAMPING_TX_ACK */
	"option-cmsg",			/* SOF_TIMESTAMPING_OPT_CMSG */
	"option-tsonly",		/* SOF_TIMESTAMPING_OPT_TSONLY */
	"option-stats",			/* SOF_TIMESTAMPING_OPT_STATS */
	"option-pktinfo",		/* SOF_TIMESTAMPING_OPT_PKTINFO */
	"option-tx-swhw",		/* SOF_TIMESTAMPING_OPT_TX_SWHW */
};

const char *const tstamp_tx_type_labels[] = {
	[HWTSTAMP_TX_OFF]		= "off",
	[HWTSTAMP_TX_ON]		= "on",
	[HWTSTAMP_TX_ONESTEP_SYNC]	= "one-step-sync",
};

const char *const tstamp_rx_filter_labels[] = {
	[HWTSTAMP_FILTER_NONE]			= "none",
	[HWTSTAMP_FILTER_ALL]			= "all",
	[HWTSTAMP_FILTER_SOME]			= "some",
	[HWTSTAMP_FILTER_PTP_V1_L4_EVENT]	= "ptpv1-l4-event",
	[HWTSTAMP_FILTER_PTP_V1_L4_SYNC]	= "ptpv1-l4-sync",
	[HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ]	= "ptpv1-l4-delay-req",
	[HWTSTAMP_FILTER_PTP_V2_L4_EVENT]	= "ptpv2-l4-event",
	[HWTSTAMP_FILTER_PTP_V2_L4_SYNC]	= "ptpv2-l4-sync",
	[HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ]	= "ptpv2-l4-delay-req",
	[HWTSTAMP_FILTER_PTP_V2_L2_EVENT]	= "ptpv2-l2-event",
	[HWTSTAMP_FILTER_PTP_V2_L2_SYNC]	= "ptpv2-l2-sync",
	[HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ]	= "ptpv2-l2-delay-req",
	[HWTSTAMP_FILTER_PTP_V2_EVENT]		= "ptpv2-event",
	[HWTSTAMP_FILTER_PTP_V2_SYNC]		= "ptpv2-sync",
	[HWTSTAMP_FILTER_PTP_V2_DELAY_REQ]	= "ptpv2-delay-req",
	[HWTSTAMP_FILTER_NTP_ALL]		= "ntp-all",
};

struct info_data {
	struct common_req_info		reqinfo_base;

	/* everything below here will be reset for each device in dumps */
	struct common_reply_data	repdata_base;
	struct ethtool_drvinfo		drvinfo;
	struct ethtool_ts_info		tsinfo;
};

static const struct nla_policy get_info_policy[ETHA_INFO_MAX + 1] = {
	[ETHA_INFO_UNSPEC]		= { .type = NLA_REJECT },
	[ETHA_INFO_DEV]			= { .type = NLA_NESTED },
	[ETHA_INFO_INFOMASK]		= { .type = NLA_U32 },
	[ETHA_INFO_COMPACT]		= { .type = NLA_FLAG },
	[ETHA_INFO_DRVINFO]		= { .type = NLA_REJECT },
	[ETHA_INFO_TSINFO]		= { .type = NLA_REJECT },
};

/* parse_request() handler */
static int parse_info(struct common_req_info *req_info, struct sk_buff *skb,
		      struct genl_info *info, const struct nlmsghdr *nlhdr)
{
	struct nlattr *tb[ETHA_INFO_MAX + 1];
	int ret;

	ret = ethnlmsg_parse(nlhdr, tb, ETHA_INFO_MAX, get_info_policy, info);
	if (ret < 0)
		return ret;

	if (tb[ETHA_INFO_DEV]) {
		req_info->dev = ethnl_dev_get(info, tb[ETHA_INFO_DEV]);
		if (IS_ERR(req_info->dev)) {
			ret = PTR_ERR(req_info->dev);
			req_info->dev = NULL;
			return ret;
		}
	}
	if (tb[ETHA_INFO_INFOMASK])
		req_info->req_mask = nla_get_u32(tb[ETHA_INFO_INFOMASK]);
	if (tb[ETHA_INFO_COMPACT])
		req_info->compact = true;
	if (req_info->req_mask == 0)
		req_info->req_mask = ETH_INFO_IM_ALL;

	return 0;
}

/* prepare_data() handler */
static int prepare_info(struct common_req_info *req_info,
			struct genl_info *info)
{
	struct info_data *data =
		container_of(req_info, struct info_data, reqinfo_base);
	struct net_device *dev = data->repdata_base.dev;
	u32 req_mask = req_info->req_mask & ETH_INFO_IM_ALL;
	int ret;

	ret = ethnl_before_ops(dev);
	if (ret < 0)
		return ret;
	if (req_mask & ETH_INFO_IM_DRVINFO) {
		ret = __ethtool_get_drvinfo(dev, &data->drvinfo);
		if (ret < 0)
			req_mask &= ~ETH_INFO_IM_DRVINFO;
	}
	if (req_mask & ETH_INFO_IM_TSINFO) {
		ret = __ethtool_get_ts_info(dev, &data->tsinfo);
		if (ret < 0)
			req_mask &= ~ETH_INFO_IM_TSINFO;
	}
	ethnl_after_ops(dev);

	data->repdata_base.info_mask = req_mask;
	if (req_info->req_mask & ~req_mask)
		warn_partial_info(info);
	return 0;
}

static int drvinfo_size(const struct ethtool_drvinfo *drvinfo)
{
	int len = 0;

	len += ethnl_str_ifne_size(drvinfo->driver);
	len += ethnl_str_ifne_size(drvinfo->fw_version);
	len += ethnl_str_ifne_size(drvinfo->bus_info);
	len += ethnl_str_ifne_size(drvinfo->erom_version);

	return nla_total_size(len);
}

static int tsinfo_size(const struct ethtool_ts_info *tsinfo, bool compact)
{
	const unsigned int flags = compact ? ETHNL_BITSET_COMPACT : 0;
	int len = 0;
	int ret;

	/* if any of these exceeds 32, we need a different interface to talk to
	 * NIC drivers anyway
	 */
	BUILD_BUG_ON(__SOF_TIMESTAMPING_COUNT > 32);
	BUILD_BUG_ON(__HWTSTAMP_TX_COUNT > 32);
	BUILD_BUG_ON(__HWTSTAMP_FILTER_COUNT > 32);

	ret = ethnl_bitset32_size(__SOF_TIMESTAMPING_COUNT,
				  &tsinfo->so_timestamping, NULL,
				  so_timestamping_labels, flags);
	if (ret < 0)
		return ret;
	len += ret;
	ret = ethnl_bitset32_size(__HWTSTAMP_TX_COUNT,
				  &tsinfo->tx_types, NULL,
				  tstamp_tx_type_labels, flags);
	if (ret < 0)
		return ret;
	len += ret;
	ret = ethnl_bitset32_size(__HWTSTAMP_FILTER_COUNT,
				  &tsinfo->rx_filters, NULL,
				  tstamp_rx_filter_labels, flags);
	if (ret < 0)
		return ret;
	len += ret;
	len += nla_total_size(sizeof(u32));

	return nla_total_size(len);
}

/* reply_size() handler */
static int info_size(const struct common_req_info *req_info)
{
	const struct info_data *data =
		container_of(req_info, struct info_data, reqinfo_base);
	u32 info_mask = data->repdata_base.info_mask;
	int len = 0;

	len += dev_ident_size();
	if (info_mask & ETH_INFO_IM_DRVINFO)
		len += drvinfo_size(&data->drvinfo);
	if (info_mask & ETH_INFO_IM_TSINFO) {
		int ret = tsinfo_size(&data->tsinfo, req_info->compact);

		if (ret < 0)
			return ret;
		len += ret;
	}

	return len;
}

static int fill_drvinfo(struct sk_buff *skb,
			const struct ethtool_drvinfo *drvinfo)
{
	struct nlattr *nest = ethnl_nest_start(skb, ETHA_INFO_DRVINFO);
	int ret;

	if (!nest)
		return -EMSGSIZE;
	ret = -EMSGSIZE;
	if (ethnl_put_str_ifne(skb, ETHA_DRVINFO_DRIVER, drvinfo->driver) ||
	    ethnl_put_str_ifne(skb, ETHA_DRVINFO_FWVERSION,
			       drvinfo->fw_version) ||
	    ethnl_put_str_ifne(skb, ETHA_DRVINFO_BUSINFO, drvinfo->bus_info) ||
	    ethnl_put_str_ifne(skb, ETHA_DRVINFO_EROM_VER,
			       drvinfo->erom_version))
		goto err;

	nla_nest_end(skb, nest);
	return 0;
err:
	nla_nest_cancel(skb, nest);
	return ret;
}

static int fill_tsinfo(struct sk_buff *skb,
		       const struct ethtool_ts_info *tsinfo, bool compact)
{
	const unsigned int flags = compact ? ETHNL_BITSET_COMPACT : 0;
	struct nlattr *nest = ethnl_nest_start(skb, ETHA_INFO_TSINFO);
	int ret;

	if (!nest)
		return -EMSGSIZE;
	ret = ethnl_put_bitset32(skb, ETHA_TSINFO_TIMESTAMPING,
				 __SOF_TIMESTAMPING_COUNT,
				 &tsinfo->so_timestamping, NULL,
				 so_timestamping_labels, flags);
	if (ret < 0)
		goto err;
	ret = -EMSGSIZE;
	if (tsinfo->phc_index >= 0 &&
	    nla_put_u32(skb, ETHA_TSINFO_PHC_INDEX, tsinfo->phc_index))
		goto err;

	ret = ethnl_put_bitset32(skb, ETHA_TSINFO_TX_TYPES, __HWTSTAMP_TX_COUNT,
				 &tsinfo->tx_types, NULL, tstamp_tx_type_labels,
				 flags);
	if (ret < 0)
		goto err;
	ret = ethnl_put_bitset32(skb, ETHA_TSINFO_RX_FILTERS,
				 __HWTSTAMP_FILTER_COUNT, &tsinfo->rx_filters,
				 NULL, tstamp_rx_filter_labels, flags);
	if (ret < 0)
		goto err;

	nla_nest_end(skb, nest);
	return 0;
err:
	nla_nest_cancel(skb, nest);
	return ret;
}

/* fill_reply() handler */
static int fill_info(struct sk_buff *skb,
		     const struct common_req_info *req_info)
{
	const struct info_data *data =
		container_of(req_info, struct info_data, reqinfo_base);
	u32 info_mask = data->repdata_base.info_mask;
	int ret;

	if (info_mask & ETH_INFO_IM_DRVINFO) {
		ret = fill_drvinfo(skb, &data->drvinfo);
		if (ret < 0)
			return ret;
	}
	if (info_mask & ETH_INFO_IM_TSINFO) {
		ret = fill_tsinfo(skb, &data->tsinfo, req_info->compact);
		if (ret < 0)
			return ret;
	}

	return 0;
}

const struct get_request_ops info_request_ops = {
	.request_cmd		= ETHNL_CMD_GET_INFO,
	.reply_cmd		= ETHNL_CMD_SET_INFO,
	.dev_attrtype		= ETHA_INFO_DEV,
	.data_size		= sizeof(struct info_data),
	.repdata_offset		= offsetof(struct info_data, repdata_base),

	.parse_request		= parse_info,
	.prepare_data		= prepare_info,
	.reply_size		= info_size,
	.fill_reply		= fill_info,
};
