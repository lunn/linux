/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef _NET_ETHTOOL_NETLINK_H
#define _NET_ETHTOOL_NETLINK_H

#include <linux/ethtool_netlink.h>
#include <linux/netdevice.h>
#include <net/genetlink.h>
#include <net/sock.h>

#define ETHNL_SET_ERRMSG(info, msg) \
	do { if (info) GENL_SET_ERR_MSG(info, msg); } while (0)

extern u32 ethnl_bcast_seq;

extern struct genl_family ethtool_genl_family;

struct net_device *ethnl_dev_get(struct genl_info *info, struct nlattr *nest);
int ethnl_fill_dev(struct sk_buff *msg, struct net_device *dev, u16 attrtype);

struct sk_buff *ethnl_reply_init(size_t payload, struct net_device *dev, u8 cmd,
				 u16 dev_attrtype, struct genl_info *info,
				 void **ehdrp);

#if BITS_PER_LONG == 64 && defined(__BIG_ENDIAN)
void ethnl_bitmap_to_u32(unsigned long *bitmap, unsigned int nwords);
#else
static inline void ethnl_bitmap_to_u32(unsigned long *bitmap,
				       unsigned int nwords)
{
}
#endif

static inline int ethnl_str_size(const char *s)
{
	return nla_total_size(strlen(s) + 1);
}

static inline int ethnl_str_ifne_size(const char *s)
{
	return s[0] ? ethnl_str_size(s) : 0;
}

static inline int ethnl_put_str_ifne(struct sk_buff *skb, int attrtype,
				     const char *s)
{
	if (!s[0])
		return 0;
	return nla_put_string(skb, attrtype, s);
}

static inline struct nlattr *ethnl_nest_start(struct sk_buff *skb,
					      int attrtype)
{
	return nla_nest_start(skb, attrtype | NLA_F_NESTED);
}

static inline int ethnlmsg_parse(const struct nlmsghdr *nlh,
				 struct nlattr *tb[], int maxtype,
				 const struct nla_policy *policy,
				 struct genl_info *info)
{
	return nlmsg_parse_strict(nlh, GENL_HDRLEN, tb, maxtype, policy,
				  info ? info->extack : NULL);
}

/* ethnl_update_* return true if the value is changed */
static inline bool ethnl_update_u32(u32 *dst, struct nlattr *attr)
{
	u32 val;

	if (!attr)
		return false;
	val = nla_get_u32(attr);
	if (*dst == val)
		return false;

	*dst = val;
	return true;
}

static inline bool ethnl_update_u8(u8 *dst, struct nlattr *attr)
{
	u8 val;

	if (!attr)
		return false;
	val = nla_get_u8(attr);
	if (*dst == val)
		return false;

	*dst = val;
	return true;
}

/* update u32 value used as bool from NLA_U8 */
static inline bool ethnl_update_bool32(u32 *dst, struct nlattr *attr)
{
	u8 val;

	if (!attr)
		return false;
	val = !!nla_get_u8(attr);
	if (!!*dst == val)
		return false;

	*dst = val;
	return true;
}

static inline bool ethnl_update_binary(u8 *dst, unsigned int len,
				       struct nlattr *attr)
{
	if (!attr)
		return false;
	if (nla_len(attr) < len)
		len = nla_len(attr);
	if (!memcmp(dst, nla_data(attr), len))
		return false;

	memcpy(dst, nla_data(attr), len);
	return true;
}

static inline bool ethnl_update_bitfield32(u32 *dst, struct nlattr *attr)
{
	struct nla_bitfield32 change;
	u32 newval;

	if (!attr)
		return false;
	change = nla_get_bitfield32(attr);
	newval = (*dst & ~change.selector) | (change.value & change.selector);
	if (*dst == newval)
		return false;

	*dst = newval;
	return true;
}

static inline void warn_partial_info(struct genl_info *info)
{
	ETHNL_SET_ERRMSG(info, "not all requested data could be retrieved");
}

/* Check user privileges explicitly to allow finer access control based on
 * context of the request or hiding part of the information from unprivileged
 * users
 */
static inline bool ethnl_is_privileged(struct sk_buff *skb)
{
	struct net *net = sock_net(skb->sk);

	return netlink_ns_capable(skb, net->user_ns, CAP_NET_ADMIN);
}

/* total size of ETHA_*_DEV nested attribute; this is an upper estimate so that
 * we do not need to hold RTNL longer than necessary to prevent rename between
 * estimating the size and composing the message
 */
static inline unsigned int dev_ident_size(void)
{
	return nla_total_size(nla_total_size(sizeof(u32)) +
			      nla_total_size(IFNAMSIZ));
}

#endif /* _NET_ETHTOOL_NETLINK_H */
