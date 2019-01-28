// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Aquantia PHY
 *
 * Author: Shaohui Xie <Shaohui.Xie@freescale.com>
 *
 * Copyright 2015 Freescale Semiconductor, Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/hwmon.h>
#include <linux/phy.h>
#include <linux/mdio.h>

#define PHY_ID_AQ1202	0x03a1b445
#define PHY_ID_AQ2104	0x03a1b460
#define PHY_ID_AQR105	0x03a1b4a2
#define PHY_ID_AQR106	0x03a1b4d0
#define PHY_ID_AQR107	0x03a1b4e0
#define PHY_ID_AQCS109	0x03a1b5c2
#define PHY_ID_AQR405	0x03a1b4b0

#define MDIO_AN_TX_VEND_STATUS1			0xc800
#define MDIO_AN_TX_VEND_STAUTS1_FULL_DUPLEX	BIT(0)
#define MDIO_AN_TX_VEND_STAUTS1_10BASET		(0x0 << 1)
#define MDIO_AN_TX_VEND_STAUTS1_100BASETX	(0x1 << 1)
#define MDIO_AN_TX_VEND_STAUTS1_1000BASET	(0x2 << 1)
#define MDIO_AN_TX_VEND_STAUTS1_10GBASET	(0x3 << 1)
#define MDIO_AN_TX_VEND_STAUTS1_2500BASET	(0x4 << 1)
#define MDIO_AN_TX_VEND_STAUTS1_5000BASET	(0x5 << 1)
#define MDIO_AN_TX_VEND_STAUTS1_RATE_MASK	(0x7 << 1)
#define MDIO_AN_TX_VEND_INT_STATUS2		0xcc01
#define MDIO_AN_TX_VEND_INT_MASK2		0xd401

/* PHY XS System Interface Connection Status */
#define MDIO_XS_SYSIF_STATUS			0xe812
#define MDIO_XS_SYSIF_MODE_SHIFT		3
#define MDIO_XS_SYSIF_MODE_MASK			0x1f
#define MDIO_XS_SYSIF_MODE_BACKPLANE_KR		0
#define MDIO_XS_SYSIF_MODE_BACKPLANE_KX		1
#define MDIO_XS_SYSIF_MODE_XFI			2
#define MDIO_XS_SYSIF_MODE_USXGMII		3
#define MDIO_XS_SYSIF_MODE_XAUI			4
#define MDIO_XS_SYSIF_MODE_XAUI_PAUSE		5
#define MDIO_XS_SYSIF_MODE_SGMII		6
#define MDIO_XS_SYSIF_MODE_RXAUI		7
#define MDIO_XS_SYSIF_MODE_MAC			8
#define MDIO_XS_SYSIF_MODE_OFF			9
#define MDIO_XS_SYSIF_MODE_OCSGMII		10

/* Vendor specific 1, MMIO_MMD_VEND1 */
#define VEND1_THERMAL_PROV_HIGH_TEMP_FAIL	0xc421
#define VEND1_THERMAL_PROV_LOW_TEMP_FAIL	0xc422
#define VEND1_THERMAL_PROV_HIGH_TEMP_WARN	0xc423
#define VEND1_THERMAL_PROV_LOW_TEMP_WARN	0xc424
#define VEND1_THERMAL_STAT1			0xc820
#define VEND1_THERMAL_STAT2			0xc821
#define VEND1_THERMAL_STAT2_VALID		BIT(0)
#define VEND1_GENERAL_STAT1			0xc830
#define VEND1_GENERAL_STAT1_HIGH_TEMP_FAIL	BIT(0xe)
#define VEND1_GENERAL_STAT1_LOW_TEMP_FAIL	BIT(0xd)
#define VEND1_GENERAL_STAT1_HIGH_TEMP_WARN	BIT(0xc)
#define VEND1_GENERAL_STAT1_LOW_TEMP_WARN	BIT(0xb)

#define VEND1_GLOBAL_INT_STD_MASK		0xff00
#define VEND1_GLOBAL_INT_STD_MASK_ALL		BIT(0x0)
#define VEND1_GLOBAL_INT_STD_MASK_GBE		BIT(0x6)
#define VEND1_GLOBAL_INT_STD_MASK_AN2		BIT(0x7)
#define VEND1_GLOBAL_INT_STD_MASK_AN1		BIT(0x8)
#define VEND1_GLOBAL_INT_STD_MASK_PHY_XS2	BIT(0x9)
#define VEND1_GLOBAL_INT_STD_MASK_PHY_XS1	BIT(0xa)
#define VEND1_GLOBAL_INT_STD_MASK_PCS3		BIT(0xb)
#define VEND1_GLOBAL_INT_STD_MASK_PCS3		BIT(0xb)
#define VEND1_GLOBAL_INT_STD_MASK_PCS2		BIT(0xc)
#define VEND1_GLOBAL_INT_STD_MASK_PCS1		BIT(0xd)
#define VEND1_GLOBAL_INT_STD_MASK_PMA2		BIT(0xe)
#define VEND1_GLOBAL_INT_STD_MASK_PMA1		BIT(0xf)

#define VEND1_GLOBAL_INT_VEND_MASK		0xff01
#define VEND1_GLOBAL_INT_VEND_MASK_GLOBAL3	BIT(0x0)
#define VEND1_GLOBAL_INT_VEND_MASK_GLOBAL2	BIT(0x1)
#define VEND1_GLOBAL_INT_VEND_MASK_GLOBAL1	BIT(0x2)
#define VEND1_GLOBAL_INT_VEND_MASK_GBE		BIT(0xb)
#define VEND1_GLOBAL_INT_VEND_MASK_AN		BIT(0xc)
#define VEND1_GLOBAL_INT_VEND_MASK_PHY_XS	BIT(0xd)
#define VEND1_GLOBAL_INT_VEND_MASK_PCS		BIT(0xe)
#define VEND1_GLOBAL_INT_VEND_MASK_PMA		BIT(0xf)

struct aqr_priv {
	struct device *hwmon_dev;
	char *hwmon_name;
};

static int aqr_config_aneg(struct phy_device *phydev)
{
	linkmode_copy(phydev->supported, phy_10gbit_features);
	linkmode_copy(phydev->advertising, phydev->supported);

	return 0;
}

static int aqr_config_init(struct phy_device *phydev)
{
	/* Check that the PHY interface type is compatible */
	if (phydev->interface != PHY_INTERFACE_MODE_SGMII &&
	    phydev->interface != PHY_INTERFACE_MODE_XAUI &&
	    phydev->interface != PHY_INTERFACE_MODE_RXAUI &&
	    phydev->interface != PHY_INTERFACE_MODE_10GKR)
		return -ENODEV;

	return 0;
}

static int aqr_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		err = phy_write_mmd(phydev, MDIO_MMD_AN,
				    MDIO_AN_TX_VEND_INT_MASK2, 1);
		if (err < 0)
			return err;

		err = phy_write_mmd(phydev, MDIO_MMD_VEND1,
				    VEND1_GLOBAL_INT_STD_MASK,
				    VEND1_GLOBAL_INT_STD_MASK_ALL);
		if (err < 0)
			return err;

		err = phy_write_mmd(phydev, MDIO_MMD_VEND1,
				    VEND1_GLOBAL_INT_VEND_MASK,
				    VEND1_GLOBAL_INT_VEND_MASK_GLOBAL3 |
				    VEND1_GLOBAL_INT_VEND_MASK_AN);
	} else {
		err = phy_write_mmd(phydev, MDIO_MMD_AN,
				    MDIO_AN_TX_VEND_INT_MASK2, 0);
		if (err < 0)
			return err;

		err = phy_write_mmd(phydev, MDIO_MMD_VEND1,
				    VEND1_GLOBAL_INT_STD_MASK, 0);
		if (err < 0)
			return err;

		err = phy_write_mmd(phydev, MDIO_MMD_VEND1,
				    VEND1_GLOBAL_INT_VEND_MASK, 0);
	}

	return err;
}

static int aqr_ack_interrupt(struct phy_device *phydev)
{
	int reg;

	reg = phy_read_mmd(phydev, MDIO_MMD_AN,
			   MDIO_AN_TX_VEND_INT_STATUS2);
	return (reg < 0) ? reg : 0;
}

static int aqr_read_status(struct phy_device *phydev)
{
	int reg;

	reg = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_STAT1);
	reg = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_STAT1);
	if (reg & MDIO_STAT1_LSTATUS)
		phydev->link = 1;
	else
		phydev->link = 0;

	reg = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_TX_VEND_STATUS1);
	mdelay(10);
	reg = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_TX_VEND_STATUS1);

	switch (reg & MDIO_AN_TX_VEND_STAUTS1_RATE_MASK) {
	case MDIO_AN_TX_VEND_STAUTS1_10GBASET:
		phydev->speed = SPEED_10000;
		break;
	case MDIO_AN_TX_VEND_STAUTS1_5000BASET:
		phydev->speed = SPEED_50000;
		break;
	case MDIO_AN_TX_VEND_STAUTS1_2500BASET:
		phydev->speed = SPEED_2500;
		break;
	case MDIO_AN_TX_VEND_STAUTS1_1000BASET:
		phydev->speed = SPEED_1000;
		break;
	case MDIO_AN_TX_VEND_STAUTS1_100BASETX:
		phydev->speed = SPEED_100;
		break;
	case MDIO_AN_TX_VEND_STAUTS1_10BASET:
		phydev->speed = SPEED_10;
		break;
	default:
		phydev->speed = SPEED_UNKNOWN;
		break;
	}

	if (reg & MDIO_AN_TX_VEND_STAUTS1_FULL_DUPLEX)
		phydev->duplex = DUPLEX_FULL;
	else
		phydev->duplex = DUPLEX_HALF;

	reg = phy_read_mmd(phydev, MDIO_MMD_PHYXS, MDIO_XS_SYSIF_STATUS);

	switch ((reg >> MDIO_XS_SYSIF_MODE_SHIFT) & MDIO_XS_SYSIF_MODE_MASK) {
	case MDIO_XS_SYSIF_MODE_BACKPLANE_KR:
		phydev->interface = PHY_INTERFACE_MODE_10GKR;
		break;
	case MDIO_XS_SYSIF_MODE_SGMII:
		phydev->interface = PHY_INTERFACE_MODE_SGMII;
		break;
	case MDIO_XS_SYSIF_MODE_XAUI:
	case MDIO_XS_SYSIF_MODE_XAUI_PAUSE:
		phydev->interface = PHY_INTERFACE_MODE_XAUI;
		break;
	case MDIO_XS_SYSIF_MODE_RXAUI:
		phydev->interface = PHY_INTERFACE_MODE_RXAUI;
		break;
	default:
		phydev->interface = PHY_INTERFACE_MODE_NA;
	}

	return 0;
}

#ifdef CONFIG_HWMON
static umode_t aqr_hwmon_is_visible(const void *data,
				    enum hwmon_sensor_types type,
				    u32 attr, int channel)
{
	if (type != hwmon_temp)
		return 0;

	switch (attr) {
	case hwmon_temp_input:
	case hwmon_temp_min_alarm:
	case hwmon_temp_max_alarm:
	case hwmon_temp_lcrit_alarm:
	case hwmon_temp_crit_alarm:
	case hwmon_temp_min:
	case hwmon_temp_max:
	case hwmon_temp_lcrit:
	case hwmon_temp_crit:
		return 0444;
	default:
		return 0;
	}
}

static int aqr_hwmon_get(struct phy_device *phydev, int reg, long *value)
{
	int temp = phy_read_mmd(phydev, MDIO_MMD_VEND1, reg);

	if (temp < 0)
		return temp;

	if (temp > 0x8000)
		temp -= 0x10000;

	*value = temp * 1000 / 256;

	return 0;
}

static int aqr_hwmon_status1(struct phy_device *phydev, int bit, long *value)
{
	int reg = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_GENERAL_STAT1);

	if (reg < 0)
		return reg;

	*value = !!(reg & bit);

	return 0;
}

static int aqr_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			  u32 attr, int channel, long *value)
{
	struct phy_device *phydev = dev_get_drvdata(dev);
	int reg;

	if (type != hwmon_temp)
		return -EOPNOTSUPP;

	switch (attr) {
	case hwmon_temp_input:
		reg = phy_read_mmd(phydev, MDIO_MMD_VEND1,
				   VEND1_THERMAL_STAT2);
		if (reg < 0)
			return reg;
		if (!(reg & VEND1_THERMAL_STAT2_VALID))
			return -EIO;

		return aqr_hwmon_get(phydev, VEND1_THERMAL_STAT1, value);

	case hwmon_temp_lcrit:
		return aqr_hwmon_get(phydev, VEND1_THERMAL_PROV_LOW_TEMP_FAIL,
				     value);

	case hwmon_temp_min:
		return aqr_hwmon_get(phydev, VEND1_THERMAL_PROV_LOW_TEMP_WARN,
				     value);

	case hwmon_temp_max:
		return aqr_hwmon_get(phydev, VEND1_THERMAL_PROV_HIGH_TEMP_WARN,
				     value);

	case hwmon_temp_crit:
		return aqr_hwmon_get(phydev, VEND1_THERMAL_PROV_HIGH_TEMP_FAIL,
				     value);

	case hwmon_temp_lcrit_alarm:
		return aqr_hwmon_status1(phydev,
					 VEND1_GENERAL_STAT1_LOW_TEMP_FAIL,
					 value);

	case hwmon_temp_min_alarm:
		return aqr_hwmon_status1(phydev,
					 VEND1_GENERAL_STAT1_LOW_TEMP_WARN,
					 value);

	case hwmon_temp_max_alarm:
		return aqr_hwmon_status1(phydev,
					 VEND1_GENERAL_STAT1_HIGH_TEMP_WARN,
					 value);

	case hwmon_temp_crit_alarm:
		return aqr_hwmon_status1(phydev,
					 VEND1_GENERAL_STAT1_HIGH_TEMP_FAIL,
					 value);
	default:
		return -EOPNOTSUPP;
	}

	return -EOPNOTSUPP;
}

static const struct hwmon_ops aqr_hwmon_ops = {
	.is_visible = aqr_hwmon_is_visible,
	.read = aqr_hwmon_read,
};

static u32 aqr_hwmon_chip_config[] = {
	HWMON_C_REGISTER_TZ,
	0,
};

static const struct hwmon_channel_info aqr_hwmon_chip = {
	.type = hwmon_chip,
	.config = aqr_hwmon_chip_config,
};

static u32 aqr_hwmon_temp_config[] = {
	HWMON_T_INPUT |
	HWMON_T_MAX | HWMON_T_MIN |
	HWMON_T_MAX_ALARM | HWMON_T_MIN_ALARM |
	HWMON_T_CRIT | HWMON_T_LCRIT |
	HWMON_T_CRIT_ALARM | HWMON_T_LCRIT_ALARM,
	0,
};

static const struct hwmon_channel_info aqr_hwmon_temp = {
	.type = hwmon_temp,
	.config = aqr_hwmon_temp_config,
};

static const struct hwmon_channel_info *aqr_hwmon_info[] = {
	&aqr_hwmon_chip,
	&aqr_hwmon_temp,
	NULL,
};

static const struct hwmon_chip_info aqr_hwmon_chip_info = {
	.ops = &aqr_hwmon_ops,
	.info = aqr_hwmon_info,
};

static int aqr_hwmon_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct aqr_priv *priv = dev_get_drvdata(&phydev->mdio.dev);
	int i, j;

	priv->hwmon_name = devm_kstrdup(dev, dev_name(dev), GFP_KERNEL);
	if (!priv->hwmon_name)
		return -ENODEV;

	for (i = j = 0; priv->hwmon_name[i]; i++) {
		if (isalnum(priv->hwmon_name[i])) {
			if (i != j)
				priv->hwmon_name[j] = priv->hwmon_name[i];
			j++;
		}
	}
	priv->hwmon_name[j] = '\0';

	priv->hwmon_dev = devm_hwmon_device_register_with_info(dev,
				priv->hwmon_name, phydev,
				&aqr_hwmon_chip_info, NULL);

	return PTR_ERR_OR_ZERO(priv->hwmon_dev);
}
#else
static inline int aqr_hwmon_config(struct phy_device *phydev, bool enable)
{
	return 0;
}

static int aqr_hwmon_probe(struct phy_device *phydev)
{
	return 0;
}
#endif

static int aqr_probe(struct phy_device *phydev)
{
	struct aqr_priv *priv;
	u32 mmd_mask = MDIO_DEVS_PMAPMD | MDIO_DEVS_AN;
	int ret;

	if (!phydev->is_c45 ||
	    (phydev->c45_ids.devices_in_package & mmd_mask) != mmd_mask)
		return -ENODEV;

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(&phydev->mdio.dev, priv);

	ret = aqr_hwmon_probe(phydev);
	if (ret)
		return ret;

	return 0;
}

static struct phy_driver aqr_driver[] = {
{
	.phy_id		= PHY_ID_AQ1202,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Aquantia AQ1202",
	.aneg_done	= genphy_c45_aneg_done,
	.get_features	= genphy_c45_read_abilities,
	.config_init	= aqr_config_init,
	.config_aneg    = aqr_config_aneg,
	.config_intr	= aqr_config_intr,
	.ack_interrupt	= aqr_ack_interrupt,
	.read_status	= aqr_read_status,
},
{
	.phy_id		= PHY_ID_AQ2104,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Aquantia AQ2104",
	.aneg_done	= genphy_c45_aneg_done,
	.get_features	= genphy_c45_read_abilities,
	.config_init	= aqr_config_init,
	.config_aneg    = aqr_config_aneg,
	.config_intr	= aqr_config_intr,
	.ack_interrupt	= aqr_ack_interrupt,
	.read_status	= aqr_read_status,
},
{
	.phy_id		= PHY_ID_AQR105,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Aquantia AQR105",
	.aneg_done	= genphy_c45_aneg_done,
	.get_features	= genphy_c45_read_abilities,
	.config_init	= aqr_config_init,
	.config_aneg    = aqr_config_aneg,
	.config_intr	= aqr_config_intr,
	.ack_interrupt	= aqr_ack_interrupt,
	.read_status	= aqr_read_status,
},
{
	.phy_id		= PHY_ID_AQR106,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Aquantia AQR106",
	.aneg_done	= genphy_c45_aneg_done,
	.get_features	= genphy_c45_read_abilities,
	.config_init	= aqr_config_init,
	.config_aneg    = aqr_config_aneg,
	.config_intr	= aqr_config_intr,
	.ack_interrupt	= aqr_ack_interrupt,
	.read_status	= aqr_read_status,
},
{
	.phy_id		= PHY_ID_AQR107,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Aquantia AQR107",
	.probe          = aqr_probe,
	.aneg_done	= genphy_c45_aneg_done,
	.get_features	= genphy_c45_read_abilities,
	.config_init	= aqr_config_init,
	.config_aneg    = aqr_config_aneg,
	.config_intr	= aqr_config_intr,
	.ack_interrupt	= aqr_ack_interrupt,
	.read_status	= aqr_read_status,
},
{
	.phy_id		= PHY_ID_AQCS109,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Aquantia AQCS109",
	.probe          = aqr_probe,
	.aneg_done	= genphy_c45_aneg_done,
	.get_features	= genphy_c45_read_abilities,
	.config_init	= aqr_config_init,
	.config_aneg    = aqr_config_aneg,
	.config_intr	= aqr_config_intr,
	.ack_interrupt	= aqr_ack_interrupt,
	.read_status	= aqr_read_status,
},
{
	.phy_id		= PHY_ID_AQR405,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Aquantia AQR405",
	.aneg_done	= genphy_c45_aneg_done,
	.get_features	= genphy_c45_read_abilities,
	.config_init	= aqr_config_init,
	.config_aneg    = aqr_config_aneg,
	.config_intr	= aqr_config_intr,
	.ack_interrupt	= aqr_ack_interrupt,
	.read_status	= aqr_read_status,
},
};

module_phy_driver(aqr_driver);

static struct mdio_device_id __maybe_unused aqr_tbl[] = {
	{ PHY_ID_AQ1202, 0xfffffff0 },
	{ PHY_ID_AQ2104, 0xfffffff0 },
	{ PHY_ID_AQR105, 0xfffffff0 },
	{ PHY_ID_AQR106, 0xfffffff0 },
	{ PHY_ID_AQR107, 0xfffffff0 },
	{ PHY_ID_AQCS109, 0xfffffff0 },
	{ PHY_ID_AQR405, 0xfffffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, aqr_tbl);

MODULE_DESCRIPTION("Aquantia PHY driver");
MODULE_AUTHOR("Shaohui Xie <Shaohui.Xie@freescale.com>");
MODULE_LICENSE("GPL v2");
