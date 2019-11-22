/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Marvell 88E6xxx Spanning Tree Safety Net
 *
 * Copyright (c) 2019 Andrew Lunn <andrew@lunn.ch>
 */

#ifndef _MV88E6XXX_STSN_H
#define _MV88E6XXX_STSN_H

int zii_stsn_setup(struct mv88e6xxx_chip *chip);
void zii_stsn_teardown(struct mv88e6xxx_chip *chip);
void zii_stsn_violation(struct mv88e6xxx_chip *chip, int port);
void zii_stsn_serdes_power(struct mv88e6xxx_chip *chip, int port, bool on);
#endif /*  _MV88E6XXX_STSN_H */
