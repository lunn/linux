/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#include <linux/phy.h>
#include "netlink.h"
#include "common.h"
#include "bitset.h"

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

/* ACT_PHYS_ID */

static const struct nla_policy physid_policy[ETHA_PHYSID_MAX + 1] = {
	[ETHA_PHYSID_UNSPEC]		= { .type = NLA_REJECT },
	[ETHA_PHYSID_DEV]		= { .type = NLA_NESTED },
	[ETHA_PHYSID_LENGTH]		= { .type = NLA_U32 },
};

void ethnl_physid_notify(struct net_device *dev,
                          struct netlink_ext_ack *extack, unsigned int cmd,
                          u32 req_mask, const void *data)
{
	u32 timeout = *(const u32 *)data;
	struct sk_buff *skb;
	void *msg_payload;
	int msg_len;
	int ret;

	msg_len = dev_ident_size() + nla_total_size(sizeof(u32));
	skb = genlmsg_new(msg_len, GFP_KERNEL);
	if (!skb)
		return;
	msg_payload = genlmsg_put(skb, 0, ++ethnl_bcast_seq,
				  &ethtool_genl_family, 0,
				  ETHNL_CMD_ACT_PHYS_ID);
	if (!msg_payload)
		goto err_skb;

	ret = ethnl_fill_dev(skb, dev, ETHA_PHYSID_DEV);
	if (ret < 0)
		goto err_skb;
	if (nla_put_u32(skb, ETHA_PHYSID_LENGTH, timeout))
		goto err_skb;

	genlmsg_end(skb, msg_payload);
	genlmsg_multicast(&ethtool_genl_family, skb, 0, ETHNL_MCGRP_MONITOR,
			  GFP_KERNEL);
	return;

err_skb:
	nlmsg_free(skb);
}

int ethnl_act_phys_id(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *tb[ETHA_PHYSID_MAX + 1];
	unsigned long timeout = 0;
	struct net_device *dev;
	int ret;

	ret = ethnlmsg_parse(info->nlhdr, tb, ETHA_PHYSID_MAX, physid_policy,
			     info);
	if (ret < 0)
		return ret;
	dev = ethnl_dev_get(info, tb[ETHA_PHYSID_DEV]);
	if (IS_ERR(dev))
		return PTR_ERR(dev);
	if (tb[ETHA_PHYSID_LENGTH])
		timeout = nla_get_u32(tb[ETHA_PHYSID_LENGTH]);

	rtnl_lock();
	ret = ethnl_before_ops(dev);
	if (ret < 0)
		goto out;
	ret = __ethtool_phys_id(dev, timeout);
	if (ret == 0 && signal_pending(current))
		ret = -EINTR;
	ethnl_after_ops(dev);

out:
	rtnl_unlock();
	dev_put(dev);
	return ret;
}

/* ACT_RESET */

const char *const reset_flag_names[] = {
	"mgmt",
	"irq",
	"dma",
	"filter",
	"offload",
	"mac",
	"phy",
	"ram",
	"ap"
};

static const struct nla_policy reset_policy[ETHA_RESET_MAX + 1] = {
	[ETHA_RESET_UNSPEC]		= { .type = NLA_REJECT },
	[ETHA_RESET_DEV]		= { .type = NLA_NESTED },
	[ETHA_RESET_COMPACT]		= { .type = NLA_FLAG },
	[ETHA_RESET_ALL]		= { .type = NLA_FLAG },
	[ETHA_RESET_ALL_DEDICATED]	= { .type = NLA_FLAG },
	[ETHA_RESET_DEDICATED]		= { .type = NLA_NESTED },
	[ETHA_RESET_SHARED]		= { .type = NLA_NESTED },
};

static int reset_size(u32 flags, u32 orig_flags, bool compact)
{
	const unsigned int bitset_flags = compact ? ETHNL_BITSET_COMPACT : 0;
	int len = dev_ident_size();
	u32 bitmap, bitmask;
	int ret;

	BUILD_BUG_ON(ETH_RESET_NFLAGS > 16);

	if (flags == ETH_RESET_ALL)
		return len + nla_total_size(0);
	bitmap = flags >> 16;
	bitmask = orig_flags >> 16;
	ret = ethnl_bitset32_size(ETH_RESET_NFLAGS, &bitmap, &bitmask,
				  reset_flag_names, bitset_flags);
	if (ret < 0)
		return ret;
	len += ret;

	if ((flags & ETH_RESET_DEDICATED) == ETH_RESET_DEDICATED) {
		len += nla_total_size(0);
	} else {
		bitmap = flags & ETH_RESET_DEDICATED;
		bitmask = orig_flags & ETH_RESET_DEDICATED;
		ret = ethnl_bitset32_size(ETH_RESET_NFLAGS, &bitmap, &bitmask,
					  reset_flag_names, bitset_flags);
		if (ret < 0)
			return ret;
		len += ret;
	}

	return len;
}

static int fill_reset(struct sk_buff *skb, u32 flags, u32 orig_flags,
		      bool compact)
{
	const unsigned int bitset_flags = compact ? ETHNL_BITSET_COMPACT : 0;
	u32 bitmap, bitmask;
	int ret;

	if (flags == ETH_RESET_ALL)
		return nla_put_flag(skb, ETHA_RESET_ALL) ? -EMSGSIZE : 0;

	if ((flags & ETH_RESET_DEDICATED) == ETH_RESET_DEDICATED) {
		if (nla_put_flag(skb, ETHA_RESET_ALL_DEDICATED))
			return -EMSGSIZE;
	} else {
		bitmap = flags & ETH_RESET_DEDICATED;
		bitmask = orig_flags & ETH_RESET_DEDICATED;
		ret = ethnl_put_bitset32(skb, ETHA_RESET_DEDICATED,
					 ETH_RESET_NFLAGS, &bitmap, &bitmask,
					 reset_flag_names, bitset_flags);
		if (ret < 0)
			return ret;
	}

	bitmap = flags >> ETH_RESET_SHARED_SHIFT;
	bitmask = orig_flags >> ETH_RESET_SHARED_SHIFT;
	return ethnl_put_bitset32(skb, ETHA_RESET_SHARED, ETH_RESET_NFLAGS,
				  &bitmap, &bitmask, reset_flag_names,
				  bitset_flags);
}

void ethnl_reset_notify(struct net_device *dev,
                          struct netlink_ext_ack *extack, unsigned int cmd,
                          u32 req_mask, const void *data)
{
	u32 flags = *(const u32 *)data;
	struct sk_buff *skb;
	void *msg_payload;
	int msg_len;
	int ret;

	msg_len = reset_size(flags, flags, true);
	if (msg_len < 0)
		return;
	skb = genlmsg_new(msg_len, GFP_KERNEL);
	if (!skb)
		return;
	msg_payload = genlmsg_put(skb, 0, ++ethnl_bcast_seq,
				  &ethtool_genl_family, 0,
				  ETHNL_CMD_ACT_RESET);
	if (!msg_payload)
		goto err_skb;

	ret = ethnl_fill_dev(skb, dev, ETHA_RESET_DEV);
	if (ret < 0)
		goto err_skb;
	ret = fill_reset(skb, flags, flags, true);
	if (ret < 0)
		goto err_skb;

	genlmsg_end(skb, msg_payload);
	genlmsg_multicast(&ethtool_genl_family, skb, 0, ETHNL_MCGRP_MONITOR,
			  GFP_KERNEL);
	return;

err_skb:
	nlmsg_free(skb);
}

static int parse_reset(struct nlattr **tb, u32 *val, struct genl_info *info)
{
	u32 flags;
	int ret;

	if (tb[ETHA_RESET_ALL]) {
		*val = ETH_RESET_ALL;
		return 0;
	}

	*val = 0;
	flags = 0;
	ethnl_update_bitset32(&flags, NULL, ETH_RESET_NFLAGS,
			      tb[ETHA_RESET_SHARED], &ret, reset_flag_names,
			      false, info);
	if (ret < 0)
		return ret;
	*val |= (flags << ETH_RESET_SHARED_SHIFT);

	if (tb[ETHA_RESET_ALL_DEDICATED])
		*val |= ETH_RESET_DEDICATED;
	else {
		flags = 0;
		ethnl_update_bitset32(&flags, NULL, ETH_RESET_NFLAGS,
				      tb[ETHA_RESET_DEDICATED], &ret,
				      reset_flag_names, false, info);
		if (ret < 0)
			return ret;
		*val |= flags;
	}

	return 0;
}

int ethnl_act_reset(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *tb[ETHA_RESET_MAX + 1];
	struct sk_buff *rskb = NULL;
	struct net_device *dev;
	u32 orig_flags, flags;
	void *reply_payload;
	int reply_ret = 0;
	int reply_len;
	bool compact;
	int ret;

	ret = ethnlmsg_parse(info->nlhdr, tb, ETHA_RESET_MAX, reset_policy,
			     info);
	if (ret < 0)
		return ret;
	dev = ethnl_dev_get(info, tb[ETHA_RESET_DEV]);
	if (IS_ERR(dev))
		return PTR_ERR(dev);
	if (!dev->ethtool_ops->reset)
		return -EOPNOTSUPP;
	compact = (tb[ETHA_RESET_COMPACT] != NULL);
	ret = parse_reset(tb, &flags, info);
	if (ret < 0)
		goto out_dev;

	orig_flags = flags;
	rtnl_lock();
	ret = ethnl_before_ops(dev);
	if (ret < 0)
		goto out_rtnl;
	ret = dev->ethtool_ops->reset(dev, &flags);
	ethnl_after_ops(dev);
	if (ret < 0)
		goto out_rtnl;

	flags = orig_flags & ~flags;
	if (flags)
		ethnl_reset_notify(dev, NULL, ETHNL_CMD_ACT_RESET, 0, &flags);

	/* compose reply message */
	reply_len = reset_size(flags, orig_flags, compact);
	reply_ret = -EFAULT;
	if (reply_len < 0)
		goto out;
	reply_ret = -ENOMEM;
	rskb = ethnl_reply_init(reply_len, dev, ETHNL_CMD_ACT_RESET,
				ETHA_RESET_DEV, info, &reply_payload);
	if (!rskb)
		goto err_rskb;
	reply_ret = fill_reset(rskb, flags, orig_flags, compact);
	if (reply_ret < 0)
		goto err_rskb;
	rtnl_unlock();
	dev_put(dev);
	genlmsg_end(rskb, reply_payload);
	reply_ret = genlmsg_reply(rskb, info);
	goto out;

err_rskb:
	WARN_ONCE(ret == -EMSGSIZE,
		  "calculated message payload length (%d) not sufficient\n",
		  reply_len);
	if (rskb)
		nlmsg_free(rskb);
out_rtnl:
	rtnl_unlock();
out_dev:
	dev_put(dev);
out:
	if (reply_ret < 0)
		ETHNL_SET_ERRMSG(info, "failed to send reply message");
	return ret;
}

/* ACT_CABLE_TEST */

static const struct nla_policy cable_test_policy[ETHA_CABLE_TEST_MAX + 1] = {
	[ETHA_CABLE_TEST_UNSPEC]	= { .type = NLA_REJECT },
	[ETHA_CABLE_TEST_DEV]		= { .type = NLA_NESTED },
};

void ethnl_cable_test_notify(struct net_device *dev,
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
				  ETHNL_CMD_ACT_CABLE_TEST);
	if (!msg_payload)
		goto err_skb;

	ret = ethnl_fill_dev(skb, dev, ETHA_CABLE_TEST_DEV);
	if (ret < 0)
		goto err_skb;
	genlmsg_end(skb, msg_payload);
	genlmsg_multicast(&ethtool_genl_family, skb, 0, ETHNL_MCGRP_MONITOR,
			  GFP_KERNEL);
	return;

err_skb:
	nlmsg_free(skb);
}

int ethnl_act_cable_test(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *tb[ETHA_CABLE_TEST_MAX + 1];
	struct net_device *dev;
	int ret;

	ret = ethnlmsg_parse(info->nlhdr, tb, ETHA_CABLE_TEST_MAX,
			     cable_test_policy,
			     info);
	if (ret < 0)
		return ret;
	dev = ethnl_dev_get(info, tb[ETHA_CABLE_TEST_DEV]);
	if (IS_ERR(dev))
		return PTR_ERR(dev);
	ret = -EOPNOTSUPP;
	if (!dev->phydev)
		goto out_dev;

	rtnl_lock();
	ret = ethnl_before_ops(dev);
	if (ret < 0)
		goto out_rtnl;
	ret = phy_start_cable_test(dev->phydev);
	ethnl_after_ops(dev);
	if (ret == 0)
		ethtool_notify(dev, NULL, ETHNL_CMD_ACT_CABLE_TEST, 0, NULL);

out_rtnl:
	rtnl_unlock();
out_dev:
	dev_put(dev);
	return ret;
}
