/*
 * Marvell 88E6xxx Switch GPIO Controller Support
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
#include "gpio.h"
#include "port.h"
#include <linux/pinctrl/pinmux.h>
#include "../../../pinctrl/pinctrl-utils.h"

/* TODO I have a lot of stubs that need cleaning... */
#pragma GCC diagnostic ignored "-Wunused-variable"

/*
 * The register interface supports up to 16 GPIOs.
 * Some implementations may have fewer GPIOs than this.
 */
static const struct pinctrl_pin_desc mv88e6xxx_pin_descs[] = {
	PINCTRL_PIN(0,  "gpio0"),
	PINCTRL_PIN(1,  "gpio1"),
	PINCTRL_PIN(2,  "gpio2"),
	PINCTRL_PIN(3,  "gpio3"),
	PINCTRL_PIN(4,  "gpio4"),
	PINCTRL_PIN(5,  "gpio5"),
	PINCTRL_PIN(6,  "gpio6"),
	PINCTRL_PIN(7,  "gpio7"),
	PINCTRL_PIN(8,  "gpio8"),
	PINCTRL_PIN(9,  "gpio9"),
	PINCTRL_PIN(10, "gpio10"),
	PINCTRL_PIN(11, "gpio11"),
	PINCTRL_PIN(12, "gpio12"),
	PINCTRL_PIN(13, "gpio13"),
	PINCTRL_PIN(14, "gpio14"),
	PINCTRL_PIN(15, "gpio15"),
};

static const unsigned mv88e6xxx_pin_numbers[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};

static const char * const mv88e6xxx_gpio_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
	"gpio4", "gpio5", "gpio6", "gpio7",
	"gpio8", "gpio9", "gpio10", "gpio11",
	"gpio12", "gpio13", "gpio14", "gpio15"
};

#define DEFINE_MV88E6XXX_GPIO_PIN_GRP(idx) \
	{ \
		.name = "gpio" #idx, \
		.pins = &(mv88e6xxx_pin_numbers[idx]), \
		.npins = 1, \
	}

#define DEFINE_MV88E6XXX_PIN_GRP(model, nm) \
	{ \
		.name = #nm, \
		.pins = model ## _pins_ ## nm, \
		.npins = ARRAY_SIZE(model ## _pins_ ## nm), \
	}

#define DEFINE_MV88E6XXX_CMODE_FUNC(model, nm) \
	{ \
		.name = #nm, \
		.groups = model ## _ ## nm ## _groups, \
		.ngroups = ARRAY_SIZE(model ## _ ## nm ## _groups), \
		.type = MV88E6XXX_PINMUX_TYPE_CMODE, \
	}

#define DEFINE_MV88E6XXX_EXT_SMI_FUNC(model, nm) \
	{ \
		.name = #nm, \
		.groups = model ## _ ## nm ## _groups, \
		.ngroups = ARRAY_SIZE(model ## _ ## nm ## _groups), \
		.type = MV88E6XXX_PINMUX_TYPE_EXT_SMI, \
	}

#define DEFINE_MV88E6XXX_GPIO_FUNC(nm, v) \
	{ \
		.name = #nm, \
		.groups = NULL, \
		.ngroups = 0, \
		.type = MV88E6XXX_PINMUX_TYPE_GPIO, \
		.value = v, \
	}

/* MV88E6XXX_FAMILY_6341: 6141 6341 */
static const unsigned mv88e6341_pins_p0_fd_mii[] = {0, 1, 2, 3, 4, 5, 6};
static const unsigned mv88e6341_pins_p0_mii[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
static const unsigned mv88e6341_pins_p0_rmii[] = {0, 2, 3, 4};
static const unsigned mv88e6341_pins_p0_rgmii[] = {0, 1, 2, 3, 4, 5, 6};
static const unsigned mv88e6341_pins_ext_smi[] = {7, 8};
static const unsigned mv88e6341_pins_i2c0[] = {7, 8};
static const unsigned mv88e6341_pins_i2c1[] = {9, 10};

static const struct mv88e6xxx_pin_group mv88e6341_pin_groups[] = {
	DEFINE_MV88E6XXX_PIN_GRP(mv88e6341, p0_fd_mii),
	DEFINE_MV88E6XXX_PIN_GRP(mv88e6341, p0_mii),
	DEFINE_MV88E6XXX_PIN_GRP(mv88e6341, p0_rmii),
	DEFINE_MV88E6XXX_PIN_GRP(mv88e6341, p0_rgmii),
	DEFINE_MV88E6XXX_PIN_GRP(mv88e6341, ext_smi),
	DEFINE_MV88E6XXX_PIN_GRP(mv88e6341, i2c0),
	DEFINE_MV88E6XXX_PIN_GRP(mv88e6341, i2c1),
};

static const char * const mv88e6341_p0_groups[] = {
	"p0_fd_mii", "p0_mii", "p0_rmii", "p0_rgmii"
};
static const char * const mv88e6341_ext_smi_groups[] = {"ext_smi"};
static const char * const mv88e6341_i2c_groups[] = {"i2c0", "i2c1"};

static const struct mv88e6xxx_pinmux_function_info mv88e6341_pmux_funcs[] = {
	DEFINE_MV88E6XXX_GPIO_FUNC(ptp_trig, 1),
	DEFINE_MV88E6XXX_GPIO_FUNC(ptp_evreq, 2),
	DEFINE_MV88E6XXX_GPIO_FUNC(ptp_extclk, 3),
	DEFINE_MV88E6XXX_GPIO_FUNC(rx_clk0, 4),
	DEFINE_MV88E6XXX_GPIO_FUNC(rx_clk1, 5),
	DEFINE_MV88E6XXX_CMODE_FUNC(mv88e6341, p0),
	DEFINE_MV88E6XXX_EXT_SMI_FUNC(mv88e6341, ext_smi),
/*	DEFINE_MV88E6XXX_FUNC_GRP(mv88e6341, i2c), */
};

const struct mv88e6xxx_pinctrl_info mv88e6341_pinctrl_info = {
	.groups = mv88e6341_pin_groups,
	.ngroups = ARRAY_SIZE(mv88e6341_pin_groups),
	.funcs = mv88e6341_pmux_funcs,
	.nfuncs = ARRAY_SIZE(mv88e6341_pmux_funcs),
	.ext_smi_reg = 0x02,
};

/* MV88E6XXX_FAMILY_6320: 6320 6321 */
static const unsigned mv88e6320_pins_p5_fd_mii[] = {0, 1, 2, 5, 6};
static const unsigned mv88e6320_pins_p5_mii[] = {0, 1, 2, 5, 6, 7, 8};
static const unsigned mv88e6320_pins_p5_rmii[] = {0, 2};
static const unsigned mv88e6320_pins_p5_rgmii[] = {7, 8};
static const unsigned mv88e6320_pins_p0_fd_mii[] = {9, 10, 11, 12, 13, 14};
static const unsigned mv88e6320_pins_p0_mii[] = {9, 10};
static const unsigned mv88e6320_pins_p0_gmii[] = {9, 10, 11, 12, 13, 14};

static const char * const mv88e6320_p5_groups[] = {
	"p5_fd_mii", "p5_mii", "p5_rmii", "p5_rgmii"
};
static const char * const mv88e6320_p0_groups[] = {
	"p0_fd_mii", "p0_mii", "p0_gmii"
};

static const struct mv88e6xxx_pin_group mv88e6320_pin_groups[] = {
	DEFINE_MV88E6XXX_PIN_GRP(mv88e6320, p5_fd_mii),
	DEFINE_MV88E6XXX_PIN_GRP(mv88e6320, p5_mii),
	DEFINE_MV88E6XXX_PIN_GRP(mv88e6320, p5_rmii),
	DEFINE_MV88E6XXX_PIN_GRP(mv88e6320, p5_rgmii),
	DEFINE_MV88E6XXX_PIN_GRP(mv88e6320, p0_fd_mii),
	DEFINE_MV88E6XXX_PIN_GRP(mv88e6320, p0_mii),
	DEFINE_MV88E6XXX_PIN_GRP(mv88e6320, p0_gmii),
};

/* TODO: 6320 doesn't have rxclk0/rxclk1, only 6321 does */
static const struct mv88e6xxx_pinmux_function_info mv88e6320_pmux_funcs[] = {
	DEFINE_MV88E6XXX_GPIO_FUNC(ptp_trig, 1),
	DEFINE_MV88E6XXX_GPIO_FUNC(ptp_evreq, 2),
	DEFINE_MV88E6XXX_GPIO_FUNC(ptp_extclk, 3),
	DEFINE_MV88E6XXX_GPIO_FUNC(rx_clk0, 4),
	DEFINE_MV88E6XXX_GPIO_FUNC(rx_clk1, 5),
	DEFINE_MV88E6XXX_GPIO_FUNC(clk125, 7),
	DEFINE_MV88E6XXX_CMODE_FUNC(mv88e6320, p0),
	DEFINE_MV88E6XXX_CMODE_FUNC(mv88e6320, p5),
};

const struct mv88e6xxx_pinctrl_info mv88e6320_pinctrl_info = {
	.groups = mv88e6320_pin_groups,
	.ngroups = ARRAY_SIZE(mv88e6320_pin_groups),
	.funcs = mv88e6320_pmux_funcs,
	.nfuncs = ARRAY_SIZE(mv88e6320_pmux_funcs),
	.ext_smi_reg = 0x63,
};

/* MV88E6XXX_FAMILY_6352: 6172 6176 6240 6352 */
static const unsigned mv88e6352_pins_p5_mii[] = {8, 9, 10, 11, 14};
static const unsigned mv88e6352_pins_p5_rmii[] = {10, 11, 12, 14};
static const unsigned mv88e6352_pins_p5_rgmii[] = {8, 9, 10, 11, 14};
static const unsigned mv88e6352_pins_p6_mii[] = {1, 2};
static const unsigned mv88e6352_pins_p6_gmii[] = {1, 2, 3, 4, 5, 6};

static const struct mv88e6xxx_pin_group mv88e6352_pin_groups[] = {
	DEFINE_MV88E6XXX_PIN_GRP(mv88e6352, p5_mii),
	DEFINE_MV88E6XXX_PIN_GRP(mv88e6352, p5_rmii),
	DEFINE_MV88E6XXX_PIN_GRP(mv88e6352, p5_rgmii),
	DEFINE_MV88E6XXX_PIN_GRP(mv88e6352, p6_mii),
	DEFINE_MV88E6XXX_PIN_GRP(mv88e6352, p6_gmii),
};

static const char * const mv88e6352_p5_groups[] = {
	"p5_mii", "p5_rmii", "p5_rgmii"
};
static const char * const mv88e6352_p6_groups[] = {
	"p6_mii", "p6_gmii"
};

static const struct mv88e6xxx_pinmux_function_info mv88e6352_pmux_funcs[] = {
	DEFINE_MV88E6XXX_GPIO_FUNC(ptp_trig, 1),
	DEFINE_MV88E6XXX_GPIO_FUNC(ptp_evreq, 2),
	DEFINE_MV88E6XXX_GPIO_FUNC(ptp_extclk, 3),
	DEFINE_MV88E6XXX_GPIO_FUNC(rx_clk0, 4),
	DEFINE_MV88E6XXX_GPIO_FUNC(rx_clk1, 5),
	DEFINE_MV88E6XXX_GPIO_FUNC(clk125, 7),
	DEFINE_MV88E6XXX_CMODE_FUNC(mv88e6352, p5),
	DEFINE_MV88E6XXX_CMODE_FUNC(mv88e6352, p6),
};

const struct mv88e6xxx_pinctrl_info mv88e6352_pinctrl_info = {
	.groups = mv88e6352_pin_groups,
	.ngroups = ARRAY_SIZE(mv88e6352_pin_groups),
	.funcs = mv88e6352_pmux_funcs,
	.nfuncs = ARRAY_SIZE(mv88e6352_pmux_funcs),
	.ext_smi_reg = -1,
};

/* MV88E6XXX_FAMILY_6390: 6190 6190X 6191 6290 6390 6390X */
static const unsigned mv88e6390_pins_p0_fd_mii[] = {0, 1, 2, 3, 4, 5, 6};
static const unsigned mv88e6390_pins_p0_mii[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
static const unsigned mv88e6390_pins_p0_rmii[] = {0, 2, 3, 4};
static const unsigned mv88e6390_pins_ext_smi[] = {7, 8};

static const struct mv88e6xxx_pin_group mv88e6390_pin_groups[] = {
	DEFINE_MV88E6XXX_PIN_GRP(mv88e6390, p0_fd_mii),
	DEFINE_MV88E6XXX_PIN_GRP(mv88e6390, p0_mii),
	DEFINE_MV88E6XXX_PIN_GRP(mv88e6390, p0_rmii),
	DEFINE_MV88E6XXX_PIN_GRP(mv88e6390, ext_smi),
};

static const char * const mv88e6390_p0_groups[] = {
	"p0_fd_mii", "p0_mii", "p0_rmii",
};
static const char * const mv88e6390_ext_smi_groups[] = {"ext_smi"};

static const struct mv88e6xxx_pinmux_function_info mv88e6390_pmux_funcs[] = {
	DEFINE_MV88E6XXX_GPIO_FUNC(ptp_trig, 1),
	DEFINE_MV88E6XXX_GPIO_FUNC(ptp_evreq, 2),
	DEFINE_MV88E6XXX_GPIO_FUNC(ptp_extclk, 3),
	DEFINE_MV88E6XXX_GPIO_FUNC(rx_clk0, 4),
	DEFINE_MV88E6XXX_GPIO_FUNC(rx_clk1, 5),
	DEFINE_MV88E6XXX_GPIO_FUNC(ptp_1pps, 6),
	DEFINE_MV88E6XXX_GPIO_FUNC(clk125, 7),
	DEFINE_MV88E6XXX_CMODE_FUNC(mv88e6390, p0),
	DEFINE_MV88E6XXX_EXT_SMI_FUNC(mv88e6390, ext_smi),
};

const struct mv88e6xxx_pinctrl_info mv88e6390_pinctrl_info = {
	.groups = mv88e6390_pin_groups,
	.ngroups = ARRAY_SIZE(mv88e6390_pin_groups),
	.funcs = mv88e6390_pmux_funcs,
	.nfuncs = ARRAY_SIZE(mv88e6390_pmux_funcs),
	.ext_smi_reg = 0x02,
};

/* function groups */

static int mv88e6xxx_pctrl_get_groups_count(struct pinctrl_dev *pdev)
{
	struct mv88e6xxx_chip *chip = pinctrl_dev_get_drvdata(pdev);
	const struct mv88e6xxx_pinctrl_info *pinfo = chip->info->pinctrl_info;

	return pinfo->ngroups;
}

static const char *mv88e6xxx_pctrl_get_group_name(struct pinctrl_dev *pdev,
						  unsigned func_selector)
{
	struct mv88e6xxx_chip *chip = pinctrl_dev_get_drvdata(pdev);
	const struct mv88e6xxx_pinctrl_info *pinfo = chip->info->pinctrl_info;

	return pinfo->groups[func_selector].name;
}

static int mv88e6xxx_pctrl_get_group_pins(struct pinctrl_dev *pdev,
					  unsigned group_selector,
					  const unsigned **pins,
					  unsigned *num_pins)
{
	struct mv88e6xxx_chip *chip = pinctrl_dev_get_drvdata(pdev);
	const struct mv88e6xxx_pinctrl_info *pinfo = chip->info->pinctrl_info;

	*pins = pinfo->groups[group_selector].pins;
	*num_pins = pinfo->groups[group_selector].npins;

	return 0;
}

static const struct pinctrl_ops mv88e6xxx_pinctrl_ops = {
	.get_groups_count = mv88e6xxx_pctrl_get_groups_count,
	.get_group_name = mv88e6xxx_pctrl_get_group_name,
	.get_group_pins = mv88e6xxx_pctrl_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_group,
	.dt_free_map = pinctrl_utils_free_map,
};

static int mv88e6xxx_pmux_request(struct pinctrl_dev *pdev,
				  unsigned offset)
{
	struct mv88e6xxx_chip *chip = pinctrl_dev_get_drvdata(pdev);
	const struct mv88e6xxx_pinctrl_info *pinfo = chip->info->pinctrl_info;

	/* TODO: something goes here */

	return 0;
}

static int mv88e6xxx_pmux_get_functions_count(struct pinctrl_dev *pdev)
{
	struct mv88e6xxx_chip *chip = pinctrl_dev_get_drvdata(pdev);
	const struct mv88e6xxx_pinctrl_info *pinfo = chip->info->pinctrl_info;

	return pinfo->nfuncs;
}

static const char *mv88e6xxx_pmux_get_function_name(struct pinctrl_dev *pdev,
						    unsigned func_selector)
{
	struct mv88e6xxx_chip *chip = pinctrl_dev_get_drvdata(pdev);
	const struct mv88e6xxx_pinctrl_info *pinfo = chip->info->pinctrl_info;

	return pinfo->funcs[func_selector].name;
}

static int mv88e6xxx_pmux_get_function_groups(struct pinctrl_dev *pdev,
					      unsigned func_selector,
					      const char * const **groups,
					      unsigned *num_groups)
{
	struct mv88e6xxx_chip *chip = pinctrl_dev_get_drvdata(pdev);
	const struct mv88e6xxx_pinctrl_info *pinfo = chip->info->pinctrl_info;

	if (pinfo->funcs[func_selector].groups) {
		*groups = pinfo->funcs[func_selector].groups;
		*num_groups = pinfo->funcs[func_selector].ngroups;
	} else {
		/* if groups == NULL, then this function can be on any GPIO */
		*groups = mv88e6xxx_gpio_groups;
		*num_groups = mv88e6xxx_num_gpio(chip);
	}

	return 0;
}

static int mv88e6xxx_pmux_set_mux(struct pinctrl_dev *pdev,
				  unsigned func_selector,
				  unsigned group_selector)
{
	struct mv88e6xxx_chip *chip = pinctrl_dev_get_drvdata(pdev);
	const struct mv88e6xxx_pinctrl_info *pinfo = chip->info->pinctrl_info;
	const struct mv88e6xxx_pin_group *pgrp =
		&pinfo->groups[group_selector];
	const struct mv88e6xxx_pinmux_function_info *func =
		&pinfo->funcs[func_selector];
	unsigned int ctrl_value = 0;
	int ret;

	/* TODO this needs to call into the gpio ops */

	return 0;
}

static int mv88e6xxx_pmux_gpio_request_enable(struct pinctrl_dev *pdev,
					      struct pinctrl_gpio_range *range,
					      u32 pin)
{
	struct mv88e6xxx_chip *chip = pinctrl_dev_get_drvdata(pdev);
	const struct mv88e6xxx_pinctrl_info *pinfo = chip->info->pinctrl_info;

	/* TODO anything to do here? */

	return 0;
}

static int mv88e6xxx_pmux_gpio_set_direction(struct pinctrl_dev *pctldev,
					     struct pinctrl_gpio_range *range,
					     unsigned pin, bool input)
{
	struct gpio_chip *chip = range->gc;

/*	if (input)
		mv88e6xxx_gpio_direction_input(chip, pin);
	else
		mv88e6xxx_gpio_direction_output(chip, pin, 0);*/

	return 0;
}

static const struct pinmux_ops mv88e6xxx_pinmux_ops = {
	.request = mv88e6xxx_pmux_request,
	.get_functions_count = mv88e6xxx_pmux_get_functions_count,
	.get_function_name = mv88e6xxx_pmux_get_function_name,
	.get_function_groups = mv88e6xxx_pmux_get_function_groups,
	.set_mux = mv88e6xxx_pmux_set_mux,
	.gpio_request_enable = mv88e6xxx_pmux_gpio_request_enable,
	.gpio_set_direction = mv88e6xxx_pmux_gpio_set_direction,
};


/**
 * Translate the cmode value to the string identifier used
 * for pin groups.
 */
static inline const char *mv88e6xxx_cmode_name(u8 cmode)
{
	switch (cmode) {
	case MV88E6XXX_PORT_STS_CMODE_FD_MII:
		return "fd_mii";
	case MV88E6XXX_PORT_STS_CMODE_MII_PHY:
	case MV88E6XXX_PORT_STS_CMODE_MII_MAC:
		return "mii";
	case MV88E6XXX_PORT_STS_CMODE_GMII:
		return "gmii";
	case MV88E6XXX_PORT_STS_CMODE_RMII_PHY:
	case MV88E6XXX_PORT_STS_CMODE_RMII_MAC:
		return "rmii";
	case MV88E6XXX_PORT_STS_CMODE_RGMII:
		return "rgmii";
	default:
		return NULL;
	};
}

/**
 * Initialize the pinctrl state for relevant pins based on the
 * cmode setting for a port.
 */
int mv88e6xxx_pinctrl_request_port(struct mv88e6xxx_chip *chip,
				   int port)
{
	const char *cmode_name;
	struct pinctrl *pinctrl;
	struct pinctrl_state *state;
	int ret;
	char state_name[24] = {0};
	u8 cmode;

	ret = mv88e6xxx_port_get_cmode(chip, port, &cmode);
	if (ret < 0)
		return ret;

	cmode_name = mv88e6xxx_cmode_name(cmode);
	if (!cmode_name) {
		/* if not found, assume that this is a cmode that can't be
		 * pinmuxed and therefore we don't need to reserve anything.
		 */
		return 0;
	}

	snprintf(state_name, sizeof(state_name), "p%d_%s", port, cmode_name);

	pinctrl = pinctrl_get(chip->dev);
	if (IS_ERR(pinctrl)) {
		return PTR_ERR(pinctrl);
		return 0;
	}

	state = pinctrl_lookup_state(pinctrl, state_name);
	if (IS_ERR(state)) {
		/* if not found, then this chipset doesn't have this port
		 * pinmuxed with anything else, so we don't need to reserve
		 * anything.
		 */
		pinctrl_put(pinctrl);
		return 0;
	}

	ret = pinctrl_select_state(pinctrl, state);
	if (ret < 0) {
		dev_warn(chip->dev, "couldn't reserve cmode-directed pins for %s",
			 state_name);
		pinctrl_put(pinctrl);
		return ret;
	}

	return 0;
}


static int mv88e6xxx_gpio_get(struct gpio_chip *gc, unsigned pin)
{
	struct mv88e6xxx_chip *chip = gpiochip_get_data(gc);
	int ret;

	mutex_lock(&chip->reg_lock);
	ret = chip->info->ops->gpio_ops->get_data(chip, pin);
	mutex_unlock(&chip->reg_lock);

	return ret;
}

static void mv88e6xxx_gpio_set(struct gpio_chip *gc, unsigned pin,
			      int value)
{
	struct mv88e6xxx_chip *chip = gpiochip_get_data(gc);
	int ret;

	mutex_lock(&chip->reg_lock);
	ret = chip->info->ops->gpio_ops->set_data(chip, pin, value);
	mutex_unlock(&chip->reg_lock);

	if (ret < 0)
		dev_err(chip->dev, "couldn't set gpio %u", pin);
}

static int mv88e6xxx_gpio_direction_input(struct gpio_chip *gc,
					  unsigned pin)
{
	struct mv88e6xxx_chip *chip = gpiochip_get_data(gc);
	int err;

	mutex_lock(&chip->reg_lock);

	/* Check with the pinctrl driver to see if this is usable as input */
	err = pinctrl_gpio_direction_input(chip->gpio_chip.base + pin);
	if (err)
		goto unlock;

	err = chip->info->ops->gpio_ops->set_dir(chip, pin, true);

unlock:
	mutex_unlock(&chip->reg_lock);
	return err;
}

static int mv88e6xxx_gpio_direction_output(struct gpio_chip *gc,
					   unsigned pin, int value)
{
	struct mv88e6xxx_chip *chip = gpiochip_get_data(gc);
	int err;

	mutex_lock(&chip->reg_lock);

	err = pinctrl_gpio_direction_output(chip->gpio_chip.base + pin);
	if (err)
		goto unlock;

	err = chip->info->ops->gpio_ops->set_data(chip, pin, value);
	if (err)
		goto unlock;

	err = chip->info->ops->gpio_ops->set_dir(chip, pin, false);

unlock:
	mutex_unlock(&chip->reg_lock);
	return err;
}

static int mv88e6xxx_gpio_get_direction(struct gpio_chip *gc,
					unsigned pin)
{
	struct mv88e6xxx_chip *chip = gpiochip_get_data(gc);
	int ret;

	mutex_lock(&chip->reg_lock);
	ret = chip->info->ops->gpio_ops->get_dir(chip, pin);
	mutex_unlock(&chip->reg_lock);	

	return ret;
}

int mv88e6xxx_gpio_setup(struct mv88e6xxx_chip *chip)
{
	struct gpio_chip *gc = &chip->gpio_chip;
	int err;
	int i;

	if (!chip->info->ops->gpio_ops)
		return 0;

	chip->pinctrl_desc.name = "mv88e6xxx-pinctrl";
	chip->pinctrl_desc.owner = THIS_MODULE;
	chip->pinctrl_desc.pctlops = &mv88e6xxx_pinctrl_ops;
	chip->pinctrl_desc.pmxops = &mv88e6xxx_pinmux_ops;
	chip->pinctrl_desc.pins = mv88e6xxx_pin_descs;
	chip->pinctrl_desc.npins = mv88e6xxx_num_gpio(chip);

	err = pinctrl_register_and_init(&chip->pinctrl_desc,
					chip->dev, chip, &chip->pinctrl);
	if (err) {
		dev_err(chip->dev, "Failed to register pinctrl device");
		return err;
	}

	gc->parent = chip->dev;
	gc->label = dev_name(chip->dev);
	gc->base = -1;
	gc->ngpio = mv88e6xxx_num_gpio(chip);
	gc->owner = THIS_MODULE;
	gc->can_sleep = true;

	gc->request = gpiochip_generic_request;
	gc->free = gpiochip_generic_free;
	gc->get = mv88e6xxx_gpio_get;
	gc->set = mv88e6xxx_gpio_set;
	gc->direction_input = mv88e6xxx_gpio_direction_input;
	gc->direction_output = mv88e6xxx_gpio_direction_output;
	gc->get_direction = mv88e6xxx_gpio_get_direction;

	/* NOTE: gpiochip_add_data will call mv88e6xxx_gpio_get_direction,
	 * which will also acquire reg_lock. Therefore, we can't have
	 * reg_lock held. TODO fix this
	 */
	mutex_unlock(&chip->reg_lock);
	err = gpiochip_add_data(gc, chip);
	mutex_lock(&chip->reg_lock);
	if (err) {
		dev_err(chip->dev, "failed to add GPIO controller\n");
		return err;
	}

	/* TODO: replace with gpiochip_add_pin_range ? */

	chip->pinctrl_range.name = gc->label;
	chip->pinctrl_range.pin_base = 0;
	chip->pinctrl_range.base = gc->base;
	chip->pinctrl_range.npins = gc->ngpio;
	chip->pinctrl_range.gc = gc;

	pinctrl_add_gpio_range(chip->pinctrl, &chip->pinctrl_range);

	err = pinctrl_enable(chip->pinctrl);
	if (err) {
		dev_err(chip->dev, "couldn't enable pinctrl device");
		return err;
	}

#if 0
	/* TODO: The thing that I'm trying to do here is to use pinctrl
	 * to reserve the pins that are already in use by hardware for
	 * MII, RGMII, etc. However, this runs into a problem with this
	 * driver trying to be its own pinctrl client. */
	for (i = 0; i < mv88e6xxx_num_ports(chip); i++) {
		err = mv88e6xxx_pinctrl_request_port(chip, i);
		if (err)
			return err;
	}
#endif

	return 0;
}
