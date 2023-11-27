/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __QCA8K_LEDS_H
#define __QCA8K_LEDS_H

/* Leds Support function */
#ifdef CONFIG_NET_DSA_QCA8K_LEDS_SUPPORT
int qca8k_led_brightness_set(struct dsa_switch *ds, int port_num,
			     u8 led_num, enum led_brightness brightness);
int qca8k_led_blink_set(struct dsa_switch *ds, int port_num, u8 led_num,
			unsigned long *delay_on,
			unsigned long *delay_off);
int qca8k_led_hw_control_is_supported(struct dsa_switch *ds,
				      int port, u8 led,
				      unsigned long rules);
int qca8k_led_hw_control_set(struct dsa_switch *ds, int port_num, u8 led_num,
			     unsigned long rules);
int qca8k_led_hw_control_get(struct dsa_switch *ds, int port_num, u8 led_num,
			     unsigned long *rules);
#endif /* CONFIG_NET_DSA_QCA8K_LEDS_SUPPORT */
#endif /* __QCA8K_LEDS_H */
