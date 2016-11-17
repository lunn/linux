/*
 * Marvell 88E6xxx SERDES manipulation, via SMI bus
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2016 Andrew Lunn <andrew@lunn.ch>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _MV88E6XXX_SERDES_H
#define _MV88E6XXX_SERDES_H

#include "mv88e6xxx.h"

int mv88e6352_serdes_power(struct mv88e6xxx_chip *chip, int port, bool on);

#endif
