/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef _NET_ETHTOOL_NETLINK_H
#define _NET_ETHTOOL_NETLINK_H

#include <linux/ethtool_netlink.h>
#include <linux/netdevice.h>
#include <net/genetlink.h>
#include <net/sock.h>
#include <linux/net_tstamp.h>

#define ETHNL_SET_ERRMSG(info, msg) \
	do { if (info) GENL_SET_ERR_MSG(info, msg); } while (0)

#define __SOF_TIMESTAMPING_COUNT (const_ilog2(SOF_TIMESTAMPING_LAST) + 1)
#define __HWTSTAMP_TX_COUNT (HWTSTAMP_TX_LAST + 1)
#define __HWTSTAMP_FILTER_COUNT (HWTSTAMP_FILTER_LAST + 1)

extern u32 ethnl_bcast_seq;

extern struct genl_family ethtool_genl_family;

extern const char *const so_timestamping_labels[];
extern const char *const tstamp_tx_type_labels[];
extern const char *const tstamp_rx_filter_labels[];
extern const char *const link_mode_names[];

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

/* GET request handling */

struct common_reply_data;

/* The structure holding data for unified processing a GET request consists of
 * two parts: request info and reply data. Request info starts at offset 0 with
 * embedded struct common_req_info, is usually filled by ->parse_request() and
 * is common for all reply messages to one request. Reply data start with
 * embedded struct common_reply_data and contain data specific to a reply
 * message (usually one per device for dump requests); this part is filled by
 * ->prepare_data()
 */

/**
 * struct common_req_info - base type of request information for GET requests
 * @reply_data: pointer to reply data within the same block
 * @dev:        network device the request is for (may be null)
 * @req_mask:   request mask, bitmap of requested information
 * @compact:    true if compact format of bitsets in reply is requested
 *
 * This is a common base, additional members may follow after this structure.
 */
struct common_req_info {
	struct common_reply_data	*reply_data;
	struct net_device		*dev;
	u32				req_mask;
	bool				compact;
};

/**
 * struct common_reply_data - base type of reply data for GET requests
 * @dev:       device for current reply message; in single shot requests it is
 *             equal to &common_req_info.dev; in dumps it's different for each
 *             reply message
 * @info_mask: bitmap of information actually provided in reply; it is a subset
 *             of &common_req_info.req_mask with cleared bits corresponding to
 *             information which cannot be provided
 *
 * This structure is usually followed by additional members filled by
 * ->prepare_data() and used by ->cleanup().
 */
struct common_reply_data {
	struct net_device		*dev;
	u32				info_mask;
};

static inline int ethnl_before_ops(struct net_device *dev)
{
	if (dev && dev->ethtool_ops->begin)
		return dev->ethtool_ops->begin(dev);
	else
		return 0;
}

static inline void ethnl_after_ops(struct net_device *dev)
{
	if (dev && dev->ethtool_ops->complete)
		dev->ethtool_ops->complete(dev);
}

/**
 * struct get_request_ops - unified handling of GET requests
 * @request_cmd:    command id for request (GET)
 * @reply_cmd:      command id for reply (SET)
 * @dev_attr:       attribute type for device specification
 * @data_size:      total length of data structure
 * @repdata_offset: offset of "reply data" part (struct common_reply_data)
 * @allow_nodev_do: do not fail if device is not specified for non-dump request
 * @parse_request:
 *	parse request message and fill request info; request info is zero
 *	initialized on entry except reply_data pointer (which is initialized)
 * @prepare_data:
 *	retrieve data needed to compose a reply message; reply data are zero
 *	initialized on entry except for @dev
 * @reply_size:
 *	return size of reply message payload without device specification;
 *	returned size may be bigger than actual reply size but it must suffice
 *	to hold the reply
 * @fill_reply:
 *	fill reply message payload using the data prepared by @prepare_data()
 * @cleanup
 *	(optional) called when data are no longer needed; use e.g. to free
 *	any additional data structures allocated in prepare_data() which are
 *	not part of the main structure
 *
 * Description of variable parts of GET request handling when using the unified
 * infrastructure. When used, a pointer to an instance of this structure is to
 * be added to &get_requests array, generic handlers ethnl_get_doit(),
 * ethnl_get_dumpit(), ethnl_get_start() and ethnl_get_done() used in
 * @ethnl_genl_ops and (optionally) ethnl_std_notify() as notification handler
 * in &ethnl_notify_handlers.
 */
struct get_request_ops {
	u8			request_cmd;
	u8			reply_cmd;
	u16			dev_attrtype;
	unsigned int		data_size;
	unsigned int		repdata_offset;
	bool			allow_nodev_do;

	int (*parse_request)(struct common_req_info *req_info,
			     struct sk_buff *skb, struct genl_info *info,
			     const struct nlmsghdr *nlhdr);
	int (*prepare_data)(struct common_req_info *req_info,
			    struct genl_info *info);
	int (*reply_size)(const struct common_req_info *req_info);
	int (*fill_reply)(struct sk_buff *skb,
			  const struct common_req_info *req_info);
	void (*cleanup)(struct common_req_info *req_info);
};

/* request handlers */

extern const struct get_request_ops strset_request_ops;
extern const struct get_request_ops info_request_ops;
extern const struct get_request_ops settings_request_ops;
extern const struct get_request_ops params_request_ops;

int ethnl_set_settings(struct sk_buff *skb, struct genl_info *info);
int ethnl_set_params(struct sk_buff *skb, struct genl_info *info);
int ethnl_act_nway_rst(struct sk_buff *skb, struct genl_info *info);
int ethnl_act_phys_id(struct sk_buff *skb, struct genl_info *info);

/* notify handlers */

void ethnl_nwayrst_notify(struct net_device *dev,
			  struct netlink_ext_ack *extack, unsigned int cmd,
			  u32 req_mask, const void *data);
void ethnl_physid_notify(struct net_device *dev, struct netlink_ext_ack *extack,
			 unsigned int cmd, u32 req_mask, const void *data);

#endif /* _NET_ETHTOOL_NETLINK_H */
