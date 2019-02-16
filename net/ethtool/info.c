// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note

#include "netlink.h"
#include "common.h"
#include "bitset.h"

struct info_data {
	struct common_req_info		reqinfo_base;

	/* everything below here will be reset for each device in dumps */
	struct common_reply_data	repdata_base;
	struct ethtool_drvinfo		drvinfo;
};

static const struct nla_policy get_info_policy[ETHTOOL_A_INFO_MAX + 1] = {
	[ETHTOOL_A_INFO_UNSPEC]		= { .type = NLA_REJECT },
	[ETHTOOL_A_INFO_DEV]		= { .type = NLA_NESTED },
	[ETHTOOL_A_INFO_INFOMASK]	= { .type = NLA_U32 },
	[ETHTOOL_A_INFO_COMPACT]	= { .type = NLA_FLAG },
	[ETHTOOL_A_INFO_DRVINFO]	= { .type = NLA_REJECT },
};

/* parse_request() handler */
static int parse_info(struct common_req_info *req_info, struct sk_buff *skb,
		      struct genl_info *info, const struct nlmsghdr *nlhdr)
{
	struct nlattr *tb[ETHTOOL_A_INFO_MAX + 1];
	int ret;

	ret = nlmsg_parse(nlhdr, GENL_HDRLEN, tb, ETHTOOL_A_INFO_MAX,
			  get_info_policy, info ? info->extack : NULL);
	if (ret < 0)
		return ret;

	if (tb[ETHTOOL_A_INFO_DEV]) {
		req_info->dev = ethnl_dev_get(info, tb[ETHTOOL_A_INFO_DEV]);
		if (IS_ERR(req_info->dev)) {
			ret = PTR_ERR(req_info->dev);
			req_info->dev = NULL;
			return ret;
		}
	}
	if (tb[ETHTOOL_A_INFO_INFOMASK])
		req_info->req_mask = nla_get_u32(tb[ETHTOOL_A_INFO_INFOMASK]);
	if (tb[ETHTOOL_A_INFO_COMPACT])
		req_info->compact = true;
	if (req_info->req_mask == 0)
		req_info->req_mask = ETHTOOL_IM_INFO_ALL;

	return 0;
}

/* prepare_data() handler */
static int prepare_info(struct common_req_info *req_info,
			struct genl_info *info)
{
	struct info_data *data =
		container_of(req_info, struct info_data, reqinfo_base);
	struct net_device *dev = data->repdata_base.dev;
	u32 req_mask = req_info->req_mask & ETHTOOL_IM_INFO_ALL;
	int ret;

	ret = ethnl_before_ops(dev);
	if (ret < 0)
		return ret;
	if (req_mask & ETHTOOL_IM_INFO_DRVINFO) {
		ret = __ethtool_get_drvinfo(dev, &data->drvinfo);
		if (ret < 0)
			req_mask &= ~ETHTOOL_IM_INFO_DRVINFO;
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

	if (drvinfo->driver[0])
		len += ethnl_str_size(drvinfo->driver);
	if (drvinfo->fw_version[0])
		len += ethnl_str_size(drvinfo->fw_version);
	if (drvinfo->bus_info[0])
		len += ethnl_str_size(drvinfo->bus_info);
	if (drvinfo->erom_version[0])
		len += ethnl_str_size(drvinfo->erom_version);

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
	if (info_mask & ETHTOOL_IM_INFO_DRVINFO)
		len += drvinfo_size(&data->drvinfo);

	return len;
}

static int fill_drvinfo(struct sk_buff *skb,
			const struct ethtool_drvinfo *drvinfo)
{
	struct nlattr *nest;
	int ret;

	nest = nla_nest_start(skb, ETHTOOL_A_INFO_DRVINFO);
	if (!nest)
		return -EMSGSIZE;
	ret = -EMSGSIZE;
	if ((drvinfo->driver[0] &&
	     nla_put_string(skb, ETHTOOL_A_DRVINFO_DRIVER, drvinfo->driver)) ||
	    (drvinfo->fw_version[0] &&
	     nla_put_string(skb, ETHTOOL_A_DRVINFO_FWVERSION,
			    drvinfo->fw_version)) ||
	    (drvinfo->bus_info[0] &&
	     nla_put_string(skb, ETHTOOL_A_DRVINFO_BUSINFO,
			    drvinfo->bus_info)) ||
	    (drvinfo->erom_version[0] &&
	     nla_put_string(skb, ETHTOOL_A_DRVINFO_EROM_VER,
			    drvinfo->erom_version)))
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

	if (info_mask & ETHTOOL_IM_INFO_DRVINFO) {
		ret = fill_drvinfo(skb, &data->drvinfo);
		if (ret < 0)
			return ret;
	}

	return 0;
}

const struct get_request_ops info_request_ops = {
	.request_cmd		= ETHNL_CMD_GET_INFO,
	.reply_cmd		= ETHNL_CMD_SET_INFO,
	.dev_attrtype		= ETHTOOL_A_INFO_DEV,
	.data_size		= sizeof(struct info_data),
	.repdata_offset		= offsetof(struct info_data, repdata_base),

	.parse_request		= parse_info,
	.prepare_data		= prepare_info,
	.reply_size		= info_size,
	.fill_reply		= fill_info,
};
