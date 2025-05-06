// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for maxio PHYs
 *
 * Author: zhao yang <yang_zhao@maxio-tech.com>
 *
 * Copyright (c) 2004 maxio technology, Inc.
 */
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/phy.h>

#define MAE0621A_PAGE_SELECT		0x1f
#define MAE0621A_INER			0x12
#define MAE0621A_INER_LINK_STATUS	BIT(4)
#define MAE0621A_INSR			0x1d
#define MAE0621A_TX_DELAY		(BIT(6) | BIT(7))
#define MAE0621A_RX_DELAY		(BIT(4) | BIT(5))
#define MAE0621A_CLK_MODE_REG		0x02
#define MAE0621A_WORK_STATUS_REG	0x1d

static int mae0621a_read_page(struct phy_device *phydev)
{
	return __phy_read(phydev, MAE0621A_PAGE_SELECT);
}

static int mae0621a_write_page(struct phy_device *phydev, int page)
{
	return __phy_write(phydev, MAE0621A_PAGE_SELECT, page);
}

static int mae0621a_clk_init(struct phy_device *phydev)
{
	int workmode, clkmode;
	int ret;

	ret = genphy_soft_reset(phydev);
	if (ret < 0)
		return ret;

	workmode = phy_read_paged(phydev, 0xa43,
				  MAE0621A_WORK_STATUS_REG);
	if (workmode < 0)
		return workmode;

	clkmode = phy_read_paged(phydev, 0xd92, MAE0621A_CLK_MODE_REG);
	if (clkmode < 0)
		return clkmode;

	if (0 == (workmode & BIT(5))) {
		if (0 == (clkmode & BIT(8))) {
			/* oscillator */
			ret = phy_write_paged(phydev, 0xd92,
					      MAE0621A_CLK_MODE_REG,
					      clkmode | BIT(8));
		} else {
			/* crystal */
			ret = phy_write_paged(phydev, 0xd92,
					      MAE0621A_CLK_MODE_REG,
					      clkmode & ~BIT(8));
		}
	}

	return ret;
}

static int mae0621a_config_init(struct phy_device *phydev)
{
	u16 val;
	int ret;

	mae0621a_clk_init(phydev);

	phy_disable_eee(phydev);

	/* enable downshift */
	ret = phy_write_paged(phydev, 0xd8f, 0x0, 0x300);
	if (ret < 0)
		return ret;

	/* adjust TX/RX delay */
	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		val = 0x0;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		val = MAE0621A_TX_DELAY | MAE0621A_RX_DELAY;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		val = MAE0621A_RX_DELAY;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		val = MAE0621A_TX_DELAY;
		break;
	default: /* the rest of the modes imply leaving delays as is. */
		goto delay_skip;
	}

	ret = phy_read_paged(phydev, 0xd96, 0x0);
	if (ret < 0) {
		phydev_err(phydev, "Failed to read RGMII delay register\n");
		return ret;
	}

	ret &= ~(MAE0621A_TX_DELAY | MAE0621A_RX_DELAY);

	ret = phy_write_paged(phydev, 0xd96, 0x0, val | ret);
	if (ret < 0) {
		phydev_err(phydev, "Failed to write RGMII delay register\n");
		return ret;
	}

delay_skip:
	ret = genphy_soft_reset(phydev);
	if (ret < 0)
		return ret;

	return 0;
}

static int mae0621a_resume(struct phy_device *phydev)
{
	int ret;

	ret = genphy_resume(phydev);
	if (ret < 0)
		return ret;

	return genphy_soft_reset(phydev);
}

static int mae0621a_probe(struct phy_device *phydev)
{
	int ret;

	ret = mae0621a_clk_init(phydev);
	mdelay(100);
	return ret;
}

static struct phy_driver mae0621_drvs[] = {
	{
		PHY_ID_MATCH_EXACT(0x7b744411),
		.name		= "MAE0621A Gigabit Ethernet",
		.features	= PHY_GBIT_FEATURES,
		.probe		= mae0621a_probe,
		.config_init	= mae0621a_config_init,
		.config_aneg	= genphy_config_aneg,
		.read_status	= genphy_read_status,
		.suspend	= genphy_suspend,
		.resume		= mae0621a_resume,
		.read_page	= mae0621a_read_page,
		.write_page	= mae0621a_write_page,
	},
};
module_phy_driver(mae0621_drvs);

static const struct mdio_device_id __maybe_unused mae0621a_tbl[] = {
	{ PHY_ID_MATCH_EXACT(0x7b744411) },
	{ }
};

MODULE_DEVICE_TABLE(mdio, mae0621a_tbl);

MODULE_DESCRIPTION("MAE0621A PHY driver");
MODULE_AUTHOR("Zhao Yang");
MODULE_LICENSE("GPL");
