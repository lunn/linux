/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#include "netlink.h"
#include "common.h"
#include "bitset.h"

#define RXFLOW_ALL_HASHFNS \
       ((1 << (ETH_RSS_HASH_FUNCS_COUNT - 1)) | \
	((1 << (ETH_RSS_HASH_FUNCS_COUNT - 1)) - 1))
#define FLOW_TYPE_COUNT (ETHER_FLOW + 1)
#define HASHOPT_FLOW_TYPES \
	(BIT(TCP_V4_FLOW) | \
	 BIT(UDP_V4_FLOW) | \
	 BIT(SCTP_V4_FLOW) | \
	 BIT(AH_ESP_V4_FLOW) | \
	 BIT(TCP_V6_FLOW) | \
	 BIT(UDP_V6_FLOW) | \
	 BIT(SCTP_V6_FLOW) | \
	 BIT(AH_ESP_V6_FLOW) | \
	 BIT(AH_V4_FLOW) | \
	 BIT(ESP_V4_FLOW) | \
	 BIT(AH_V6_FLOW) | \
	 BIT(ESP_V6_FLOW) | \
	 BIT(IPV4_FLOW) | \
	 BIT(IPV6_FLOW))
#define RXH_ALL 0xfe
#define RXH_COUNT 8

static const struct nla_policy get_rxflow_policy[ETHA_RXFLOW_MAX + 1] = {
	[ETHA_RXFLOW_UNSPEC]		= { .type = NLA_REJECT },
	[ETHA_RXFLOW_DEV]		= { .type = NLA_NESTED },
	[ETHA_RXFLOW_INFOMASK]		= { .type = NLA_U32 },
	[ETHA_RXFLOW_COMPACT]		= { .type = NLA_FLAG },
	[ETHA_RXFLOW_CTXOP]		= { .type = NLA_REJECT },
	[ETHA_RXFLOW_CONTEXT]		= { .type = NLA_U32 },
	[ETHA_RXFLOW_NRINGS]		= { .type = NLA_REJECT },
	[ETHA_RXFLOW_HASH_FN]		= { .type = NLA_REJECT },
	[ETHA_RXFLOW_HASH_KEY]		= { .type = NLA_REJECT },
	[ETHA_RXFLOW_HASH_OPTS]		= { .type = NLA_REJECT },
	[ETHA_RXFLOW_INDTBL_SIZE]	= { .type = NLA_REJECT },
	[ETHA_RXFLOW_INDIR_TBL]		= { .type = NLA_REJECT },
};

struct rxflow_data {
	struct common_req_info		reqinfo_base;
	u32				req_context;
	u32				req_flow_type;

	/* everything below here will be reset for each device in dumps */
	struct common_reply_data	repdata_base;
	unsigned int			entry_size;
	u32				indtbl_size;
	u32				hkey_size;
	u32				n_rings;
	u32				*indir_tbl;
	u8				*hkey;
	u32				hash_fn;
	u32				hash_fields[FLOW_TYPE_COUNT];
};

static int parse_rxflow(struct common_req_info *req_info, struct sk_buff *skb,
			struct genl_info *info, const struct nlmsghdr *nlhdr)
{
	struct rxflow_data *data =
		container_of(req_info, struct rxflow_data, reqinfo_base);
	struct nlattr *tb[ETHA_RXFLOW_MAX + 1];
	int ret;

	ret = ethnlmsg_parse(nlhdr, tb, ETHA_RXFLOW_MAX, get_rxflow_policy,
			     info);
	if (ret < 0)
		return ret;

	if (tb[ETHA_RXFLOW_DEV]) {
		req_info->dev = ethnl_dev_get(info, tb[ETHA_RXFLOW_DEV]);
		if (IS_ERR(req_info->dev)) {
			ret = PTR_ERR(req_info->dev);
			req_info->dev = NULL;
			return ret;
		}
	}
	if (tb[ETHA_RXFLOW_INFOMASK])
		req_info->req_mask = nla_get_u32(tb[ETHA_RXFLOW_INFOMASK]);
	if (tb[ETHA_RXFLOW_COMPACT])
		req_info->compact = true;
	if (tb[ETHA_RXFLOW_CONTEXT])
		data->req_context = nla_get_u32(tb[ETHA_RXFLOW_CONTEXT]);
	if (req_info->req_mask == 0)
		req_info->req_mask = ETH_RXFLOW_IM_ALL;

	return 0;
}

static int alloc_hkey(struct rxflow_data *data)
{
	if (!data->hkey_size)
		return -EOPNOTSUPP;
	data->hkey = kzalloc(data->hkey_size, GFP_KERNEL);
	return data->hkey ? 0 : -ENOMEM;
}

static int get_hash_opts(struct net_device *dev, struct rxflow_data *data)
{
	struct ethtool_rxnfc cmd = {
		.cmd		= ETHTOOL_GRXFH,
		.rss_context	= data->req_context,
	};
	u32 req_flow_type = data->req_flow_type;
	u32 *fields = data->hash_fields;
	unsigned int idx;
	int ret;

	for (idx = 0; idx < FLOW_TYPE_COUNT; idx++) {
		if ((req_flow_type && idx != req_flow_type) ||
		    !(HASHOPT_FLOW_TYPES & (1 << idx)))
			continue;
		cmd.flow_type = (data->req_context ? FLOW_RSS : 0) | idx;
		ret = dev->ethtool_ops->get_rxnfc(dev, &cmd, NULL);
		if (ret < 0)
			continue;
		WARN_ONCE(cmd.data >> 32,
			  "%s: ethtool_ops->get_rxnfc() returned more than 32 flags\n",
			  netdev_name(dev));
		fields[idx] = (u32)cmd.data;
	}

	return 0;
}

static int alloc_indtbl(struct rxflow_data *data)
{
	u32 max_ring;

	if (!data->indtbl_size)
		return -EOPNOTSUPP;

	max_ring = data->n_rings - 1;
	data->entry_size = (max_ring >> 16) ? 4 : ((max_ring >> 8) ? 2 : 1);
	data->indir_tbl = kcalloc(data->indtbl_size, sizeof(u32), GFP_KERNEL);
	return data->indir_tbl ? 0 : -ENOMEM;
}

static int prepare_rxflow(struct common_req_info *req_info,
			  struct genl_info *info)
{
	struct rxflow_data *data =
		container_of(req_info, struct rxflow_data, reqinfo_base);
	struct ethtool_rxnfc rx_rings = { .cmd = ETHTOOL_GRXRINGS };
	struct net_device *dev = data->repdata_base.dev;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	u32 req_mask = req_info->req_mask;
	u8 hash_fn = 0;
	int ret;

	if (!ops->get_rxnfc)
		return -EOPNOTSUPP;
	if (data->req_context && !ops->get_rxfh_context)
		return -EOPNOTSUPP;
	if (!data->req_context && !ops->get_rxfh)
		return -EOPNOTSUPP;

	ret = ethnl_before_ops(dev);
	if (ret < 0)
		return ret;

	if (req_mask & ETH_RXFLOW_IM_INDTBL)
		req_info->req_mask = (req_mask |= ETH_RXFLOW_IM_INFO);
	ret = dev->ethtool_ops->get_rxnfc(dev, &rx_rings, NULL);
	if (ret < 0)
		return ret;
	data->n_rings = rx_rings.data;
	if (ops->get_rxfh_indir_size)
		data->indtbl_size = ops->get_rxfh_indir_size(dev);
	if (ops->get_rxfh_key_size)
		data->hkey_size = ops->get_rxfh_key_size(dev);

	if (req_mask & ETH_RXFLOW_IM_HKEY) {
		ret = alloc_hkey(data);
		if (ret < 0)
			req_mask &= ~ETH_RXFLOW_IM_HKEY;
	}
	if (req_mask & ETH_RXFLOW_IM_HASHOPTS) {
		ret = get_hash_opts(dev, data);
		if (ret < 0)
			req_mask &= ~ETH_RXFLOW_IM_HASHOPTS;
	}
	if (req_mask & ETH_RXFLOW_IM_INDTBL) {
		ret = alloc_indtbl(data);
		if (ret < 0)
			req_mask &= ~ETH_RXFLOW_IM_INDTBL;
	}
	if (data->req_context)
		ret = ops->get_rxfh_context(dev, data->indir_tbl, data->hkey,
					    &hash_fn, data->req_context);
	else
		ret = ops->get_rxfh(dev, data->indir_tbl, data->hkey, &hash_fn);
	if (ret == 0)
		data->hash_fn = hash_fn;
	ethnl_after_ops(dev);

	data->repdata_base.info_mask = req_mask;
	if (ret == 0 && req_info->req_mask & ~req_mask)
		warn_partial_info(info);
	return ret;
}

static int hashopts_size(const u32 *fields)
{
	unsigned int i;
	int len = 0;

	for (i = 0; i < FLOW_TYPE_COUNT; i++) {
		unsigned int i_len;

		if (!fields[i])
			continue;
		i_len = (fields[i] & RXH_DISCARD) ?
			0 : sizeof(struct nla_bitfield32);
		len += nla_total_size(nla_total_size(sizeof(u32)) +
				      nla_total_size(i_len));
	}

	return nla_total_size(len);
}

static int indtbl_size(const struct rxflow_data *data)
{
	unsigned int len;

	/* block data */
	len = nla_total_size(data->indtbl_size * data->entry_size);
	/* block nest */
	len =  nla_total_size(2 *  nla_total_size(sizeof(u32)) + len);
	/* ETHA_RXFLOW_INDTBL_SIZE */
	len += nla_total_size(sizeof(u32));

	return len;
}

static int rxflow_size(const struct common_req_info *req_info)
{
	const struct rxflow_data *data =
		container_of(req_info, struct rxflow_data, reqinfo_base);
	u32 info_mask = data->repdata_base.info_mask;
	const u32 all_hashfn = RXFLOW_ALL_HASHFNS;
	int len = 0;
	int ret;

	len += dev_ident_size();
	if (data->req_context)
		len += nla_total_size(sizeof(u32));
	if (info_mask & ETH_RXFLOW_IM_INFO)
		len += nla_total_size(sizeof(u32));
	if (info_mask & ETH_RXFLOW_IM_HASHFN) {
		const unsigned int flags =
			(req_info->compact ? ETHNL_BITSET_COMPACT : 0) |
			ETHNL_BITSET_LEGACY_NAMES;

		ret = ethnl_bitset32_size(ETH_RSS_HASH_FUNCS_COUNT,
					  &data->hash_fn, &all_hashfn,
					  rss_hash_func_strings, flags);
		if (ret < 0)
			return ret;
		len += ret;
	}
	if (info_mask & ETH_RXFLOW_IM_HKEY)
		len += nla_total_size(data->hkey_size);
	if (info_mask & ETH_RXFLOW_IM_HASHOPTS)
		len += hashopts_size(data->hash_fields);
	if (info_mask & ETH_RXFLOW_IM_INDTBL)
		len += indtbl_size(data);

	return len;
}

static int fill_rxflow_hashfn(struct sk_buff *skb,
			      const struct rxflow_data *data)
{
	const unsigned int flags =
		(data->reqinfo_base.compact ? ETHNL_BITSET_COMPACT : 0) |
		ETHNL_BITSET_LEGACY_NAMES;
	const u32 all_hashfn = RXFLOW_ALL_HASHFNS;
	int ret;

	ret = ethnl_put_bitset32(skb, ETHA_RXFLOW_HASH_FN,
				 ETH_RSS_HASH_FUNCS_COUNT, &data->hash_fn,
				 &all_hashfn, rss_hash_func_strings, flags);
	if (ret < 0)
		return ret;

	return 0;
}

static int fill_hashopts(struct sk_buff *skb, const u32 *fields)
{
	struct nlattr *attr_opts;
	struct nlattr *attr_opt;
	unsigned int i;
	int ret;

	attr_opts = ethnl_nest_start(skb, ETHA_RXFLOW_HASH_OPTS);
	if (!attr_opts)
		return -EMSGSIZE;

	for (i = 0; i < FLOW_TYPE_COUNT; i++) {
		if (!fields[i])
			continue;
		ret = -EMSGSIZE;
		attr_opt = ethnl_nest_start(skb, ETHA_RXHASHOPTS_OPT);
		if (!attr_opt)
			goto err;

		if (nla_put_u32(skb, ETHA_RXHASHOPT_FLOWTYPE, i))
		       goto err;
		if (fields[i] & RXH_DISCARD) {
			if (nla_put_flag(skb, ETHA_RXHASHOPT_DISCARD))
				goto err;
		} else {
			if (nla_put_bitfield32(skb, ETHA_RXHASHOPT_FIELDS,
					       fields[i], RXH_ALL))
				goto err;
		}

		nla_nest_end(skb, attr_opt);
	}

	nla_nest_end(skb, attr_opts);
	return 0;
err:
	nla_nest_cancel(skb, attr_opts);
	return ret;
}

static int fill_indir_tbl(struct sk_buff *skb, const struct rxflow_data *data)
{
	struct nlattr *tbl, *block, *attr;
	u16 block_attrtype;
	unsigned int i;
	int ret;

	if (nla_put_u32(skb, ETHA_RXFLOW_INDTBL_SIZE, data->indtbl_size))
		return -EMSGSIZE;
	tbl = ethnl_nest_start(skb, ETHA_RXFLOW_INDIR_TBL);
	if (!tbl)
		return -EMSGSIZE;

	switch(data->entry_size) {
	case 4:
		block_attrtype = ETHA_INDTBL_BLOCK32;
		break;
	case 2:
		block_attrtype = ETHA_INDTBL_BLOCK16;
		break;
	case 1:
		block_attrtype = ETHA_INDTBL_BLOCK8;
		break;
	default:
		WARN_ONCE(1, "invalid indir_tbl entry size %u\n",
			  data->entry_size);
		return -EFAULT;
	}
	ret = -EMSGSIZE;
	block = ethnl_nest_start(skb, block_attrtype);
	if (!block)
		goto err;

	if (nla_put_u32(skb, ETHA_ITBLK_START, 0) ||
	    nla_put_u32(skb, ETHA_ITBLK_LEN, data->indtbl_size))
		goto err;
	switch(data->entry_size) {
	case 4:
		if (nla_put(skb, ETHA_ITBLK_DATA,
			    data->indtbl_size * sizeof(u32),
			    data->indir_tbl))
			goto err;
		break;
	case 2:
		attr = nla_reserve(skb, ETHA_ITBLK_DATA,
				   data->indtbl_size * data->entry_size);
		if (!attr)
			goto err;
		for (i = 0; i < data->indtbl_size; i++)
			((u16 *)nla_data(attr))[i] = data->indir_tbl[i];
		break;
	case 1:
		attr = nla_reserve(skb, ETHA_ITBLK_DATA,
				   data->indtbl_size * data->entry_size);
		if (!attr)
			goto err;
		for (i = 0; i < data->indtbl_size; i++)
			((u8 *)nla_data(attr))[i] = data->indir_tbl[i];
		break;
	}

	nla_nest_end(skb, block);
	nla_nest_end(skb, tbl);
	return 0;

err:
	nla_nest_cancel(skb, tbl);
	return ret;
}

static int fill_rxflow(struct sk_buff *skb,
		       const struct common_req_info *req_info)
{
	const struct rxflow_data *data =
		container_of(req_info, struct rxflow_data, reqinfo_base);
	u32 info_mask = data->repdata_base.info_mask;
	int ret;

	if (data->req_context &&
	    nla_put_u32(skb, ETHA_RXFLOW_CONTEXT, data->req_context))
		return -EMSGSIZE;
	if ((info_mask & ETH_RXFLOW_IM_INFO) &&
	    nla_put_u32(skb, ETHA_RXFLOW_NRINGS, data->n_rings))
		return -EMSGSIZE;
	if (info_mask & ETH_RXFLOW_IM_HASHFN) {
		ret = fill_rxflow_hashfn(skb, data);
		if (ret < 0)
			return ret;
	}
	if (info_mask & ETH_RXFLOW_IM_HKEY) {
		if (nla_put(skb, ETHA_RXFLOW_HASH_KEY, data->hkey_size,
			    data->hkey))
			return -EMSGSIZE;
	}
	if (info_mask & ETH_RXFLOW_IM_HASHOPTS) {
		ret = fill_hashopts(skb, data->hash_fields);
		if (ret < 0)
			return ret;
	}
	if (info_mask & ETH_RXFLOW_IM_INDTBL) {
		ret = fill_indir_tbl(skb, data);
		if (ret < 0)
			return ret;
	}

	return 0;
}

void rxflow_cleanup(struct common_req_info *req_info)
{
	struct rxflow_data *data =
		container_of(req_info, struct rxflow_data, reqinfo_base);

	kfree(data->indir_tbl);
	kfree(data->hkey);
}

const struct get_request_ops rxflow_request_ops = {
	.request_cmd		= ETHNL_CMD_GET_RXFLOW,
	.reply_cmd		= ETHNL_CMD_SET_RXFLOW,
	.dev_attrtype		= ETHA_RXFLOW_DEV,
	.data_size		= sizeof(struct rxflow_data),
	.repdata_offset		= offsetof(struct rxflow_data, repdata_base),

	.parse_request		= parse_rxflow,
	.prepare_data		= prepare_rxflow,
	.reply_size		= rxflow_size,
	.fill_reply		= fill_rxflow,
	.cleanup		= rxflow_cleanup,
};
