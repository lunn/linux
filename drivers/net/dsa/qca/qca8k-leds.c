// SPDX-License-Identifier: GPL-2.0
#include <linux/regmap.h>
#include <net/dsa.h>

#include "qca8k.h"

static int
qca8k_get_enable_led_reg(int port_num, int led_num, struct qca8k_led_pattern_en *reg_info)
{
	switch (port_num) {
	case 0:
		reg_info->reg = QCA8K_LED_CTRL_REG(led_num);
		reg_info->shift = QCA8K_LED_PHY0123_CONTROL_RULE_SHIFT;
		break;
	case 1:
	case 2:
	case 3:
		/* Port 123 are controlled on a different reg */
		reg_info->reg = QCA8K_LED_CTRL_REG(3);
		reg_info->shift = QCA8K_LED_PHY123_PATTERN_EN_SHIFT(port_num, led_num);
		break;
	case 4:
		reg_info->reg = QCA8K_LED_CTRL_REG(led_num);
		reg_info->shift = QCA8K_LED_PHY0123_CONTROL_RULE_SHIFT;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
qca8k_led_brightness_set(struct qca8k_led *led,
			 enum led_brightness brightness)
{
	struct qca8k_led_pattern_en reg_info;
	struct qca8k_priv *priv = led->priv;
	u32 mask, val = QCA8K_LED_ALWAYS_OFF;

	qca8k_get_enable_led_reg(led->port_num, led->led_num, &reg_info);

	if (brightness)
		val = QCA8K_LED_ALWAYS_ON;

	if (led->port_num == 0 || led->port_num == 4) {
		mask = QCA8K_LED_PATTERN_EN_MASK;
		val <<= QCA8K_LED_PATTERN_EN_SHIFT;
	} else {
		mask = QCA8K_LED_PHY123_PATTERN_EN_MASK;
	}

	return regmap_update_bits(priv->regmap, reg_info.reg,
				  mask << reg_info.shift,
				  val << reg_info.shift);
}

static int
qca8k_cled_brightness_set_blocking(struct led_classdev *ldev,
				   enum led_brightness brightness)
{
	struct qca8k_led *led = container_of(ldev, struct qca8k_led, cdev);

	return qca8k_led_brightness_set(led, brightness);
}

static enum led_brightness
qca8k_led_brightness_get(struct qca8k_led *led)
{
	struct qca8k_led_pattern_en reg_info;
	struct qca8k_priv *priv = led->priv;
	u32 val;
	int ret;

	qca8k_get_enable_led_reg(led->port_num, led->led_num, &reg_info);

	ret = regmap_read(priv->regmap, reg_info.reg, &val);
	if (ret)
		return 0;

	val >>= reg_info.shift;

	if (led->port_num == 0 || led->port_num == 4) {
		val &= QCA8K_LED_PATTERN_EN_MASK;
		val >>= QCA8K_LED_PATTERN_EN_SHIFT;
	} else {
		val &= QCA8K_LED_PHY123_PATTERN_EN_MASK;
	}

	return val > 0 ? 1 : 0;
}

static enum led_brightness
qca8k_cled_brightness_get(struct led_classdev *ldev)
{
	struct qca8k_led *led = container_of(ldev, struct qca8k_led, cdev);

	return qca8k_led_brightness_get(led);
}

static int
qca8k_cled_blink_set(struct led_classdev *ldev,
		     unsigned long *delay_on,
		     unsigned long *delay_off)
{
	struct qca8k_led *led = container_of(ldev, struct qca8k_led, cdev);
	u32 mask, val = QCA8K_LED_ALWAYS_BLINK_4HZ;
	struct qca8k_led_pattern_en reg_info;
	struct qca8k_priv *priv = led->priv;

	if (*delay_on == 0 && *delay_off == 0) {
		*delay_on = 125;
		*delay_off = 125;
	}

	if (*delay_on != 125 || *delay_off != 125) {
		/* The hardware only supports blinking at 4Hz. Fall back
		 * to software implementation in other cases.
		 */
		return -EINVAL;
	}

	qca8k_get_enable_led_reg(led->port_num, led->led_num, &reg_info);

	if (led->port_num == 0 || led->port_num == 4) {
		mask = QCA8K_LED_PATTERN_EN_MASK;
		val <<= QCA8K_LED_PATTERN_EN_SHIFT;
	} else {
		mask = QCA8K_LED_PHY123_PATTERN_EN_MASK;
	}

	regmap_update_bits(priv->regmap, reg_info.reg, mask << reg_info.shift,
			   val << reg_info.shift);

	return 0;
}

static int
qca8k_parse_port_leds(struct qca8k_priv *priv, struct fwnode_handle *port, int port_num)
{
	struct fwnode_handle *led = NULL, *leds = NULL;
	struct led_init_data init_data = { };
	struct qca8k_led *port_led;
	int led_num, port_index;
	const char *state;
	int ret;

	leds = fwnode_get_named_child_node(port, "leds");
	if (!leds) {
		dev_dbg(priv->dev, "No Leds node specified in device tree for port %d!\n",
			port_num);
		return 0;
	}

	fwnode_for_each_child_node(leds, led) {
		/* Reg represent the led number of the port.
		 * Each port can have at least 3 leds attached
		 * Commonly:
		 * 1. is gigabit led
		 * 2. is mbit led
		 * 3. additional status led
		 */
		if (fwnode_property_read_u32(led, "reg", &led_num))
			continue;

		if (led_num >= QCA8K_LED_PORT_COUNT) {
			dev_warn(priv->dev, "Invalid LED reg defined %d", port_num);
			continue;
		}

		port_index = 3 * port_num + led_num;

		port_led = &priv->ports_led[port_index];
		port_led->port_num = port_num;
		port_led->led_num = led_num;
		port_led->priv = priv;

		ret = fwnode_property_read_string(led, "default-state", &state);
		if (!ret) {
			if (!strcmp(state, "on")) {
				port_led->cdev.brightness = 1;
				qca8k_led_brightness_set(port_led, 1);
			} else if (!strcmp(state, "off")) {
				port_led->cdev.brightness = 0;
				qca8k_led_brightness_set(port_led, 0);
			} else if (!strcmp(state, "keep")) {
				port_led->cdev.brightness =
					qca8k_led_brightness_get(port_led);
			}
		}

		port_led->cdev.max_brightness = 1;
		port_led->cdev.brightness_set_blocking = qca8k_cled_brightness_set_blocking;
		port_led->cdev.brightness_get = qca8k_cled_brightness_get;
		port_led->cdev.blink_set = qca8k_cled_blink_set;
		init_data.default_label = ":port";
		init_data.devicename = "qca8k";
		init_data.fwnode = led;

		ret = devm_led_classdev_register_ext(priv->dev, &port_led->cdev, &init_data);
		if (ret)
			dev_warn(priv->dev, "Failed to int led");
	}

	return 0;
}

int
qca8k_setup_led_ctrl(struct qca8k_priv *priv)
{
	struct fwnode_handle *mdio, *port;
	int port_num;
	int ret;

	mdio = device_get_named_child_node(priv->dev, "mdio");
	if (!mdio) {
		dev_info(priv->dev, "No MDIO node specified in device tree!\n");
		return 0;
	}

	fwnode_for_each_child_node(mdio, port) {
		if (fwnode_property_read_u32(port, "reg", &port_num))
			continue;

		/* Each port can have at least 3 different leds attached */
		ret = qca8k_parse_port_leds(priv, port, port_num);
		if (ret)
			return ret;
	}

	return 0;
}
