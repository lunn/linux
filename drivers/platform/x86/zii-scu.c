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

#include <linux/dmi.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/mdio-gpio.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_data/b53.h>
#include <linux/platform_data/mv88e6xxx.h>
#include <linux/platform_data/pca953x.h>
#include <linux/platform_device.h>
#include <linux/phy.h>
#include <linux/spi/spi.h>
#include <net/dsa.h>

struct zii_scu_data {
	struct platform_device *pdev;
};

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
};

static const struct mdio_board_info bdinfo = {
	.bus_id = "gpio-0",
	.modalias = "mv88e6085",
	.mdio_addr = 0,
	.platform_data = &dsa_mv88e6xxx_pdata,
};

static int zii_scu_mdio_init(void)
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

static int zii_scu_marvell_switch(struct zii_scu_data *data)
{
	struct device *dev = &data->pdev->dev;
	int err;

	dsa_mv88e6xxx_pdata.netdev = dev_get_by_name(&init_net, "eno1");
	if (!dsa_mv88e6xxx_pdata.netdev) {
		pr_err("Error finding Ethernet device\n");
		return -ENODEV;
	}

	err = mdiobus_register_board_info(&bdinfo, 1);
	if (err) {
		dev_err(dev, "Error setting up MDIO board info\n");
		return err;
	}

	gpiod_add_lookup_table(&zii_scu_mdio_gpiod_table);

	err = zii_scu_mdio_init();
	if (err)
		dev_err(dev, "Error setting up MDIO bit banging\n");

	return err;
}

static int zii_scu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct zii_scu_data * data;

	data = devm_kzalloc(dev, sizeof(struct zii_scu_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->pdev = pdev;


	gpiod_add_lookup_table(&zii_scu_gpio_table);

	return zii_scu_marvell_switch(data);
}

static struct platform_driver zii_scu_driver = {
	.probe = zii_scu_probe,
	.driver = {
		.name = "zii_scu",
		.owner = THIS_MODULE,
	},
};

static int zii_scu_create_platform_device(const struct dmi_system_id *id)
{
	static struct platform_device *zii_scu_pdev;
	int ret;

	zii_scu_pdev = platform_device_alloc("zii_scu", -1);
	if (!zii_scu_pdev)
		return -ENOMEM;

	ret = platform_device_add(zii_scu_pdev);
	if (ret)
		goto err;

	return 0;
err:
	platform_device_put(zii_scu_pdev);
	return ret;
}

static const struct dmi_system_id zii_scu_device_table[] __initconst = {
	{
		.ident = "IMS SCU version 1, Core 2 Duo",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "PXT"),
		},
		.callback = zii_scu_create_platform_device,
	},
	{
		.ident = "IMS SCU version 2, Ivy Bridge",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-bIP2"),
		},
		.callback = zii_scu_create_platform_device,
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, zii_scu_device_table);

static int __init zii_scu_init(void)
{
	if (!dmi_check_system(zii_scu_device_table))
		return -ENODEV;

	return platform_driver_register(&zii_scu_driver);
}

module_init(zii_scu_init);
MODULE_LICENSE("GPL");
