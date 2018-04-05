/*
 * ZII SCU2/3/4 board driver
 *
 * Copyright (c) 2012, 2014 Guenter Roeck <linux@roeck-us.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/gpio/machine.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mdio-gpio.h>
#include <linux/module.h>
#include <linux/platform_data/mv88e6xxx.h>
#include <linux/platform_device.h>
#include <linux/phy.h>
#include <net/dsa.h>

static struct gpiod_lookup_table zii_scu_mdio_gpiod_table = {
	.dev_id = "mdio-gpio.0",
	.table = {
		GPIO_LOOKUP_IDX("gpio_ich", 17, NULL, MDIO_GPIO_MDC,
				GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("gpio_ich", 2, NULL, MDIO_GPIO_MDIO,
				GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("gpio_ich", 21, NULL, MDIO_GPIO_MDO,
				GPIO_ACTIVE_LOW),
	},
};

static struct gpiod_lookup_table zii_scu_gpio_table = {
	.dev_id = "gpio-0",
	.table = {
		GPIO_LOOKUP_IDX("gpio_ich", 20, "reset", 0, GPIO_ACTIVE_LOW),
	},
};

static struct dsa_mv88e6xxx_pdata dsa_mv88e6xxx_pdata = {
	.cd = {
		.port_names[0] = "cpu",
		.port_names[1] = "port1",
		.port_names[2] = "port2",
		.port_names[3] = "port3",
		.port_names[4] = "host2esb",
		.port_names[5] = NULL,
	},
	.compatible = "marvell,mv88e6085",
	.enabled_ports = 0x1f,
	.eeprom_len = 512,
	.parent = "0000:00:19.0",
};

static const struct mdio_board_info bdinfo = {
	.bus_id = "gpio-0",
	.modalias = "mv88e6085",
	.mdio_addr = 0,
	.platform_data = &dsa_mv88e6xxx_pdata,
};

static int zii_scu_i2c_adap_name_match(struct device *dev, void *data)
{
	struct i2c_adapter *adap = i2c_verify_adapter(dev);

	if (!adap)
		return false;

	pr_info("%s %s\n", adap->name, (char *)data);

	return !strcmp(adap->name, (char *)data);
}

static struct i2c_adapter *zii_scu_find_i2c_adapter(char *name)
{
	struct device *dev;
	struct i2c_adapter *adap;

	dev = bus_find_device(&i2c_bus_type, NULL, name,
			      zii_scu_i2c_adap_name_match);
	if (!dev)
		return NULL;

	adap = i2c_verify_adapter(dev);
	if (!adap)
		put_device(dev);

	return adap;
}

static int __init zii_scu_mdio_init(void)
{
	struct platform_device *mdio_pdev;

	mdio_pdev = platform_device_register_data(&platform_bus,
						 "mdio-gpio", 0, NULL, 0);
	if (IS_ERR(mdio_pdev)) {
		pr_err("Failed to register MDIO device\n");
		return PTR_ERR(mdio_pdev);
	}

	return 0;
}

static struct i2c_board_info scu_i2c_info_common[] = {
	/* On Main Board */
	{ I2C_BOARD_INFO("zii_scu_pic", 0x20)},			/* SCU PIC */
};

static int __init zii_scu_add_i2c_devices(struct i2c_adapter *adapter,
					  struct i2c_board_info *info,
					  int count)
{
	struct i2c_client *client;
	int i;

	for (i = 0; i < count; i++) {
		client = i2c_new_device(adapter, info);
		if (!client)
			/*
			 * Unfortunately this call does not tell us
			 * why it failed. Pick the most likely reason.
			 */
			return -EBUSY;
		info++;
	}
	return 0;
}

static int __init zii_scu_init(void)
{
	struct i2c_adapter *adapter;
	int err;

	adapter = zii_scu_find_i2c_adapter("i2c-kempld");
	if (!adapter)
		return -EPROBE_DEFER;

	err = zii_scu_add_i2c_devices(adapter, scu_i2c_info_common,
				      ARRAY_SIZE(scu_i2c_info_common));
	if (err)
		return err;

	err = mdiobus_register_board_info(&bdinfo, 1);
	if (err) {
		pr_err("Error setting up MDIO board info\n");
		return err;
	}

	gpiod_add_lookup_table(&zii_scu_mdio_gpiod_table);
	gpiod_add_lookup_table(&zii_scu_gpio_table);

	err = zii_scu_mdio_init();
	if (err) {
		pr_err("Error setting up MDIO bit banging\n");
		goto out_lookup_table;
	}

	return 0;

out_lookup_table:
	gpiod_remove_lookup_table(&zii_scu_gpio_table);
	gpiod_remove_lookup_table(&zii_scu_mdio_gpiod_table);

	return err;
}

module_init(zii_scu_init);
MODULE_LICENSE("GPL");
