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
#include "global2.h"

#if IS_ENABLED(CONFIG_NET_DSA_MV88E6XXX_ZII_RDU)
static int mv88e6xxx_g2_mgmt_en_00(struct mv88e6xxx_chip *chip, bool enabled)
{
	u16 val;
	int err;

	err = mv88e6xxx_g2_read(chip, MV88E6XXX_G2_MGMT_EN_0X, &val);
	if (err)
		return err;

	if (enabled)
		val &= ~BIT(0);
	else
		val |= BIT(0);

	return mv88e6xxx_g2_write(chip, MV88E6XXX_G2_MGMT_EN_0X, val);
}

static ssize_t bpdu_bypass_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dsa_switch *ds = dev_get_drvdata(dev);
	struct mv88e6xxx_chip *chip = ds->priv;
	u16 val;
	int err;

	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_g2_read(chip, MV88E6XXX_G2_MGMT_EN_0X, &val);
	mutex_unlock(&chip->reg_lock);
	if (err)
		return err;

	return snprintf(buf, PAGE_SIZE, "%u\n", !(val & BIT(0)));
}

static ssize_t bpdu_bypass_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct dsa_switch *ds = dev_get_drvdata(dev);
	struct mv88e6xxx_chip *chip = ds->priv;
	unsigned int enabled;
	int err;

	if (sscanf(buf, "%u", &enabled) != 1 || enabled > 1)
		return -EINVAL;

	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_g2_mgmt_en_00(chip, enabled);
	mutex_unlock(&chip->reg_lock);
	if (err)
		return err;

	return count;
}

static DEVICE_ATTR_RW(bpdu_bypass);
#endif

static struct attribute *mv88e6xxx_sysfs_attrs[] = {
#if IS_ENABLED(CONFIG_NET_DSA_MV88E6XXX_ZII_RDU)
	&dev_attr_bpdu_bypass.attr,
#endif
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
