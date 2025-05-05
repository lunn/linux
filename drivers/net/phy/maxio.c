// SPDX-License-Identifier: GPL-2.0+
/*
 * drivers/net/phy/maxio.c
 *
 * Driver for maxio PHYs
 *
 * Author: zhao yang <yang_zhao@maxio-tech.com>
 *
 * Copyright (c) 2004 maxio technology, Inc.
 */
#include <linux/bitops.h>
#include <linux/phy.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/timer.h>
#include <linux/netdevice.h>

#define MAXIO_PAGE_SELECT			    0x1f
#define MAXIO_MAE0621A_INER				0x12
#define MAXIO_MAE0621A_INER_LINK_STATUS	BIT(4)
#define MAXIO_MAE0621A_INSR				0x1d
#define MAXIO_MAE0621A_TX_DELAY			(BIT(6)|BIT(7))
#define MAXIO_MAE0621A_RX_DELAY			(BIT(4)|BIT(5))
#define MAXIO_MAE0621A_CLK_MODE_REG      0x02
#define MAXIO_MAE0621A_WORK_STATUS_REG   0x1d

static int maxio_read_page(struct phy_device *phydev)
{
	return __phy_read(phydev, MAXIO_PAGE_SELECT);
}

static int maxio_write_page(struct phy_device *phydev, int page)
{
	return __phy_write(phydev, MAXIO_PAGE_SELECT, page);
}

static int maxio_mae0621a_clk_init(struct phy_device *phydev)
{
	u32 workmode,clkmode;
	int ret;

	ret = genphy_soft_reset(phydev);
	if (ret < 0)
		return ret;

	//get workmode
	workmode = phy_read_paged(phydev, 0xa43,
				  MAXIO_MAE0621A_WORK_STATUS_REG);

	//get clkmode
	clkmode = phy_read_paged(phydev, 0xd92, MAXIO_MAE0621A_CLK_MODE_REG);

	//abnormal
	if (0 == (workmode&BIT(5))) {
		if (0 == (clkmode&BIT(8))) {
			//oscillator
			phy_write_paged(phydev, 0xd92,
					MAXIO_MAE0621A_CLK_MODE_REG,
					clkmode | BIT(8));
		} else {
			//crystal
			phy_write_paged(phydev, 0xd92,
					MAXIO_MAE0621A_CLK_MODE_REG,
					clkmode &(~ BIT(8)));
		}
	}

	return 0;
}

static int maxio_mae0621a_config_init(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	u16 val;
	int ret;

	maxio_mae0621a_clk_init(phydev);

	phy_disable_eee(phydev);

	//enable auto_speed_down
	ret = phy_write_paged(phydev, 0xd8f, 0x0, 0x300 );

	//adjust TX/RX delay
	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		val = 0x0;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		val = MAXIO_MAE0621A_TX_DELAY | MAXIO_MAE0621A_RX_DELAY;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		val = MAXIO_MAE0621A_RX_DELAY;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		val = MAXIO_MAE0621A_TX_DELAY;
		break;
	default: /* the rest of the modes imply leaving delays as is. */
		goto delay_skip;
	}

	ret = phy_read_paged(phydev, 0xd96, 0x0);
	if (ret < 0) {
		dev_err(dev, "Failed to update the TX delay register\n");
		return ret;
	}

	ret = phy_write_paged(phydev, 0xd96, 0x0, val|ret );
	if (ret < 0) {
		dev_err(dev, "Failed to update the TX delay register\n");
		return ret;
	} else if (ret == 0) {
		dev_dbg(dev,
			"2ns  delay was already %s (by pin-strapping RXD1 or bootloader configuration)\n",
			val ? "enabled" : "disabled");
	}
delay_skip:

	ret = genphy_soft_reset(phydev);
	if (ret < 0)
		return ret;

	return 0;
}


static int maxio_mae0621a_resume(struct phy_device *phydev)
{
	int ret = genphy_resume(phydev);
	if (ret < 0)
		return ret;

	return genphy_soft_reset(phydev);
}

static int maxio_mae0621a_suspend(struct phy_device *phydev)
{
	return genphy_suspend(phydev);
}

static int maxio_mae0621a_probe(struct phy_device *phydev)
{
	int ret = maxio_mae0621a_clk_init(phydev);
	mdelay(100);
	return ret;
}

static struct phy_driver maxio_nc_drvs[] = {
	{
		.phy_id			= 0x7b744411,
		.phy_id_mask	= 0x7fffffff,
		.name			= "MAE0621A Gigabit Ethernet",
		.features		= PHY_GBIT_FEATURES,
		.probe			= maxio_mae0621a_probe,
		.config_init	= maxio_mae0621a_config_init,
		.config_aneg	= genphy_config_aneg,
		.read_status	= genphy_read_status,
		.suspend		= maxio_mae0621a_suspend,
		.resume			= maxio_mae0621a_resume,
		.read_page	= maxio_read_page,
		.write_page	= maxio_write_page,
	},
};
module_phy_driver(maxio_nc_drvs);

MODULE_DESCRIPTION("Maxio PHY driver");
MODULE_AUTHOR("Zhao Yang");
MODULE_LICENSE("GPL");
