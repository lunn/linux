/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * include/uapi/linux/ethtool_netlink.h - netlink interface for ethtool
 *
 * See Documentation/networking/ethtool-netlink.txt in kernel source tree for
 * doucumentation of the interface.
 */

#ifndef _UAPI_LINUX_ETHTOOL_NETLINK_H_
#define _UAPI_LINUX_ETHTOOL_NETLINK_H_

#include <linux/ethtool.h>

enum {
	ETHNL_CMD_NOOP,
	ETHNL_CMD_EVENT,		/* only for notifications */

	__ETHNL_CMD_CNT,
	ETHNL_CMD_MAX = (__ETHNL_CMD_CNT - 1)
};

/* device specification */

enum {
	ETHA_DEV_UNSPEC,
	ETHA_DEV_INDEX,				/* u32 */
	ETHA_DEV_NAME,				/* string */

	__ETHA_DEV_CNT,
	ETHA_DEV_MAX = (__ETHA_DEV_CNT - 1)
};

/* bit sets */

enum {
	ETHA_BIT_UNSPEC,
	ETHA_BIT_INDEX,				/* u32 */
	ETHA_BIT_NAME,				/* string */
	ETHA_BIT_VALUE,				/* flag */

	__ETHA_BIT_CNT,
	ETHA_BIT_MAX = (__ETHA_BIT_CNT - 1)
};

enum {
	ETHA_BITS_UNSPEC,
	ETHA_BITS_BIT,

	__ETHA_BITS_CNT,
	ETHA_BITS_MAX = (__ETHA_BITS_CNT - 1)
};

enum {
	ETHA_BITSET_UNSPEC,
	ETHA_BITSET_LIST,			/* flag */
	ETHA_BITSET_SIZE,			/* u32 */
	ETHA_BITSET_BITS,			/* nest - ETHA_BITS_* */
	ETHA_BITSET_VALUE,			/* binary */
	ETHA_BITSET_MASK,			/* binary */

	__ETHA_BITSET_CNT,
	ETHA_BITSET_MAX = (__ETHA_BITSET_CNT - 1)
};

/* events */

enum {
	ETHA_NEWDEV_UNSPEC,
	ETHA_NEWDEV_DEV,			/* nest - ETHA_DEV_* */

	__ETHA_NEWDEV_CNT,
	ETHA_NEWDEV_MAX = (__ETHA_NEWDEV_CNT - 1)
};

enum {
	ETHA_DELDEV_UNSPEC,
	ETHA_DELDEV_DEV,			/* nest - ETHA_DEV_* */

	__ETHA_DELDEV_CNT,
	ETHA_DELDEV_MAX = (__ETHA_DELDEV_CNT - 1)
};

enum {
	ETHA_RENAMEDEV_UNSPEC,
	ETHA_RENAMEDEV_DEV,			/* nest - ETHA_DEV_* */

	__ETHA_RENAMEDEV_CNT,
	ETHA_RENAMEDEV_MAX = (__ETHA_RENAMEDEV_CNT - 1)
};

enum {
	ETHA_EVENT_UNSPEC,
	ETHA_EVENT_NEWDEV,			/* nest - ETHA_NEWDEV_* */
	ETHA_EVENT_DELDEV,			/* nest - ETHA_DELDEV_* */
	ETHA_EVENT_RENAMEDEV,			/* nest - ETHA_RENAMEDEV_* */

	__ETHA_EVENT_CNT,
	ETHA_EVENT_MAX = (__ETHA_EVENT_CNT - 1)
};

/* generic netlink info */
#define ETHTOOL_GENL_NAME "ethtool"
#define ETHTOOL_GENL_VERSION 1

#define ETHTOOL_MCGRP_MONITOR_NAME "monitor"

#endif /* _UAPI_LINUX_ETHTOOL_NETLINK_H_ */
