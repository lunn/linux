// SPDX-License-Identifier: GPL-2.0
#include <linux/property.h>
#include <linux/regmap.h>
#include <net/dsa.h>
#include <net/netdev_leds.h>

#include "qca8k.h"
#include "qca8k_leds.h"

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
		reg_info->reg = QCA8K_LED_CTRL3_REG;
		reg_info->shift = QCA8K_LED_PHY123_PATTERN_EN_SHIFT(port_num, led_num);
		break;
	case 4:
		reg_info->reg = QCA8K_LED_CTRL_REG(led_num);
		reg_info->shift = QCA8K_LED_PHY4_CONTROL_RULE_SHIFT;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
qca8k_get_control_led_reg(int port_num, int led_num, struct qca8k_led_pattern_en *reg_info)
{
	reg_info->reg = QCA8K_LED_CTRL_REG(led_num);

	/* 6 total control rule:
	 * 3 control rules for phy0-3 that applies to all their leds
	 * 3 control rules for phy4
	 */
	if (port_num == 4)
		reg_info->shift = QCA8K_LED_PHY4_CONTROL_RULE_SHIFT;
	else
		reg_info->shift = QCA8K_LED_PHY0123_CONTROL_RULE_SHIFT;

	return 0;
}

static int
qca8k_parse_netdev(unsigned long rules, u32 *offload_trigger)
{
	/* Parsing specific to netdev trigger */
	if (test_bit(TRIGGER_NETDEV_TX, &rules))
		*offload_trigger |= QCA8K_LED_TX_BLINK_MASK;
	if (test_bit(TRIGGER_NETDEV_RX, &rules))
		*offload_trigger |= QCA8K_LED_RX_BLINK_MASK;
	if (test_bit(TRIGGER_NETDEV_LINK_10, &rules))
		*offload_trigger |= QCA8K_LED_LINK_10M_EN_MASK;
	if (test_bit(TRIGGER_NETDEV_LINK_100, &rules))
		*offload_trigger |= QCA8K_LED_LINK_100M_EN_MASK;
	if (test_bit(TRIGGER_NETDEV_LINK_1000, &rules))
		*offload_trigger |= QCA8K_LED_LINK_1000M_EN_MASK;
	if (test_bit(TRIGGER_NETDEV_HALF_DUPLEX, &rules))
		*offload_trigger |= QCA8K_LED_HALF_DUPLEX_MASK;
	if (test_bit(TRIGGER_NETDEV_FULL_DUPLEX, &rules))
		*offload_trigger |= QCA8K_LED_FULL_DUPLEX_MASK;

	if (rules && !*offload_trigger)
		return -EOPNOTSUPP;

	/* Enable some default rule by default to the requested mode:
	 * - Blink at 4Hz by default
	 */
	*offload_trigger |= QCA8K_LED_BLINK_4HZ;

	return 0;
}

static int
qca8k_led_brightness_set(struct net_device *ndev, u8 led_num,
			 enum led_brightness brightness)
{
	struct dsa_switch *ds = dsa_user_to_ds(ndev);
	struct qca8k_led_pattern_en reg_info;
	struct qca8k_priv *priv = ds->priv;
	int port = dsa_user_to_index(ndev);
	u32 mask, val;
	int port_num;

	port_num = qca8k_port_to_phy(port);
	qca8k_get_enable_led_reg(port_num, led_num, &reg_info);

	val = QCA8K_LED_ALWAYS_OFF;
	if (brightness)
		val = QCA8K_LED_ALWAYS_ON;

	/* HW regs to control brightness is special and port 1-2-3
	 * are placed in a different reg.
	 *
	 * To control port 0 brightness:
	 * - the 2 bit (15, 14) of:
	 *   - QCA8K_LED_CTRL0_REG for led1
	 *   - QCA8K_LED_CTRL1_REG for led2
	 *   - QCA8K_LED_CTRL2_REG for led3
	 *
	 * To control port 4:
	 * - the 2 bit (31, 30) of:
	 *   - QCA8K_LED_CTRL0_REG for led1
	 *   - QCA8K_LED_CTRL1_REG for led2
	 *   - QCA8K_LED_CTRL2_REG for led3
	 *
	 * To control port 1:
	 *   - the 2 bit at (9, 8) of QCA8K_LED_CTRL3_REG are used for led1
	 *   - the 2 bit at (11, 10) of QCA8K_LED_CTRL3_REG are used for led2
	 *   - the 2 bit at (13, 12) of QCA8K_LED_CTRL3_REG are used for led3
	 *
	 * To control port 2:
	 *   - the 2 bit at (15, 14) of QCA8K_LED_CTRL3_REG are used for led1
	 *   - the 2 bit at (17, 16) of QCA8K_LED_CTRL3_REG are used for led2
	 *   - the 2 bit at (19, 18) of QCA8K_LED_CTRL3_REG are used for led3
	 *
	 * To control port 3:
	 *   - the 2 bit at (21, 20) of QCA8K_LED_CTRL3_REG are used for led1
	 *   - the 2 bit at (23, 22) of QCA8K_LED_CTRL3_REG are used for led2
	 *   - the 2 bit at (25, 24) of QCA8K_LED_CTRL3_REG are used for led3
	 *
	 * To abstract this and have less code, we use the port and led numm
	 * to calculate the shift and the correct reg due to this problem of
	 * not having a 1:1 map of LED with the regs.
	 */
	if (port_num == 0 || port_num == 4) {
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
qca8k_led_blink_set(struct net_device *ndev, u8 led_num,
		    unsigned long *delay_on, unsigned long *delay_off)
{
	struct dsa_switch *ds = dsa_user_to_ds(ndev);
	u32 mask, val = QCA8K_LED_ALWAYS_BLINK_4HZ;
	struct qca8k_led_pattern_en reg_info;
	struct qca8k_priv *priv = ds->priv;
	int port = dsa_user_to_index(ndev);
	int port_num;

	port_num = qca8k_port_to_phy(port);

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

	qca8k_get_enable_led_reg(port_num, led_num, &reg_info);

	if (port_num == 0 || port_num == 4) {
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
qca8k_led_trigger_offload(struct qca8k_priv *priv, int port_num, u8 led_num,
			  bool enable)
{
	struct qca8k_led_pattern_en reg_info;
	u32 mask, val = QCA8K_LED_ALWAYS_OFF;

	qca8k_get_enable_led_reg(port_num, led_num, &reg_info);

	if (enable)
		val = QCA8K_LED_RULE_CONTROLLED;

	if (port_num == 0 || port_num == 4) {
		mask = QCA8K_LED_PATTERN_EN_MASK;
		val <<= QCA8K_LED_PATTERN_EN_SHIFT;
	} else {
		mask = QCA8K_LED_PHY123_PATTERN_EN_MASK;
	}

	return regmap_update_bits(priv->regmap, reg_info.reg, mask << reg_info.shift,
				  val << reg_info.shift);
}

static bool
qca8k_led_hw_control_status(struct qca8k_priv *priv, int port_num, u8 led_num)
{
	struct qca8k_led_pattern_en reg_info;
	u32 val;

	qca8k_get_enable_led_reg(port_num, led_num, &reg_info);

	regmap_read(priv->regmap, reg_info.reg, &val);

	val >>= reg_info.shift;

	if (port_num == 0 || port_num == 4) {
		val &= QCA8K_LED_PATTERN_EN_MASK;
		val >>= QCA8K_LED_PATTERN_EN_SHIFT;
	} else {
		val &= QCA8K_LED_PHY123_PATTERN_EN_MASK;
	}

	return val == QCA8K_LED_RULE_CONTROLLED;
}

static int
qca8k_led_hw_control_is_supported(struct net_device *ndev, u8 led,
				  unsigned long rules)
{
	u32 offload_trigger = 0;

	return qca8k_parse_netdev(rules, &offload_trigger);
}

static int
qca8k_led_hw_control_set(struct net_device *ndev, u8 led_num,
			 unsigned long rules)
{
	struct dsa_switch *ds = dsa_user_to_ds(ndev);
	struct qca8k_led_pattern_en reg_info;
	struct qca8k_priv *priv = ds->priv;
	int port = dsa_user_to_index(ndev);
	u32 offload_trigger = 0;
	int port_num;
	int ret;

	port_num = qca8k_port_to_phy(port);

	ret = qca8k_parse_netdev(rules, &offload_trigger);
	if (ret)
		return ret;

	ret = qca8k_led_trigger_offload(priv, port_num, led_num, true);
	if (ret)
		return ret;

	qca8k_get_control_led_reg(port_num, led_num, &reg_info);

	return regmap_update_bits(priv->regmap, reg_info.reg,
				  QCA8K_LED_RULE_MASK << reg_info.shift,
				  offload_trigger << reg_info.shift);
}

static int
qca8k_led_hw_control_get(struct net_device *ndev, u8 led_num,
			 unsigned long *rules)
{
	struct dsa_switch *ds = dsa_user_to_ds(ndev);
	struct qca8k_led_pattern_en reg_info;
	struct qca8k_priv *priv = ds->priv;
	int port = dsa_user_to_index(ndev);
	int port_num;
	u32 val;
	int ret;

	port_num = qca8k_port_to_phy(port);

	/* With hw control not active return err */
	if (!qca8k_led_hw_control_status(priv, port_num, led_num))
		return -EINVAL;

	qca8k_get_control_led_reg(port_num, led_num, &reg_info);

	ret = regmap_read(priv->regmap, reg_info.reg, &val);
	if (ret)
		return ret;

	val >>= reg_info.shift;
	val &= QCA8K_LED_RULE_MASK;

	/* Parsing specific to netdev trigger */
	if (val & QCA8K_LED_TX_BLINK_MASK)
		set_bit(TRIGGER_NETDEV_TX, rules);
	if (val & QCA8K_LED_RX_BLINK_MASK)
		set_bit(TRIGGER_NETDEV_RX, rules);
	if (val & QCA8K_LED_LINK_10M_EN_MASK)
		set_bit(TRIGGER_NETDEV_LINK_10, rules);
	if (val & QCA8K_LED_LINK_100M_EN_MASK)
		set_bit(TRIGGER_NETDEV_LINK_100, rules);
	if (val & QCA8K_LED_LINK_1000M_EN_MASK)
		set_bit(TRIGGER_NETDEV_LINK_1000, rules);
	if (val & QCA8K_LED_HALF_DUPLEX_MASK)
		set_bit(TRIGGER_NETDEV_HALF_DUPLEX, rules);
	if (val & QCA8K_LED_FULL_DUPLEX_MASK)
		set_bit(TRIGGER_NETDEV_FULL_DUPLEX, rules);

	return 0;
}

struct netdev_leds_ops qca8k_netdev_leds_ops = {
	.brightness_set = qca8k_led_brightness_set,
	.blink_set = qca8k_led_blink_set,
	.hw_control_is_supported = qca8k_led_hw_control_is_supported,
	.hw_control_set = qca8k_led_hw_control_set,
	.hw_control_get = qca8k_led_hw_control_get,
};
