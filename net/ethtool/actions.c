/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#include "netlink.h"
#include "common.h"

/* ACT_NWAY_RST */

static const struct nla_policy nwayrst_policy[ETHA_NWAYRST_MAX + 1] = {
	[ETHA_NWAYRST_UNSPEC]		= { .type = NLA_REJECT },
	[ETHA_NWAYRST_DEV]		= { .type = NLA_NESTED },
};

void ethnl_nwayrst_notify(struct net_device *dev,
                          struct netlink_ext_ack *extack, unsigned int cmd,
                          u32 req_mask, const void *data)
{
	struct sk_buff *skb;
	void *msg_payload;
	int msg_len;
	int ret;

	msg_len = dev_ident_size();
	skb = genlmsg_new(msg_len, GFP_KERNEL);
	if (!skb)
		return;
	msg_payload = genlmsg_put(skb, 0, ++ethnl_bcast_seq,
				  &ethtool_genl_family, 0,
				  ETHNL_CMD_ACT_NWAY_RST);
	if (!msg_payload)
		goto err_skb;

	ret = ethnl_fill_dev(skb, dev, ETHA_NWAYRST_DEV);
	if (ret < 0)
		goto err_skb;
	genlmsg_end(skb, msg_payload);
	genlmsg_multicast(&ethtool_genl_family, skb, 0, ETHNL_MCGRP_MONITOR,
			  GFP_KERNEL);
	return;

err_skb:
	nlmsg_free(skb);
}

int ethnl_act_nway_rst(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *tb[ETHA_NWAYRST_MAX + 1];
	struct net_device *dev;
	int ret;

	ret = ethnlmsg_parse(info->nlhdr, tb, ETHA_NWAYRST_MAX, nwayrst_policy,
			     info);
	if (ret < 0)
		return ret;
	dev = ethnl_dev_get(info, tb[ETHA_NWAYRST_DEV]);
	if (IS_ERR(dev))
		return PTR_ERR(dev);
	ret = -EOPNOTSUPP;
	if (!dev->ethtool_ops->nway_reset)
		goto out_dev;

	rtnl_lock();
	ret = ethnl_before_ops(dev);
	if (ret < 0)
		goto out_rtnl;
	ret = dev->ethtool_ops->nway_reset(dev);
	ethnl_after_ops(dev);
	if (ret == 0)
		ethtool_notify(dev, NULL, ETHNL_CMD_ACT_NWAY_RST, 0, NULL);

out_rtnl:
	rtnl_unlock();
out_dev:
	dev_put(dev);
	return ret;
}
