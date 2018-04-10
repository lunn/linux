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

#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/mdio-gpio.h>
#include <linux/module.h>
#include <linux/platform_data/mv88e6xxx.h>
#include <linux/platform_data/pca953x.h>
#include <linux/platform_device.h>
#include <linux/phy.h>
#include <net/dsa.h>

struct zii_scu_data {
	struct platform_device *pca_x71_leds_pdev;
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

static const struct property_entry zii_scu_at24c08_properties[] = {
	PROPERTY_ENTRY_U32("pagesize", 16),
	PROPERTY_ENTRY_U32("size", 1024),
	{ },
};

static const struct property_entry zii_scu_at24c04_properties[] = {
	PROPERTY_ENTRY_U32("pagesize", 16),
	PROPERTY_ENTRY_U32("size", 512),
	{ },
};

static struct gpio_led pca_x71_gpio_leds[] = {
	{			/* bit 0 */
		.name = "scu_status:g:RD",
		.active_low = 1,
		.default_trigger = "heartbeat",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 1 */
		.name = "scu_status:a:WLess",
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 2 */
		.name = "scu_status:r:LDFail",
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 3 */
		.name = "scu_status:a:SW",
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
};

static struct gpio_led_platform_data pca_x71_gpio_led = {
	.leds = pca_x71_gpio_leds,
	.num_leds = ARRAY_SIZE(pca_x71_gpio_leds),
};

static int pca9538_x71_setup(struct i2c_client *client,
			     unsigned gpio_base, unsigned ngpio, void *context)
{
	struct zii_scu_data *data = context;
	struct device *dev = &client->dev;

	pca_x71_gpio_leds[0].gpiod = gpio_to_desc(gpio_base + 0);
	pca_x71_gpio_leds[1].gpiod = gpio_to_desc(gpio_base + 1);
	pca_x71_gpio_leds[2].gpiod = gpio_to_desc(gpio_base + 2);
	pca_x71_gpio_leds[3].gpiod = gpio_to_desc(gpio_base + 3);

	data->pca_x71_leds_pdev =
		platform_device_register_data(dev, "leds-gpio", 1,
					      &pca_x71_gpio_led,
					      sizeof(pca_x71_gpio_led));
	return 0;
}

static int pca9538_x71_teardown(struct i2c_client *client,
				unsigned gpio_base, unsigned ngpio,
				void *context)
{
	struct zii_scu_data *data = context;
	struct device *dev = &client->dev;

	platform_device_unregister(data->pca_x71_leds_pdev);
}

static const char *pca9538_x71_gpio_names[8] = {
	"pca9538_ext1:rd_led_on",
	"pca9538_ext1:wless_led_on",
	"pca9538_ext1:ld_fail_led_on",
	"pca9538_ext1:sw_led_on",
	"pca9538_ext1:discrete_out_1",
	"pca9538_ext1:discrete_out_2",
	"pca9538_ext1:discrete_out_3",
	"pca9538_ext1:discrete_out_4",
};

static struct pca953x_platform_data pca953x_x71 = {
	.gpio_base = -1,
	.irq_base = -1,
	.setup = pca9538_x71_setup,
	.teardown = pca9538_x71_teardown,
	.names = pca9538_x71_gpio_names,
};

static struct i2c_board_info scu_i2c_info_common[] = {
	/* On Main Board */
	{ I2C_BOARD_INFO("zii_scu_pic", 0x20)},	/* SCU PIC */
	{ I2C_BOARD_INFO("at24", 0x54),         /* Nameplate EEPROM */
	  .properties = zii_scu_at24c08_properties},
	{ I2C_BOARD_INFO("at24", 0x52),         /* Nameplate EEPROM */
	  .properties = zii_scu_at24c04_properties},
	{ I2C_BOARD_INFO("ds1682", 0x6b)},	/* Elapsed Time Counter */
	{ I2C_BOARD_INFO("pca9538", 0x71),	/* LEDs + Output Discretes */
	  .platform_data = &pca953x_x71},
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
	struct zii_scu_data * data;
	struct i2c_adapter *adapter;
	int err;

	data = kzalloc(sizeof(struct zii_scu_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	adapter = zii_scu_find_i2c_adapter("i2c-kempld");
	if (!adapter)
		return -EPROBE_DEFER;

	pca953x_x71.context = data;

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
