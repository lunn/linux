/*
 * Marvell 88E6xxx sysfs support
 *
 * Copyright (c) 2019 Zodiac Inflight Innovations
 *	Vivien Didelot <vivien.didelot@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/sysfs.h>

#include "chip.h"

static struct attribute *mv88e6xxx_sysfs_attrs[] = {
	NULL
};

static const struct attribute_group mv88e6xxx_sysfs_group = {
	.name	= "zii",
	.attrs	= mv88e6xxx_sysfs_attrs,
};

int mv88e6xxx_sysfs_setup(struct mv88e6xxx_chip *chip)
{
	return sysfs_create_group(&chip->dev->kobj, &mv88e6xxx_sysfs_group);
}

void mv88e6xxx_sysfs_teardown(struct mv88e6xxx_chip *chip)
{
	sysfs_remove_group(&chip->dev->kobj, &mv88e6xxx_sysfs_group);
}
