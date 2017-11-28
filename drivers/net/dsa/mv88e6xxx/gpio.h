/*
 * Marvell 88E6xxx Switch GPIO Controller support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2017 National Instruments
 *	Brandon Streiff <brandon.streiff@ni.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _MV88E6XXX_GPIO_H
#define _MV88E6XXX_GPIO_H

#include "chip.h"

#define MV88E6341_NUM_GPIO	11
#define MV88E6320_NUM_GPIO	15
#define MV88E6352_NUM_GPIO	15
#define MV88E6390_NUM_GPIO	16

struct mv88e6xxx_pin_group;
struct mv88e6xxx_pinmux_function_info;
struct mv88e6xxx_pinmux_function;

/**
 * struct mv88e6xxx_pinctrl_info
 * @groups: pingroups
 * @ngroups: number of @groups
 * @funcs: pinmux functions
 * @nfuncs: number of @funcs
 * @ext_smi_reg: register containing the NormalSMI bit
 */
struct mv88e6xxx_pinctrl_info {
	const struct mv88e6xxx_pin_group *groups;
	unsigned ngroups;
	const struct mv88e6xxx_pinmux_function_info *funcs;
	unsigned nfuncs;
	u8 ext_smi_reg;
};

/**
 * struct mv88e6xxx_pinctrl - driver data
 * @pctrl: pinctrl device
 */
//struct mv88e6xxx_pinctrl {
	//struct pinctrl_dev *pctldev;
//};

enum mv88e6xxx_pinmux_type {
	MV88E6XXX_PINMUX_TYPE_CMODE,
	MV88E6XXX_PINMUX_TYPE_EXT_SMI,
	MV88E6XXX_PINMUX_TYPE_GPIO,
};

/**
 * struct mv88e6xxx_pin_group
 * @name: group name
 * @pins: pins used by this group
 * @npins: number of @pins
 */
struct mv88e6xxx_pin_group {
	const char *name;
	const unsigned *pins;
	unsigned npins;
};

/**
 * struct mv88e6xxx_pinmux_function_info
 * @name: function name
 */
struct mv88e6xxx_pinmux_function_info {
	const char *name;
	const char * const *groups;
	unsigned ngroups;

	enum mv88e6xxx_pinmux_type type;
	int value;
};

int mv88e6xxx_gpio_setup(struct mv88e6xxx_chip *chip);

extern const struct mv88e6xxx_pinctrl_info mv88e6341_pinctrl_info;
extern const struct mv88e6xxx_pinctrl_info mv88e6320_pinctrl_info;
extern const struct mv88e6xxx_pinctrl_info mv88e6352_pinctrl_info;
extern const struct mv88e6xxx_pinctrl_info mv88e6390_pinctrl_info;

#endif /* _MV88E6XXX_GPIO_H */
