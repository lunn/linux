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

/**
 * mv88e6xxx_g2_gpio_set_ext_smi - set gpio muxing for external smi
 * @chip: chip private data
 * @external: set mux for external smi, or free for other gpio
 *
 * Some mv88e6xxx models have GPIO pins that may be configured as
 * an external SMI interface, or they may be made free for other
 * GPIO uses.
 */
int mv88e6xxx_g2_scratch_gpio_set_ext_smi(struct mv88e6xxx_chip *chip,
					  bool external)
{
	int data1_reg = MV88E6352_G2_SCRATCH_CONFIG_DATA1;
	int data2_reg = MV88E6352_G2_SCRATCH_CONFIG_DATA2;
	int smi_reg = MV88E6352_G2_SCRATCH_MISC_CFG;
	bool no_cpu;
	u8 p0_mode;
	int err;
	u8 val;

	err = mv88e6xxx_g2_scratch_read(chip, data2_reg, &val);
	if (err)
		return err;

	p0_mode = val & MV88E6352_G2_SCRATCH_CONFIG_DATA2_P0_MODE_MASK;

	if (p0_mode == 0x01 || p0_mode == 0x02)
		return -EBUSY;

	err = mv88e6xxx_g2_scratch_read(chip, data1_reg, &val);
	if (err)
		return err;

	no_cpu = !!(val & MV88E6352_G2_SCRATCH_CONFIG_DATA1_NO_CPU);

	err = mv88e6xxx_g2_scratch_read(chip, smi_reg, &val);
	if (err)
		return err;

	/* NO_CPU being 0 inverts the meaning of the bit */
	if (!no_cpu)
		external = !external;

	if (external)
		val |= MV88E6352_G2_SCRATCH_MISC_CFG_NORMALSMI;
	else
		val &= ~MV88E6352_G2_SCRATCH_MISC_CFG_NORMALSMI;

	return mv88e6xxx_g2_scratch_write(chip, smi_reg, val);
}
