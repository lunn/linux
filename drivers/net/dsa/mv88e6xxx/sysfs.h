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

#ifndef _MV88E6XXX_SYSFS_H
#define _MV88E6XXX_SYSFS_H

struct mv88e6xxx_chip;

int mv88e6xxx_sysfs_setup(struct mv88e6xxx_chip *chip);
void mv88e6xxx_sysfs_teardown(struct mv88e6xxx_chip *chip);

#endif /* _MV88E6XXX_SYSFS_H */
