// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2012 Guenter Roeck
 * Copyright (C) 2018 Andrew Lunn
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/mfd/zii-scu-pic.h>
#include <linux/module.h>
#include <linux/platform_device.h>

struct zii_scu_pic_data {
	struct platform_device *pdev;
	struct led_classdev cdev;
};

static int zii_scu_pic_leds_set(struct led_classdev *led_cdev,
				 enum led_brightness brightness)
{
	struct zii_scu_pic_data *data;

	data = container_of(led_cdev, struct zii_scu_pic_data, cdev);

	return zii_scu_pic_write_byte(data->pdev,
				      I2C_SET_SCU_PIC_FAULT_LED_STATE,
				      brightness);
}

static enum led_brightness zii_scu_pic_leds_get(struct led_classdev *led_cdev)
{
	struct zii_scu_pic_data *data;

	data = container_of(led_cdev, struct zii_scu_pic_data, cdev);

	return zii_scu_pic_read_byte(data->pdev,
				     I2C_GET_SCU_PIC_FAULT_LED_STATE);
}

static int zii_scu_pic_leds_probe(struct platform_device *pdev)
{
	struct zii_scu_pic_data *data;

	data = devm_kzalloc(&pdev->dev, sizeof(struct zii_scu_pic_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->pdev = pdev;
	data->cdev.name = "scu_status:r:Fault";
	data->cdev.brightness_set_blocking = zii_scu_pic_leds_set;
	data->cdev.brightness_get = zii_scu_pic_leds_get;
	data->cdev.max_brightness = 1;
	data->cdev.flags = LED_CORE_SUSPENDRESUME;

	platform_set_drvdata(pdev, data);

	return led_classdev_register(&pdev->dev, &data->cdev);
}

static int zii_scu_pic_leds_remove(struct platform_device *pdev)
{
	struct zii_scu_pic_data *data = platform_get_drvdata(pdev);

	led_classdev_unregister(&data->cdev);

	return 0;
}

static struct platform_driver zii_scu_pic_leds_driver = {
	.driver.name	= "zii-scu-pic-leds",
	.probe		= zii_scu_pic_leds_probe,
	.remove		= zii_scu_pic_leds_remove,
};

module_platform_driver(zii_scu_pic_leds_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:zii-scu-pic-leds");
