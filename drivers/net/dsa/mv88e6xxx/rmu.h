/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Marvell 88E6xxx Switch Remote Management Unit Support
 *
 * Copyright (c) 2022 Mattias Forsblad <mattias.forsblad@gmail.com>
 *
 */

#ifndef _MV88E6XXX_RMU_H_
#define _MV88E6XXX_RMU_H_

void mv88e6xxx_rmu_conduit_state_change(struct dsa_switch *ds,
					const struct net_device *master,
					bool operational);
void mv88e6xxx_rmu_frame2reg_handler(struct dsa_switch *ds,
				     struct sk_buff *skb,
				     u8 seqno);
#endif /* _MV88E6XXX_RMU_H_ */
