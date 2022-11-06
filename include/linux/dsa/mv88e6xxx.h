/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2021 NXP
 */

#ifndef _NET_DSA_TAG_MV88E6XXX_H
#define _NET_DSA_TAG_MV88E6XXX_H

#include <net/dsa.h>

struct dsa_tagger_data {
	/* DSA frame decoded to be from the RMU */
	void (*rmu_frame2reg)(struct dsa_switch *ds,
			      struct sk_buff *skb,
			      u8 seqno);
	/* Add DSA header to frame to be sent to switch */
	void (*rmu_reg2frame)(struct  dsa_switch *ds,
			      struct sk_buff *skb);
};

#define MV88E6XXX_VID_STANDALONE	0
#define MV88E6XXX_VID_BRIDGED		(VLAN_N_VID - 1)

#endif
