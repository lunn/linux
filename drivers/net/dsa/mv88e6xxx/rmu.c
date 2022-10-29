// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Marvell 88E6xxx Switch Remote Management Unit Support
 *
 * Copyright (c) 2022 Mattias Forsblad <mattias.forsblad@gmail.com>
 *
 */

#include <net/dsa.h>
#include "chip.h"
#include "global1.h"
#include "rmu.h"

void mv88e6xxx_rmu_conduit_state_change(struct dsa_switch *ds,
					const struct net_device *master,
					bool operational)
{
	struct dsa_port *cpu_dp = master->dsa_ptr;
	struct mv88e6xxx_chip *chip = ds->priv;
	int port;
	int ret;

	port = dsa_towards_port(ds, cpu_dp->ds->index, cpu_dp->index);

	mv88e6xxx_reg_lock(chip);

	if (operational && chip->info->ops->rmu_enable) {
		ret = chip->info->ops->rmu_enable(chip, port);

		if (ret == -EOPNOTSUPP)
			goto out;

		if (ret < 0) {
			dev_err(chip->dev, "RMU: Unable to enable on port %d %pe",
				port, ERR_PTR(ret));
			goto out;
		}

		chip->rmu_master = (struct net_device *)master;

		dev_dbg(chip->dev, "RMU: Enabled on port %d", port);
	} else {
		if (chip->info->ops->rmu_disable)
			chip->info->ops->rmu_disable(chip);

		chip->rmu_master = NULL;
	}

out:
	mv88e6xxx_reg_unlock(chip);
}
