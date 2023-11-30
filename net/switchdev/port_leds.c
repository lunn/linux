/* SPDX-License-Identifier: GPL-1.0+ */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/leds.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <net/port_leds.h>

struct port_led {
	struct list_head led_list;
	struct led_classdev led_cdev;
	struct port_leds_ops *ops;
	struct device *dev;
	void *priv;
	int port;
	u8 index;
};

#define to_port_led(d) container_of(d, struct port_led, led_cdev)

static int port_brightness_set(struct led_classdev *led_cdev,
			       enum led_brightness value)
{
	struct port_led *port_led = to_port_led(led_cdev);

	return port_led->ops->brightness_set(port_led->priv,
					     port_led->port,
					     port_led->index,
					     value);
}

static int port_blink_set(struct led_classdev *led_cdev,
			  unsigned long *delay_on, unsigned long *delay_off)
{
	struct port_led *port_led = to_port_led(led_cdev);

	return port_led->ops->blink_set(port_led->priv,
					port_led->port,
					port_led->index,
					delay_on, delay_off);
}


static __maybe_unused int
port_hw_control_is_supported(struct led_classdev *led_cdev,
			     unsigned long flags)
{
	struct port_led *port_led = to_port_led(led_cdev);

	return port_led->ops->hw_control_is_supported(port_led->priv,
						      port_led->port,
						      port_led->index,
						      flags);
}

static __maybe_unused int port_hw_control_set(struct led_classdev *led_cdev,
					      unsigned long flags)
{
	struct port_led *port_led = to_port_led(led_cdev);

	return port_led->ops->hw_control_set(port_led->priv,
					     port_led->port,
					     port_led->index,
					     flags);
}

static __maybe_unused int port_hw_control_get(struct led_classdev *led_cdev,
					      unsigned long *flags)
{
	struct port_led *port_led = to_port_led(led_cdev);

	return port_led->ops->hw_control_get(port_led->priv,
					     port_led->port,
					     port_led->index,
					     flags);
}

static struct device *
port_hw_control_get_device(struct led_classdev *led_cdev)
{
	struct port_led *port_led = to_port_led(led_cdev);

	return port_led->dev;
}

static int port_led_setup(struct device_node *led, struct device *dev,
			  struct list_head *list, void *priv, int port,
			  struct port_leds_ops *ops)
{
	struct led_init_data init_data = {};
	struct led_classdev *cdev;
	struct port_led *port_led;
	u32 index;
	int err;

	port_led = devm_kzalloc(dev, sizeof(*port_led), GFP_KERNEL);
	if (!port_led)
		return -ENOMEM;

	port_led->dev = dev;
	port_led->priv = priv;
	port_led->port = port;
	port_led->ops = ops;
	cdev = &port_led->led_cdev;

	err = of_property_read_u32(led, "reg", &index);
	if (err)
		return err;

	if (index > 255)
		return -EINVAL;

	port_led->index = index;

	if (ops->brightness_set)
		cdev->brightness_set_blocking = port_brightness_set;
	if (ops->blink_set)
		cdev->blink_set = port_blink_set;
#ifdef CONFIG_LEDS_TRIGGERS
	if (ops->hw_control_is_supported)
		cdev->hw_control_is_supported = port_hw_control_is_supported;
	if (ops->hw_control_set)
		cdev->hw_control_set = port_hw_control_set;
	if (ops->hw_control_get)
		cdev->hw_control_get = port_hw_control_get;
	cdev->hw_control_trigger = "netdev";
#endif
	cdev->hw_control_get_device = port_hw_control_get_device;
	cdev->max_brightness = 1;
	init_data.fwnode = of_fwnode_handle(led);
	init_data.devname_mandatory = true;

	init_data.devicename = dev_name(dev);
	err = devm_led_classdev_register_ext(dev, cdev, &init_data);
	if (err)
		return err;

	INIT_LIST_HEAD(&port_led->led_list);
	list_add(&port_led->led_list, list);

	return 0;
}

int port_leds_setup(struct device *dev, struct device_node *np,
		    struct list_head *list, void *priv, int port,
		    struct port_leds_ops *ops)
{
	struct device_node *leds, *led;
	int err;

	leds = of_get_child_by_name(np, "leds");
	if (!leds)
		return 0;

	for_each_available_child_of_node(leds, led) {
		err = port_led_setup(led, dev, list, priv, port, ops);
		if (err) {
			of_node_put(led);
			return err;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(port_leds_setup);

void port_leds_teardown(struct list_head *list, int port)
{
	struct led_classdev *cdev;
	struct port_led *port_led;
	struct device *dev;

	list_for_each_entry(port_led, list, led_list) {
		if (port_led->port != port)
			continue;
		dev = port_led->dev;
		cdev = &port_led->led_cdev;
		devm_led_classdev_unregister(dev, cdev);
	}
}
EXPORT_SYMBOL_GPL(port_leds_teardown);
