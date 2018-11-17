// SPDX-License-Identifier: GPL-2.0+
/*
 * TQ-Systems PLD MFD core driver, based on vendor driver by
 * Vadim V.Vlasov <vvlasov@dev.rtsoft.ru>
 *
 * Copyright (c) 2015 TQ-Systems GmbH
 * Copyright (c) 2019 Andrew Lunn <andrew@lunn.ch>
 */

#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/platform_data/i2c-ocores.h>
#include <linux/platform_device.h>

#define TQMX86_IOBASE	0x160
#define TQMX86_IOSIZE	0x3f
#define TQMX86_IOBASE_I2C	0x1a0
#define TQMX86_IOSIZE_I2C	0xa
#define TQMX86_IOBASE_WATCHDOG	0x18b
#define TQMX86_IOSIZE_WATCHDOG	0x2
#define TQMX86_IOBASE_GPIO	0x18d
#define TQMX86_IOSIZE_GPIO	0x4

#define TQMX86_REG_BOARD_ID	0x20
#define TQMX86_REG_BOARD_REV	0x21
#define TQMX86_REG_IO_EXT_INT	0x26
#define TQMX86_REG_IO_EXT_INT_MASK		0x3
#define TQMX86_REG_IO_EXT_INT_GPIO_SHIFT	4
#define TQMX86_REG_I2C_DETECT	0x47
#define TQMX86_REG_I2C_DETECT_SOFT		0xa5
#define TQMX86_REG_I2C_INT_EN	0x49

#define I2C_KIND_SOFT	1	/* Ocores I2C soft controller */
#define I2C_KIND_HARD	2	/* Machxo2 I2C hard controller */

/**
 * struct tqmx86_device_data - Internal representation of the PLD device
 * @io_base:		Pointer to the IO memory
 * @pld_clock_rate:	PLD clock frequency
 * @dev:		Pointer to kernel device structure
 * @i2c_type:		Hard of soft I2C hardware macro
 */
struct tqmx86_device_ddata {
	void __iomem	*io_base;
	u32		pld_clock_rate;
	u32		i2c_type;
};

/**
 * struct tqmx86_platform_data - PLD hardware configuration structure
 * @ioresource:		IO addresses of the PLD
 */
struct tqmx86_platform_ddata {
	struct resource	*ioresource;
};

static uint gpio_irq;
module_param(gpio_irq, uint, 0);
MODULE_PARM_DESC(gpio_irq, "GPIO IRQ number (7, 9, 12)");

static const u8 i2c_irq_ctl[] = {
	[7] = 1,
	[9] = 2,
	[12] = 3
};

static const struct resource tqmx_i2c_soft_resources[] = {
	DEFINE_RES_IO(TQMX86_IOBASE_I2C, TQMX86_IOSIZE_I2C),
};

static const struct resource tqmx_watchdog_resources[] = {
	DEFINE_RES_IO(TQMX86_IOBASE_WATCHDOG, TQMX86_IOSIZE_WATCHDOG),
};

static struct resource tqmx_gpio_resources[] = {
	DEFINE_RES_IO(TQMX86_IOBASE_GPIO, TQMX86_IOSIZE_GPIO),
	DEFINE_RES_IRQ(0)
};

static struct i2c_board_info tqmx86_i2c_devices[] = {
	{
		/* 4K EEPROM at 0x50 */
		I2C_BOARD_INFO("24c32", 0x50),
	},
};

static struct ocores_i2c_platform_data ocores_platfom_ddata = {
	.num_devices = ARRAY_SIZE(tqmx86_i2c_devices),
	.devices = tqmx86_i2c_devices,
};

static const struct mfd_cell tqmx86_devs[] = {
	{
		.name = "ocores-i2c",
		.platform_data = &ocores_platfom_ddata,
		.pdata_size = sizeof(ocores_platfom_ddata),
		.resources = tqmx_i2c_soft_resources,
		.num_resources = ARRAY_SIZE(tqmx_i2c_soft_resources),
	},
	{
		.name = "tqmx86-wdt",
		.resources = tqmx_watchdog_resources,
		.num_resources = 1,
		.ignore_resource_conflicts = 1,
	},
	{
		.name = "tqmx86-gpio",
		.resources = tqmx_gpio_resources,
		.num_resources = ARRAY_SIZE(tqmx_gpio_resources),
		.ignore_resource_conflicts = 1,
	},
};

static const struct tq_board_info {
	u8 board_id;
	char *name;
	u32 pld_clock_rate;
} tq_board_info[] = {
	{ 0, "", 0 },
	{ 1, "TQMxE38M", 33000 },
	{ 2, "TQMx50UC", 24000 },
	{ 3, "TQMxE38C", 33000 },
	{ 4, "TQMx60EB", 24000 },
	{ 5, "TQMxE39M", 25000 },
	{ 6, "TQMxE39C", 25000 },
	{ 7, "TQMxE39x", 25000 },
	{ 8, "TQMx70EB", 24000 },
	{ 9, "TQMx80UC", 24000 },
	{10, "TQMx90UC", 24000 }
};

static int tqmx86_probe(struct platform_device *pdev)
{
	u8 board_id, rev, i2c_det, i2c_ien, io_ext_int_val;
	struct device *dev = &pdev->dev;
	struct tqmx86_device_ddata *pld;
	struct resource *ioport;
	int i;

	pld = devm_kzalloc(dev, sizeof(*pld), GFP_KERNEL);
	if (!pld)
		return -ENOMEM;

	ioport = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!ioport)
		return -EINVAL;

	pld->io_base = devm_ioport_map(dev, ioport->start,
				       resource_size(ioport));
	if (!pld->io_base)
		return -ENOMEM;

	platform_set_drvdata(pdev, pld);

	board_id = ioread8(pld->io_base + TQMX86_REG_BOARD_ID);
	for (i = 0; i < ARRAY_SIZE(tq_board_info); i++)
		if (tq_board_info[i].board_id == board_id)
			break;

	if (i == ARRAY_SIZE(tq_board_info)) {
		dev_info(&pdev->dev,
			 "Board ID %d not supported by this driver\n",
			 board_id);
		return -ENODEV;
	}

	rev = ioread8(pld->io_base + TQMX86_REG_BOARD_REV);

	dev_info(&pdev->dev,
		 "Found %s - Board ID %d, PCB Revision %d, PLD Revision %d\n",
		 tq_board_info[i].name, board_id, rev >> 4, rev & 0xf);

	pld->pld_clock_rate = tq_board_info[i].pld_clock_rate;

	i2c_det = ioread8(pld->io_base + TQMX86_REG_I2C_DETECT);
	i2c_ien = ioread8(pld->io_base + TQMX86_REG_I2C_INT_EN);

	if (i2c_det == TQMX86_REG_I2C_DETECT_SOFT)
		pld->i2c_type = I2C_KIND_SOFT;
	else
		pld->i2c_type = I2C_KIND_HARD;

	io_ext_int_val =
		(i2c_irq_ctl[gpio_irq] & TQMX86_REG_IO_EXT_INT_MASK) <<
		TQMX86_REG_IO_EXT_INT_GPIO_SHIFT;

	if (io_ext_int_val) {
		iowrite8(io_ext_int_val, pld->io_base + TQMX86_REG_IO_EXT_INT);
		if (ioread8(pld->io_base + TQMX86_REG_IO_EXT_INT) !=
		    io_ext_int_val) {
			dev_warn(&pdev->dev,
				 "gpio interrupts not supported.\n");
			gpio_irq = 0;
		}
	}

	ocores_platfom_ddata.clock_khz = pld->pld_clock_rate;
	tqmx_gpio_resources[1].start = gpio_irq;

	if (pld->i2c_type == I2C_KIND_SOFT)
		return devm_mfd_add_devices(&pdev->dev, PLATFORM_DEVID_NONE,
					    tqmx86_devs,
					    ARRAY_SIZE(tqmx86_devs),
					    NULL, 0, NULL);

	/* Skip the soft I2C cell */
	return devm_mfd_add_devices(&pdev->dev, PLATFORM_DEVID_NONE,
				    &tqmx86_devs[1],
				    ARRAY_SIZE(tqmx86_devs) - 1,
				    NULL, 0, NULL);
}

static struct resource tqmx86_ioresource[] = {
	DEFINE_RES_IO(TQMX86_IOBASE, TQMX86_IOSIZE),
};

static const struct tqmx86_platform_ddata tqmx86_platform_ddata_generic = {
	.ioresource	= &tqmx86_ioresource[0],
};

static struct platform_device *tqmx86_pdev;

static int tqmx86_create_platform_device(const struct dmi_system_id *id)
{
	struct tqmx86_platform_ddata *pdata = id->driver_data;
	int ret;

	tqmx86_pdev = platform_device_alloc("tqmx86", -1);
	if (!tqmx86_pdev)
		return -ENOMEM;

	ret = platform_device_add_data(tqmx86_pdev, pdata, sizeof(*pdata));
	if (ret)
		goto err;

	ret = platform_device_add_resources(tqmx86_pdev, pdata->ioresource, 1);
	if (ret)
		goto err;

	ret = platform_device_add(tqmx86_pdev);
	if (ret)
		goto err;

	return 0;
err:
	platform_device_put(tqmx86_pdev);
	return ret;
}

static const struct dmi_system_id tqmx86_dmi_table[] __initconst = {
	{
		.ident = "TQMX86",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TQ-Group"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TQMx"),
		},
		.driver_data = (void *)&tqmx86_platform_ddata_generic,
		.callback = tqmx86_create_platform_device,
	},
	{}
};
MODULE_DEVICE_TABLE(dmi, tqmx86_dmi_table);

static struct platform_driver tqmx86_driver = {
	.driver		= {
		.name	= "tqmx86",
	},
	.probe		= tqmx86_probe,
};

static int __init tqmx86_init(void)
{
	if (gpio_irq != 0 && gpio_irq != 7 &&
	    gpio_irq != 9 && gpio_irq != 12) {
		pr_warn("tqmx86: Invalid GPIO IRQ (%d)\n", gpio_irq);
		gpio_irq = 0;
	}

	if (!dmi_check_system(tqmx86_dmi_table))
		return -ENODEV;

	return platform_driver_register(&tqmx86_driver);
}

static void __exit tqmx86_exit(void)
{
	if (tqmx86_pdev)
		platform_device_unregister(tqmx86_pdev);

	platform_driver_unregister(&tqmx86_driver);
}

module_init(tqmx86_init);
module_exit(tqmx86_exit);

MODULE_DESCRIPTION("TQx86 PLD Core Driver");
MODULE_AUTHOR("Andrew Lunn <andrew@lunn.ch>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:tqmx86");
