/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef _LINUX_ETHTOOL_NETLINK_H_
#define _LINUX_ETHTOOL_NETLINK_H_

#include <uapi/linux/ethtool_netlink.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <net/netlink.h>

#define __ETHTOOL_LINK_MODE_MASK_NWORDS \
	DIV_ROUND_UP(__ETHTOOL_LINK_MODE_MASK_NBITS, 32)

enum ethtool_multicast_groups {
	ETHNL_MCGRP_MONITOR,
};

struct ethtool_rxflow_notification_info {
	u32	ctx_op;
	u32	context;
	u32	flow_type;
};

static inline struct nlattr *ethnl_nest_start(struct sk_buff *skb,
					      int attrtype)
{
	return nla_nest_start(skb, attrtype | NLA_F_NESTED);
}

int ethnl_fill_dev(struct sk_buff *msg, struct net_device *dev, u16 attrtype);
void *ethnl_bcastmsg_put(struct sk_buff *skb, u8 cmd);
void *ethnl_bcastmsg_put_seq(struct sk_buff *skb, u8 cmd, u32 seq);
int ethnl_multicast(struct sk_buff *skb, struct net_device *dev);

#endif /* _LINUX_ETHTOOL_NETLINK_H_ */
