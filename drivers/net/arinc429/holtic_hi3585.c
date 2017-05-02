/*
 * holtic_hi3585.c - HoltIC HI-3585 interface
 *
 * Copyright (C) 2015 Marek Vasut <marex@denx.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/of_gpio.h>

#include <linux/netdevice.h>
#include <linux/skbuff.h>

#include <linux/arinc429/dev.h>

#define HI3585_MASTER_RESET	0x01
#define HI3585_SET_ACLK_DIV	0x07

#define HI3585_READ_FRAME	0x08

#define HI3585_READ_STATUS	0x0a
#define HI3585_READ_CONTROL	0x0b

#define HI3585_WRITE_FRAME	0x0e
#define HI3585_START_XMIT	0x12

struct hi3585_priv;

struct hi3585_chip_priv {
	struct arinc429_priv	arinc429;	/* must be the first member */
	struct net_device	*ndev;
	struct hi3585_priv	*adev;
	int			mux_gpio;
	int			rx_irq;
	int			tx_irq;

	struct timer_list timer;	/* FIXME */
};

struct hi3585_priv {
	struct hi3585_chip_priv *chip[6];
	struct mutex		buslock;
	struct spi_device	*spi;
};

static int spi_cs_sync(struct hi3585_chip_priv *chip, void *tx_buf,
		       void *rx_buf, size_t len)
{
	struct spi_device *spi = chip->adev->spi;
	struct spi_transfer t = {
		.tx_buf		= tx_buf,
		.rx_buf		= rx_buf,
		.len		= len,
	};
	struct spi_message m;
	int ret;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);

	/* First, configure the SPI MUX */
	gpio_set_value_cansleep(chip->mux_gpio, 1);

	/* Next, perform the SPI transfer */
	ret = spi_sync(spi, &m);

	/* Finally, deconfigure the SPI MUX */
	gpio_set_value_cansleep(chip->mux_gpio, 0);

	return ret;
}

static int hi3585_set_clock(struct net_device *dev)
{
	struct hi3585_chip_priv *chip = netdev_priv(dev);
	struct arinc429_clock *aclk = &chip->arinc429.clock;
	u8 tx_buf[2] = { HI3585_SET_ACLK_DIV, 0x01 };

	netdev_info(dev, "setting clock= %i Hz\n", aclk->freq);

	tx_buf[1] = 0x01;	/* FIXME */

	return spi_cs_sync(chip, tx_buf, NULL, sizeof(tx_buf));
}

static int hi3585_start(struct net_device *dev)
{
	struct hi3585_chip_priv *chip = netdev_priv(dev);
	u8 tx_buf = HI3585_MASTER_RESET;

	if (chip->arinc429.state != ARINC429_STATE_STOPPED) {
		netdev_info(dev, "resetting device\n");
		return spi_cs_sync(chip, &tx_buf, NULL, 1);
	}

	return 0;
}

static int hi3585_set_mode(struct net_device *dev, enum arinc429_mode mode)
{
	int ret;

	switch (mode) {
	case ARINC429_MODE_START:
		ret = hi3585_start(dev);
		if (ret)
			return ret;
		if (netif_queue_stopped(dev))
			netif_wake_queue(dev);
		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int hi3585_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct hi3585_chip_priv *chip = netdev_priv(dev);
	struct arinc429_frame *af = (struct arinc429_frame *)skb->data;
	u8 tx_buf[5] = { HI3585_WRITE_FRAME, 0, 0, 0, 0 };
	u8 start_xmit = HI3585_START_XMIT;
	int ret;

	netdev_info(dev, "tx frame [label=%02x data=%02x %02x %02x]\n",
		    af->label, af->data[0], af->data[1], af->data[2]);

	if (arinc429_dropped_invalid_skb(dev, skb))
		return NETDEV_TX_OK;

	netif_stop_queue(dev);

	tx_buf[1] = af->label;
	tx_buf[2] = af->data[0];
	tx_buf[3] = af->data[1];
	tx_buf[4] = af->data[2];

	arinc429_put_echo_skb(skb, dev, 0);

	/* Load the TX buffer into the hardware. */
	ret = spi_cs_sync(chip, tx_buf, NULL, sizeof(tx_buf));
	if (ret)
		return ret;

	/* Start the transmission. */
	return spi_cs_sync(chip, &start_xmit, NULL, 1);
}

static void hi3585_rx(struct net_device *dev)
{
	struct hi3585_chip_priv *chip = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct arinc429_frame *af;
	struct sk_buff *skb;
	u8 tx_buf[5] = { HI3585_READ_FRAME, 0, 0, 0, 0 };
	u8 rx_buf[5] = { 0, 0, 0, 0, 0 };
	int ret;

	skb = alloc_arinc429_skb(dev, &af);
	if (skb == NULL)
		return;

	ret = spi_cs_sync(chip, tx_buf, rx_buf, sizeof(tx_buf));
	if (ret)
		return;

	af->label = rx_buf[0];
	af->data[0] = rx_buf[1];
	af->data[1] = rx_buf[2];
	af->data[2] = rx_buf[3];

	netif_rx(skb);

	stats->rx_packets++;
	stats->rx_bytes += sizeof(*af);
}


static irqreturn_t hi3585_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct hi3585_chip_priv *chip = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;

	if (irq == chip->tx_irq) {
		/* transmission complete interrupt */
		stats->tx_packets++;
		stats->tx_bytes += ARINC429_MTU;
		arinc429_get_echo_skb(dev, 0);
		netif_wake_queue(dev);
	} else if (irq == chip->rx_irq) {
		/* receive interrupt */
		hi3585_rx(dev);
	} else {
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

/* FIXME FIXME FIXME */
void timer_intr(struct timer_list *t)
{
	struct hi3585_chip_priv *chip = from_timer(chip, t, timer);

	u8 tx_buf[2] = { HI3585_READ_STATUS, 0 };
	u8 rx_buf[2] = { 0, 0 };
	int ret;

	/* Load the TX buffer into the hardware. */
	ret = spi_cs_sync(chip, tx_buf, rx_buf, sizeof(tx_buf));
	WARN_ON(ret != 0);

	pr_err("stat = %02x %02x\n", rx_buf[0], rx_buf[1]);

	if (!(rx_buf[1] & 0x01))	/* RX FIFO not empty */
		hi3585_interrupt(chip->rx_irq, chip->ndev);

	if (rx_buf[1] & 0x08)		/* TX FIFO empty */
		hi3585_interrupt(chip->tx_irq, chip->ndev);

	/* restart timer (one second) */
	mod_timer(&chip->timer, round_jiffies(jiffies + HZ));
}

static int hi3585_open(struct net_device *dev)
{
	int ret;

	/* common open */
	ret = open_arinc429dev(dev);
	if (ret)
		goto exit_open;

	hi3585_start(dev);

	netif_start_queue(dev);

	return 0;
exit_open:
	return ret;
}

static int hi3585_close(struct net_device *dev)
{
	netif_stop_queue(dev);

	close_arinc429dev(dev);

	return 0;
}

static const struct net_device_ops hi3585_netdev_ops = {
	.ndo_open               = hi3585_open,
	.ndo_stop               = hi3585_close,
	.ndo_start_xmit         = hi3585_start_xmit,
	.ndo_change_mtu         = arinc429_change_mtu,
};

static int alloc_hi3585dev(struct hi3585_priv *adev, const int id,
			   const int gpio)
{
	struct net_device *dev;
	struct hi3585_chip_priv *chip;
	int ret;

	dev = alloc_arinc429dev(sizeof(*chip), NULL, 1);
	if (!dev)
		return -ENOMEM;

	chip = netdev_priv(dev);

	chip->adev = adev;

	chip->mux_gpio = gpio;
/* FIXME: tx_irq and rx_irq
	rx_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	tx_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 1);
	if (!rx_irq || !tx_irq) {
		err = -EINVAL;
		goto exit;
	}

	chip->rx_irq = rx_irq->start;
	chip->tx_irq = tx_irq->start;
*/

	chip->ndev = dev;
	chip->arinc429.do_set_clock = hi3585_set_clock;
	chip->arinc429.do_set_mode = hi3585_set_mode;
/*	chip->arinc429.ctrlmode_supported = CAN_CTRLMODE_3_SAMPLES;*/
	chip->arinc429.clock.freq = /*get_sclk()*/1000000;	/* FIXME */

	/* FIXME: Replace with RFLAG/TFLAG IRQs */
	chip->rx_irq = 1;
	chip->tx_irq = 2;
	timer_setup(&chip->timer, timer_intr, 0);
	mod_timer(&chip->timer, round_jiffies(jiffies + HZ));

	SET_NETDEV_DEV(dev, &adev->spi->dev);

	dev->flags |= IFF_ECHO;	/* we support local echo */
	dev->netdev_ops = &hi3585_netdev_ops;

	ret = register_arinc429dev(dev);
	if (ret) {
		dev_err(&adev->spi->dev, "registering failed (ret=%d)\n", ret);
		return ret;
	}

	dev_info(&adev->spi->dev, "device registered (gpio=%i)\n", gpio);

	adev->chip[id] = chip;

	return 0;
}

static int hi3585_spi_probe(struct spi_device *spi)
{
/*	int err;
	struct hi3585_priv *priv;
	struct resource *res_mem, *rx_irq, *tx_irq, *err_irq;
	unsigned short *pdata;
*/
	struct device *dev = &spi->dev;
	struct device_node *np = dev->of_node;
	struct hi3585_priv *adev;
	int i, ret;

	adev = devm_kzalloc(dev, sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return -ENOMEM;

	mutex_init(&adev->buslock);

	adev->spi = spi;

	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret < 0)
		return ret;

	dev_set_drvdata(dev, adev);

	for (i = 0; i < 6; i++) {
		int gpio = of_get_named_gpio(np, "mux-gpios", i);
		if (!gpio_is_valid(gpio))
			continue;

		ret = devm_gpio_request(dev, gpio, "hi3585");
		if (ret) {
			dev_err(dev, "can't get mux gpios\n");
			goto exit_arinc429dev_free;
		}
		gpio_direction_output(gpio, 0);

		ret = alloc_hi3585dev(adev, i, gpio);
		if (ret)
			goto exit_arinc429dev_free;
	}

	return 0;

exit_arinc429dev_free:
	for (i = 0; i < 6; i++)
		free_arinc429dev(adev->chip[i]->ndev);

	return ret;
}

static int hi3585_spi_remove(struct spi_device *spi)
{
	struct hi3585_priv *adev = dev_get_drvdata(&spi->dev);
	int i;

	for (i = 0; i < 6; i++) {
		unregister_arinc429dev(adev->chip[i]->ndev);
		free_arinc429dev(adev->chip[i]->ndev);
	}

	return 0;
}

static const struct spi_device_id hi3585_id[] = {
	{ "hi3585", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, hi3585_id);

static struct spi_driver hi3585_driver = {
	.driver = {
		.name = "hi3585",
		.owner = THIS_MODULE,
	},
	.probe = hi3585_spi_probe,
	.remove = hi3585_spi_remove,
	.id_table = hi3585_id,
};
module_spi_driver(hi3585_driver);

MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_DESCRIPTION("HoltIC HI3585 ARINC429 driver");
MODULE_LICENSE("GPL v2");
