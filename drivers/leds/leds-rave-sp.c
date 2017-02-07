// SPDX-License-Identifier: GPL-2.0+

/*
 * Driver for LEDs connected to RAVE SP device, found on Zodiac
 * Inflight Innovations platforms
 *
 * Copyright (C) 2018 Zodiac Inflight Innovations
 */

#include <linux/module.h>
#include <linux/mfd/rave-sp.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <linux/leds.h>

enum rave_sp_leds_type {
	RAVE_SP_LEDS_FRONT_PANEL,
	RAVE_SP_LEDS_MOOD,
	RAVE_SP_LEDS_READING,
	RAVE_SP_LEDS_NUM
};

enum rave_sp_leds_channel {
	RAVE_SP_LEDS_CHANNEL_POWER,
	RAVE_SP_LEDS_CHANNEL_RED,
	RAVE_SP_LEDS_CHANNEL_GREEN,
	RAVE_SP_LEDS_CHANNEL_BLUE,
	RAVE_SP_LEDS_CHANNEL_NUM
};

enum rave_sp_leds_request {
	RAVE_SP_LEDS_GET = 0,
	RAVE_SP_LEDS_SET = 1,
};

struct rave_sp_leds {
	struct rave_sp *sp;
	struct led_classdev channels[RAVE_SP_LEDS_CHANNEL_NUM];
	struct mutex lock;
	enum rave_sp_leds_type type;
};

static enum rave_sp_leds_channel
rave_sp_leds_get_channel(struct rave_sp_leds *sp_led,
			 struct led_classdev *channel)
{
	return (channel - sp_led->channels) / sizeof(channel);
}

static int rave_sp_leds_cmd_led_control(struct rave_sp_leds *sp_led,
					enum rave_sp_leds_request request,
					u8 channels[])
{
	u8 cmd[] = {
		[0]  = RAVE_SP_CMD_LED_CONTROL,
		[1]  = 0,
		[2]  = request,
		[3]  = sp_led->type,
		[4]  = true,
		[5]  = 0,
		[6]  = 0,
		[7]  = channels[RAVE_SP_LEDS_CHANNEL_POWER],
		[8]  = channels[RAVE_SP_LEDS_CHANNEL_RED],
		[9]  = channels[RAVE_SP_LEDS_CHANNEL_GREEN],
		[10] = channels[RAVE_SP_LEDS_CHANNEL_BLUE],
	};
	struct  {
		u8 on;
		u8 channels[RAVE_SP_LEDS_CHANNEL_NUM];
	} __packed response;
	int ret;

	ret = rave_sp_exec(sp_led->sp, cmd, sizeof(cmd), &response,
			   sizeof(response));
	if (ret)
		return ret;

	if (response.on)
		memcpy(channels, &response.channels,
		       sizeof(response.channels));
	else
		memset(channels, 0, sizeof(response.channels));

	return 0;
}

static enum led_brightness
rave_sp_leds_brightness_get(struct led_classdev *led)
{
	struct rave_sp_leds *sp_led = dev_get_drvdata(led->dev->parent);
	enum rave_sp_leds_channel ch = rave_sp_leds_get_channel(sp_led, led);
	u8 channels[RAVE_SP_LEDS_CHANNEL_NUM];
	int ret;

	ret = rave_sp_leds_cmd_led_control(sp_led, RAVE_SP_LEDS_GET, channels);

	return ret ? : channels[ch];
}

static int rave_sp_leds_brightness_set(struct led_classdev *led,
				       enum led_brightness value)
{
	struct rave_sp_leds *sp_led = dev_get_drvdata(led->dev->parent);
	enum rave_sp_leds_channel ch = rave_sp_leds_get_channel(sp_led, led);
	u8 channels[RAVE_SP_LEDS_CHANNEL_NUM];
	int ret;

	mutex_lock(&sp_led->lock);

	ret = rave_sp_leds_cmd_led_control(sp_led, RAVE_SP_LEDS_GET, channels);
	if (ret)
		goto unlock;

	channels[ch] = value;
	ret = rave_sp_leds_cmd_led_control(sp_led, RAVE_SP_LEDS_SET, channels);
unlock:
	mutex_unlock(&sp_led->lock);

	return ret;
}

static int rave_sp_leds_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rave_sp *sp = dev_get_drvdata(dev->parent);
	struct device_node *np = dev->of_node;
	u8 channels[RAVE_SP_LEDS_CHANNEL_NUM];
	struct rave_sp_leds *sp_led;
	struct device_node *child;
	int ret;

	sp_led = devm_kzalloc(dev, sizeof(*sp_led), GFP_KERNEL);
	if (!sp_led)
		return -ENOMEM;

	sp_led->sp = sp;
	dev_set_drvdata(dev, sp_led);
	mutex_init(&sp_led->lock);

	ret = of_property_read_u32(np, "led-sources", &sp_led->type);
	if (ret || sp_led->type >= RAVE_SP_LEDS_NUM) {
		dev_err(dev, "Could not read 'led-sources'\n");
		return -EINVAL;
	}

	ret = rave_sp_leds_cmd_led_control(sp_led, RAVE_SP_LEDS_GET, channels);
	if (ret) {
		dev_err(dev, "Failed to get state of LEDs\n");
		return ret;
	}

	for_each_child_of_node(np, child) {
		struct led_classdev *led;
		u32 reg;

		ret = of_property_read_u32(child, "reg", &reg);
		if (ret || reg >= RAVE_SP_LEDS_CHANNEL_NUM) {
			dev_err(dev, "Could not register 'reg' of %s\n",
				child->name);
			continue;
		}

		led = &sp_led->channels[reg];

		if (of_property_read_string(child, "label", &led->name))
			led->name = child->name;
		led->brightness_set_blocking = rave_sp_leds_brightness_set;
		led->brightness_get = rave_sp_leds_brightness_get;
		led->max_brightness = U8_MAX;
		led->brightness = channels[reg];

		ret = devm_led_classdev_register(dev, led);
		if (ret) {
			dev_err(dev, "Could not register %s\n", led->name);
			return ret;
		}
	}

	return 0;
}

static const struct of_device_id rave_sp_leds_of_match[] = {
	{ .compatible = "zii,rave-sp-leds" },
	{}
};

static struct platform_driver rave_sp_leds_driver = {
	.probe = rave_sp_leds_probe,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = rave_sp_leds_of_match,
	},
};
module_platform_driver(rave_sp_leds_driver);

MODULE_DEVICE_TABLE(of, rave_sp_leds_of_match);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nikita Yushchenko <nikita.yoush@cogentembedded.com>");
MODULE_AUTHOR("Andrey Smirnov <andrew.smirnov@gmail.com>");
MODULE_DESCRIPTION("RAVE SP LED driver");
