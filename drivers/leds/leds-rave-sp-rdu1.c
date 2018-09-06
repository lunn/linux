// SPDX-License-Identifier: GPL-2.0+

/*
 * Driver for LEDs connected to RAVE SP device on Zodiac Inflight Innovations
 * RDU1 platform
 *
 * Copyright (C) 2018 Zodiac Inflight Innovations
 */

#include <linux/module.h>
#include <linux/mfd/rave-sp.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <linux/leds.h>

enum {
	POWER_LED_STATE			= GENMASK(2, 0),
	POWER_LED_OFF			= 0x0,
	POWER_LED_BLUE			= 0x1,
	POWER_LED_AMBER			= 0x2,
	POWER_LED_DIM			= BIT(3),
	CONNECTOR_LED_ON		= BIT(4),
	CONNECTOR_LED_DIM		= BIT(5),
};

enum {
	BRIGHTNESS_OFF = 0,
	BRIGHTNESS_DIM = 1,
	BRIGHTNESS_FULL = 2,
	BRIGHTNESS_MAX = BRIGHTNESS_FULL,
};

enum {
	BLINK_DELAY_ON = 500,
	BLINK_DELAY_OFF = 500,
};

struct rave_sp_rdu1_leds {
	struct rave_sp *sp;
	struct led_classdev power_blue, power_amber, connector;
	struct mutex lock;
};

static int rave_sp_leds_rdu1_cmd_pwr_led(struct rave_sp_rdu1_leds *sp_led, u8 s)
{
	u8 cmd[] = {
		[0]  = RAVE_SP_CMD_PWR_LED,
		[1]  = 0,
		[2]  = s,
	};

	return rave_sp_exec(sp_led->sp, cmd, sizeof(cmd), NULL, 0);
}

static enum led_brightness
rave_sp_rdu1_leds_brightness_get(struct led_classdev *led)
{
	struct rave_sp_rdu1_leds *sp_led = dev_get_drvdata(led->dev->parent);
	struct rave_sp_status status;
	u8 s;
	int ret;

	ret = rave_sp_get_status(sp_led->sp, &status);
	if (ret)
		return ret;
	s = status.power_led_status;

	ret = BRIGHTNESS_OFF;

	if (led == &sp_led->power_blue) {
		if ((s & POWER_LED_STATE) == POWER_LED_BLUE)
			ret = (s & POWER_LED_DIM) ?
				BRIGHTNESS_DIM : BRIGHTNESS_FULL;
	} else if (led == &sp_led->power_amber) {
		if ((s & POWER_LED_STATE) == POWER_LED_AMBER)
			ret = (s & POWER_LED_DIM) ?
				BRIGHTNESS_DIM : BRIGHTNESS_FULL;
	} else if (led == &sp_led->connector) {
		if (s & CONNECTOR_LED_ON)
			ret = (s & CONNECTOR_LED_DIM) ?
				BRIGHTNESS_DIM : BRIGHTNESS_FULL;
	} else
		ret = -EINVAL;

	return ret;
}

static int rave_sp_rud1_leds_brightness_set(struct led_classdev *led,
					    enum led_brightness value)
{
	struct rave_sp_rdu1_leds *sp_led = dev_get_drvdata(led->dev->parent);
	struct rave_sp_status status;
	u8 s;
	int ret;

	mutex_lock(&sp_led->lock);

	ret = rave_sp_get_status(sp_led->sp, &status);
	if (ret)
		goto unlock;
	s = status.power_led_status;

	if (led == &sp_led->power_blue || led == &sp_led->power_amber) {
		s &= ~POWER_LED_STATE;
		if (value && led == &sp_led->power_blue)
			s |= POWER_LED_BLUE;
		if (value && led == &sp_led->power_amber)
			s |= POWER_LED_AMBER;
		if (value == (enum led_brightness)BRIGHTNESS_DIM)
			s |= POWER_LED_DIM;
		else
			s &= ~POWER_LED_DIM;
	} else if (led == &sp_led->connector) {
		if (value)
			s |= CONNECTOR_LED_ON;
		else
			s &= ~CONNECTOR_LED_ON;
		if (value == (enum led_brightness)BRIGHTNESS_DIM)
			s |= CONNECTOR_LED_DIM;
		else
			s &= ~CONNECTOR_LED_DIM;
	} else {
		ret = -EINVAL;
		goto unlock;
	}

	ret = rave_sp_leds_rdu1_cmd_pwr_led(sp_led, s);
unlock:
	mutex_unlock(&sp_led->lock);

	return ret;
}

static int rave_sp_rdu1_leds_add_led(struct device *dev, struct led_classdev *led,
		const char *name)
{
	int ret;

	led->name = name;
	led->max_brightness = BRIGHTNESS_FULL;
	led->brightness_get = rave_sp_rdu1_leds_brightness_get;
	led->brightness_set_blocking = rave_sp_rud1_leds_brightness_set;

	ret = devm_led_classdev_register(dev, led);
	if (ret)
		dev_err(dev, "could not register %s\n", name);
	return ret;
}

static int rave_sp_rdu1_leds_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rave_sp *sp = dev_get_drvdata(dev->parent);
	struct rave_sp_rdu1_leds *sp_led;
	int ret;

	sp_led = devm_kzalloc(dev, sizeof(*sp_led), GFP_KERNEL);
	if (!sp_led)
		return -ENOMEM;

	sp_led->sp = sp;
	dev_set_drvdata(dev, sp_led);
	mutex_init(&sp_led->lock);

	ret = rave_sp_rdu1_leds_add_led(dev, &sp_led->power_blue,
			"rave-sp:power:blue");
	if (ret)
		return ret;

	ret = rave_sp_rdu1_leds_add_led(dev, &sp_led->power_amber,
			"rave-sp:power:amber");
	if (ret)
		return ret;

	ret = rave_sp_rdu1_leds_add_led(dev, &sp_led->connector,
			"rave-sp:connector:blue");
	if (ret)
		return ret;

	/* move LEDs to known-supported state */
	return rave_sp_leds_rdu1_cmd_pwr_led(sp_led, 0);
}

static const struct of_device_id rave_sp_rdu1_leds_of_match[] = {
	{ .compatible = "zii,rave-sp-leds-rdu1" },
	{}
};

static struct platform_driver rave_sp_rdu1_leds_driver = {
	.probe = rave_sp_rdu1_leds_probe,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = rave_sp_rdu1_leds_of_match,
	},
};
module_platform_driver(rave_sp_rdu1_leds_driver);

MODULE_DEVICE_TABLE(of, rave_sp_rdu1_leds_of_match);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nikita Yushchenko <nikita.yoush@cogentembedded.com>");
MODULE_AUTHOR("Andrey Smirnov <andrew.smirnov@gmail.com>");
MODULE_DESCRIPTION("RAVE SP RDU1 LED driver");
