/*
 * Marvell 88E6xxx SERDES manipulation, via SMI bus
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2016 Andrew Lunn <andrew@lunn.ch>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/mii.h>

#include "global2.h"
#include "mv88e6xxx.h"
#include "port.h"
#include "serdes.h"

#define MV88E6352_ADDR_SERDES		0x0f
#define MV88E6352_SERDES_PAGE_FIBER	0x01

int mv88e6352_serdes_read(struct mv88e6xxx_chip *chip, int reg, u16 *val)
{
	return mv88e6xxx_phy_page_read(chip, MV88E6352_ADDR_SERDES,
				       MV88E6352_SERDES_PAGE_FIBER,
				       reg, val);
}

int mv88e6352_serdes_write(struct mv88e6xxx_chip *chip, int reg, u16 val)
{
	return mv88e6xxx_phy_page_write(chip, MV88E6352_ADDR_SERDES,
					MV88E6352_SERDES_PAGE_FIBER,
					reg, val);
}

static int mv88e6352_serdes_power_set(struct mv88e6xxx_chip *chip, bool on)
{
	u16 val, new_val;
	int err;

	err = mv88e6352_serdes_read(chip, MII_BMCR, &val);
	if (err)
		return err;

	if (on)
		new_val = val & ~ BMCR_PDOWN;
	else
		new_val = val | BMCR_PDOWN;

	if (val != new_val)
		err = mv88e6352_serdes_write(chip, MII_BMCR, new_val);

	return err;
}

int mv88e6352_serdes_power(struct mv88e6xxx_chip *chip, int port, bool on)
{
	int err;
	u8 cmode;

	err = mv88e6xxx_port_get_cmode(chip, port, &cmode);
	if (err)
		return err;

	if ((cmode == PORT_STATUS_CMODE_100BASE_X) ||
	    (cmode == PORT_STATUS_CMODE_1000BASE_X) ||
	    (cmode == PORT_STATUS_CMODE_SGMII)) {
		err = mv88e6352_serdes_power_set(chip, on);
		if (err < 0)
			return err;
	}

	return 0;
}

/* Set the power on/off for 10GBASE-R and 10GBASE-X4/X2 */
static int mv88e6390_serdes_power_base(struct mv88e6xxx_chip *chip, int addr,
				       bool on)
{
	u16 val, new_val;
	int reg_c45;
	int err;

	reg_c45 = MII_ADDR_C45 | MV88E6390_SERDES_DEVICE |
		MV88E6390_PCS_CONTROL_1;
	err = mv88e6xxx_phy_read(chip, addr, reg_c45, &val, false);
	if (err)
		return err;

	if (on)
		new_val = val & ~(MV88E6390_PCS_CONTROL_1_RESET |
				  MV88E6390_PCS_CONTROL_1_LOOPBACK |
				  MV88E6390_PCS_CONTROL_1_PDOWN);
	else
		new_val = val | MV88E6390_PCS_CONTROL_1_PDOWN;

	pr_info("mv88e6390_serdes_power: %d %x %x\n",
		addr, val, new_val);

	if (val != new_val)
		err = mv88e6xxx_phy_write(chip, addr, reg_c45, new_val, false);

	return err;
}

int mv88e6390_serdes_power_port9(struct mv88e6xxx_chip *chip, u8 cmode,
				 bool on)
{
	int err;

	switch (cmode) {
	case PORT_STATUS_CMODE_SGMII:
		break;
	case PORT_STATUS_CMODE_XAUI:
	case PORT_STATUS_CMODE_RXAUI:
	case PORT_STATUS_CMODE_1000BASE_X:
	case PORT_STATUS_CMODE_2500BASEX:
		err = mv88e6390_serdes_power_base(chip, MV88E6390_PORT9_LANE0,
						  on);
		if (err)
			return err;
	}

	return 0;
}

int mv88e6390_serdes_power_port10(struct mv88e6xxx_chip *chip, u8 cmode,
				  bool on)
{
	int err;

	switch (cmode) {
	case PORT_STATUS_CMODE_SGMII:
		break;
	case PORT_STATUS_CMODE_XAUI:
	case PORT_STATUS_CMODE_RXAUI:
	case PORT_STATUS_CMODE_1000BASE_X:
	case PORT_STATUS_CMODE_2500BASEX:
		err = mv88e6390_serdes_power_base(chip, MV88E6390_PORT10_LANE0,
						  on);
		if (err)
			return err;
	}

	return 0;
}

int mv88e6390_serdes_power(struct mv88e6xxx_chip *chip, int port, bool on)
{
	u8 cmode;
	int err;

	err = mv88e6xxx_port_get_cmode(chip, port, &cmode);
	if (err)
		return cmode;

	switch (port) {
	case 9:
		return mv88e6390_serdes_power_port9(chip, cmode, on);
	case 10:
		return mv88e6390_serdes_power_port10(chip, cmode, on);
	default:
		return 0;
	}
}
