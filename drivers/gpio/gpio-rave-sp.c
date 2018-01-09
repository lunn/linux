// SPDX-License-Identifier: GPL-2.0+
/*
 * GPIO driver for RAVE SP
 *
 * Copyright (C) 2018 Zodiac Inflight Innovations
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/mfd/rave-sp.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>


struct rave_sp_gpio_port {
	struct gpio_chip gc;
	struct rave_sp *sp;
};

static int rave_sp_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct rave_sp_gpio_port *port = gpiochip_get_data(gc);
	u8 cmd[] = {
		[0] = RAVE_SP_CMD_GET_GPIO_STATE,
		[1] = 0,
	};
	u16 gpios[4];
	int ret;

	const unsigned int bank = gpio / 16;
	const unsigned int line = gpio % 16;

	ret = rave_sp_exec(port->sp, cmd, sizeof(cmd), gpios, sizeof(gpios));
	if (ret)
		return ret;

	return !!(gpios[bank] & BIT(line));
}

static void rave_sp_gpio_set(struct gpio_chip *gc, unsigned int gpio, int val)
{
}

static int rave_sp_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	return 0;
}

static int rave_sp_gpio_direction_output(struct gpio_chip *chip, unsigned gpio,
					 int value)
{
	return -ENOTSUPP;
}

static int rave_sp_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct rave_sp_gpio_port *port;
	struct gpio_chip *gc;

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->sp = dev_get_drvdata(dev->parent);
	gc = &port->gc;

	gc->of_node = np;
	gc->parent  = dev;
	gc->label   = "rave-sp-gpio";
	gc->ngpio   = 4 * 16;
	gc->base    = -1;

	gc->direction_input	= rave_sp_gpio_direction_input;
	gc->get			= rave_sp_gpio_get;
	gc->direction_output	= rave_sp_gpio_direction_output;
	gc->set			= rave_sp_gpio_set;

	return gpiochip_add_data(gc, port);
}

static const struct of_device_id rave_sp_gpio_dt_ids[] = {
	{ .compatible = "zii,rave-sp-gpio", },
	{ /* sentinel */ }
};

static struct platform_driver rave_sp_gpio_driver = {
	.probe		= rave_sp_gpio_probe,
	.driver		= {
		.name = KBUILD_MODNAME,
		.of_match_table = rave_sp_gpio_dt_ids,
	},
};
module_platform_driver(rave_sp_gpio_driver);
MODULE_DEVICE_TABLE(of, rave_sp_gpio_dt_ids);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrey Smirnov <andrew.smirnov@gmail.com>");
MODULE_DESCRIPTION("RAVE SP GPIO driver");
