/*
 * linux/arinc429/netlink.h
 *
 * Definitions for the ARINC429 netlink interface
 *
 * Copyright (C) 2015 Marek Vasut <marex@denx.de>
 *
 * Based on the SocketCAN stack.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef _UAPI_ARINC429_NETLINK_H
#define _UAPI_ARINC429_NETLINK_H

#include <linux/types.h>

/*
 * ARINC429 clock parameters
 */
struct arinc429_clock {
	__u32 freq;		/* ARINC429 bus clock frequency in Hz */
};

/*
 * CAN operational and error states
 */
enum arinc429_state {
	ARINC429_STATE_ERROR_ACTIVE = 0,	/* RX/TX error count < 96 */
	ARINC429_STATE_ERROR_WARNING,	/* RX/TX error count < 128 */
	ARINC429_STATE_ERROR_PASSIVE,	/* RX/TX error count < 256 */
	ARINC429_STATE_BUS_OFF,		/* RX/TX error count >= 256 */
	ARINC429_STATE_STOPPED,		/* Device is stopped */
	ARINC429_STATE_SLEEPING,		/* Device is sleeping */
	ARINC429_STATE_MAX
};

/*
 * CAN controller mode
 */
struct arinc429_ctrlmode {
	__u32 mask;
	__u32 flags;
};

#define ARINC429_CTRLMODE_LOOPBACK		0x01	/* Loopback mode */
#define ARINC429_CTRLMODE_LISTENONLY		0x02	/* Listen-only mode */
#define ARINC429_CTRLMODE_3_SAMPLES		0x04	/* Triple sampling mode */
#define ARINC429_CTRLMODE_ONE_SHOT		0x08	/* One-Shot mode */
#define ARINC429_CTRLMODE_BERR_REPORTING	0x10	/* Bus-error reporting */
#define ARINC429_CTRLMODE_FD			0x20	/* CAN FD mode */
#define ARINC429_CTRLMODE_PRESUME_ACK	0x40	/* Ignore missing CAN ACKs */
#define ARINC429_CTRLMODE_FD_NON_ISO		0x80	/* CAN FD in non-ISO mode */

/*
 * CAN device statistics
 */
struct arinc429_device_stats {
	__u32 bus_error;	/* Bus errors */
	__u32 error_warning;	/* Changes to error warning state */
	__u32 error_passive;	/* Changes to error passive state */
	__u32 bus_off;		/* Changes to bus off state */
	__u32 arbitration_lost; /* Arbitration lost errors */
	__u32 restarts;		/* CAN controller re-starts */
};

/*
 * CAN netlink interface
 */
enum {
	IFLA_ARINC429_UNSPEC,
	IFLA_ARINC429_CLOCK,
	IFLA_ARINC429_STATE,
	IFLA_ARINC429_CTRLMODE,
	IFLA_ARINC429_RESTART_MS,
	IFLA_ARINC429_RESTART,
	__IFLA_ARINC429_MAX
};

#define IFLA_ARINC429_MAX	(__IFLA_ARINC429_MAX - 1)

#endif /* !_UAPI_ARINC429_NETLINK_H */
