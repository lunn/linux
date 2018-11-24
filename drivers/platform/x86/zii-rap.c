// SPDX-License-Identifier: GPL-2.0
/*
 * ZII RAVE AP board driver
 *
 */

#include <linux/dmi.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mdio-gpio.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/platform_data/mdio-gpio.h>
#include <linux/platform_data/mv88e6xxx.h>
#include <linux/platform_device.h>
#include <net/dsa.h>

struct zii_rap_data {
	struct platform_device *pdev;
};

static struct i2c_board_info zii_rap_i2c_devices[] = {
	{
		/* GPIO Expander at 0x21 */
		I2C_BOARD_INFO("sx1502q", 0x21),
	},
	{
		/* 4K EEPROM at 0x52 */
		I2C_BOARD_INFO("24c32", 0x52),
	},
	{
		/* 4K EEPROM at 0x54 */
		I2C_BOARD_INFO("24c32", 0x54),
	},
	{
		/* Real Time Clock at 0x68 */
		I2C_BOARD_INFO("ds1341", 0x68),
	},
	{
		/* Elapsed time counter at 0x6b */
		I2C_BOARD_INFO("ds1682", 0x6b),
	},
};

static struct gpiod_lookup_table zii_rap_mdio_gpiod_table = {
	.dev_id = "mdio-gpio.0",
	.table = {
		GPIO_LOOKUP_IDX("gpio-tqmx86", 0, NULL, MDIO_GPIO_MDC,
				GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("gpio-tqmx86", 5, NULL, MDIO_GPIO_MDIO,
				GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("gpio-tqmx86", 1, NULL, MDIO_GPIO_MDO,
				GPIO_ACTIVE_LOW),
	},
};

static struct dsa_mv88e6xxx_pdata dsa_mv88e6xxx_pdata = {
	.cd = {
		.port_names[0] = NULL,
		.port_names[1] = "cpu",
		.port_names[2] = "red",
		.port_names[3] = "blue",
		.port_names[4] = "green",
		.port_names[5] = NULL,
		.port_names[6] = NULL,
		.port_names[7] = NULL,
		.port_names[8] = "waic0",
	},
	.compatible = "marvell,mv88e6190",
	.enabled_ports = BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(8),
	.eeprom_len = 65536,
};

static const struct mdio_gpio_platform_data mdio_gpio_pdata = {
	.phy_mask = ~0,
	.phy_ignore_ta_mask = ~0,
};

static const struct mdio_board_info bdinfo = {
	.bus_id = "gpio-0",
	.modalias = "mv88e6085",
	.mdio_addr = 0,
	.platform_data = &dsa_mv88e6xxx_pdata,
};

static int zii_rap_i2c_adap_name_match(struct device *dev, void *data)
{
	struct i2c_adapter *adap = i2c_verify_adapter(dev);

	if (!adap)
		return false;

	return !strcmp(adap->name, (char *)data);
}

static struct i2c_adapter *zii_rap_find_i2c_adapter(char *name)
{
	struct device *dev;
	struct i2c_adapter *adap;

	dev = bus_find_device(&i2c_bus_type, NULL, name,
			      zii_rap_i2c_adap_name_match);
	if (!dev)
		return NULL;

	adap = i2c_verify_adapter(dev);
	if (!adap)
		put_device(dev);

	return adap;
}

static int zii_rap_add_i2c_devices(struct zii_rap_data *data,
				   struct i2c_adapter *adapter,
				   const struct i2c_board_info *info,
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

static int zii_rap_mdio_init(struct device *dev)
{
	struct platform_device *mdio_pdev;

	mdio_pdev = platform_device_register_data(&platform_bus,
						  "mdio-gpio", 0,
						  &mdio_gpio_pdata,
						  sizeof(mdio_gpio_pdata));
	if (IS_ERR(mdio_pdev)) {
		dev_err(dev, "Failed to register MDIO device\n");
		return PTR_ERR(mdio_pdev);
	}

	return 0;
}

static struct gpiod_lookup_table zii_rap_gpiod_table = {
	.dev_id = "zii_rap",
	.table = {
		GPIO_LOOKUP_IDX("gpio-tqmx86", 6, "irq", 0,
				GPIO_ACTIVE_LOW),
	}
};

static int zii_rap_marvell_switch(struct zii_rap_data *data)
{
	struct device *dev = &data->pdev->dev;
	struct gpio_desc *gpio_switch_irq;
	int err, irq;

	dsa_mv88e6xxx_pdata.netdev = dev_get_by_name(&init_net, "eth0");
	if (!dsa_mv88e6xxx_pdata.netdev) {
		dev_err(dev, "Error finding Ethernet device\n");
		return -ENODEV;
	}

	gpiod_add_lookup_table(&zii_rap_gpiod_table);

	gpio_switch_irq = devm_gpiod_get(dev, "irq", GPIOD_IN);
	if (IS_ERR(gpio_switch_irq)) {
		dev_err(dev, "Error getting Switch interrupt GPIO\n");
		err = PTR_ERR(gpio_switch_irq);
		goto out;
	}

	irq = gpiod_to_irq(gpio_switch_irq);
	if (irq < 0) {
		dev_err(dev, "Error getting Switch interrupt\n");
		err = irq;
		goto out;
	}

	devm_gpiod_put(dev, gpio_switch_irq);

	dsa_mv88e6xxx_pdata.irq = irq;

	err = mdiobus_register_board_info(&bdinfo, 1);
	if (err) {
		dev_err(dev, "Error setting up MDIO board info\n");
		goto out;
	}

	gpiod_add_lookup_table(&zii_rap_mdio_gpiod_table);

	err = zii_rap_mdio_init(dev);
	if (err)
		dev_err(dev, "Error setting up MDIO bit banging\n");

	return err;

out:
	dev_put(dsa_mv88e6xxx_pdata.netdev);

	return err;
}

static int zii_rap_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct i2c_adapter *adapter;
	struct zii_rap_data *data;
	int err;

	data = devm_kzalloc(dev, sizeof(struct zii_rap_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->pdev = pdev;

	adapter = zii_rap_find_i2c_adapter("i2c-ocores");
	if (!adapter)
		return -EPROBE_DEFER;

	err = zii_rap_add_i2c_devices(data, adapter, zii_rap_i2c_devices,
				      ARRAY_SIZE(zii_rap_i2c_devices));
	if (err)
		return err;

	return zii_rap_marvell_switch(data);
}

static struct platform_driver zii_rap_driver = {
	.probe = zii_rap_probe,
	.driver = {
		.name = "zii_rap",
	},
};

static int zii_rap_create_platform_device(const struct dmi_system_id *id)
{
	static struct platform_device *zii_rap_pdev;
	int ret;

	zii_rap_pdev = platform_device_alloc("zii_rap", -1);
	if (!zii_rap_pdev)
		return -ENOMEM;

	ret = platform_device_add(zii_rap_pdev);
	if (ret)
		goto err;

	return 0;
err:
	platform_device_put(zii_rap_pdev);
	return ret;
}

static const struct dmi_system_id zii_rap_device_table[] __initconst = {
	{
		.ident = "TQMX86",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TQ-Group"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TQMx"),
		},
		.callback = zii_rap_create_platform_device,
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, zii_rap_device_table);

static int __init zii_rap_init(void)
{
	if (!dmi_check_system(zii_rap_device_table))
		return -ENODEV;

	return platform_driver_register(&zii_rap_driver);
}

module_init(zii_rap_init);
MODULE_LICENSE("GPL");
