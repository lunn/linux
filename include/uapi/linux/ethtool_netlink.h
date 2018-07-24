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
	ETHTOOL_A_DEV_UNSPEC,
	ETHTOOL_A_DEV_INDEX,			/* u32 */
	ETHTOOL_A_DEV_NAME,			/* string */

	__ETHTOOL_A_DEV_CNT,
	ETHTOOL_A_DEV_MAX = (__ETHTOOL_A_DEV_CNT - 1)
};

/* bit sets */

enum {
	ETHTOOL_A_BIT_UNSPEC,
	ETHTOOL_A_BIT_INDEX,			/* u32 */
	ETHTOOL_A_BIT_NAME,			/* string */
	ETHTOOL_A_BIT_VALUE,			/* flag */

	__ETHTOOL_A_BIT_CNT,
	ETHTOOL_A_BIT_MAX = (__ETHTOOL_A_BIT_CNT - 1)
};

enum {
	ETHTOOL_A_BITS_UNSPEC,
	ETHTOOL_A_BITS_BIT,

	__ETHTOOL_A_BITS_CNT,
	ETHTOOL_A_BITS_MAX = (__ETHTOOL_A_BITS_CNT - 1)
};

enum {
	ETHTOOL_A_BITSET_UNSPEC,
	ETHTOOL_A_BITSET_LIST,			/* flag */
	ETHTOOL_A_BITSET_SIZE,			/* u32 */
	ETHTOOL_A_BITSET_BITS,			/* nest - ETHTOOL_A_BITS_* */
	ETHTOOL_A_BITSET_VALUE,			/* binary */
	ETHTOOL_A_BITSET_MASK,			/* binary */

	__ETHTOOL_A_BITSET_CNT,
	ETHTOOL_A_BITSET_MAX = (__ETHTOOL_A_BITSET_CNT - 1)
};

/* events */

enum {
	ETHTOOL_A_NEWDEV_UNSPEC,
	ETHTOOL_A_NEWDEV_DEV,			/* nest - ETHTOOL_A_DEV_* */

	__ETHTOOL_A_NEWDEV_CNT,
	ETHTOOL_A_NEWDEV_MAX = (__ETHTOOL_A_NEWDEV_CNT - 1)
};

enum {
	ETHTOOL_A_DELDEV_UNSPEC,
	ETHTOOL_A_DELDEV_DEV,			/* nest - ETHTOOL_A_DEV_* */

	__ETHTOOL_A_DELDEV_CNT,
	ETHTOOL_A_DELDEV_MAX = (__ETHTOOL_A_DELDEV_CNT - 1)
};

enum {
	ETHTOOL_A_RENAMEDEV_UNSPEC,
	ETHTOOL_A_RENAMEDEV_DEV,		/* nest - ETHTOOL_A_DEV_* */

	__ETHTOOL_A_RENAMEDEV_CNT,
	ETHTOOL_A_RENAMEDEV_MAX = (__ETHTOOL_A_RENAMEDEV_CNT - 1)
};

enum {
	ETHTOOL_A_EVENT_UNSPEC,
	ETHTOOL_A_EVENT_NEWDEV,			/* nest - ETHTOOL_A_NEWDEV_* */
	ETHTOOL_A_EVENT_DELDEV,			/* nest - ETHTOOL_A_DELDEV_* */
	ETHTOOL_A_EVENT_RENAMEDEV,		/* nest - ETHTOOL_A_RENAMEDEV_* */

	__ETHTOOL_A_EVENT_CNT,
	ETHTOOL_A_EVENT_MAX = (__ETHTOOL_A_EVENT_CNT - 1)
};

/* generic netlink info */
#define ETHTOOL_GENL_NAME "ethtool"
#define ETHTOOL_GENL_VERSION 1

#define ETHTOOL_MCGRP_MONITOR_NAME "monitor"

#endif /* _UAPI_LINUX_ETHTOOL_NETLINK_H_ */
