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

void ethnl_rxflow_notify(struct net_device *dev,
			 struct netlink_ext_ack *extack, unsigned int cmd,
			 u32 req_mask, const void *_data)
{
	const struct ethtool_rxflow_notification_info *ninfo = _data;
	struct common_req_info *req_info;
	struct rxflow_data data = {};
	struct sk_buff *skb;
	void *msg_payload;
	int msg_len;
	int ret;

	req_info = &data.reqinfo_base;
	req_info->reply_data = &data.repdata_base;
	req_info->dev = dev;
	req_info->req_mask = req_mask;
	req_info->compact = true;
	data.repdata_base.dev = dev;
	if (ninfo) {
		data.req_context = ninfo->context;
		data.req_flow_type = ninfo->flow_type;
	}

	ret = prepare_rxflow(req_info, NULL);
	if (ret < 0)
		goto err_data;
	msg_len = rxflow_size(req_info);
	if (msg_len < 0)
		goto err_data;
	skb = genlmsg_new(msg_len, GFP_KERNEL);
	if (!skb)
		goto err_data;
	msg_payload = genlmsg_put(skb, 0, ++ethnl_bcast_seq,
				  &ethtool_genl_family, 0,
				  ETHNL_CMD_SET_RXFLOW);
	if (!msg_payload)
		goto err_skb;

	ret = ethnl_fill_dev(skb, dev, ETHA_RXFLOW_DEV);
	if (ret < 0)
		goto err_skb;
	if (ninfo) {
		ret = -EMSGSIZE;
		if (ninfo->ctx_op &&
		    nla_put_u32(skb, ETHA_RXFLOW_CTXOP, ninfo->ctx_op))
			goto err_skb;
	}

	if (ninfo && ninfo->ctx_op == ETH_RXFLOW_CTXOP_DEL) {
		ret = -EMSGSIZE;
		if (nla_put_u32(skb, ETHA_RXFLOW_CONTEXT, ninfo->context))
			goto err_skb;
	} else {
		ret = fill_rxflow(skb, req_info);
		if (ret < 0)
			goto err_skb;
	}
	rxflow_cleanup(req_info);
	genlmsg_end(skb, msg_payload);

	genlmsg_multicast(&ethtool_genl_family, skb, 0, ETHNL_MCGRP_MONITOR,
			  GFP_KERNEL);
	return;

err_skb:
	nlmsg_free(skb);
err_data:
	rxflow_cleanup(req_info);
}

/* SET_RXFLOW */

static const struct nla_policy set_rxflow_policy[ETHA_RXFLOW_MAX + 1] = {
	[ETHA_RXFLOW_UNSPEC]		= { .type = NLA_REJECT },
	[ETHA_RXFLOW_DEV]		= { .type = NLA_NESTED },
	[ETHA_RXFLOW_INFOMASK]		= { .type = NLA_REJECT },
	[ETHA_RXFLOW_COMPACT]		= { .type = NLA_FLAG },
	[ETHA_RXFLOW_CTXOP]		= { .type = NLA_U32 },
	[ETHA_RXFLOW_CONTEXT]		= { .type = NLA_U32 },
	[ETHA_RXFLOW_NRINGS]		= { .type = NLA_REJECT },
	[ETHA_RXFLOW_HASH_FN]		= { .type = NLA_NESTED },
	[ETHA_RXFLOW_HASH_KEY]		= { .type = NLA_BINARY },
	[ETHA_RXFLOW_HASH_OPTS]		= { .type = NLA_NESTED },
	[ETHA_RXFLOW_INDTBL_SIZE]	= { .type = NLA_REJECT },
	[ETHA_RXFLOW_INDIR_TBL]		= { .type = NLA_NESTED },
};

static int set_rxflow_sanity_checks(struct nlattr *tb[], struct genl_info *info,
				    u32 ctxop, u32 context)
{
	switch(ctxop) {
	case ETH_RXFLOW_CTXOP_SET:
		break;
	case ETH_RXFLOW_CTXOP_NEW:
		if (context) {
			ETHNL_SET_ERRMSG(info,
					 "cannot set context id for new context");
			return -EINVAL;
		}
		if (tb[ETHA_RXFLOW_HASH_OPTS]) {
			ETHNL_SET_ERRMSG(info,
					 "hash options not allowed with new context");
			return -EINVAL;
		}
		break;
	case ETH_RXFLOW_CTXOP_DEL:
		if (!context) {
			ETHNL_SET_ERRMSG(info,
					 "cannot delete main context");
			return -EINVAL;
		}
		if (tb[ETHA_RXFLOW_HASH_FN] || tb[ETHA_RXFLOW_HASH_KEY] ||
		    tb[ETHA_RXFLOW_HASH_OPTS] || tb[ETHA_RXFLOW_INDIR_TBL]) {
			ETHNL_SET_ERRMSG(info,
					 "data passed when deleting context");
			return -EINVAL;
		}
		break;
	default:
		ETHNL_SET_ERRMSG(info, "unknown context operation");
		return -EOPNOTSUPP;
	}

	return 0;
}

static const u32 all_bits = ~(u32)0;

const struct nla_policy rxhashopt_policy[] = {
	[ETHA_RXHASHOPT_UNSPEC]		= { .type = NLA_REJECT },
	[ETHA_RXHASHOPT_FLOWTYPE]	= { .type = NLA_U32 },
	[ETHA_RXHASHOPT_FIELDS]		= { .type = NLA_BITFIELD32,
					    .validation_data = &all_bits },
	[ETHA_RXHASHOPT_DISCARD]	= { .type = NLA_FLAG },
};

static int set_rxflow_hash_opts(struct net_device *dev, unsigned int context,
				const struct nlattr *opts_attr,
				struct genl_info *info)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	struct nlattr *tb[ETHA_RXHASHOPT_MAX + 1];
	const struct nlattr *opt_attr;
	int ret, rem;

	if (!ops->get_rxnfc || !ops->set_rxnfc)
		return -EOPNOTSUPP;

	nla_for_each_nested(opt_attr, opts_attr, rem) {
		struct ethtool_rxnfc rxnfc = {
			.cmd		= ETHTOOL_SRXFH,
			.rss_context	= context,
		};
		struct ethtool_rxflow_notification_info ninfo = {
			.context	= context,
		};

		if (nla_type(opt_attr) != ETHA_RXHASHOPTS_OPT) {
			ETHNL_SET_ERRMSG(info,
					 "unexpected attribute in ETHA_RXFLOW_HASH_OPTS");
			return genl_err_attr(info, -EINVAL, opt_attr);
		}
		ret = nla_parse_nested_strict(tb, ETHA_RXHASHOPT_MAX, opt_attr,
					      rxhashopt_policy, info->extack);
		if (ret < 0)
			return ret;
		if (tb[ETHA_RXHASHOPT_DISCARD] && tb[ETHA_RXHASHOPT_FIELDS])
			return -EINVAL;
		if (!tb[ETHA_RXHASHOPT_FLOWTYPE] ||
		    (!tb[ETHA_RXHASHOPT_DISCARD] && !tb[ETHA_RXHASHOPT_FIELDS]))
			return -EINVAL;

		ninfo.flow_type = nla_get_u32(tb[ETHA_RXHASHOPT_FLOWTYPE]);
		rxnfc.flow_type = ninfo.flow_type | (context ? FLOW_RSS : 0);
		if (tb[ETHA_RXHASHOPT_DISCARD]) {
			rxnfc.data = RXH_DISCARD;
		} else {
			struct ethtool_rxnfc grxnfc = rxnfc;
			struct nla_bitdield32;
			u32 fields;

			grxnfc.cmd = ETHTOOL_GRXFH;
			ret = ops->get_rxnfc(dev, &grxnfc, NULL);
			if (ret < 0)
				return ret;
			fields = (grxnfc.data & RXH_DISCARD) ? 0 : grxnfc.data;
			if (!ethnl_update_bitfield32(&fields,
						     tb[ETHA_RXHASHOPT_FIELDS]))
				continue;
			rxnfc.data = fields;
		}

		ret = ops->set_rxnfc(dev, &rxnfc);
		if (ret < 0)
			return ret;
		ethnl_rxflow_notify(dev, info->extack, ETHNL_CMD_SET_RXFLOW,
				    ETH_RXFLOW_IM_HASHOPTS, &ninfo);
	}

	return 0;
}

static int set_rxflow_prep_hashfn(struct net_device *dev,
				  struct genl_info *info,
				  const struct nlattr *attr, u8 *phashfn,
				  u32 *info_mask)
{
	u32 hash_fn = 0;
	bool mod;
	int ret;

	if (!attr) {
		*phashfn = ETH_RSS_HASH_NO_CHANGE;
		return 0;
	}
	mod = ethnl_update_bitset32(&hash_fn, NULL, ETH_RSS_HASH_FUNCS_COUNT,
				    attr, &ret, rss_hash_func_strings, true,
				    info);
	if (ret < 0)
		return ret;
	if (hash_fn > U8_MAX) {
		ETHNL_SET_ERRMSG(info,
				 "only first 8 hash functions supported");
		ret = -EINVAL;
	} else {
		*phashfn = mod ? (u8)hash_fn : ETH_RSS_HASH_NO_CHANGE;
		*info_mask |= (mod ? ETH_RXFLOW_IM_HASHFN : 0);
	}
	return ret;
}

static int set_rxflow_prep_hkey(struct net_device *dev, struct genl_info *info,
				const struct nlattr *attr, u8 **phkey,
				u32 *info_mask)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	u32 hkey_size = 0;

	*phkey = NULL;
	if (!attr)
		return 0;
	if (ops->get_rxfh_key_size)
		hkey_size = ops->get_rxfh_key_size(dev);
	if (!hkey_size)
		return -EOPNOTSUPP;
	if (nla_len(attr) != hkey_size) {
		ETHNL_SET_ERRMSG(info, "hash key size does not match");
		return -EINVAL;
	}

	*phkey = nla_data(attr);
	*info_mask |= ETH_RXFLOW_IM_HKEY;
	return 0;
}

static const struct nla_policy indtbl_block_policy[ETHA_ITBLK_MAX + 1] = {
	[ETHA_ITBLK_UNSPEC]	= { .type = NLA_REJECT },
	[ETHA_ITBLK_START]	= { .type = NLA_U32 },
	[ETHA_ITBLK_LEN]	= { .type = NLA_U32 },
	[ETHA_ITBLK_DATA]	= { .type = NLA_BINARY },
};

static int apply_block(u32 *table, unsigned int size,
		       const struct nlattr *block, unsigned int nrings,
		       unsigned int entry_size, struct genl_info *info)
{
	struct nlattr *tb[ETHA_ITBLK_MAX + 1];
	const u32 *src32;
	const u16 *src16;
	const u8 *src8;
	unsigned int start, blen, i;
	int ret;

	ret = nla_parse_nested_strict(tb, ETHA_ITBLK_MAX, block,
				      indtbl_block_policy, info->extack);
	if (ret < 0)
		return ret;
	if (tb[ETHA_ITBLK_DATA])
		return -EINVAL;

	start = tb[ETHA_ITBLK_START] ? nla_get_u32(tb[ETHA_ITBLK_START]) : 0;
	if (start >= size)
		return -EINVAL;
	if (tb[ETHA_ITBLK_LEN]) {
		blen = nla_get_u32(tb[ETHA_ITBLK_LEN]);
		if (start + blen > size)
			return -EINVAL;
	} else {
		blen = size - start;
	}
	if (nla_len(tb[ETHA_ITBLK_DATA]) < blen * entry_size)
		return -EINVAL;

	switch(entry_size) {
	case 4:
		src32 = nla_data(tb[ETHA_ITBLK_DATA]);
		for (i = 0; i < blen; i++)
			if (src32[i] >= nrings)
				goto data_err;
		memcpy(table + start, src32, blen * entry_size);
		break;
	case 2:
		src16 = nla_data(tb[ETHA_ITBLK_DATA]);
		for (i = 0; i < blen; i++) {
			if (src16[i] >= nrings)
				goto data_err;
			table[start + i] = src16[i];
		}
		break;
	case 1:
		src8 = nla_data(tb[ETHA_ITBLK_DATA]);
		for (i = 0; i < blen; i++) {
			if (src8[i] >= nrings)
				goto data_err;
			table[start + i] = src8[i];
		}
		break;
	}

	return 0;
data_err:
	ETHNL_SET_ERRMSG(info, "indtbl entry exceeds max ring number");
	return -EINVAL;
}

static const struct nla_policy indtbl_pattern_policy[ETHA_ITPAT_MAX + 1] = {
	[ETHA_ITPAT_UNSPEC]	= { .type = NLA_REJECT },
	[ETHA_ITPAT_START]	= { .type = NLA_U32 },
	[ETHA_ITPAT_LEN]	= { .type = NLA_U32 },
	[ETHA_ITPAT_MIN_RING]	= { .type = NLA_U32 },
	[ETHA_ITPAT_MAX_RING]	= { .type = NLA_U32 },
	[ETHA_ITPAT_OFFSET]	= { .type = NLA_U32 },
};

static int apply_pattern(u32 *table, unsigned int size,
			 const struct nlattr *pattern, unsigned int n_rings,
			 struct genl_info *info)
{
	struct nlattr *tb[ETHA_ITPAT_MAX + 1];
	unsigned int max_ring = n_rings - 1;
	unsigned int blen, mod, n, i;
	unsigned int min_ring = 0;
	unsigned int offset = 0;
	unsigned int start = 0;
	int ret;

	ret = nla_parse_nested_strict(tb, ETHA_ITPAT_MAX, pattern,
				      indtbl_pattern_policy, info->extack);
	if (ret < 0)
		return ret;

	if (tb[ETHA_ITPAT_START])
		start = nla_get_u32(tb[ETHA_ITPAT_START]);
	if (start >= size)
		return -EINVAL;
	if (tb[ETHA_ITPAT_LEN]) {
		blen = nla_get_u32(tb[ETHA_ITPAT_LEN]);
		if (start + blen > size)
			return -EINVAL;
	} else {
		blen = size - start;
	}
	if (tb[ETHA_ITPAT_MIN_RING])
		min_ring = nla_get_u32(tb[ETHA_ITPAT_MIN_RING]);
	if (tb[ETHA_ITPAT_MAX_RING])
		max_ring = nla_get_u32(tb[ETHA_ITPAT_MAX_RING]);
	if (tb[ETHA_ITPAT_OFFSET])
		offset = nla_get_u32(tb[ETHA_ITPAT_OFFSET]);
	if (min_ring >= n_rings || max_ring < min_ring || max_ring >= n_rings)
		return -EINVAL;
	mod = max_ring - min_ring + 1;

	for (i = 0; i < blen && i < mod; i++)
		table[start + i] = min_ring + (start + i + offset) % mod;
	n = blen / mod;
	for (i = 0; i < n - 1; i++)
		memcpy(table + start + i * mod, table + start,
		       mod * sizeof(table[0]));
	if (blen % mod)
		memcpy(table + start + n * mod, table + start, blen % mod);

	return 0;
}

static const struct nla_policy indtbl_weights_policy[ETHA_ITWGHT_MAX + 1] = {
	[ETHA_ITWGHT_UNSPEC]	= { .type = NLA_REJECT },
	[ETHA_ITWGHT_VALUES]	= { .type = NLA_BINARY },
	[ETHA_ITWGHT_WEIGHTS]	= { .type = NLA_BINARY },
};

static int apply_weights(u32 *table, unsigned int size,
			 const struct nlattr *attr, unsigned int n_rings,
			 struct genl_info *info)
{
	struct nlattr *tb[ETHA_ITWGHT_MAX + 1];
	const u32 *weights = NULL;
	const u32 *values = NULL;
	unsigned int sum = 0;
	unsigned int count;
	int ring, ret, i;
	s64 balance;

	ret = nla_parse_nested_strict(tb, ETHA_ITWGHT_MAX, attr,
				      indtbl_weights_policy, info->extack);
	if (ret < 0)
		return ret;
	if (!tb[ETHA_ITWGHT_WEIGHTS] ||
	    (nla_len(tb[ETHA_ITWGHT_WEIGHTS]) % sizeof(weights[0])))
		return -EINVAL;
	weights = nla_data(tb[ETHA_ITWGHT_WEIGHTS]);
	count = nla_len(tb[ETHA_ITWGHT_WEIGHTS]) / sizeof(weights[0]);
	if (!count)
		return -EINVAL;
	if (tb[ETHA_ITWGHT_VALUES]) {
		values = nla_data(tb[ETHA_ITWGHT_VALUES]);
		if (nla_len(tb[ETHA_ITWGHT_VALUES]) !=
		    nla_len(tb[ETHA_ITWGHT_WEIGHTS]))
			return -EINVAL;
	}

	sum = 0;
	for (i = 0; i < count; i++) {
		if (weights[i] > size - sum)
			return -EINVAL;
		sum += weights[i];
	}
	if (!sum)
		return -EINVAL;

	/* This is the same algorithm as in fill_indir_table() in ethtool.
	 * Our balance is  i * sum - (*indir_size) * partial + sum - 1
	 * there. Adding sum -1 compensates for absence of the rounding error
	 * in ethtool code.
	 */
	balance = sum - 1;
	ring = -1;
	for (i = 0; i < size; i++) {
		while (balance >= 0)
			balance -= size * weights[++ring];
		table[i] = values ? values[ring] : ring;
		balance += sum;
	}

	return 0;
}

static int set_rxflow_prep_indtbl(struct net_device *dev,
				  struct genl_info *info,
				  const struct nlattr *attr, u32 **pindtbl,
				  u32 *info_mask, bool *reset)
{
	struct ethtool_rxnfc rx_rings = { .cmd = ETHTOOL_GRXRINGS };
	const struct ethtool_ops *ops = dev->ethtool_ops;
	const struct nlattr *patch;
	bool mod = false;
	int ret, rem;
	u32 size = 0;
	u32 *table;
	u32 nrings;

	*reset = false;
	*pindtbl = NULL;
	if (!attr)
		return 0;
	if (!ops->get_rxnfc)
		return -EOPNOTSUPP;
	ret = ops->get_rxnfc(dev, &rx_rings, NULL);
	if (ret < 0)
		return ret;
	nrings = rx_rings.data;
	if (ops->get_rxfh_indir_size)
		size = ops->get_rxfh_indir_size(dev);
	if (!size)
		return -EOPNOTSUPP;
	table = kcalloc(size, sizeof(table[0]), GFP_KERNEL);
	if (!table)
		return -ENOMEM;
	*pindtbl = 0;

	nla_for_each_nested(patch, attr, rem) {
		int ptype = nla_type(patch);

		switch(ptype) {
		case ETHA_INDTBL_BLOCK32:
			ret = apply_block(table, size, patch, nrings, 4, info);
			break;
		case ETHA_INDTBL_BLOCK16:
			ret = apply_block(table, size, patch, nrings, 2, info);
			break;
		case ETHA_INDTBL_BLOCK8:
			ret = apply_block(table, size, patch, nrings, 1, info);
			break;
		case ETHA_INDTBL_PATTERN:
			ret = apply_pattern(table, size, patch, nrings, info);
			break;
		case ETHA_INDTBL_WEIGHTS:
			ret = apply_weights(table, size, patch, nrings, info);
			break;
		default:
			ETHNL_SET_ERRMSG(info, "unknown indir table patch type");
			ret = genl_err_attr(info, -EINVAL, patch);
		}
		if (ret < 0) {
			kfree(table);
			return ret;
		}
		mod = true;
	}
#if 0
	if (!mod) {
		unsigned int i;

		for (i = 0; i < size; i++)
			table[i] = i % rx_rings.data;
		*reset = true;
	}
#endif
	*pindtbl = table;
	*info_mask |= ETH_RXFLOW_IM_INDTBL;

	return 0;
}

static int set_rxflow_del_context(struct net_device *dev, u32 context,
				  struct genl_info *info)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	int ret;

	ret = ops->set_rxfh_context(dev, NULL, NULL, ETH_RSS_HASH_NO_CHANGE,
				    &context, true);
	if (ret == 0) {
		struct ethtool_rxflow_notification_info ninfo = {
			.ctx_op		= ETH_RXFLOW_CTXOP_DEL,
			.context	= context,
		};

		ethnl_rxflow_notify(dev, info->extack, ETHNL_CMD_SET_RXFLOW, 0,
				    &ninfo);
	}
	return ret;
}

static int send_set_rxflow_reply(struct net_device *dev,
				 struct genl_info *info, u32 context)
{
	struct sk_buff *skb;
	void *reply_payload;
	int reply_len;
	int ret;

	reply_len = dev_ident_size();
	reply_len += 2 * nla_total_size(sizeof(u32));
	skb = ethnl_reply_init(reply_len, dev, ETHNL_CMD_SET_RXFLOW,
			       ETHA_RXFLOW_DEV, info, &reply_payload);
	if (!skb)
		return -ENOMEM;
	ret = ethnl_fill_dev(skb, dev, ETHA_RXFLOW_DEV);
	if (ret < 0)
		goto err_skb;
	ret = -EMSGSIZE;
	if (nla_put_u32(skb, ETHA_RXFLOW_CTXOP, ETH_RXFLOW_CTXOP_NEW) ||
	    nla_put_u32(skb, ETHA_RXFLOW_CONTEXT, context))
		goto err_skb;

	genlmsg_end(skb, reply_payload);
	return genlmsg_reply(skb, info);

err_skb:
	WARN_ONCE(ret == -EMSGSIZE,
		  "calculated message payload length (%d) not sufficient\n",
		  reply_len);
	if (skb)
		nlmsg_free(skb);
	return ret;
}

int ethnl_set_rxflow(struct sk_buff *skb, struct genl_info *info)
{
	unsigned int ctxop = ETH_RXFLOW_CTXOP_SET;
	struct nlattr *tb[ETHA_RXFLOW_MAX + 1];
	const struct ethtool_ops *ops;
	struct net_device *dev;
	u32 *indtbl = NULL;
	bool reset_indtbl;
	u32 info_mask = 0;
	u32 context = 0;
	bool do_rxfh;
	u8 hash_fn;
	u8 *hkey;
	int ret;

	ret = ethnlmsg_parse(info->nlhdr, tb, ETHA_RXFLOW_MAX,
			     set_rxflow_policy, info);
	if (ret < 0)
		return ret;
	if (tb[ETHA_RXFLOW_CONTEXT])
		context = nla_get_u32(tb[ETHA_RXFLOW_CONTEXT]);
	if (tb[ETHA_RXFLOW_CTXOP])
		ctxop = nla_get_u32(tb[ETHA_RXFLOW_CTXOP]);
	ret = set_rxflow_sanity_checks(tb, info, ctxop, context);
	if (ret < 0)
		return ret;
	do_rxfh = tb[ETHA_RXFLOW_HASH_FN] || tb[ETHA_RXFLOW_HASH_KEY] ||
		  tb[ETHA_RXFLOW_INDIR_TBL];

	dev = ethnl_dev_get(info, tb[ETHA_RXFLOW_DEV]);
	if (IS_ERR(dev))
		return PTR_ERR(dev);
	ops = dev->ethtool_ops;

	rtnl_lock();
	ret = ethnl_before_ops(dev);
	if (ret < 0)
		goto out_rtnl;

	if (tb[ETHA_RXFLOW_HASH_OPTS]) {
		ret = set_rxflow_hash_opts(dev, context,
					   tb[ETHA_RXFLOW_HASH_OPTS], info);
		if (ret < 0)
			goto out_ops;
	}
	if (!do_rxfh)
		goto out_ops;

	ret = -EOPNOTSUPP;
	if (context && (!ops->get_rxfh_context || !ops->set_rxfh_context))
		goto out_ops;
	if (!context && (!ops->get_rxfh || !ops->set_rxfh))
		goto out_ops;

	if (ctxop == ETH_RXFLOW_CTXOP_DEL) {
		ret = set_rxflow_del_context(dev, context, info);
		goto out_ops;
	}
	if (ctxop == ETH_RXFLOW_CTXOP_NEW)
		context = ETH_RXFH_CONTEXT_ALLOC;
	ret = set_rxflow_prep_hashfn(dev, info, tb[ETHA_RXFLOW_HASH_FN],
				     &hash_fn, &info_mask);
	if (ret < 0)
		goto out_ops;
	ret = set_rxflow_prep_hkey(dev, info, tb[ETHA_RXFLOW_HASH_KEY], &hkey,
				   &info_mask);
	if (ret < 0)
		goto out_free;
	ret = set_rxflow_prep_indtbl(dev, info, tb[ETHA_RXFLOW_INDIR_TBL],
				     &indtbl, &info_mask, &reset_indtbl);
	if (ret < 0)
		goto out_free;
	if (context)
		ret = ops->set_rxfh_context(dev, indtbl, hkey, hash_fn,
					    &context, false);
	else
		ret = ops->set_rxfh(dev, indtbl, hkey, hash_fn);
	if (ret == 0 && !context && tb[ETHA_RXFLOW_INDIR_TBL]) {
		/* indicate whether rxfh was set to default */
		if (reset_indtbl)
			dev->priv_flags |= IFF_RXFH_CONFIGURED;
		else
			dev->priv_flags &= ~IFF_RXFH_CONFIGURED;
	}
	if (ctxop == ETH_RXFLOW_CTXOP_NEW && ret == 0) {
		ret = send_set_rxflow_reply(dev, info, context);
		if (ret < 0) {
			ETHNL_SET_ERRMSG(info, "failed to send reply message");
			ret = 0;
		}
	}

out_free:
	kfree(indtbl);
out_ops:
	if (ret == 0 && (info_mask || ctxop != ETH_RXFLOW_CTXOP_SET)) {
		const struct ethtool_rxflow_notification_info ninfo = {
			.ctx_op		= ctxop,
			.context	= context,
		};

		ethnl_rxflow_notify(dev, info->extack, ETHNL_CMD_SET_RXFLOW,
				    info_mask, &ninfo);
	}
	ethnl_after_ops(dev);
out_rtnl:
	rtnl_unlock();
	dev_put(dev);
	return ret;
}
