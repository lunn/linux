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
#define MV88E6XXX_RMU_REQ_CODE_MIB		htons(0x1020)
#define MV88E6XXX_RMU_REQ_CODE_REG_RW		htons(0x2000)
#define MV88E6XXX_RMU_REQ_DATA			htons(0x0000)

#define MV88E6XXX_RMU_REQ_RW_0_OP_WAIT_1	(0x3 << 10)
#define MV88E6XXX_RMU_REQ_RW_0_OP_READ		(0x2 << 10)
#define MV88E6XXX_RMU_REQ_RW_0_OP_WRITE		(0x1 << 10)
#define MV88E6XXX_RMU_REQ_RW_0_OP_WAIT_0	(0x0 << 10)

#define MV88E6XXX_RMU_REQ_RW_0_READ(_addr_, _reg_)		 \
	htons(MV88E6XXX_RMU_REQ_RW_0_OP_READ |			 \
	      ((_addr_) << 5) |					 \
	      (_reg_))
#define MV88E6XXX_RMU_REQ_RW_0_WRITE(_addr_, _reg_)		 \
	htons(MV88E6XXX_RMU_REQ_RW_0_OP_WRITE |			 \
	      ((_addr_) << 5) |					 \
	      (_reg_))

#define MV88E6XXX_RMU_REQ_RW_0_WAIT_0(_addr_, _reg_)		   \
	htons(MV88E6XXX_RMU_REQ_RW_0_OP_WAIT_0 |		   \
	      ((_addr_) << 5) |					   \
	      (_reg_))
#define MV88E6XXX_RMU_REQ_RW_0_WAIT_1(_addr_, _reg_)		   \
	htons(MV88E6XXX_RMU_REQ_RW_0_OP_WAIT_1 |		   \
	      ((_addr_) << 5) |					   \
	      (_reg_))

#define MV88E6XXX_RMU_REQ_RW_0_END		htons(0xffff)
#define MV88E6XXX_RMU_REQ_RW_1_END		htons(0xffff)

#define MV88E6XXX_RMU_RESP_FORMAT_1		htons(0x0001)
#define MV88E6XXX_RMU_RESP_FORMAT_2		htons(0x0002)
#define MV88E6XXX_RMU_RESP_CODE_GOT_ID		htons(0x0000)
#define MV88E6XXX_RMU_RESP_CODE_MIB		htons(0x1020)
#define MV88E6XXX_RMU_RESP_CODE_REG_RW		htons(0x2000)

struct mv88e6xxx_rmu_header {
	__be16 format;
	__be16 prodnr;
	__be16 code;
} __packed;

struct mv88e6xxx_rmu_rw_resp {
	struct mv88e6xxx_rmu_header rmu_header;
	__be16 cmd;
	__be16 value;
	__be16 end0;
	__be16 end1;
} __packed;

struct mv88e6xxx_rmu_mib_resp {
	struct mv88e6xxx_rmu_header rmu_header;
	__be16 swport;
	__be32 timestamp;
	__be32 bank0[32];
	__be16 port[6];
};

int mv88e6xxx_rmu_stats(struct mv88e6xxx_chip *chip, int port,
			uint64_t *data,
			struct mv88e6xxx_hw_stat *hw_stats,
			int num_hw_stats,
			u16 bank1_select, u16 histogram);
int mv88e6xxx_rmu_write(struct mv88e6xxx_chip *chip, int addr, int reg,
			u16 val);
int mv88e6xxx_rmu_read(struct mv88e6xxx_chip *chip, int addr, int reg,
		       u16 *val);
int mv88e6xxx_rmu_wait_bit(struct mv88e6xxx_chip *chip, int addr, int reg,
			   int bit, int val);
void mv88e6xxx_rmu_conduit_state_change(struct dsa_switch *ds,
					const struct net_device *master,
					bool operational);
void mv88e6xxx_rmu_frame2reg_handler(struct dsa_switch *ds,
				     struct sk_buff *skb,
				     u8 seqno);
#endif /* _MV88E6XXX_RMU_H_ */
