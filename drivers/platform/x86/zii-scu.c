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
#include <linux/platform_data/mv88e6xxx.h>
#include <linux/platform_data/pca953x.h>
#include <linux/platform_device.h>
#include <linux/phy.h>
#include <net/dsa.h>

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
		.default_trigger = "heartbeat",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 1 */
		.name = "scu_status:a:WLess",
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 2 */
		.name = "scu_status:r:LDFail",
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 3 */
		.name = "scu_status:a:SW",
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
};

static struct gpio_led_platform_data pca_x71_gpio_led = {
	.leds = pca_x71_gpio_leds,
	.num_leds = ARRAY_SIZE(pca_x71_gpio_leds),
};

struct nvmem_cell_info nameplate_cells[] = {
	{
		.name = "checksum",
		.offset = 2,
		.bytes = 1,
	},
	{
		.name = "part_number",
		.offset = 11,
		.bytes = 11,
	},
};

static struct pca953x_platform_data pca953x_x71;

static struct i2c_board_info scu_i2c_info_common[] = {
	/* On Main Board */
	{ I2C_BOARD_INFO("zii_scu_pic", 0x20)},	/* SCU PIC */
	{ I2C_BOARD_INFO("at24", 0x54),         /* Nameplate EEPROM */
	  .properties = zii_scu_at24c08_properties},
	{ I2C_BOARD_INFO("at24", 0x52),         /* BIT EEPROM */
	  .properties = zii_scu_at24c04_properties},
	{ I2C_BOARD_INFO("ds1682", 0x6b)},	/* Elapsed Time Counter */
	{ I2C_BOARD_INFO("pca9538", 0x71),	/* LEDs + Output Discretes */
	  .platform_data = &pca953x_x71},
};

struct zii_scu_data {
	struct platform_device *pdev;
	struct platform_device *pca_x71_leds_pdev;
	struct i2c_client *i2c_common_clients[ARRAY_SIZE(scu_i2c_info_common)];
	struct i2c_adapter *adapter_kempld;
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

static struct gpiod_lookup_table zii_scu_leds_gpiod_table = {
	.dev_id = "zii_scu",
	.table = {
		GPIO_LOOKUP_IDX("pca9538", 0, "scu_status:g:RD", 0,
				GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("pca9538", 1, "scu_status:a:WLess", 0,
				GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("pca9538", 2, "scu_status:r:LDFail", 0,
				GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("pca9538", 3, "scu_status:a:SW", 0,
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

static int pca9538_x71_setup(struct i2c_client *client,
			     unsigned gpio_base, unsigned ngpio, void *context)
{
	struct zii_scu_data *data = context;
	struct device *dev = &data->pdev->dev;

	pca_x71_gpio_leds[0].gpiod = gpiod_get(dev, "scu_status:g:RD",
					       GPIO_ACTIVE_LOW);
	pca_x71_gpio_leds[1].gpiod = gpiod_get(dev, "scu_status:a:WLess",
					       GPIO_ACTIVE_LOW);
	pca_x71_gpio_leds[2].gpiod = gpiod_get(dev, "scu_status:r:LDFail",
					       GPIO_ACTIVE_LOW);
	pca_x71_gpio_leds[3].gpiod = gpiod_get(dev, "scu_status:a:SW",
					       GPIO_ACTIVE_LOW);

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

	platform_device_unregister(data->pca_x71_leds_pdev);

	return 0;
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

static int zii_scu_add_i2c_devices(struct zii_scu_data *data,
				   struct i2c_adapter *adapter,
				   struct i2c_board_info *info,
				   int count)
{
	int i;

	for (i = 0; i < count; i++) {
		data->i2c_common_clients[i] = i2c_new_device(adapter, info);
		if (!data->i2c_common_clients[i])
			/*
			 * Unfortunately this call does not tell us
			 * why it failed. Pick the most likely reason.
			 */
			return -EBUSY;
		info++;
	}
	return 0;
}

static void zii_scu_nameplate_bound(struct zii_scu_data *data,
				    struct i2c_client *i2c_client)
{
	struct device *dev = &i2c_client->dev;
	struct nvmem_device *nvmem;
	struct nvmem_cell *cell;
	char *partnumber;
	u8 *checksumbuf;
	u8 checksum;
	size_t len;
	char *name;
	int err;

	dev_info(dev, "Nameplate\n");

	name = kasprintf(GFP_KERNEL, "%d-00540", data->adapter_kempld->nr);
	nvmem = nvmem_device_get(NULL, name);
	kfree(name);

	if (!nvmem) {
		dev_err(dev, "Nameplate nvmem device not found\n");
		return;
	}

	err = nvmem_add_cells(nvmem, nameplate_cells,
			      ARRAY_SIZE(nameplate_cells));
	if (err) {
		dev_err(dev, "Error adding cells to nameplate nvmem device\n");
		return;
	}

	cell = nvmem_cell_get(NULL, "checksum");
	if (IS_ERR(cell)) {
		dev_err(dev, "Error getting checksum cell\n");
		return;
	}
	checksumbuf = (u8 *)nvmem_cell_read(cell, &len);
	nvmem_cell_put(cell);

	if (IS_ERR(checksumbuf) || len != 1) {
		dev_err(dev, "Error getting checksum from cell\n");
		return;
	}
	checksum = checksumbuf[0];
	kfree(checksumbuf);
	dev_info(dev, "checksum: %d\n", checksum);

	cell = nvmem_cell_get(NULL, "part_number");
	if (IS_ERR(cell)) {
		dev_err(dev, "Error getting part number cell\n");
		return;
	}
	partnumber = (u8 *)nvmem_cell_read(cell, &len);
	nvmem_cell_put(cell);

	if (IS_ERR(partnumber)) {
		dev_err(dev, "Error getting part number from cell\n");
		return;
	}
	dev_info(dev, "part number: %s\n", partnumber);

	kfree(partnumber);
}

struct zii_scu_notifier {
	struct notifier_block nb;
	struct zii_scu_data *data;
};

#define nb_to_data(x) container_of(x, struct zii_scu_notifier, nb)->data

static int zii_scu_platform_notifier_call(struct notifier_block *nb,
					unsigned long event, void *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	dev_info(&pdev->dev, "event %ld\n", event);

	return NOTIFY_DONE;
}

static int zii_scu_i2c_notifier_call(struct notifier_block *nb,
				     unsigned long event, void *dev)
{
	struct i2c_client *i2c_client = to_i2c_client(dev);
	struct zii_scu_data *data = nb_to_data(nb);

	if (event != BUS_NOTIFY_BOUND_DRIVER || !data)
		return NOTIFY_DONE;

	if (data->i2c_common_clients[1] == i2c_client)
		zii_scu_nameplate_bound(data, i2c_client);

	return NOTIFY_DONE;
}

static struct zii_scu_notifier zii_scu_platform_n = {
	.nb.notifier_call = zii_scu_platform_notifier_call,
};

static struct zii_scu_notifier zii_scu_i2c_n = {
	.nb.notifier_call = zii_scu_i2c_notifier_call,
};

static int zii_scu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct i2c_adapter *adapter;
	struct zii_scu_data * data;
	int err;

	data = devm_kzalloc(dev, sizeof(struct zii_scu_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->pdev = pdev;

	adapter = zii_scu_find_i2c_adapter("i2c-kempld");
	if (!adapter)
		return -EPROBE_DEFER;

	data->adapter_kempld = adapter;
	pca953x_x71.context = data;
	zii_scu_i2c_n.data = data;
	zii_scu_platform_n.data = data;

	bus_register_notifier(&platform_bus_type,
			      &zii_scu_platform_n.nb);
	bus_register_notifier(&i2c_bus_type, &zii_scu_i2c_n.nb);

	err = zii_scu_add_i2c_devices(data, adapter, scu_i2c_info_common,
				      ARRAY_SIZE(scu_i2c_info_common));
	if (err) {
		pr_err("Error adding common i2c devices\n");
		return err;
	}

	err = mdiobus_register_board_info(&bdinfo, 1);
	if (err) {
		pr_err("Error setting up MDIO board info\n");
		return err;
	}

	gpiod_add_lookup_table(&zii_scu_mdio_gpiod_table);
	gpiod_add_lookup_table(&zii_scu_gpio_table);
	gpiod_add_lookup_table(&zii_scu_leds_gpiod_table);

	err = zii_scu_mdio_init();
	if (err) {
		pr_err("Error setting up MDIO bit banging\n");
		goto out_lookup_table;
	}

	return 0;

out_lookup_table:
	gpiod_remove_lookup_table(&zii_scu_leds_gpiod_table);
	gpiod_remove_lookup_table(&zii_scu_gpio_table);
	gpiod_remove_lookup_table(&zii_scu_mdio_gpiod_table);
	return err;
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
			DMI_MATCH(DMI_BOARD_NAME, "COMe-bSC6"),
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
	{
		.ident = "IMS SCU version 2, Sandy Bridge",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-bSC2"),
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
