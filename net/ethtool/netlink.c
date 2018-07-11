// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note

#include <net/sock.h>
#include <linux/ethtool_netlink.h>
#include "netlink.h"

u32 ethnl_bcast_seq;

static bool ethnl_ok __read_mostly;

static const struct nla_policy dev_policy[ETHA_DEV_MAX + 1] = {
	[ETHA_DEV_UNSPEC]	= { .type = NLA_REJECT },
	[ETHA_DEV_INDEX]	= { .type = NLA_U32 },
	[ETHA_DEV_NAME]		= { .type = NLA_NUL_STRING,
				    .len = IFNAMSIZ - 1 },
};

/**
 * ethnl_dev_get() - get device identified by nested attribute
 * @info: genetlink info (also used for extack error reporting)
 * @nest: nest attribute with device identification
 *
 * Finds the network device identified by ETHA_DEV_INDEX (ifindex) or
 * ETHA_DEV_NAME (name) attributes in a nested attribute @nest. If both
 * are supplied, they must identify the same device. If successful, takes
 * a reference to the device which is to be released by caller.
 *
 * Return: pointer to the device if successful, ERR_PTR(err) on error
 */
struct net_device *ethnl_dev_get(struct genl_info *info, struct nlattr *nest)
{
	struct net *net = genl_info_net(info);
	struct nlattr *tb[ETHA_DEV_MAX + 1];
	struct net_device *dev;
	int ret;

	if (!nest) {
		ETHNL_SET_ERRMSG(info,
				 "mandatory device identification missing");
		return ERR_PTR(-EINVAL);
	}
	ret = nla_parse_nested_strict(tb, ETHA_DEV_MAX, nest, dev_policy,
				      info->extack);
	if (ret < 0)
		return ERR_PTR(ret);

	if (tb[ETHA_DEV_INDEX]) {
		dev = dev_get_by_index(net, nla_get_u32(tb[ETHA_DEV_INDEX]));
		if (!dev)
			return ERR_PTR(-ENODEV);
		/* if both ifindex and ifname are passed, they must match */
		if (tb[ETHA_DEV_NAME]) {
			const char *nl_name = nla_data(tb[ETHA_DEV_NAME]);

			if (strncmp(dev->name, nl_name, IFNAMSIZ)) {
				dev_put(dev);
				ETHNL_SET_ERRMSG(info,
						 "ifindex and ifname do not match");
				return ERR_PTR(-ENODEV);
			}
		}
		return dev;
	} else if (tb[ETHA_DEV_NAME]) {
		dev = dev_get_by_name(net, nla_data(tb[ETHA_DEV_NAME]));
		if (!dev)
			return ERR_PTR(-ENODEV);
	} else {
		ETHNL_SET_ERRMSG(info, "either ifindex or ifname required");
		return ERR_PTR(-EINVAL);
	}

	if (!netif_device_present(dev)) {
		dev_put(dev);
		ETHNL_SET_ERRMSG(info, "device not present");
		return ERR_PTR(-ENODEV);
	}
	return dev;
}

/**
 * ethnl_fill_dev() - Put device identification nest into a message
 * @msg:      skb with the message
 * @dev:      network device to describe
 * @attrtype: attribute type to use for the nest
 *
 * Create a nested attribute with attributes describing given network device.
 * Clean up on error.
 *
 * Return: 0 on success, error value (-EMSGSIZE only) on error
 */
int ethnl_fill_dev(struct sk_buff *msg, struct net_device *dev, u16 attrtype)
{
	struct nlattr *nest;
	int ret = -EMSGSIZE;

	nest = ethnl_nest_start(msg, attrtype);
	if (!nest)
		return -EMSGSIZE;

	if (nla_put_u32(msg, ETHA_DEV_INDEX, (u32)dev->ifindex))
		goto err;
	if (nla_put_string(msg, ETHA_DEV_NAME, dev->name))
		goto err;

	nla_nest_end(msg, nest);
	return 0;
err:
	nla_nest_cancel(msg, nest);
	return ret;
}

/**
 * ethnl_reply_init() - Create skb for a reply and fill device identification
 * @payload: payload length (without netlink and genetlink header)
 * @dev:     device the reply is about (may be null)
 * @cmd:     ETHNL_CMD_* command for reply
 * @info:    genetlink info of the received packet we respond to
 * @ehdrp:   place to store payload pointer returned by genlmsg_new()
 *
 * Return: pointer to allocated skb on success, NULL on error
 */
struct sk_buff *ethnl_reply_init(size_t payload, struct net_device *dev, u8 cmd,
				 u16 dev_attrtype, struct genl_info *info,
				 void **ehdrp)
{
	struct sk_buff *rskb;
	void *ehdr;

	rskb = genlmsg_new(payload, GFP_KERNEL);
	if (!rskb) {
		ETHNL_SET_ERRMSG(info,
				 "failed to allocate reply message");
		return NULL;
	}

	ehdr = genlmsg_put_reply(rskb, info, &ethtool_genl_family, 0, cmd);
	if (!ehdr)
		goto err;
	if (ehdrp)
		*ehdrp = ehdr;
	if (dev) {
		int ret = ethnl_fill_dev(rskb, dev, dev_attrtype);

		if (ret < 0)
			goto err;
	}

	return rskb;
err:
	nlmsg_free(rskb);
	return NULL;
}

/* notifications */

typedef void (*ethnl_notify_handler_t)(struct net_device *dev,
				       struct netlink_ext_ack *extack,
				       unsigned int cmd, u32 req_mask,
				       const void *data);

ethnl_notify_handler_t ethnl_notify_handlers[] = {
};

void ethtool_notify(struct net_device *dev, struct netlink_ext_ack *extack,
		    unsigned int cmd, u32 req_mask, const void *data)
{
	if (unlikely(!ethnl_ok))
		return;
	ASSERT_RTNL();

	if (likely(cmd < ARRAY_SIZE(ethnl_notify_handlers) &&
		   ethnl_notify_handlers[cmd]))
		ethnl_notify_handlers[cmd](dev, extack, cmd, req_mask, data);
	else
		WARN_ONCE(1, "notification %u not implemented (dev=%s, req_mask=0x%x)\n",
			  cmd, netdev_name(dev), req_mask);
}
EXPORT_SYMBOL(ethtool_notify);

/* genetlink setup */

static const struct genl_ops ethtool_genl_ops[] = {
};

static const struct genl_multicast_group ethtool_nl_mcgrps[] = {
	[ETHNL_MCGRP_MONITOR] = { .name = ETHTOOL_MCGRP_MONITOR_NAME },
};

struct genl_family ethtool_genl_family = {
	.hdrsize	= 0,
	.name		= ETHTOOL_GENL_NAME,
	.version	= ETHTOOL_GENL_VERSION,
	.netnsok	= true,
	.parallel_ops	= true,
	.ops		= ethtool_genl_ops,
	.n_ops		= ARRAY_SIZE(ethtool_genl_ops),
	.mcgrps		= ethtool_nl_mcgrps,
	.n_mcgrps	= ARRAY_SIZE(ethtool_nl_mcgrps),
};

/* module setup */

static int __init ethnl_init(void)
{
	int ret;

	ret = genl_register_family(&ethtool_genl_family);
	if (WARN(ret < 0, "ethtool: genetlink family registration failed"))
		return ret;
	ethnl_ok = true;

	return 0;
}

subsys_initcall(ethnl_init);
