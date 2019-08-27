/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Marvell 88E6xxx debugfs interface support
 *
 * Copyright (c) 2016-2019 Vivien Didelot <vivien.didelot@gmail.com>
 */

#ifndef _MV88E6XXX_DEBUGFS_H
#define _MV88E6XXX_DEBUGFS_H

void mv88e6xxx_dbg_create(struct mv88e6xxx_chip *chip);
void mv88e6xxx_dbg_destroy(struct mv88e6xxx_chip *chip);

#endif /* _MV88E6XXX_DEBUGFS_H */
