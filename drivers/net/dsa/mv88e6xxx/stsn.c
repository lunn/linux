// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Marvell 88E6xxx Spanning Tree Safety Net
 *
 * Copyright (c) 2019 Andrew Lunn <andrew@lunn.ch>
 */

#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <net/dsa.h>

#include "chip.h"
#include "port.h"
#include "serdes.h"
#include "stsn.h"

#define MII_CSCR1 		0x10
#define MII_CSCR1_TX_DISABLE 	BIT(3)

struct dentry *debugfs_zii_hacks;

struct zii_stsn_priv {
	struct dentry *debugfs_dir;
	bool enabled[DSA_MAX_PORTS];
	bool violated[DSA_MAX_PORTS];
};

void zii_stsn_teardown(struct mv88e6xxx_chip *chip)
{
	struct zii_stsn_priv *stsn = chip->stsn;

	debugfs_remove_recursive(stsn->debugfs_dir);
	kfree(stsn);
}

int zii_stsn_setup(struct mv88e6xxx_chip *chip)
{
	struct zii_stsn_priv *stsn;
	char name[64];
	int port;

	stsn = kzalloc(sizeof(*stsn), GFP_KERNEL);
	if (!stsn)
		return -ENOMEM;

	chip->stsn = stsn;

	if (!debugfs_zii_hacks)
		debugfs_zii_hacks = debugfs_create_dir("zii_hacks", NULL);

	stsn->debugfs_dir = debugfs_create_dir(dev_name(chip->dev),
							debugfs_zii_hacks);

	for (port = 0; port < chip->ds->num_ports; port++) {
		if (!dsa_is_user_port(chip->ds, port))
			continue;

		snprintf(name, sizeof(name),
			 "shutdown_link_on_member_violation_%d", port);
		debugfs_create_bool(name, 0600, stsn->debugfs_dir,
				    &stsn->enabled[port]);

		snprintf(name, sizeof(name), "link_shutdown_%d", port);
		debugfs_create_bool(name, 0400, stsn->debugfs_dir,
				    &stsn->violated[port]);
	}

	return 0;
}

static void zii_stsn_phy_tx_disable(struct mv88e6xxx_chip *chip, int port)
{
	struct mii_bus *bus;
	u16 reg;

	if (!chip->info->ops->phy_write || !chip->info->ops->phy_read) {
		dev_err(chip->dev, "%s: No PHY ops\n", __func__);
		return;
	}

	bus = mv88e6xxx_default_mdio_bus(chip);

	chip->info->ops->phy_read(chip, bus, port, MII_CSCR1, &reg);
	reg |= MII_CSCR1_TX_DISABLE;
	chip->info->ops->phy_write(chip, bus, port, MII_CSCR1, reg);
}

void zii_stsn_violation(struct mv88e6xxx_chip *chip, int port)
{
	struct zii_stsn_priv *stsn = chip->stsn;
	u8 cmode = chip->ports[port].cmode;
	u8 lane;

	if (!dsa_is_user_port(chip->ds, port))
		return;

	if (!stsn->enabled[port])
		return;

	switch (cmode) {
	case MV88E6XXX_PORT_STS_CMODE_PHY:
		zii_stsn_phy_tx_disable(chip, port);
		dev_info(chip->dev, "%s: Port %d PHY TX disabled\n",
			 __func__, port);
		break;
	case MV88E6XXX_PORT_STS_CMODE_100BASEX:
	case MV88E6XXX_PORT_STS_CMODE_1000BASEX:
	case MV88E6XXX_PORT_STS_CMODE_SGMII:
	case MV88E6XXX_PORT_STS_CMODE_2500BASEX:
	case MV88E6XXX_PORT_STS_CMODE_XAUI:
	case MV88E6XXX_PORT_STS_CMODE_RXAUI:
		lane = mv88e6xxx_serdes_get_lane(chip, port);
		if (lane && chip->info->ops->serdes_power) {
			chip->info->ops->serdes_power(chip, port, lane, false);
			dev_info(chip->dev, "%s: Port %d SERDES powered down\n",
				 __func__, port);
		} else {
			dev_info(chip->dev,
				 "%s: Port %d missing SERDES lane!\n",
				 __func__, port);
		}
		break;
	default:
		dev_info(chip->dev,
			 "%s: Unsupported CMODE %d. Violation ignored\n",
			 __func__, cmode);
		break;
	}

	if (stsn->violated[port]) {
		dev_info(chip->dev, "Port %d violated again!", port);
		return;
	}

	stsn->violated[port] = true;
}

void zii_stsn_serdes_power(struct mv88e6xxx_chip *chip, int port, bool on)
{
	struct zii_stsn_priv *stsn = chip->stsn;
	if (!dsa_is_user_port(chip->ds, port))
		return;

	if (!stsn->enabled[port])
		return;

	if (stsn->violated[port]) {
		dev_info(chip->dev, "Port %d SERDES powered up\n", port);
		stsn->violated[port]= false;
	}
}
