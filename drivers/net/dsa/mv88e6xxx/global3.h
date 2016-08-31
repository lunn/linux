/*
 * Marvell 88E6xxx Switch Global 3 Registers (TCAM) support
 *
 * Copyright (c) 2016 Andrew Lunn <andrew@lunn.ch>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _MV88E6XXX_GLOBAL3_H
#define _MV88E6XXX_GLOBAL3_H

#include "chip.h"

#define GLOBAL3_TCAM_OP			0x00
#define GLOBAL3_TCAM_OP_BUSY		BIT(15)
#define GLOBAL3_TCAM_OP_FLUSH_ALL	((1 << 12) | GLOBAL3_TCAM_OP_BUSY)
#define GLOBAL3_TCAM_OP_FLUSH_ENTRY	((2 << 12) | GLOBAL3_TCAM_OP_BUSY)
#define GLOBAL3_TCAM_OP_LOAD		((3 << 12) | GLOBAL3_TCAM_OP_BUSY)
#define GLOBAL3_TCAM_OP_GET_NEXT	((4 << 12) | GLOBAL3_TCAM_OP_BUSY)
#define GLOBAL3_TCAM_OP_READ		((5 << 12) | GLOBAL3_TCAM_OP_BUSY)
#define GLOBAL3_TCAM_OP_PAGE2		(0x2 << 10)
#define GLOBAL3_TCAM_OP_PAGE1		(0x1 << 10)
#define GLOBAL3_TCAM_OP_PAGE0		(0x0 << 10)
#define GLOBAL3_P0_KEY1		0x02
#define GLOBAL3_P0_KEY1_FRAME_TYPE_MASK		(3 << 14)
#define GLOBAL3_P0_KEY1_FRAME_TYPE_NORNAL	(0x0 << 6)
#define GLOBAL3_P0_KEY1_FRAME_TYPE_DSA		(0x1 << 6)
#define GLOBAL3_P0_KEY1_FRAME_TYPE_PROVIDER	(0x2 << 6)
#define GLOBAL3_P0_KEY2		0x03
#define GLOBAL3_P0_KEY2_SRC_PORT_VECTOR_MASK		(0x7f << 8)
#define GLOBAL3_P0_KEY2_SRC_PORT_VECTOR_MASK_SHIFT	8
#define GLOBAL3_P0_KEY2_SRC_PORT_VECTOR_VALUE_MASK	(0x7f)
#define GLOBAL3_P0_KEY3		0x04
#define GLOBAL3_P0_KEY3_PPRI_MASK			(0xf << 12)
#define GLOBAL3_P0_KEY3_PVID_MASK			(0xf << 8)
#define GLOBAL3_P0_KEY4		0x05
#define GLOBAL3_P0_KEY4_PVID_MASK			(0xff << 8)
#define GLOBAL3_P2_ACTION1	0x02
#define GLOBAL3_P2_ACTION1_CONTINUE		BIT(15)
#define GLOBAL3_P2_ACTION1_INTERRUPT		BIT(14)
#define GLOBAL3_P2_ACTION1_INC_TCAM_COUNTER	BIT(13)
#define GLOBAL3_P2_ACTION1_VID_OVERRIDE		BIT(12)
#define GLOBAL3_P2_ACTION1_VID_MASK		0x7ff
#define GLOBAL3_P2_ACTION2	0x3
#define GLOBAL3_P2_ACTION2_FLOW_ID_MASK		0xff00
#define GLOBAL3_P2_ACTION2_FLOW_ID_0		(0x0 << 8)
#define GLOBAL3_P2_ACTION2_FLOW_ID_1		(0x1 << 8)
#define GLOBAL3_P2_ACTION2_FLOW_ID_2		(0x2 << 8)
#define GLOBAL3_P2_ACTION2_FLOW_ID_3		(0x3 << 8)
#define GLOBAL3_P2_ACTION2_QPRI_OVERRIDE	BIT(7)
#define GLOBAL3_P2_ACTION2_QPRI_MASK		0xf0
#define GLOBAL3_P2_ACTION2_QPRI_0		(0x0 << 4)
#define GLOBAL3_P2_ACTION2_QPRI_1		(0x1 << 4)
#define GLOBAL3_P2_ACTION2_QPRI_2		(0x2 << 4)
#define GLOBAL3_P2_ACTION2_QPRI_3		(0x3 << 4)
#define GLOBAL3_P2_ACTION2_FPRI_OVERRIDE	BIT(3)
#define GLOBAL3_P2_ACTION2_FPRI_MASK		0x7
#define GLOBAL3_P2_ACTION2_FPRI_0		(0x0 << 0)
#define GLOBAL3_P2_ACTION2_FPRI_1		(0x1 << 0)
#define GLOBAL3_P2_ACTION2_FPRI_2		(0x2 << 0)
#define GLOBAL3_P2_ACTION2_FPRI_3		(0x3 << 0)
#define GLOBAL3_P2_ACTION3		0x04
#define GLOBAL3_P2_ACTION3_DPV_OVERRIDE		BIT(11)
#define GLOBAL3_P2_ACTION4		0x05
#define GLOBAL3_P2_ACTION4_FRAME_ACTION_OVERRIDE	BIT(15)
#define GLOBAL3_P2_ACTION4_FRAME_ACTION_SRC_IS_TAGGED	BIT(14)
#define GLOBAL3_P2_ACTION4_FRAME_ACTION_PVID		BIT(13)
#define GLOBAL3_P2_ACTION4_FRAME_ACTION_MGMT		BIT(12)
#define GLOBAL3_P2_ACTION4_FRAME_ACTION_ARP		BIT(11)
#define GLOBAL3_P2_ACTION4_FRAME_ACTION_SNOOP		BIT(10)
#define GLOBAL3_P2_ACTION4_FRAME_ACTION_POLICY_MIRROR	BIT(9)
#define GLOBAL3_P2_ACTION4_FRAME_ACTION_POLICY_TRAP	BIT(8)
#define GLOBAL3_P2_ACTION4_FRAME_ACTION_SANRL		BIT(7)
#define GLOBAL3_P2_ACTION4_FRAME_ACTION_DANRL		BIT(6)
#define GLOBAL3_P2_ACTION4_FRAME_ACTION_MASK		0xfff0
#define GLOBAL3_P2_ACTION4_LOAD_BALANCE_OVERRIDE	BIT(3)
#define GLOBAL3_P2_ACTION4_LOAD_BALANCE_MASK		0x7
#define GLOBAL3_P2_DEBUG28		0x1c
#define GLOBAL3_P2_DEBUG31		0x1f

#define MV88E6XXX_TCAM_PARAM_DISABLED	INT_MIN

struct mv88e6xxx_tcam_data {
	u16 page0[32];
	u16 page1[32];
	u16 page2[32];
};

enum mv88e6xxx_tcam_param {
	MV88E6XXX_P0_KEY1_FRAME_TYPE,
	MV88E6XXX_P0_KEY2_SRC_PORT_VECTOR,
	MV88E6XXX_P0_KEY3_PPRI,
	MV88E6XXX_P0_KEY4_PVID,
	MV88E6XXX_P2_ACTION1_INTERRUPT,
	MV88E6XXX_P2_ACTION1_INC_TCAM_COUNTER,
	MV88E6XXX_P2_ACTION1_VID,
	MV88E6XXX_P2_ACTION2_FLOW_ID,
	MV88E6XXX_P2_ACTION2_QPRI,
	MV88E6XXX_P2_ACTION2_FPRI,
	MV88E6XXX_P2_ACTION3_DST_PORT_VECTOR,
	MV88E6XXX_P2_ACTION4_FRAME_ACTION,
	MV88E6XXX_P2_ACTION4_LOAD_BALANCE,
	MV88E6XXX_P2_DEBUG_PORT,
	MV88E6XXX_P2_DEBUG_HIT,
};

#ifdef CONFIG_NET_DSA_MV88E6XXX_GLOBAL3

int mv88e6xxx_g3_tcam_get_next(struct mv88e6xxx_chip *chip, u32 *entry,
			       struct mv88e6xxx_tcam_data *data);
int mv88e6xxx_g3_tcam_read(struct mv88e6xxx_chip *chip, u32 entry,
			   struct mv88e6xxx_tcam_data *data);
int mv88e6xxx_g3_tcam_load_entry(struct mv88e6xxx_chip *chip, u32 entry,
				 struct mv88e6xxx_tcam_data *data);
int mv88e6xxx_g3_tcam_flush_entry(struct mv88e6xxx_chip *chip, u32 entry);
int mv88e6xxx_g3_tcam_flush_all(struct mv88e6xxx_chip *chip);
int mv88e6xxx_g3_tcam_set(struct mv88e6xxx_chip *chip,
			  struct mv88e6xxx_tcam_data *data,
			  enum mv88e6xxx_tcam_param param, u16 value);
int mv88e6xxx_g3_tcam_get(struct mv88e6xxx_chip *chip,
			  struct mv88e6xxx_tcam_data *data,
			  enum mv88e6xxx_tcam_param param, int *value);
int mv88e6xxx_g3_tcam_get_match(struct mv88e6xxx_chip *chip,
				struct mv88e6xxx_tcam_data *data,
				unsigned int offset, u8 *octet, u8 *mask);
int mv88e6xxx_g3_tcam_set_match(struct mv88e6xxx_chip *chip,
				struct mv88e6xxx_tcam_data *data,
				unsigned int offset, u8 octet, u8 mask);

#else /* !CONFIG_NET_DSA_MV88E6XXX_GLOBAL3 */

int mv88e6xxx_g3_tcam_get_next(struct mv88e6xxx_chip *chip, u32 *entry,
			       struct mv88e6xxx_tcam_data *data)
{
	return -EOPNOTSUPP;
}

int mv88e6xxx_g3_tcam_read(struct mv88e6xxx_chip *chip, u32 entry,
			   struct mv88e6xxx_tcam_data *data)
{
	return -EOPNOTSUPP;
}

int mv88e6xxx_g3_tcam_load_entry(struct mv88e6xxx_chip *chip, u32 entry,
				 struct mv88e6xxx_tcam_data *data)
{
	return -EOPNOTSUPP;
}

int mv88e6xxx_g3_tcam_flush_entry(struct mv88e6xxx_chip *chip, u32 entry)
{
	return -EOPNOTSUPP;
}

int mv88e6xxx_g3_tcam_flush_all(struct mv88e6xxx_chip *chip)
{
	return -EOPNOTSUPP;
}

int mv88e6xxx_g3_tcam_set(struct mv88e6xxx_chip *chip,
			  struct mv88e6xxx_tcam_data *data,
			  enum mv88e6xxx_tcam_param param, u16 value)
{
	return -EOPNOTSUPP;
}

int mv88e6xxx_g3_tcam_get(struct mv88e6xxx_chip *chip,
			  struct mv88e6xxx_tcam_data *data,
			  enum mv88e6xxx_tcam_param param, int *value)
{
	return -EOPNOTSUPP;
}

int mv88e6xxx_g3_tcam_get_match(struct mv88e6xxx_chip *chip,
				struct mv88e6xxx_tcam_data *data,
				unsigned int offset, u8 *octet, u8 *mask)
{
	return -EOPNOTSUPP;
}

int mv88e6xxx_g3_tcam_set_match(struct mv88e6xxx_chip *chip,
				struct mv88e6xxx_tcam_data *data,
				unsigned int offset, u8 octet, u8 mask)
{
	return -EOPNOTSUPP;
}

#endif /* CONFIG_NET_DSA_MV88E6XXX_GLOBAL3 */

#endif /* _MV88E6XXX_GLOBAL3_H */
