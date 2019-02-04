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

#include <linux/bitfield.h>
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

static int mv88e6xxx_g2_scratch_misc_read(struct mv88e6xxx_chip *chip, u8 index, u8 *data)
{
	u16 val;
	int err;

	if (chip->info->family != MV88E6XXX_FAMILY_6352 &&
	    chip->info->family != MV88E6XXX_FAMILY_6390)
		return -EOPNOTSUPP;

	val = index << __bf_shf(MV88E6XXX_G2_SCRATCH_MISC_PTR_MASK);
	val &= MV88E6XXX_G2_SCRATCH_MISC_PTR_MASK;

	err = mv88e6xxx_g2_write(chip, MV88E6XXX_G2_SCRATCH_MISC_MISC, val);
	if (err)
		return err;

	err = mv88e6xxx_g2_read(chip, MV88E6XXX_G2_SCRATCH_MISC_MISC, &val);
	if (err)
		return err;

	*data = val & MV88E6XXX_G2_SCRATCH_MISC_DATA_MASK;

	return 0;
}

static int mv88e6xxx_g2_scratch_misc_write(struct mv88e6xxx_chip *chip, u8 index, u8 data)
{
	u16 val;

	if (chip->info->family != MV88E6XXX_FAMILY_6352 &&
	    chip->info->family != MV88E6XXX_FAMILY_6390)
		return -EOPNOTSUPP;

	val = index << __bf_shf(MV88E6XXX_G2_SCRATCH_MISC_PTR_MASK);
	val &= MV88E6XXX_G2_SCRATCH_MISC_PTR_MASK;

	val |= data & MV88E6XXX_G2_SCRATCH_MISC_DATA_MASK;

	val |= MV88E6XXX_G2_SCRATCH_MISC_UPDATE;

	return mv88e6xxx_g2_write(chip, MV88E6XXX_G2_SCRATCH_MISC_MISC, val);
}

static ssize_t scratch_show(struct device *dev, struct device_attribute *attr, char *buf, int scratch)
{
	struct dsa_switch *ds = dev_get_drvdata(dev);
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;
	u8 val;

	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_g2_scratch_misc_read(chip, scratch, &val);
	mutex_unlock(&chip->reg_lock);
	if (err)
		return err;

	return snprintf(buf, PAGE_SIZE, "0x%.2x\n", val);
}

static ssize_t scratch_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count, int scratch)
{
	struct dsa_switch *ds = dev_get_drvdata(dev);
	struct mv88e6xxx_chip *chip = ds->priv;
	unsigned int val;
	int err;

	if (sscanf(buf, "%x", &val) != 1 || val > 0xff)
		return -EINVAL;

	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_g2_scratch_misc_write(chip, scratch, val);
	mutex_unlock(&chip->reg_lock);
	if (err)
		return err;

	return count;
}

static ssize_t global2scratch0_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scratch_show(dev, attr, buf, 0);
}

static ssize_t global2scratch0_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	return scratch_store(dev, attr, buf, count, 0);
}

static ssize_t global2scratch1_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scratch_show(dev, attr, buf, 1);
}

static ssize_t global2scratch1_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	return scratch_store(dev, attr, buf, count, 1);
}

static DEVICE_ATTR_RW(global2scratch0);
static DEVICE_ATTR_RW(global2scratch1);

static struct attribute *mv88e6xxx_sysfs_attrs[] = {
#if IS_ENABLED(CONFIG_NET_DSA_MV88E6XXX_ZII_RDU)
	&dev_attr_bpdu_bypass.attr,
#endif
	&dev_attr_global2scratch0.attr,
	&dev_attr_global2scratch1.attr,
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
