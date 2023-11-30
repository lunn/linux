/* SPDX-License-Identifier: GPL-1.0+ */

/* Helpers used for creating and managing LEDs on a port of a
 * switch. */

#ifndef _NET_PORT_LEDS_H
#define _NET_PORT_LEDS_H

struct port_leds_ops {
	int (*brightness_set)(void *priv, int port, u8 led,
			      enum led_brightness brightness);
	int (*blink_set)(void *priv, int port, u8 led, unsigned long *delay_on,
			 unsigned long *delay_off);
	int (*hw_control_is_supported)(void *priv, int port, u8 led,
				       unsigned long flags);
	int (*hw_control_set)(void *priv, int port, u8 led,
			      unsigned long flags);
	int (*hw_control_get)(void *priv, int port, u8 led,
			      unsigned long *flags);
};

/**
 * port_leds_setup - Parse DT node and create LEDs for port
 *
 * @dev: struct dev for the port
 * @np: ethernet-node in device tree
 * @list: list to add LEDs to
 * @priv: private value specific to the driver, passed to callbacks.
 * @port: Port of switch these leds belong to
 * @ops: structure of ops to manipulate the LED.
 *
 * Parse the device tree node, as described in ethernet-controller.yaml,
 * and find any LEDs. For each LED found, create an LED and register
 * it with the LED subsystem. The LED will be added to the list, which can
 * be shared by all ports of the device. The ops structure contains the
 * callbacks needed to control the LEDs. priv will be passed to these ops,
 * along with port and the led index to identify the LED to be acted on.
 *
 * Return 0 in success, otherwise an negative error code.
 */
int port_leds_setup(struct device *dev, struct device_node *np,
		    struct list_head *list, void *priv, int port,
		    struct port_leds_ops *ops);

/**
 * port_leds_teardown - Remove LEDs for a port
 *
 * @list: list to add LEDs to
 * @port: Port of switch these leds belong to
 *
 * Unregister all LEDs for the given port, freeing up any allocated
 * memory.
 */
void port_leds_teardown(struct list_head *list, int port);

#endif /* _NET_PORT_LEDS_H */
