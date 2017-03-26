/*
 * Marvell 88E6xxx Switch dpipe support
 *
 * Copyright (c) 2017 Andrew Lunn <andrew@lunn.ch>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _MV88E6XXX_DPIPE_H
#define _MV88E6XXX_DPIPE_H

int mv88e6xxx_dpipe_register(struct mv88e6xxx_chip *chip);
void mv88e6xxx_dpipe_unregister(struct mv88e6xxx_chip *chip);

#endif /* _MV88E6XXX_DPIPE_H */
