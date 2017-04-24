/*
 * Marvell 88E6xxx Switch Global 2 Scratch & Misc Registers support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2017 National Instruments
 *      Brandon Streiff <brandon.streiff@ni.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "chip.h"
#include "global2.h"

/* Offset 0x1A: Scratch and Misc. Register */
static int mv88e6xxx_g2_scratch_read(struct mv88e6xxx_chip *chip, int reg,
				     u8 *data)
{
	u16 value;
	int err;

	err = mv88e6xxx_g2_write(chip, MV88E6XXX_G2_SCRATCH_MISC_MISC,
				 reg << 8);
	if (err)
		return err;

	err = mv88e6xxx_g2_read(chip, MV88E6XXX_G2_SCRATCH_MISC_MISC, &value);
	if (err)
		return err;

	*data = (value & MV88E6XXX_G2_SCRATCH_MISC_DATA_MASK);

	return 0;
}

static int mv88e6xxx_g2_scratch_write(struct mv88e6xxx_chip *chip, int reg,
				      u8 data)
{
	u16 value = (reg << 8) | data;

	return mv88e6xxx_g2_update(chip, MV88E6XXX_G2_SCRATCH_MISC_MISC, value);
}
