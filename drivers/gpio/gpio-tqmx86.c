// SPDX-License-Identifier: GPL-2.0
/*
 * TQ-Systems TQMx86 PLD GPIO driver
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>

#define TQMX86_NGPIO	8
#define TQMX86_DIR_MASK	0xf0	/* 0-3 - output, 4-7 - input */
#define TQMX86_GPIODD	0	/* GPIO Data Direction Register */
#define TQMX86_GPIOD	1	/* GPIO Data Register */
#define TQMX86_GPIIC	3	/* GPI Interrupt Configuration Register */
#define TQMX86_GPIIS	4	/* GPI Interrupt Status Register */

#define TQMX86_GPII_RISING	2
#define TQMX86_GPII_FALLING	1

struct tqmx86_gpio_data {
	struct gpio_chip	chip;
	void __iomem		*io_base;
	struct irq_domain	*domain;
	int			irq;
	spinlock_t		spinlock;
	u8			irq_type[4];
	int			irqs[4];	/* mapped irqs */
};

static u8 tqmx86_gpio_read(struct tqmx86_gpio_data *gd, unsigned int reg)
{
	return ioread8(gd->io_base + reg);
}

static void tqmx86_gpio_write(struct tqmx86_gpio_data *gd, u8 val,
			      unsigned int reg)
{
	iowrite8(val, gd->io_base + reg);
}

static int tqmx86_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct tqmx86_gpio_data *gpio = gpiochip_get_data(chip);

	return !!(tqmx86_gpio_read(gpio, TQMX86_GPIOD) & (1 << offset));
}

static void tqmx86_gpio_set(struct gpio_chip *chip, unsigned int offset,
			    int value)
{
	u8 val;
	unsigned long flags;
	struct tqmx86_gpio_data *gpio = gpiochip_get_data(chip);

	spin_lock_irqsave(&gpio->spinlock, flags);
	val = tqmx86_gpio_read(gpio, TQMX86_GPIOD);
	if (value)
		val |= 1 << offset;
	else
		val &= ~(1 << offset);
	tqmx86_gpio_write(gpio, val, TQMX86_GPIOD);
	spin_unlock_irqrestore(&gpio->spinlock, flags);
}

static int tqmx86_gpio_direction_input(struct gpio_chip *chip,
				       unsigned int offset)
{
	/* Direction cannot be changed */
	if ((1 << offset) & TQMX86_DIR_MASK)
		return 0;
	else
		return -EINVAL;
}

static int tqmx86_gpio_direction_output(struct gpio_chip *chip,
					unsigned int offset,
					int value)
{
	/* Direction cannot be changed */
	if (((1 << offset) & TQMX86_DIR_MASK) == 0)
		return 0;
	else
		return -EINVAL;
}

static int tqmx86_gpio_get_direction(struct gpio_chip *chip,
				     unsigned int offset)
{
	return !!(TQMX86_DIR_MASK & (1 << offset));
}

static int tqmx86_gpio_to_irq(struct gpio_chip *chip, unsigned int offset)
{
	struct tqmx86_gpio_data *gpio = gpiochip_get_data(chip);
	int ret;

	if (offset < 4 || offset > 7)
		return -EINVAL;
	ret = irq_create_mapping(gpio->domain, offset - 4);
	if (ret > 0)
		gpio->irqs[offset - 4] = ret;

	return ret;
}

static void tqmx86_gpio_irq_noop(struct irq_data *data)
{
}

static void tqmx86_gpio_irq_mask(struct irq_data *data)
{
	struct tqmx86_gpio_data *gpio = data->domain->host_data;
	unsigned long flags, irq_mask = data->mask;
	int i;
	u8 gpiic, mask = 0;

	for_each_set_bit(i, &irq_mask, 4)
		mask |= 3 << (2 * i);

	if (mask) {
		spin_lock_irqsave(&gpio->spinlock, flags);
		gpiic = tqmx86_gpio_read(gpio, TQMX86_GPIIC);
		gpiic &= ~mask;
		tqmx86_gpio_write(gpio, gpiic, TQMX86_GPIIC);
		spin_unlock_irqrestore(&gpio->spinlock, flags);
	}
}

static void tqmx86_gpio_irq_unmask(struct irq_data *data)
{
	struct tqmx86_gpio_data *gpio = data->domain->host_data;
	unsigned long flags, irq_mask = data->mask;
	int i;
	u8 gpiic, mask = 0;

	for_each_set_bit(i, &irq_mask, 4)
		mask |= gpio->irq_type[i] << (2 * i);

	if (mask) {
		spin_lock_irqsave(&gpio->spinlock, flags);
		gpiic = tqmx86_gpio_read(gpio, TQMX86_GPIIC);
		gpiic |= mask;
		tqmx86_gpio_write(gpio, gpiic, TQMX86_GPIIC);
		spin_unlock_irqrestore(&gpio->spinlock, flags);
	}
}

static int tqmx86_gpio_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct tqmx86_gpio_data *gpio = data->domain->host_data;
	unsigned int edge_type = type & IRQF_TRIGGER_MASK;
	unsigned int offset = data->hwirq * 2; /* 2 bits per line */
	unsigned long flags;
	u8 new_type, gpiic;

	if (edge_type == IRQ_TYPE_EDGE_RISING)
		new_type = TQMX86_GPII_RISING;
	else if (edge_type == IRQ_TYPE_EDGE_FALLING)
		new_type = TQMX86_GPII_FALLING;
	else
		return -EINVAL; /* not supported */

	gpio->irq_type[offset] = new_type;

	spin_lock_irqsave(&gpio->spinlock, flags);
	gpiic = tqmx86_gpio_read(gpio, TQMX86_GPIIC);
	gpiic &= ~(3 << offset);
	gpiic |= new_type << offset;
	tqmx86_gpio_write(gpio, gpiic, TQMX86_GPIIC);
	spin_unlock_irqrestore(&gpio->spinlock, flags);
	/* TODO: find offset, set GPIIC */

	irqd_set_trigger_type(data, type);

	return IRQ_SET_MASK_OK;
}

static irqreturn_t tqmx86_gpio_irq_cascade(int irq, void *data)
{
	struct tqmx86_gpio_data *gpio = data;
	u8 irq_status = tqmx86_gpio_read(gpio, TQMX86_GPIIS);
	int i;
	unsigned long irq_bits;

	if (!irq_status)
		return IRQ_HANDLED;

	tqmx86_gpio_write(gpio, irq_status, TQMX86_GPIIS);

	irq_bits = irq_status;
	for_each_set_bit(i, &irq_bits, 4)
		generic_handle_irq(irq_find_mapping(gpio->domain, i));

	return IRQ_HANDLED;
}

static int tqmx86_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tqmx86_gpio_data *gpio;
	struct gpio_chip *chip;
	struct resource *res;
	void __iomem *io_base;
	int ret;
	int irq;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (IS_ERR(res)) {
		dev_err(&pdev->dev, "Cannot get I/O\n");
		return PTR_ERR(res);
	}
	io_base = devm_ioport_map(&pdev->dev, res->start, resource_size(res));
	if (IS_ERR(io_base))
		return PTR_ERR(io_base);

	gpio = devm_kzalloc(dev, sizeof(*gpio), GFP_KERNEL);
	if (gpio == NULL)
		return -ENOMEM;

	spin_lock_init(&gpio->spinlock);
	gpio->io_base = io_base;

	tqmx86_gpio_write(gpio, (u8)~TQMX86_DIR_MASK, TQMX86_GPIODD);

	platform_set_drvdata(pdev, gpio);

	chip = &gpio->chip;
	chip->label = "gpio-tqmx86";
	chip->owner = THIS_MODULE;
	chip->can_sleep = false;
	chip->base = -1;
	chip->direction_input = tqmx86_gpio_direction_input;
	chip->direction_output = tqmx86_gpio_direction_output;
	chip->get_direction = tqmx86_gpio_get_direction;
	chip->get = tqmx86_gpio_get;
	chip->set = tqmx86_gpio_set;
	chip->ngpio = TQMX86_NGPIO;

	if (irq) {
		struct irq_chip_generic *gc;

		ret = devm_request_irq(dev, irq, tqmx86_gpio_irq_cascade,
				       IRQF_TRIGGER_NONE,
				       dev_name(dev), gpio);

		if (ret != 0) {
			dev_err(dev, "Can't request irq.\n");
			return ret;
		}

		gpio->domain = irq_domain_add_linear(dev->of_node, 4,
						     &irq_generic_chip_ops,
						     gpio);
		if (!gpio->domain)
			return -ENOMEM;

		ret = irq_alloc_domain_generic_chips(gpio->domain, 4, 1,
						     chip->label,
						     handle_simple_irq,
						     0, 0, 0);
		if (ret)
			return ret;

		gc = gpio->domain->gc->gc[0];
		gc->private = gpio;
		gc->chip_types[0].chip.irq_ack = tqmx86_gpio_irq_noop;
		gc->chip_types[0].chip.irq_mask = tqmx86_gpio_irq_mask;
		gc->chip_types[0].chip.irq_unmask = tqmx86_gpio_irq_unmask;
		gc->chip_types[0].chip.irq_set_type = tqmx86_gpio_irq_set_type;

		chip->to_irq = tqmx86_gpio_to_irq;
	}

	ret = gpiochip_add_data(chip, gpio);
	if (ret) {
		dev_err(dev, "Could not register GPIO chip\n");
		return ret;
	}

	dev_info(dev, "GPIO functionality initialized with %d pins\n",
		 chip->ngpio);

	return 0;
}

static int tqmx86_gpio_remove(struct platform_device *pdev)
{
	struct tqmx86_gpio_data *gpio = platform_get_drvdata(pdev);
	int i;

	if (gpio->chip.to_irq) {
		for (i = 0; i < 4; i++) {
			if (gpio->irqs[i])
				irq_dispose_mapping(gpio->irqs[i]);
		}
		kfree(gpio->domain->gc);
		irq_domain_remove(gpio->domain);
	}
	gpiochip_remove(&gpio->chip);
	return 0;
}

static struct platform_driver tqmx86_gpio_driver = {
	.driver = {
		.name = "tqmx86-gpio",
	},
	.probe		= tqmx86_gpio_probe,
	.remove		= tqmx86_gpio_remove,
};

module_platform_driver(tqmx86_gpio_driver);

MODULE_DESCRIPTION("TQMx86 PLD GPIO Driver");
MODULE_AUTHOR("Vadim V.Vlasov <vvlasov@dev.rtsoft.ru>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:tqmx86-gpio");
