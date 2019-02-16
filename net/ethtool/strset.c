// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note

#include <linux/ethtool.h>
#include <linux/phy.h>
#include "netlink.h"
#include "common.h"

enum strset_type {
	ETH_SS_TYPE_NONE,
	ETH_SS_TYPE_LEGACY,
	ETH_SS_TYPE_SIMPLE,
};

struct strset_info {
	enum strset_type type;
	bool per_dev;
	bool free_data;
	unsigned int count;
	union {
		const char (*legacy)[ETH_GSTRING_LEN];
		const char * const *simple;
		void *ptr;
	} data;
};

static const struct strset_info info_template[] = {
	[ETH_SS_TEST] = {
		.type		= ETH_SS_TYPE_LEGACY,
		.per_dev	= true,
	},
	[ETH_SS_STATS] = {
		.type		= ETH_SS_TYPE_LEGACY,
		.per_dev	= true,
	},
	[ETH_SS_PRIV_FLAGS] = {
		.type		= ETH_SS_TYPE_LEGACY,
		.per_dev	= true,
	},
	[ETH_SS_NTUPLE_FILTERS] = {
		.type		= ETH_SS_TYPE_NONE,
	},
	[ETH_SS_FEATURES] = {
		.type		= ETH_SS_TYPE_LEGACY,
		.per_dev	= false,
		.count		= ARRAY_SIZE(netdev_features_strings),
		.data		= { .legacy = netdev_features_strings },
	},
	[ETH_SS_RSS_HASH_FUNCS] = {
		.type		= ETH_SS_TYPE_LEGACY,
		.per_dev	= false,
		.count		= ARRAY_SIZE(rss_hash_func_strings),
		.data		= { .legacy = rss_hash_func_strings },
	},
	[ETH_SS_TUNABLES] = {
		.type		= ETH_SS_TYPE_LEGACY,
		.per_dev	= false,
		.count		= ARRAY_SIZE(tunable_strings),
		.data		= { .legacy = tunable_strings },
	},
	[ETH_SS_PHY_STATS] = {
		.type		= ETH_SS_TYPE_LEGACY,
		.per_dev	= true,
	},
	[ETH_SS_PHY_TUNABLES] = {
		.type		= ETH_SS_TYPE_LEGACY,
		.per_dev	= false,
		.count		= ARRAY_SIZE(phy_tunable_strings),
		.data		= { .legacy = phy_tunable_strings },
	},
	[ETH_SS_TSTAMP_SOF] = {
		.type		= ETH_SS_TYPE_SIMPLE,
		.per_dev	= false,
		.count		= __SOF_TIMESTAMPING_COUNT,
		.data		= { .simple = so_timestamping_labels },
	},
	[ETH_SS_TSTAMP_TX_TYPE] = {
		.type		= ETH_SS_TYPE_SIMPLE,
		.per_dev	= false,
		.count		= __HWTSTAMP_TX_COUNT,
		.data		= { .simple = tstamp_tx_type_labels },
	},
	[ETH_SS_TSTAMP_RX_FILTER] = {
		.type		= ETH_SS_TYPE_SIMPLE,
		.per_dev	= false,
		.count		= __HWTSTAMP_FILTER_COUNT,
		.data		= { .simple = tstamp_rx_filter_labels },
	},
	[ETH_SS_LINK_MODES] = {
		.type		= ETH_SS_TYPE_SIMPLE,
		.per_dev	= false,
		.count		= __ETHTOOL_LINK_MODE_MASK_NBITS,
		.data		= { .simple = link_mode_names },
	},
};

struct strset_data {
	struct common_req_info		reqinfo_base;
	u32				req_ids;
	bool				counts_only;

	/* everything below here will be reset for each device in dumps */
	struct common_reply_data	repdata_base;
	struct strset_info		info[ETH_SS_COUNT];
};

static const struct nla_policy get_strset_policy[ETHA_STRSET_MAX + 1] = {
	[ETHA_STRSET_UNSPEC]		= { .type = NLA_REJECT },
	[ETHA_STRSET_DEV]		= { .type = NLA_NESTED },
	[ETHA_STRSET_COUNTS]		= { .type = NLA_FLAG },
	[ETHA_STRSET_STRINGSET]		= { .type = NLA_NESTED },
};

static const struct nla_policy get_stringset_policy[ETHA_STRINGSET_MAX + 1] = {
	[ETHA_STRINGSET_UNSPEC]		= { .type = NLA_REJECT },
	[ETHA_STRINGSET_ID]		= { .type = NLA_U32 },
	[ETHA_STRINGSET_COUNT]		= { .type = NLA_REJECT },
	[ETHA_STRINGSET_STRINGS]	= { .type = NLA_REJECT },
};

static bool id_requested(const struct strset_data *data, u32 id)
{
	return data->req_ids & (1U << id);
}

static bool include_set(const struct strset_data *data, u32 id)
{
	bool per_dev;

	BUILD_BUG_ON(ETH_SS_COUNT >= BITS_PER_BYTE * sizeof(data->req_ids));

	if (data->req_ids)
		return id_requested(data, id);

	per_dev = data->info[id].per_dev;
	if (data->info[id].type == ETH_SS_TYPE_NONE)
		return false;
	return data->repdata_base.dev ? per_dev : !per_dev;
}

const char *str_value(const struct strset_info *info, unsigned int i)
{
	switch (info->type) {
	case ETH_SS_TYPE_LEGACY:
		return info->data.legacy[i];
	case ETH_SS_TYPE_SIMPLE:
		return info->data.simple[i];
	default:
		WARN_ONCE(1, "unexpected string set type");
		return "";
	}
}

static int get_strset_id(const struct nlattr *nest, u32 *val,
			 struct genl_info *info)
{
	struct nlattr *tb[ETHA_STRINGSET_MAX + 1];
	int ret;

	ret = nla_parse_nested_strict(tb, ETHA_STRINGSET_MAX, nest,
				      get_stringset_policy,
				      info ? info->extack : NULL);
	if (ret < 0)
		return ret;
	if (!tb[ETHA_STRINGSET_ID])
		return -EINVAL;

	*val = nla_get_u32(tb[ETHA_STRINGSET_ID]);
	return 0;
}

/* parse_request() handler */
static int parse_strset(struct common_req_info *req_info, struct sk_buff *skb,
			struct genl_info *info, const struct nlmsghdr *nlhdr)
{
	struct strset_data *data =
		container_of(req_info, struct strset_data, reqinfo_base);
	struct nlattr *attr;
	int rem, ret;

	ret = nlmsg_validate(nlhdr, GENL_HDRLEN, ETHA_STRSET_MAX,
			     get_strset_policy, info ? info->extack : NULL);
	if (ret < 0)
		return ret;

	nlmsg_for_each_attr(attr, nlhdr, GENL_HDRLEN, rem) {
		u32 id;

		switch (nla_type(attr)) {
		case ETHA_STRSET_DEV:
			req_info->dev = ethnl_dev_get(info, attr);
			if (IS_ERR(req_info->dev)) {
				ret = PTR_ERR(req_info->dev);
				req_info->dev = NULL;
				return ret;
			}
			break;
		case ETHA_STRSET_COUNTS:
			data->counts_only = true;
			break;
		case ETHA_STRSET_STRINGSET:
			ret = get_strset_id(attr, &id, info);
			if (ret < 0)
				return ret;
			if (ret >= ETH_SS_COUNT)
				return -EOPNOTSUPP;
			data->req_ids |= (1U << id);
			break;
		default:
			ETHNL_SET_ERRMSG(info,
					 "unexpected attribute in ETHNL_CMD_GET_STRSET message");
			return genl_err_attr(info, -EINVAL, attr);
		}
	}

	return 0;
}

static void free_strset(struct strset_data *data)
{
	unsigned int i;

	for (i = 0; i < ETH_SS_COUNT; i++)
		if (data->info[i].free_data) {
			kfree(data->info[i].data.ptr);
			data->info[i].data.ptr = NULL;
			data->info[i].free_data = false;
		}
}

static int prepare_one_stringset(struct strset_info *info,
				 struct net_device *dev, unsigned int id,
				 bool counts_only)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	void *strings;
	int count, ret;

	if (id == ETH_SS_PHY_STATS && dev->phydev &&
	    !ops->get_ethtool_phy_stats)
		ret = phy_ethtool_get_sset_count(dev->phydev);
	else if (ops->get_sset_count && ops->get_strings)
		ret = ops->get_sset_count(dev, id);
	else
		ret = -EOPNOTSUPP;
	if (ret <= 0) {
		info->count = 0;
		return 0;
	}

	count = ret;
	if (!counts_only) {
		strings = kcalloc(count, ETH_GSTRING_LEN, GFP_KERNEL);
		if (!strings)
			return -ENOMEM;
		if (id == ETH_SS_PHY_STATS && dev->phydev &&
		    !ops->get_ethtool_phy_stats)
			phy_ethtool_get_strings(dev->phydev, strings);
		else
			ops->get_strings(dev, id, strings);
		info->data.legacy = strings;
		info->free_data = true;
	}
	info->count = count;

	return 0;
}

/* prepare_data() handler */
static int prepare_strset(struct common_req_info *req_info,
			  struct genl_info *info)
{
	struct strset_data *data =
		container_of(req_info, struct strset_data, reqinfo_base);
	struct net_device *dev = data->repdata_base.dev;
	unsigned int i;
	int ret;

	BUILD_BUG_ON(ARRAY_SIZE(info_template) != ETH_SS_COUNT);
	memcpy(&data->info, &info_template, sizeof(data->info));

	if (!dev) {
		for (i = 0; i < ETH_SS_COUNT; i++) {
			if (id_requested(data, i) &&
			    data->info[i].per_dev) {
				ETHNL_SET_ERRMSG(info,
						 "requested per device strings without dev");
				return -EINVAL;
			}
		}
	}

	ret = ethnl_before_ops(dev);
	if (ret < 0)
		goto err_strset;
	for (i = 0; i < ETH_SS_COUNT; i++) {
		if (!include_set(data, i) || !data->info[i].per_dev)
			continue;
		if (WARN_ONCE(data->info[i].type != ETH_SS_TYPE_LEGACY,
			      "unexpected string set type %u",
			      data->info[i].type))
			goto err_ops;

		ret = prepare_one_stringset(&data->info[i], dev, i,
					    data->counts_only);
		if (ret < 0)
			goto err_ops;
	}
	ethnl_after_ops(dev);

	return 0;
err_ops:
	ethnl_after_ops(dev);
err_strset:
	free_strset(data);
	return ret;
}

static int legacy_set_size(const char (*set)[ETH_GSTRING_LEN],
			   unsigned int count)
{
	unsigned int len = 0;
	unsigned int i;

	for (i = 0; i < count; i++)
		len += nla_total_size(nla_total_size(sizeof(u32)) +
				      ethnl_str_size(set[i]));
	len = 2 * nla_total_size(sizeof(u32)) + nla_total_size(len);

	return nla_total_size(len);
}

static int simple_set_size(const char * const *set, unsigned int count)
{
	unsigned int len = 0;
	unsigned int i;

	for (i = 0; i < count; i++)
		len += nla_total_size(nla_total_size(sizeof(u32)) +
				      ethnl_str_size(set[i]));
	len = 2 * nla_total_size(sizeof(u32)) + nla_total_size(len);

	return nla_total_size(len);
}

static int set_size(const struct strset_info *info, bool counts_only)
{
	if (info->count == 0)
		return 0;
	if (counts_only)
		return nla_total_size(2 * nla_total_size(sizeof(u32)));

	switch (info->type) {
	case ETH_SS_TYPE_LEGACY:
		return legacy_set_size(info->data.legacy, info->count);
	case ETH_SS_TYPE_SIMPLE:
		return simple_set_size(info->data.simple, info->count);
	default:
		return -EINVAL;
	};
}

/* reply_size() handler */
static int strset_size(const struct common_req_info *req_info)
{
	const struct strset_data *data =
		container_of(req_info, struct strset_data, reqinfo_base);
	unsigned int i;
	int len = 0;
	int ret;

	len += dev_ident_size();
	for (i = 0; i < ETH_SS_COUNT; i++) {
		const struct strset_info *info = &data->info[i];

		if (!include_set(data, i) || info->type == ETH_SS_TYPE_NONE)
			continue;

		ret = set_size(info, data->counts_only);
		if (ret < 0)
			return ret;
		len += ret;
	}

	return len;
}

static int fill_string(struct sk_buff *skb, const struct strset_info *info,
		       u32 idx)
{
	struct nlattr *string = ethnl_nest_start(skb, ETHA_STRINGS_STRING);

	if (!string)
		return -EMSGSIZE;
	if (nla_put_u32(skb, ETHA_STRING_INDEX, idx) ||
	    nla_put_string(skb, ETHA_STRING_VALUE, str_value(info, idx)))
		return -EMSGSIZE;
	nla_nest_end(skb, string);

	return 0;
}

static int fill_set(struct sk_buff *skb, const struct strset_data *data, u32 id)
{
	const struct strset_info *info = &data->info[id];
	struct nlattr *strings;
	struct nlattr *nest;
	unsigned int i = (unsigned int)(-1);

	if (info->type == ETH_SS_TYPE_NONE)
		return -EOPNOTSUPP;
	if (info->count == 0)
		return 0;
	nest = ethnl_nest_start(skb, ETHA_STRSET_STRINGSET);
	if (!nest)
		return -EMSGSIZE;

	if (nla_put_u32(skb, ETHA_STRINGSET_ID, id) ||
	    nla_put_u32(skb, ETHA_STRINGSET_COUNT, info->count))
		goto err;

	if (!data->counts_only) {
		strings = ethnl_nest_start(skb, ETHA_STRINGSET_STRINGS);
		if (!strings)
			goto err;
		for (i = 0; i < info->count; i++) {
			if (fill_string(skb, info, i) < 0)
				goto err;
		}
		nla_nest_end(skb, strings);
	}

	nla_nest_end(skb, nest);
	return 0;

err:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

/* fill_reply() handler */
static int fill_strset(struct sk_buff *skb,
		       const struct common_req_info *req_info)
{
	const struct strset_data *data =
		container_of(req_info, struct strset_data, reqinfo_base);
	unsigned int i;
	int ret;

	for (i = 0; i < ETH_SS_COUNT; i++)
		if (include_set(data, i)) {
			ret = fill_set(skb, data, i);
			if (ret < 0)
				return ret;
		}

	return 0;
}

const struct get_request_ops strset_request_ops = {
	.request_cmd		= ETHNL_CMD_GET_STRSET,
	.reply_cmd		= ETHNL_CMD_SET_STRSET,
	.dev_attrtype		= ETHA_STRSET_DEV,
	.data_size		= sizeof(struct strset_data),
	.repdata_offset		= offsetof(struct strset_data, repdata_base),
	.allow_nodev_do		= true,

	.parse_request		= parse_strset,
	.prepare_data		= prepare_strset,
	.reply_size		= strset_size,
	.fill_reply		= fill_strset,
};
