/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Marvell 88E6xxx Switch Remote Management Unit Support
 *
 * Copyright (c) 2022 Mattias Forsblad <mattias.forsblad@gmail.com>
 *
 */

#ifndef _MV88E6XXX_RMU_H_
#define _MV88E6XXX_RMU_H_

#define MV88E6XXX_RMU_WAIT_TIME_MS		20

#define MV88E6XXX_RMU_REQ_FORMAT_GET_ID		htons(0x0000)
#define MV88E6XXX_RMU_REQ_FORMAT_SOHO		htons(0x0001)
#define MV88E6XXX_RMU_REQ_PAD			htons(0x0000)
#define MV88E6XXX_RMU_REQ_CODE_GET_ID		htons(0x0000)
#define MV88E6XXX_RMU_REQ_DATA			htons(0x0000)

#define MV88E6XXX_RMU_RESP_FORMAT_1		htons(0x0001)
#define MV88E6XXX_RMU_RESP_FORMAT_2		htons(0x0002)

#define MV88E6XXX_RMU_RESP_CODE_GOT_ID		htons(0x0000)

struct mv88e6xxx_rmu_header {
	__be16 format;
	__be16 prodnr;
	__be16 code;
} __packed;

void mv88e6xxx_rmu_conduit_state_change(struct dsa_switch *ds,
					const struct net_device *master,
					bool operational);
void mv88e6xxx_rmu_frame2reg_handler(struct dsa_switch *ds,
				     struct sk_buff *skb,
				     u8 seqno);
#endif /* _MV88E6XXX_RMU_H_ */
