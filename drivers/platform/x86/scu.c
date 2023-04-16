// SPDX-License-Identifier: GPL-2.0
/*
 * SCU board driver
 *
 * Copyright (c) 2012, 2014 Guenter Roeck <linux@roeck-us.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/leds.h>
#include <linux/platform_data/mdio-gpio.h>
#include <linux/platform_device.h>
#include <linux/processor.h>
#include <linux/gpio/machine.h>
#include <linux/mdio-gpio.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/dmi.h>
#include <linux/i2c.h>
//#include <linux/i2c-gpio.h>
#include <linux/version.h>
#include <linux/platform_data/pca953x.h>
#include <linux/platform_data/b53.h>
#include <linux/platform_data/mv88e6xxx.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/nvmem-consumer.h>
#include <net/dsa.h>
#include <asm/byteorder.h>

#define SCU_EXT_GPIO_BASE(x)	(0 + (x) * 8)
#define SCU_EXT_GPIO(chip, x)	(SCU_EXT_GPIO_BASE(chip) + (x))

/* Front panel LEDs */
#define SCU_RD_LED_GPIO		SCU_EXT_GPIO(1, 0)
#define SCU_WLES_LED_GPIO	SCU_EXT_GPIO(1, 1)
#define SCU_LD_FAIL_LED_GPIO	SCU_EXT_GPIO(1, 2)
/* shared control with PIC */
#define SCU_SW_LED_GPIO		SCU_EXT_GPIO(1, 3)

#define SCU_SD_ACTIVE_1_GPIO	SCU_EXT_GPIO(2, 0)
#define SCU_SD_ERROR_1_GPIO	SCU_EXT_GPIO(2, 1)
#define SCU_SD_ACTIVE_2_GPIO	SCU_EXT_GPIO(2, 2)
#define SCU_SD_ERROR_2_GPIO	SCU_EXT_GPIO(2, 3)
#define SCU_SD_ACTIVE_3_GPIO	SCU_EXT_GPIO(2, 4)
#define SCU_SD_ERROR_3_GPIO	SCU_EXT_GPIO(2, 5)

#define SCU_SD_ACTIVE_4_GPIO	SCU_EXT_GPIO(3, 0)
#define SCU_SD_ERROR_4_GPIO	SCU_EXT_GPIO(3, 1)
#define SCU_SD_ACTIVE_5_GPIO	SCU_EXT_GPIO(3, 2)
#define SCU_SD_ERROR_5_GPIO	SCU_EXT_GPIO(3, 3)
#define SCU_SD_ACTIVE_6_GPIO	SCU_EXT_GPIO(3, 4)
#define SCU_SD_ERROR_6_GPIO	SCU_EXT_GPIO(3, 5)

/* SCU4 Specific */
#define SCU4_SD_ACTIVE_1_GPIO	SCU_EXT_GPIO(2, 0)
#define SCU4_SD_ERROR_1_GPIO	SCU_EXT_GPIO(2, 1)
#define SCU4_SD_ACTIVE_2_GPIO	SCU_EXT_GPIO(2, 2)
#define SCU4_SD_ERROR_2_GPIO	SCU_EXT_GPIO(2, 3)
#define SCU4_SD_ACTIVE_3_GPIO	SCU_EXT_GPIO(3, 2)
#define SCU4_SD_ERROR_3_GPIO	SCU_EXT_GPIO(3, 3)

#define SCU4_SD_ACTIVE_4_GPIO	SCU_EXT_GPIO(2, 4)
#define SCU4_SD_ERROR_4_GPIO	SCU_EXT_GPIO(2, 5)
#define SCU4_SD_ACTIVE_5_GPIO	SCU_EXT_GPIO(3, 4)
#define SCU4_SD_ERROR_5_GPIO	SCU_EXT_GPIO(3, 5)
#define SCU4_SD_ACTIVE_6_GPIO	SCU_EXT_GPIO(3, 0)
#define SCU4_SD_ERROR_6_GPIO	SCU_EXT_GPIO(3, 1)

struct __packed eeprom_data {
	unsigned short length;			/* 0 - 1 */
	unsigned char checksum;			/* 2 */
	unsigned char have_gsm_modem;		/* 3 */
	unsigned char have_cdma_modem;		/* 4 */
	unsigned char have_wifi_modem;		/* 5 */
	unsigned char have_rhdd;		/* 6 */
	unsigned char have_dvd;			/* 7 */
	unsigned char have_tape;		/* 8 */
	unsigned char have_humidity_sensor;	/* 9 */
	unsigned char have_fiber_channel;	/* 10 */
	unsigned char lru_part_number[11];	/* 11 - 21 Box Part Number */
	unsigned char lru_revision[7];		/* 22 - 28 Box Revision */
	unsigned char lru_serial_number[7];	/* 29 - 35 Box Serial Number */
	unsigned char lru_date_of_manufacture[7];
				/* 36 - 42 Box Date of Manufacture */
	unsigned char board_part_number[11];
				/* 43 - 53 Base Board Part Number */
	unsigned char board_revision[7];
				/* 54 - 60 Base Board Revision */
	unsigned char board_serial_number[7];
				/* 61 - 67 Base Board Serial Number */
	unsigned char board_date_of_manufacture[7];
				/* 68 - 74 Base Board Date of Manufacture */
	unsigned char board_updated_date_of_manufacture[7];
				/* 75 - 81 Updated Box Date of Manufacture */
	unsigned char board_updated_revision[7];
				/* 82 - 88 Updated Box Revision */
	unsigned char dummy[7];	/* 89 - 95 spare/filler */
};

enum scu_version { scu1, scu2, scu3, scu4, scu4c, unknown };

struct scu_data;

struct scu_platform_data {
	const char *board_type;
	const char *lru_part_number;
	const char *board_part_number;
	const char *board_dash_number;
	enum scu_version version;
	int eeprom_len;
	struct i2c_board_info *i2c_board_info;
	int num_i2c_board_info;
	struct spi_board_info *spi_board_info;
	int num_spi_board_info;
	struct dsa_mv88e6xxx_pdata *dsa_pdata;
	const struct mdio_board_info *dsa_bdinfo;
	void (*init)(struct scu_data *data);
	void (*remove)(struct scu_data *data);
};

struct scu_data {
	struct device *dev;			/* SCU platform device */
	struct net_device *netdev;		/* Ethernet device */
	struct platform_device *mdio_dev;	/* MDIO device */
	struct platform_device *dsa_dev;	/* DSA device */
	struct proc_dir_entry *rave_proc_dir;
	struct mutex write_lock;
	struct platform_device *leds_pdev[3];
	struct i2c_adapter *adapter;		/* I2C adapter */
	struct spi_master *master;		/* SPI master */
	struct i2c_client *client[10];		/* I2C clients */
	struct spi_device *spidev[1];		/* SPI devices */
	const struct scu_platform_data *pdata;
	bool have_write_magic;
	struct eeprom_data eeprom;
	struct nvmem_device *nvmem;
	bool eeprom_accessible;
	bool eeprom_valid;
};

#define SCU_EEPROM_LEN_EEPROM	36
#define SCU_EEPROM_LEN_GEN1	36
#define SCU_EEPROM_LEN_GEN2	75
#define SCU_EEPROM_LEN_GEN3	75		/* Preliminary */

#define SCU_LRU_PARTNUM_GEN1	"00-5001"
#define SCU_LRU_PARTNUM_GEN2	"00-5010"
#define SCU_LRU_PARTNUM_GEN3	"00-5013"
#define SCU_LRU_PARTNUM_GEN4	"00-5031"
#define SCU_LRU_PARTNUM_GEN4_COPPER "00-5032"

#define SCU_ZII_BOARD_PARTNUM	"05-0041"
#define SCU_ZII_BOARD_DASHNUM_SCU3  "01"
#define SCU_ZII_BOARD_DASHNUM_SCU2  "02"
#define SCU_ZII_BOARD_DASHNUM_SCU4  "11"
#define SCU_ZII_BOARD_DASHNUM_SCU4_COPPER  "12"

#define SCU_WRITE_MAGIC		5482328594ULL

/* sysfs */

unsigned char scu_get_checksum(unsigned char *ptr, int len)
{
	unsigned char checksum = 0;
	int i;

	for (i = 0; i < len; i++)
		checksum += ptr[i];
	return checksum;
}

static int scu_update_checksum(struct scu_data *data)
{
	unsigned char checksum;
	int ret;

	data->eeprom.checksum = 0;
	checksum = scu_get_checksum((unsigned char *)&data->eeprom,
				    data->pdata->eeprom_len);
	data->eeprom.checksum = ~checksum + 1;
	ret = nvmem_device_write(data->nvmem,
			0x300 + offsetof(struct eeprom_data, checksum),
			sizeof(data->eeprom.checksum),
			&data->eeprom.checksum);
	if (ret <= 0)
		return ret < 0 ? ret : -EIO;
	return 0;
}

static ssize_t board_type_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct scu_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", data->pdata->board_type);
}

static ssize_t attribute_magic_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct scu_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", data->have_write_magic);
}

static ssize_t attribute_magic_store(struct device *dev,
				     struct device_attribute *devattr,
				     const char *buf, size_t count)
{
	struct scu_data *data = dev_get_drvdata(dev);
	unsigned long long magic;
	int err;

	err = kstrtoull(buf, 10, &magic);
	if (err)
		return err;

	data->have_write_magic = magic == SCU_WRITE_MAGIC;

	return count;
}

static ssize_t scu_object_show(char *buf, char *data, int len)
{
	return snprintf(buf, PAGE_SIZE, "%.*s\n", len, data);
}

static ssize_t scu_object_store(struct scu_data *data, int offset,
				const char *in, char *out, ssize_t len)
{
	char buffer[12] = { 0 };
	char *cp;
	int ret;

	if (!data->have_write_magic)
		return -EACCES;

	strlcpy(buffer, in, sizeof(buffer));
	/* do not copy newline from input string */
	cp = strchr(buffer, '\n');
	if (cp)
		*cp = '\0';

	if (len > sizeof(buffer))
		len = sizeof(buffer);

	mutex_lock(&data->write_lock);
	strncpy(out, buffer, len);

	/* Write entire eeprom if it was marked invalid */
	if (!data->eeprom_valid) {
		offset = 0;
		/* Write checksumed and non checksumed data */
		len = sizeof(data->eeprom);
		out = (char *)&data->eeprom;
	}

	ret = nvmem_device_write(data->nvmem, 0x300 + offset, len, out);
	if (ret <= 0) {
		data->eeprom_valid = false;
		if (ret == 0)
			ret = -EIO;
		goto error;
	}
	if (offset < data->pdata->eeprom_len) {
		/*
		 * Write to checksummed area of eeprom
		 * Update checksum
		 */
		ret = scu_update_checksum(data);
		if (ret < 0) {
			data->eeprom_valid = false;
			goto error;
		}
	}
	data->eeprom_valid = true;
error:
	mutex_unlock(&data->write_lock);
	return ret;
}

static ssize_t lru_part_number_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct scu_data *data = dev_get_drvdata(dev);

	return scu_object_show(buf, data->eeprom.lru_part_number,
			       sizeof(data->eeprom.lru_part_number));
}

static ssize_t lru_part_number_store(struct device *dev,
				     struct device_attribute *devattr,
				     const char *buf, size_t count)
{
	struct scu_data *data = dev_get_drvdata(dev);
	int ret;

	ret  = scu_object_store(data,
				offsetof(struct eeprom_data, lru_part_number),
				buf, data->eeprom.lru_part_number,
				sizeof(data->eeprom.lru_part_number));
	return ret < 0 ? ret : count;
}

static ssize_t lru_serial_number_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct scu_data *data = dev_get_drvdata(dev);

	return scu_object_show(buf, data->eeprom.lru_serial_number,
			       sizeof(data->eeprom.lru_serial_number));
}

static ssize_t lru_serial_number_store(struct device *dev,
				       struct device_attribute *devattr,
				       const char *buf, size_t count)
{
	struct scu_data *data = dev_get_drvdata(dev);
	int ret;

	ret = scu_object_store(data,
				offsetof(struct eeprom_data, lru_serial_number),
				buf, data->eeprom.lru_serial_number,
				sizeof(data->eeprom.lru_serial_number));
	return ret < 0 ? ret : count;
}

static ssize_t lru_revision_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct scu_data *data = dev_get_drvdata(dev);

	return scu_object_show(buf, data->eeprom.lru_revision,
			       sizeof(data->eeprom.lru_revision));
}

static ssize_t lru_revision_store(struct device *dev,
				  struct device_attribute *devattr,
				  const char *buf, size_t count)
{
	struct scu_data *data = dev_get_drvdata(dev);
	int ret;

	ret = scu_object_store(data,
				offsetof(struct eeprom_data, lru_revision),
				buf, data->eeprom.lru_revision,
				sizeof(data->eeprom.lru_revision));
	return ret < 0 ? ret : count;
}

static ssize_t lru_date_of_manufacture_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct scu_data *data = dev_get_drvdata(dev);

	return scu_object_show(buf, data->eeprom.lru_date_of_manufacture,
			       sizeof(data->eeprom.lru_date_of_manufacture));
}

static ssize_t lru_date_of_manufacture_store(struct device *dev,
					     struct device_attribute *devattr,
					     const char *buf, size_t count)
{
	struct scu_data *data = dev_get_drvdata(dev);
	int ret;

	ret = scu_object_store(data,
				offsetof(struct eeprom_data,
					 lru_date_of_manufacture),
				buf, data->eeprom.lru_date_of_manufacture,
				sizeof(data->eeprom.lru_date_of_manufacture));
	return ret < 0 ? ret : count;
}

static ssize_t board_part_number_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct scu_data *data = dev_get_drvdata(dev);

	return scu_object_show(buf, data->eeprom.board_part_number,
			       sizeof(data->eeprom.board_part_number));
}

static ssize_t board_part_number_store(struct device *dev,
				       struct device_attribute *devattr,
				       const char *buf, size_t count)
{
	struct scu_data *data = dev_get_drvdata(dev);
	int ret;

	ret = scu_object_store(data,
				offsetof(struct eeprom_data, board_part_number),
				buf, data->eeprom.board_part_number,
				sizeof(data->eeprom.board_part_number));
	return ret < 0 ? ret : count;
}

static ssize_t board_serial_number_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct scu_data *data = dev_get_drvdata(dev);

	return scu_object_show(buf, data->eeprom.board_serial_number,
			       sizeof(data->eeprom.board_serial_number));
}

static ssize_t board_serial_number_store(struct device *dev,
					 struct device_attribute *devattr,
					 const char *buf, size_t count)
{
	struct scu_data *data = dev_get_drvdata(dev);
	int ret;

	ret = scu_object_store(data,
			offsetof(struct eeprom_data, board_serial_number),
			buf, data->eeprom.board_serial_number,
			sizeof(data->eeprom.board_serial_number));
	return ret < 0 ? ret : count;
}

static ssize_t board_revision_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct scu_data *data = dev_get_drvdata(dev);

	return scu_object_show(buf, data->eeprom.board_revision,
			       sizeof(data->eeprom.board_revision));
}

static ssize_t board_revision_store(struct device *dev,
				   struct device_attribute *devattr,
				   const char *buf, size_t count)
{
	struct scu_data *data = dev_get_drvdata(dev);
	int ret;

	ret = scu_object_store(data,
				offsetof(struct eeprom_data, board_revision),
				buf, data->eeprom.board_revision,
				sizeof(data->eeprom.board_revision));
	return ret < 0 ? ret : count;
}

static ssize_t board_date_of_manufacture_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct scu_data *data = dev_get_drvdata(dev);

	return scu_object_show(buf, data->eeprom.board_date_of_manufacture,
			       sizeof(data->eeprom.board_date_of_manufacture));
}

static ssize_t board_date_of_manufacture_store(struct device *dev,
					      struct device_attribute *devattr,
					      const char *buf, size_t count)
{
	struct scu_data *data = dev_get_drvdata(dev);
	int ret;

	ret = scu_object_store(data,
				offsetof(struct eeprom_data,
					 board_date_of_manufacture),
				buf, data->eeprom.board_date_of_manufacture,
				sizeof(data->eeprom.board_date_of_manufacture));
	return ret < 0 ? ret : count;
}

static ssize_t
board_updated_revision_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct scu_data *data = dev_get_drvdata(dev);

	return scu_object_show(buf, data->eeprom.board_updated_revision,
			       sizeof(data->eeprom.board_updated_revision));
}

static ssize_t
board_updated_revision_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct scu_data *data = dev_get_drvdata(dev);
	int ret;

	ret = scu_object_store(data,
				offsetof(struct eeprom_data,
					 board_updated_revision),
				buf, data->eeprom.board_updated_revision,
				sizeof(data->eeprom.board_updated_revision));
	return ret < 0 ? ret : count;
}

static ssize_t
board_updated_date_of_manufacture_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct scu_data *data = dev_get_drvdata(dev);

	return scu_object_show(buf,
			data->eeprom.board_updated_date_of_manufacture,
			sizeof(data->eeprom.board_updated_date_of_manufacture));
}

static ssize_t
board_updated_date_of_manufacture_store(struct device *dev,
					struct device_attribute *devattr,
					const char *buf, size_t count)
{
	struct scu_data *data = dev_get_drvdata(dev);
	int ret;

	ret = scu_object_store(data,
			offsetof(struct eeprom_data,
				 board_updated_date_of_manufacture),
			buf, data->eeprom.board_updated_date_of_manufacture,
			sizeof(data->eeprom.board_updated_date_of_manufacture));
	return ret < 0 ? ret : count;
}

static DEVICE_ATTR_RO(board_type);
static DEVICE_ATTR_RW(attribute_magic);
static DEVICE_ATTR_RW(lru_part_number);
static DEVICE_ATTR_RW(lru_serial_number);
static DEVICE_ATTR_RW(lru_revision);
static DEVICE_ATTR_RW(lru_date_of_manufacture);			/* SCU2/SCU3/SCU4 only */
static DEVICE_ATTR_RW(board_part_number);			/* SCU2/SCU3/SCU4 only */
static DEVICE_ATTR_RW(board_serial_number);			/* SCU2/SCU3/SCU4 only */
static DEVICE_ATTR_RW(board_revision);				/* SCU2/SCU3/SCU4 only */
static DEVICE_ATTR_RW(board_date_of_manufacture);		/* SCU2/SCU3/SCU4 only */
static DEVICE_ATTR_RW(board_updated_revision);			/* SCU2/SCU3/SCU4 only */
static DEVICE_ATTR_RW(board_updated_date_of_manufacture);	/* SCU2/SCU3/SCU4 only */

static struct attribute *scu_base_attrs[] = {
	&dev_attr_board_type.attr,
	NULL,
};

static struct attribute_group scu_base_group = {
	.attrs = scu_base_attrs,
};

static struct attribute *scu_eeprom_attrs[] = {
	&dev_attr_attribute_magic.attr,
	&dev_attr_lru_part_number.attr,			/* 1 */
	&dev_attr_lru_serial_number.attr,
	&dev_attr_lru_revision.attr,
	&dev_attr_lru_date_of_manufacture.attr,		/* 4 */
	&dev_attr_board_part_number.attr,
	&dev_attr_board_serial_number.attr,
	&dev_attr_board_revision.attr,
	&dev_attr_board_date_of_manufacture.attr,
	&dev_attr_board_updated_revision.attr,
	&dev_attr_board_updated_date_of_manufacture.attr,
	NULL
};

static umode_t scu_attr_is_visible(struct kobject *kobj, struct attribute *attr,
				   int index)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct scu_data *data = dev_get_drvdata(dev);
	umode_t mode = attr->mode;

	/*
	 * If the eeprom has not been processed, disable its attributes.
	 * If it has been processed but is not accessible, disable
	 * write accesses to it.
	 */
	if (index >= 1 && !data->eeprom_accessible)
		mode &= 0444;

	if (index >= 4 && data->pdata->version == scu1)
		return 0;

	return mode;
}

static struct attribute_group scu_eeprom_group = {
	.attrs = scu_eeprom_attrs,
	.is_visible = scu_attr_is_visible,
};

#define SCU_EEPROM_TEST_SCRATCHPAD_SIZE	32

static ssize_t scu_eeprom_test_scratchpad_read(struct file *filp,
					       struct kobject *kobj,
					       struct bin_attribute *attr,
					       char *buf, loff_t off,
					       size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct scu_data *data = dev_get_drvdata(dev);

	if (count == 0)
		return 0;

	if (off >= attr->size)
		return -EFBIG;

	if (off + count >= SCU_EEPROM_TEST_SCRATCHPAD_SIZE)
		count = SCU_EEPROM_TEST_SCRATCHPAD_SIZE - off;

	return nvmem_device_read(data->nvmem, off, count, buf);
}

static ssize_t scu_eeprom_test_scratchpad_write(struct file *filp,
						struct kobject *kobj,
						struct bin_attribute *attr,
						char *buf, loff_t off,
						size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct scu_data *data = dev_get_drvdata(dev);

	if (count == 0)
		return 0;

	if (off >= attr->size)
		return -EFBIG;

	if (off + count >= SCU_EEPROM_TEST_SCRATCHPAD_SIZE)
		count = SCU_EEPROM_TEST_SCRATCHPAD_SIZE - off;

	return nvmem_device_write(data->nvmem, off, count, buf);
}

/* base offset for 32 byte "eeprom_test_scratchpad" file is 0 */

static struct bin_attribute scu_eeprom_test_scratchpad_file = {
	.attr = {
		.name = "eeprom_test_scratchpad",
		.mode = 0644,
	},
	.size = SCU_EEPROM_TEST_SCRATCHPAD_SIZE,
	.read = scu_eeprom_test_scratchpad_read,
	.write = scu_eeprom_test_scratchpad_write,
};

/* platform data */

static struct gpio_led pca_gpio_leds1[] = {
	{			/* bit 0 */
		.name = "scu_status:g:RD",
		.gpio = SCU_RD_LED_GPIO,
		.active_low = 1,
		.default_trigger = "heartbeat",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 1 */
		.name = "scu_status:a:WLess",
		.gpio = SCU_WLES_LED_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 2 */
		.name = "scu_status:r:LDFail",
		.gpio = SCU_LD_FAIL_LED_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 3 */
		.name = "scu_status:a:SW",
		.gpio = SCU_SW_LED_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	}
};

static struct gpio_led_platform_data pca_gpio_led_info1 = {
	.leds = pca_gpio_leds1,
	.num_leds = ARRAY_SIZE(pca_gpio_leds1),
};

static struct gpio_led pca_gpio_leds2[] = {
	{			/* bit 0 */
		.name = "SD1:g:active",
		.gpio = SCU_SD_ACTIVE_1_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 1 */
		.name = "SD1:a:error",
		.gpio = SCU_SD_ERROR_1_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 2 */
		.name = "SD2:g:active",
		.gpio = SCU_SD_ACTIVE_2_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 3 */
		.name = "SD2:a:error",
		.gpio = SCU_SD_ERROR_2_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 4 */
		.name = "SD3:g:active",
		.gpio = SCU_SD_ACTIVE_3_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 5 */
		.name = "SD3:a:error",
		.gpio = SCU_SD_ERROR_3_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	}
};

static struct gpio_led_platform_data pca_gpio_led_info2 = {
	.leds = pca_gpio_leds2,
	.num_leds = ARRAY_SIZE(pca_gpio_leds2),
};

static struct gpio_led pca_gpio_leds3[] = {
	{			/* bit 0 */
		.name = "SD4:g:active",
		.gpio = SCU_SD_ACTIVE_4_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 1 */
		.name = "SD4:a:error",
		.gpio = SCU_SD_ERROR_4_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 2 */
		.name = "SD5:g:active",
		.gpio = SCU_SD_ACTIVE_5_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 3 */
		.name = "SD5:a:error",
		.gpio = SCU_SD_ERROR_5_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 4 */
		.name = "SD6:g:active",
		.gpio = SCU_SD_ACTIVE_6_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 5 */
		.name = "SD6:a:error",
		.gpio = SCU_SD_ERROR_6_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	}
};

static struct gpio_led_platform_data pca_gpio_led_info3 = {
	.leds = pca_gpio_leds3,
	.num_leds = ARRAY_SIZE(pca_gpio_leds3),
};

/* SCU4 LEDs
 *
 * NOTE: The following is the mapping from the schematic, but it
 * appers the layout does not match the schematic.  Use the GPIO
 * defines at the top of this file for the real GPIO mappings.
 */

static struct gpio_led pca_gpio_leds2_scu4[] = {
	{			/* bit 0 */
		.name = "SD1:g:active",
		.gpio = SCU4_SD_ACTIVE_1_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 1 */
		.name = "SD1:a:error",
		.gpio = SCU4_SD_ERROR_1_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 2 */
		.name = "SD2:g:active",
		.gpio = SCU4_SD_ACTIVE_2_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 3 */
		.name = "SD2:a:error",
		.gpio = SCU4_SD_ERROR_2_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 4 */
		.name = "SD3:g:active",
		.gpio = SCU4_SD_ACTIVE_3_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 5 */
		.name = "SD3:a:error",
		.gpio = SCU4_SD_ERROR_3_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	}
};

static struct gpio_led_platform_data pca_gpio_led_info2_scu4 = {
	.leds = pca_gpio_leds2_scu4,
	.num_leds = ARRAY_SIZE(pca_gpio_leds2_scu4),
};

static struct gpio_led pca_gpio_leds3_scu4[] = {
	{			/* bit 0 */
		.name = "SD4:g:active",
		.gpio = SCU4_SD_ACTIVE_4_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 1 */
		.name = "SD4:a:error",
		.gpio = SCU4_SD_ERROR_4_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 2 */
		.name = "SD5:g:active",
		.gpio = SCU4_SD_ACTIVE_5_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 3 */
		.name = "SD5:a:error",
		.gpio = SCU4_SD_ERROR_5_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 4 */
		.name = "SD6:g:active",
		.gpio = SCU4_SD_ACTIVE_6_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
	{			/* bit 5 */
		.name = "SD6:a:error",
		.gpio = SCU4_SD_ERROR_6_GPIO,
		.active_low = 1,
		.default_trigger = "none",
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	}
};

static struct gpio_led_platform_data pca_gpio_led_info3_scu4 = {
	.leds = pca_gpio_leds3_scu4,
	.num_leds = ARRAY_SIZE(pca_gpio_leds3_scu4),
};

static void scu_pca_leds_register(struct device *parent,
				  struct scu_data *data)
{
	data->leds_pdev[0] =
		platform_device_register_data(parent, "leds-gpio", 1,
					      &pca_gpio_led_info1,
					      sizeof(pca_gpio_led_info1));
	if (data->pdata->version == scu4) {
		data->leds_pdev[1] =
			platform_device_register_data(parent, "leds-gpio", 2,
						      &pca_gpio_led_info2_scu4,
						      sizeof(pca_gpio_led_info2_scu4));

		data->leds_pdev[2] =
			platform_device_register_data(parent, "leds-gpio", 3,
						      &pca_gpio_led_info3_scu4,
						      sizeof(pca_gpio_led_info3_scu4));
	} else {
		data->leds_pdev[1] =
			platform_device_register_data(parent, "leds-gpio", 2,
						      &pca_gpio_led_info2,
						      sizeof(pca_gpio_led_info2));

		data->leds_pdev[2] =
			platform_device_register_data(parent, "leds-gpio", 3,
						      &pca_gpio_led_info3,
						      sizeof(pca_gpio_led_info3));
	}
}

static void scu_pca_leds_unregister(struct scu_data *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(data->leds_pdev); i++)
		platform_device_unregister(data->leds_pdev[i]);
}

static const char *pca9538_ext0_gpio_names[8] = {
	"pca9538_ext0:wireless_ena_1",
	"pca9538_ext0:wireless_ena_2",
	"pca9538_ext0:wireless_a_radio_disable",
	"pca9538_ext0:wireless_a_reset",
	"pca9538_ext0:in_spare_1",
	"pca9538_ext0:in_spare_2",
	"pca9538_ext0:wireless_b_radio_disable",
	"pca9538_ext0:wireless_b_reset",
};

static const char *pca9538_ext1_gpio_names[8] = {
	"pca9538_ext1:rd_led_on",
	"pca9538_ext1:wless_led_on",
	"pca9538_ext1:ld_fail_led_on",
	"pca9538_ext1:sw_led_on",
	"pca9538_ext1:discrete_out_1",
	"pca9538_ext1:discrete_out_2",
	"pca9538_ext1:discrete_out_3",
	"pca9538_ext1:discrete_out_4",
};

static const char *pca9538_ext2_gpio_names[8] = {
	"pca9538_ext2:sd_active_1",
	"pca9538_ext2:sd_error_1",
	"pca9538_ext2:sd_active_2",
	"pca9538_ext2:sd_error_2",
	"pca9538_ext2:sd_active_3",
	"pca9538_ext2:sd_error_3",
	"pca9538_ext2:hub_6_reset",
	"pca9538_ext2:hub_6_config_status",
};

static const char *pca9538_ext3_gpio_names[8] = {
	"pca9538_ext3:sd_active_4",
	"pca9538_ext3:sd_error_4",
	"pca9538_ext3:sd_active_5",
	"pca9538_ext3:sd_error_5",
	"pca9538_ext3:sd_active_6",
	"pca9538_ext3:sd_error_6",
	"pca9538_ext3:hub_2_reset",
	"pca9538_ext3:hub_2_config_status",
};

static const char *pca9557_gpio_names[8] = {
	"pca9557:sd_card_detect_1",
	"pca9557:sd_card_detect_2",
	"pca9557:sd_card_detect_3",
	"pca9557:sd_card_detect_4",
	"pca9557:sd_card_detect_5",
	"pca9557:sd_card_detect_6",
	"pca9557:spare1",
	"pca9557:spare2",
};

/* SCU4 Specific Names */
static const char *pca9538_ext2_gpio_names_scu4[8] = {
	"pca9538_ext2:sd_active_1",
	"pca9538_ext2:sd_error_1",
	"pca9538_ext2:sd_active_2",
	"pca9538_ext2:sd_error_2",
	"pca9538_ext2:sd_error_3",
	"pca9538_ext2:sd_active_3",
	"pca9538_ext2:hub_1_reset",
	"pca9538_ext2:hub_2_reset",
};

static const char *pca9538_ext3_gpio_names_scu4[8] = {
	"pca9538_ext3:sd_error_4",
	"pca9538_ext3:sd_active_4",
	"pca9538_ext3:sd_error_5",
	"pca9538_ext3:sd_active_5",
	"pca9538_ext3:sd_error_6",
	"pca9538_ext3:sd_active_6",
	"pca9538_ext3:hub_3_reset",
	"pca9538_ext3:hub_4_reset",
};

static const char *pca9557_gpio_names_scu4[8] = {
	"pca9557:sd_card_detect_1",
	"pca9557:sd_card_detect_2",
	"pca9557:sd_card_detect_3",
	"pca9557:sd_card_detect_4",
	"pca9557:sd_card_detect_5",
	"pca9557:sd_card_detect_6",
	"pca9557:pmode_sel",
	"pca9557:spare2",
};

/* New in SCU4 */
static const char *pca9554_gpio_names[8] = {
	"pca9554:sdr_copper_rev0",
	"pca9554:sdr_copper_rev1",
	"pca9554:sdr_copper_rev2",
	"pca9554:sdr_copper_rev3",
	"pca9554:sdr_copper_rev4",
	"pca9554:sdr_assembly_0",
	"pca9554:sdr_assembly_1",
	"pca9554:sdr_assembly_2",
};

static int scu_gpio_common_setup(unsigned int gpio_base, unsigned int ngpio,
				 u32 mask, u32 is_input, u32 is_active,
				 u32 active_low)
{
	int i;
	unsigned long flags;

	for (i = 0; i < ngpio; i++) {
		if (!(mask & (1 << i)))
			continue;
		flags = GPIOF_EXPORT_DIR_FIXED;
		if (is_input & (1 << i)) {
			flags |= GPIOF_DIR_IN;
		} else {
			flags |= GPIOF_DIR_OUT;
			if (is_active & (1 << i))
				flags |= GPIOF_INIT_HIGH;
		}
		if (active_low & (1 << i))
			flags |= GPIOF_ACTIVE_LOW;
		gpio_request_one(gpio_base + i, flags, NULL);
	}
	return 0;
}

static int scu_pca9538_ext0_setup(struct i2c_client *client,
				  unsigned int gpio_base, unsigned int ngpio,
				  void *context)
{
	scu_gpio_common_setup(gpio_base, ngpio, 0xff, 0x33, 0xcc, 0x00);
	return 0;
}

static int scu_pca9538_ext1_setup(struct i2c_client *client,
				  unsigned int gpio_base, unsigned int ngpio,
				  void *context)
{
	scu_gpio_common_setup(gpio_base, ngpio, 0xf0, 0x00, 0x00, 0x00);
	return 0;
}

static int scu_pca9538_ext2_setup(struct i2c_client *client,
				  unsigned int gpio_base, unsigned int ngpio,
				  void *context)
{
	scu_gpio_common_setup(gpio_base, ngpio, 0xc0, 0x80, 0x40, 0x00);
	return 0;
}

static int scu_pca9538_ext3_setup(struct i2c_client *client,
				  unsigned int gpio_base, unsigned int ngpio,
				  void *context)
{
	scu_gpio_common_setup(gpio_base, ngpio, 0xc0, 0x80, 0x40, 0x00);
	return 0;
}

static int scu_pca9557_setup(struct i2c_client *client,
			     unsigned int gpio_base, unsigned int ngpio,
			     void *context)
{
	scu_gpio_common_setup(gpio_base, ngpio, 0x3f, 0x3f, 0x00, 0x3f);
	return 0;
}

/* SCU4 Specific */
static int scu_pca9538_ext2_setup_scu4(struct i2c_client *client,
				       unsigned int gpio_base,
				       unsigned int ngpio, void *context)
{
	return scu_gpio_common_setup(gpio_base, ngpio, 0xc0, 0x00, 0x00, 0xc0);
}

static int scu_pca9538_ext3_setup_scu4(struct i2c_client *client,
				       unsigned int gpio_base,
				       unsigned int ngpio,
				       void *context)
{
	return scu_gpio_common_setup(gpio_base, ngpio, 0xc0, 0x00, 0x00, 0xc0);
}

static int scu_pca9557_setup_scu4(struct i2c_client *client,
				  unsigned int gpio_base,
				  unsigned int ngpio, void *context)
{
	return scu_gpio_common_setup(gpio_base, ngpio, 0x7f, 0x3f, 0x00, 0x3f);
}

static int scu_pca9554_setup(struct i2c_client *client,
			     unsigned int gpio_base, unsigned int ngpio,
			     void *context)
{
	return scu_gpio_common_setup(gpio_base, ngpio, 0xff, 0xff, 0x00, 0x00);
}

static void scu_gpio_common_teardown(unsigned int gpio_base, int ngpio, u32 mask)
{
	int i;

	for (i = 0; i < ngpio; i++) {
		if (mask & (1 << i)) {
			gpio_unexport(gpio_base + i);
			gpio_free(gpio_base + i);
		}
	}
}

void scu_pca9538_ext0_teardown(struct i2c_client *client,
			       unsigned int gpio_base, unsigned int ngpio,
			       void *context)
{
	scu_gpio_common_teardown(gpio_base, ngpio, 0xff);
}

void scu_pca9538_ext1_teardown(struct i2c_client *client,
			       unsigned int gpio_base, unsigned int ngpio,
			       void *context)
{
	scu_gpio_common_teardown(gpio_base, ngpio, 0xf0);
}

void scu_pca9538_ext2_teardown(struct i2c_client *client,
			       unsigned int gpio_base, unsigned int ngpio,
			       void *context)
{
	scu_gpio_common_teardown(gpio_base, ngpio, 0xc0);
}

void scu_pca9538_ext3_teardown(struct i2c_client *client,
			       unsigned int gpio_base, unsigned int ngpio,
			       void *context)
{
	scu_gpio_common_teardown(gpio_base, ngpio, 0xc0);
}

void scu_pca9557_teardown(struct i2c_client *client,
			  unsigned int gpio_base, unsigned int ngpio,
			  void *context)
{
	scu_gpio_common_teardown(gpio_base, ngpio, 0x3f);
}

void scu_pca9554_teardown(struct i2c_client *client,
			  unsigned int gpio_base, unsigned int ngpio,
			  void *context)
{
	scu_gpio_common_teardown(gpio_base, ngpio, 0x3f);
}

static int scu_gpiochip_match_name(struct gpio_chip *chip, void *data)
{
	return !strcmp(chip->label, data);
}

static struct gpio_chip *scu_find_chip_by_name(const char *name)
{
	return gpiochip_find((void *)name, scu_gpiochip_match_name);
}

static struct pca953x_platform_data scu_pca953x_pdata[] = {
	/* SCU 1/2/3 */
	[0] = {.gpio_base = SCU_EXT_GPIO_BASE(0),
		.irq_base = -1,
		.setup = scu_pca9538_ext0_setup,
		.teardown = scu_pca9538_ext0_teardown,
		.names = pca9538_ext0_gpio_names},
	[1] = {.gpio_base = SCU_EXT_GPIO_BASE(1),
		.irq_base = -1,
		.setup = scu_pca9538_ext1_setup,
		.teardown = scu_pca9538_ext1_teardown,
		.names = pca9538_ext1_gpio_names},
	[2] = {.gpio_base = SCU_EXT_GPIO_BASE(2),
		.irq_base = -1,
		.setup = scu_pca9538_ext2_setup,
		.teardown = scu_pca9538_ext2_teardown,
		.names = pca9538_ext2_gpio_names},
	[3] = {.gpio_base = SCU_EXT_GPIO_BASE(3),
		.irq_base = -1,
		.setup = scu_pca9538_ext3_setup,
		.teardown = scu_pca9538_ext3_teardown,
		.names = pca9538_ext3_gpio_names},
	[4] = {.gpio_base = SCU_EXT_GPIO_BASE(4),
		.irq_base = -1,
		.setup = scu_pca9557_setup,
		.teardown = scu_pca9557_teardown,
		.names = pca9557_gpio_names},

	/* SCU4 Specific */
	[5] = {.gpio_base = SCU_EXT_GPIO_BASE(2),
		.irq_base = -1,
		.setup = scu_pca9538_ext2_setup_scu4,
		.teardown = scu_pca9538_ext2_teardown,
		.names = pca9538_ext2_gpio_names_scu4},
	[6] = {.gpio_base = SCU_EXT_GPIO_BASE(3),
		.irq_base = -1,
		.setup = scu_pca9538_ext3_setup_scu4,
		.teardown = scu_pca9538_ext3_teardown,
		.names = pca9538_ext3_gpio_names_scu4},
	[7] = {.gpio_base = SCU_EXT_GPIO_BASE(4),
		.irq_base = -1,
		.setup = scu_pca9557_setup_scu4,
		.teardown = scu_pca9557_teardown,
		.names = pca9557_gpio_names_scu4},
	[8] = {.gpio_base = SCU_EXT_GPIO_BASE(5),
		.irq_base = -1,
		.setup = scu_pca9554_setup,
		.teardown = scu_pca9554_teardown,
		.names = pca9554_gpio_names},
};

static void scu_populate_unit_info(struct nvmem_device *nvmem,
				   void *context);

static struct i2c_board_info scu_i2c_info_common[] = {
	/* On Main Board */
	{ I2C_BOARD_INFO("scu_pic", 0x20)},	/* SCU PIC */
	{ I2C_BOARD_INFO("24c08", 0x54),
	  .dev_name = "Nameplate eeprom"},	/* Nameplate EEPROM */
	{ I2C_BOARD_INFO("24c04", 0x52),	/* Bit Eeprom */
	  .dev_name = "Bit eeprom"},		/* Bit Eeprom */
	{ I2C_BOARD_INFO("ds1682", 0x6b)},	/* Elapsed Time Counter */
	{ I2C_BOARD_INFO("pca9538", 0x71),	/* LEDs + Output Discretes */
	  .platform_data = &scu_pca953x_pdata[1],},
};

/*
 * NOTE
 * Due to the nature of how the IRQ is set for the pca9538 IO
 * expander @ 0x70 it must be the fist element in the following
 * arrays, as it is modified without knowing that there
 * are multiple board types.
 */

static struct i2c_board_info scu_i2c_info_scu2[] = {
	{ I2C_BOARD_INFO("pca9538", 0x70),	/* Input Discretes */
	  .platform_data = &scu_pca953x_pdata[0],},
	{ I2C_BOARD_INFO("pca9538", 0x72),
	  .platform_data = &scu_pca953x_pdata[2],},
	{ I2C_BOARD_INFO("pca9538", 0x73),
	  .platform_data = &scu_pca953x_pdata[3],},
	{ I2C_BOARD_INFO("sc18is602", 0x28)},
};

static struct i2c_board_info scu_i2c_info_scu3[] = {
	{ I2C_BOARD_INFO("pca9538", 0x70),	/* Input Discretes */
	  .platform_data = &scu_pca953x_pdata[0],},
	{ I2C_BOARD_INFO("pca9538", 0x72),
	  .platform_data = &scu_pca953x_pdata[2],},
	{ I2C_BOARD_INFO("pca9538", 0x73),
	  .platform_data = &scu_pca953x_pdata[3],},
	{ I2C_BOARD_INFO("pca9557", 0x1b),
	  .platform_data = &scu_pca953x_pdata[4],},
};


static struct i2c_board_info scu_i2c_info_scu4[] = {
	/* On Main Board */
	{ I2C_BOARD_INFO("pca9538", 0x70),	/* Input Discretes */
	  .platform_data = &scu_pca953x_pdata[0],},

	/* On SDR */
	{ I2C_BOARD_INFO("pca9538", 0x72),	/* SD Card LEDS 1-3 and Hubs 1-2 Reset */
	  .platform_data = &scu_pca953x_pdata[5],},
	{ I2C_BOARD_INFO("pca9538", 0x73),	/* SD Card LEDS 4-6 and Hubs 3-4 Reset */
	  .platform_data = &scu_pca953x_pdata[6],},
	{ I2C_BOARD_INFO("pca9538", 0x1C),	/* SD Card Detect 1-6 */
	  .platform_data = &scu_pca953x_pdata[7],},
	{ I2C_BOARD_INFO("pca9554", 0x23),	/* SDR Copper and Assemly Rev */
	  .platform_data = &scu_pca953x_pdata[8],},
};

/* SCU specific gpio pin names. Only works if module is built into kernel. */
const char * const ichx_gpiolib_names[128] = {
	[0] = "switch_interrupt",	/* GPI0 */
	[3] = "ac_loss_detect",		/* GPI3 */
	[16] = "debug_out",		/* GPO0 */
	[20] = "switch_reset",		/* GPO3 */
};

static struct gpiod_lookup_table scu_switch_gpiod_table = {
	.dev_id = "gpio-0:00",
	.table = {
		/* Not used, the driver does not support interrupts */
		GPIO_LOOKUP_IDX("gpio_ich", 0, "switch_irq", 0, 0),
		GPIO_LOOKUP_IDX("gpio_ich", 20, "reset", 0,
				GPIO_ACTIVE_LOW),
		{ },
	}
};

static struct gpiod_lookup_table mdio_gpiod_table = {
	.dev_id = "mdio-gpio.0",
	.table = {
		GPIO_LOOKUP_IDX("gpio_ich", 17, NULL, MDIO_GPIO_MDC,
				GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("gpio_ich", 2, NULL, MDIO_GPIO_MDIO,
				GPIO_OPEN_SOURCE),
		GPIO_LOOKUP_IDX("gpio_ich", 21, NULL, MDIO_GPIO_MDO,
				GPIO_ACTIVE_LOW),
		{} /* Terminating entry */
	},
};

static const struct mdio_gpio_platform_data mdio_gpio_pdata = {
	.phy_mask = ~0,
	.phy_ignore_ta_mask = ~0,
};

static void scu_pch_gpio_setup(struct scu_data *data)
{
	struct gpio_chip *chip = scu_find_chip_by_name("gpio_ich");
	int irq;

	scu_pca953x_pdata[0].irq_base = -1;
	data->pdata->i2c_board_info[0].irq = 0;

	if (!chip) {
		dev_err(data->dev, "GPIO chip gpio_ich missing\n");
		return;
	}

	irq = gpio_to_irq(chip->base + 1);	/* GPI1 */
	if (irq > 0) {
		scu_pca953x_pdata[0].irq_base = 0;
		data->pdata->i2c_board_info[0].irq = irq;
	}

	/* generic: 3 (input), 16, */
	scu_gpio_common_setup(chip->base, 16, 0x10008, 0x000008,
			      0x0, 0x0);
	gpiod_add_lookup_table(&mdio_gpiod_table);

	data->mdio_dev = platform_device_register_data(&platform_bus,
						       "mdio-gpio", 0,
						       &mdio_gpio_pdata,
						       sizeof(mdio_gpio_pdata));

	if (IS_ERR(data->mdio_dev)) {
		dev_err(data->dev, "Failed to register MDIO device\n");
		data->mdio_dev = NULL;
		return;
	}
}

static void scu_pch_gpio_teardown(struct scu_data *data)
{
	struct gpio_chip *chip = scu_find_chip_by_name("gpio_ich");

	if (chip) {
		platform_device_unregister(data->mdio_dev);
		scu_gpio_common_teardown(chip->base, 22, 0x30000b);
	}
}

static struct dsa_mv88e6xxx_pdata dsa_mv88e6xxx_pdata_scu3 = {
	.cd = {
		.port_names[0] = "cpu",
		.port_names[1] = "port1",
		.port_names[2] = "port2",
		.port_names[3] = "port8",
		.port_names[4] = "host2esb",
		.port_names[5] = NULL,
	},
	.compatible = "marvell,mv88e6085",
	.enabled_ports = BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4),
	.eeprom_len = 0x200,
};

static struct dsa_mv88e6xxx_pdata dsa_mv88e6xxx_pdata_scu4 = {
	.cd = {
		.port_names[0] = "cpu",
		.port_names[1] = "eth_cu_100_1",
		.port_names[2] = NULL,
		.port_names[3] = "eth_cu_1000_2",
		.port_names[4] = "eth_cu_1000_1",
		.port_names[5] = NULL,
	},
	.compatible = "marvell,mv88e6085",
	.enabled_ports = BIT(0) | BIT(1) | BIT(3) | BIT(4),
	.eeprom_len = 0x200,
};

static const struct mdio_board_info bdinfo_scu3 = {
	.bus_id = "gpio-0",
	.modalias = "mv88e6085",
	.mdio_addr = 0,
	.platform_data = &dsa_mv88e6xxx_pdata_scu3,
};

static const struct mdio_board_info bdinfo_scu4 = {
	.bus_id = "gpio-0",
	.modalias = "mv88e6085",
	.mdio_addr = 0,
	.platform_data = &dsa_mv88e6xxx_pdata_scu4,
};

static void scu_setup_ethernet_switch(struct scu_data *data)
{
	const struct mdio_board_info *dsa_bdinfo = data->pdata->dsa_bdinfo;
	struct dsa_mv88e6xxx_pdata *dsa_pdata = data->pdata->dsa_pdata;
	struct device *dev = data->dev;
	int err;

	if (!dsa_bdinfo || !dsa_pdata)
		return;

	dsa_pdata->netdev = dev_get_by_name(&init_net, "eth0");
	if (!dsa_pdata->netdev) {
		dev_err(dev, "Error finding Ethernet device\n");
		return;
	}

	/* This table allows the switch to find its reset GPIO. */
	gpiod_add_lookup_table(&scu_switch_gpiod_table);

	err = mdiobus_register_board_info(dsa_bdinfo, 1);
	if (err) {
		dev_err(dev, "Error setting up MDIO board info\n");
		goto out;
	}
	return;

out:
	dev_put(dsa_pdata->netdev);
}

static void scu3_init(struct scu_data *data)
{
	scu_setup_ethernet_switch(data);
	scu_pch_gpio_setup(data);
}

static void scu3_remove(struct scu_data *data)
{
	scu_pch_gpio_teardown(data);
	platform_device_unregister(data->dsa_dev);
}

static struct b53_platform_data b53_switch_pdata = {
	.enabled_ports	= 0x1f,
	.cd = {
		.port_names[0]	= "netaux1",
		.port_names[1]	= "netaux2",
		.port_names[2]	= "netcploop",
		.port_names[3]	= "netseatloop",
		.port_names[4]	= "cpu",
		/* netdev is filled at runtime */
	},
};

static void scu_b53_switch_init(struct scu_data *data)
{
	if (data->netdev)
		b53_switch_pdata.cd.netdev[4] = &data->netdev->dev;
}

static struct spi_board_info scu1_spi_info[] = {
	{
		.modalias = "b53-switch",
		.bus_num = 0,
		.chip_select = 0,
		.max_speed_hz = 2000000,
		.mode = SPI_MODE_3,
		.platform_data = &b53_switch_pdata,
	},
};

static struct spi_board_info scu2_spi_info[] = {
	{
#ifdef TESTING
		.modalias = "lm70",
		.bus_num = 0,
		.chip_select = 0,
		.max_speed_hz = 1000000,
		.mode = SPI_MODE_0,
#else
		.modalias = "robodebug",
		.bus_num = 0,
		.chip_select = 0,
		.max_speed_hz = 2000000,
		.mode = SPI_MODE_3,
#endif
	},
};

static struct scu_platform_data scu_platform_data[] = {
	[scu1] = {
		.board_type = "SCU1 x86",
		.lru_part_number = SCU_LRU_PARTNUM_GEN1,
		.version = scu1,
		.eeprom_len = SCU_EEPROM_LEN_GEN1,
		.i2c_board_info = scu_i2c_info_scu2,
		.num_i2c_board_info = ARRAY_SIZE(scu_i2c_info_scu2),
		.spi_board_info = scu1_spi_info,
		.num_spi_board_info = ARRAY_SIZE(scu1_spi_info),
		.init = scu_b53_switch_init,
	},
	[scu2] = {
		.board_type = "SCU2 x86",
		.lru_part_number = SCU_LRU_PARTNUM_GEN2,
		.version = scu2,
		.eeprom_len = SCU_EEPROM_LEN_GEN2,
		.i2c_board_info = scu_i2c_info_scu2,
		.num_i2c_board_info = ARRAY_SIZE(scu_i2c_info_scu2),
		.spi_board_info = scu2_spi_info,
		.num_spi_board_info = ARRAY_SIZE(scu2_spi_info),
	},
	[scu3] = {
		.board_type = "SCU3 x86",
		.lru_part_number = SCU_LRU_PARTNUM_GEN3,
		.board_part_number = SCU_ZII_BOARD_PARTNUM,
		.board_dash_number = SCU_ZII_BOARD_DASHNUM_SCU3,
		.version = scu3,
		.eeprom_len = SCU_EEPROM_LEN_GEN3,
		.i2c_board_info = scu_i2c_info_scu3,
		.num_i2c_board_info = ARRAY_SIZE(scu_i2c_info_scu3),
		.dsa_pdata = &dsa_mv88e6xxx_pdata_scu3,
		.dsa_bdinfo = &bdinfo_scu3,
		.init = scu3_init,
		.remove = scu3_remove,
	},
	[scu4] = {
		.board_type = "SCU4 x86",
		.lru_part_number = SCU_LRU_PARTNUM_GEN4,
		.board_part_number = SCU_ZII_BOARD_PARTNUM,
		.board_dash_number = SCU_ZII_BOARD_DASHNUM_SCU4,
		.version = scu4,
		.eeprom_len = SCU_EEPROM_LEN_GEN3,
		.i2c_board_info = scu_i2c_info_scu4,
		.num_i2c_board_info = ARRAY_SIZE(scu_i2c_info_scu4),
		.dsa_pdata = &dsa_mv88e6xxx_pdata_scu4,
		.dsa_bdinfo = &bdinfo_scu4,
		.init = scu3_init,
		.remove = scu3_remove,
	},

	[scu4c] = {
		.board_type = "SCU4 Copper x86",
		.lru_part_number = SCU_LRU_PARTNUM_GEN4_COPPER,
		.board_part_number = SCU_ZII_BOARD_PARTNUM,
		.board_dash_number = SCU_ZII_BOARD_DASHNUM_SCU4_COPPER,
		.version = scu4,
		.eeprom_len = SCU_EEPROM_LEN_GEN3,
		.i2c_board_info = scu_i2c_info_scu4,
		.num_i2c_board_info = ARRAY_SIZE(scu_i2c_info_scu4),
		.dsa_pdata = &dsa_mv88e6xxx_pdata_scu4,
		.dsa_bdinfo = &bdinfo_scu4,
		.init = scu3_init,
		.remove = scu3_remove,
	},
	[unknown] = {
		.board_type = "UNKNOWN",
		.version = unknown,
		.eeprom_len = SCU_EEPROM_LEN_GEN3,
		.i2c_board_info = scu_i2c_info_scu3,
		.num_i2c_board_info = ARRAY_SIZE(scu_i2c_info_scu3),
		.dsa_bdinfo = &bdinfo_scu3,
		.init = scu3_init,
		.remove = scu3_remove,
	},
};

static int scu_instantiate_i2c(struct scu_data *data, int base,
			       struct i2c_board_info *info, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		data->client[base + i] = i2c_new_client_device(data->adapter, info);
		if (!data->client[base + i]) {
			/*
			 * Unfortunately this call does not tell us
			 * why it failed. Pick the most likely reason.
			 */
			return -EBUSY;
		}
		info++;
	}
	return 0;
}

#if 0
static int scu_instantiate_spi(struct scu_data *data,
			       struct spi_board_info *info, int count)
{
	struct spi_master *master;
	int i;
	/* SPI bus number matches i2c bus number (set by sc18is602 driver) */
	master = spi_busnum_to_master(data->adapter->nr);
	if (!master) {
		dev_err(data->dev, "Failed to find SPI adapter\n");
		return -ENODEV;
	}
	data->master = master;

	for (i = 0; i < count; i++) {
		info->bus_num = master->bus_num;
		/* ignore errors */
		data->spidev[i] = spi_new_device(master, info);
		info++;
	}

	return 0;
}
#endif

/*
 * This is the callback function when a a specifc at24 eeprom is found.
 * Its reads out the eeprom contents via the read function passed back in via
 * struct memory_accessor. It then calls part_number_proc, serial_number_proc,
 * and dom_proc to populate the procfs entries for each specific field.
 */
static void scu_populate_unit_info(struct nvmem_device *nvmem,
				   void *context)
{
	const struct scu_platform_data *pdata = &scu_platform_data[unknown];
	struct scu_data *data = context;
	unsigned char *ptr;
	int i, len;

	data->nvmem = nvmem;

	ptr = (unsigned char *)&data->eeprom;
	/* Read Data structure from EEPROM */
	if (nvmem_device_read(nvmem, 0x300, sizeof(data->eeprom), ptr) <= 0) {
		dev_err(data->dev, "Failed to read eeprom data\n");
		goto error;
	}

	/* EEPROM is accessible, permit write access to it */
	data->eeprom_accessible = true;

	/* Special case - eeprom not programmed */
	if (data->eeprom.length == 0xffff && data->eeprom.checksum == 0xff) {
		/* Assume it is SCU3, but report different board type */
		memset(&data->eeprom, '\0', sizeof(data->eeprom));
		data->eeprom.length = cpu_to_le16(SCU_EEPROM_LEN_EEPROM);
		goto unprogrammed;
	}

	/* Sanity check */
	if (le16_to_cpu(data->eeprom.length) != SCU_EEPROM_LEN_EEPROM) {
		dev_err(data->dev,
			"Bad eeprom data length: Expected %d, got %d\n",
			SCU_EEPROM_LEN_EEPROM,
			le16_to_cpu(data->eeprom.length));
		goto error;
	}

	/* Update platform data based on part number retrieved from EEPROM */
	for (i = 0; i < ARRAY_SIZE(scu_platform_data); i++) {
		const struct scu_platform_data *tpdata = &scu_platform_data[i];

		if (tpdata->lru_part_number == NULL)
			continue;
		if (!strncmp(data->eeprom.lru_part_number,
			     tpdata->lru_part_number,
			     strlen(tpdata->lru_part_number))) {
			pdata = tpdata;
			break;
		}
	}

	/* Check if we have an SCU2 with a Zii mainboard and update
	 * the struct if yes
	 */
	if (pdata->version == scu2 &&
	    strnstr(data->eeprom.board_part_number,
		    SCU_ZII_BOARD_PARTNUM,
		    strlen(SCU_ZII_BOARD_PARTNUM)) != NULL) {
		struct scu_platform_data *newSCU2pdata = &scu_platform_data[scu3];

		newSCU2pdata->board_type = "SCU2 x86";
		newSCU2pdata->lru_part_number = SCU_LRU_PARTNUM_GEN2,
		newSCU2pdata->version = scu2,
		newSCU2pdata->spi_board_info = NULL;
		newSCU2pdata->num_spi_board_info = 0;
		newSCU2pdata->board_part_number = SCU_ZII_BOARD_PARTNUM;
		newSCU2pdata->init = scu3_init;
		newSCU2pdata->remove = scu3_remove;
		pdata = newSCU2pdata;
	}

unprogrammed:
	data->pdata = pdata;
	/*
	 * We know as much as we will ever find out about the platform.
	 * Perform final platform initialization and instantiate additional
	 * I2C devices as well as SPI devices now, prior to validating
	 * the EEPROM checksum.
	 */
	if (pdata->init)
		pdata->init(data);

	if (pdata->i2c_board_info)
		scu_instantiate_i2c(data, ARRAY_SIZE(scu_i2c_info_common),
				    pdata->i2c_board_info,
				    pdata->num_i2c_board_info);

#if 0
	if (pdata->spi_board_info)
		scu_instantiate_spi(data, pdata->spi_board_info,
				    pdata->num_spi_board_info);
#endif
	len = data->pdata->eeprom_len;

	/* Validate checksum */
	if (scu_get_checksum(ptr, len)) {
		dev_err(data->dev,
			"EEPROM data checksum error: expected 0, got 0x%x [len=%d]\n",
			scu_get_checksum(ptr, len), len);
		/* TBD: Do we want to clear the eeprom in this case ? */
		goto error_noclean;
	}

	/* Create sysfs attributes based on retrieved platform data */
	data->eeprom_valid = true;
	goto done;

error:
	memset(&data->eeprom, '\0', sizeof(data->eeprom));
	data->eeprom.length = cpu_to_le16(SCU_EEPROM_LEN_EEPROM);
error_noclean:
	data->eeprom_valid = false;
done:
	if (sysfs_create_group(&data->dev->kobj, &scu_eeprom_group))
		;
	if (sysfs_create_bin_file(&data->dev->kobj,
				  &scu_eeprom_test_scratchpad_file))
		;
}

static int scu_i2c_adap_name_match(struct device *dev, const void *data)
{
	struct i2c_adapter *adap = i2c_verify_adapter(dev);

	if (!adap)
		return false;

	return !strcmp(adap->name, (char *)data);
}

static struct i2c_adapter *scu_find_i2c_adapter(char *name)
{
	struct device *dev;
	struct i2c_adapter *adap;

	dev = bus_find_device(&i2c_bus_type, NULL, name,
			      scu_i2c_adap_name_match);
	if (!dev)
		return NULL;

	adap = i2c_verify_adapter(dev);
	if (!adap)
		put_device(dev);

	return adap;
}

const char *scu_modules[] = {
	"kempld-core",
	"i2c-kempld",
	"spi-sc18is602",
	"lpc_ich",
	"gpio_ich",
	"mdio-gpio",
	"dsa",
	NULL
};

static void scu_request_modules(bool wait)
{
	struct module *m;
	int i;

	/*
	 * Try to load modules which we are going to need later on.
	 * Fail silently; if loading the module is not successful
	 * we'll bail out later on.
	 */
	for (i = 0; scu_modules[i]; i++) {
		rcu_read_lock_sched();
		m = find_module(scu_modules[i]);
		rcu_read_unlock_sched();
		if (!m) {
			if (wait)
				request_module(scu_modules[i]);
			else
				request_module_nowait(scu_modules[i]);
		}
	}
}

static int scu_proc_board_type_show(struct seq_file *m, void *v)
{
	struct scu_data *data = (struct scu_data *)m->private;

	seq_printf(m, "%s\n", data->pdata->board_type);

	return 0;
}

static int scu_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, scu_proc_board_type_show, pde_data(inode));
}

static const struct proc_ops scu_proc_ops = {
	.proc_open = scu_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

int scu_nvmem_match(struct device *dev, const void *data)
{
	if (strstr(dev_name(dev), data))
		return 1;
	return 0;
}

static int scu_probe(struct platform_device *pdev)
{
	struct nvmem_device *nvmem_np;
	struct proc_dir_entry *rave_board_type;
	struct device *dev = &pdev->dev;
	struct i2c_adapter *adapter;
	struct scu_data *data;
	int i, ret;

	scu_request_modules(true);

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);

	data->dev = dev;

	mutex_init(&data->write_lock);

	/*
	 * The adapter driver should have been loaded by now.
	 * If not, try again later.
	 */
	adapter = scu_find_i2c_adapter("i2c-kempld");
	if (!adapter) {
		dev_info(dev, "Defering probe, i2c-kempld missing\n");
		return -EPROBE_DEFER;
	}
	data->adapter = adapter;

	data->rave_proc_dir = proc_mkdir("rave", NULL);
	if (!data->rave_proc_dir) {
		ret = -ENODEV;
		dev_err(dev, "Error creating proc directory\n");
		goto error_put;
	}
	rave_board_type = proc_create_data("board_type", 0, data->rave_proc_dir,
					   &scu_proc_ops, data);
	if (rave_board_type == NULL) {
		ret = -ENODEV;
		dev_err(dev, "Error creating proc board_type\n");
		goto error_remove;
	}

	ret = scu_instantiate_i2c(data, 0, scu_i2c_info_common,
				  ARRAY_SIZE(scu_i2c_info_common));
	if (ret) {
		dev_err(dev, "Error instantiateding i2c devices\n");
		goto error_i2c_client;
	}

	/*XXX Here is we have export eeprom cells as part of
	 * EEPROM/NVMEM or inside of populate_unit_info
	 */
	nvmem_np = nvmem_device_find("Nameplate eeprom", scu_nvmem_match);
	if (nvmem_np)
		scu_populate_unit_info(nvmem_np, data);
	else
		dev_info(dev, "Failed to find nameplate eeprom\n");

	scu_pca_leds_register(dev, data);

	ret = sysfs_create_group(&dev->kobj, &scu_base_group);
	if (ret) {
		dev_err(dev, "Failed to create sysfs group\n");
		goto error_group;
	}

	return 0;

error_group:
	scu_pca_leds_unregister(data);
error_i2c_client:
	for (i = 0; i < ARRAY_SIZE(data->client) && data->client[i]; i++)
		i2c_unregister_device(data->client[i]);
error_remove:
	proc_remove(data->rave_proc_dir);
error_put:
	put_device(&adapter->dev);
	return ret;
}

static int __exit scu_remove(struct platform_device *pdev)
{
	struct scu_data *data = platform_get_drvdata(pdev);
	int i;

	sysfs_remove_bin_file(&data->dev->kobj,
			      &scu_eeprom_test_scratchpad_file);
	sysfs_remove_group(&pdev->dev.kobj, &scu_eeprom_group);
	sysfs_remove_group(&pdev->dev.kobj, &scu_base_group);

	if (data->pdata && data->pdata->remove)
		data->pdata->remove(data);

	scu_pca_leds_unregister(data);

	for (i = 0; i < ARRAY_SIZE(data->spidev); i++)
		spi_unregister_device(data->spidev[i]);
	spi_master_put(data->master);

	for (i = 0; i < ARRAY_SIZE(data->client) && data->client[i]; i++)
		i2c_unregister_device(data->client[i]);

	proc_remove(data->rave_proc_dir);

	put_device(&data->adapter->dev);
	dev_put(data->netdev);

	return 0;
}

static struct platform_driver scu_driver = {
	.probe = scu_probe,
	.remove = __exit_p(scu_remove),
	.driver = {
		.name = "scu",
		.owner = THIS_MODULE,
	},
};

static struct platform_device *scu_pdev;

static int scu_create_platform_device(const struct dmi_system_id *id)
{
	int ret;

	scu_pdev = platform_device_alloc("scu", -1);
	if (!scu_pdev)
		return -ENOMEM;

	ret = platform_device_add(scu_pdev);
	if (ret)
		goto err;

	return 0;
err:
	platform_device_put(scu_pdev);
	return ret;
}

static const struct dmi_system_id scu_device_table[] __initconst = {
	{
		.ident = "IMS SCU version 1, Core 2 Duo",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "PXT"),
		},
		.callback = scu_create_platform_device,
	},
	{
		.ident = "IMS SCU version 2, Ivy Bridge",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-bSC6"),
		},
		.callback = scu_create_platform_device,
	},
	{
		.ident = "IMS SCU version 2, Ivy Bridge",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-bIP2"),
		},
		.callback = scu_create_platform_device,
	},
	{
		.ident = "IMS SCU version 2, Sandy Bridge",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-bSC2"),
		},
		.callback = scu_create_platform_device,
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, scu_device_table);

static int __init scu_init(void)
{
	if (!dmi_check_system(scu_device_table))
		return -ENODEV;

	scu_request_modules(false);

	return platform_driver_register(&scu_driver);
}
module_init(scu_init);

static void __exit scu_exit(void)
{
	if (scu_pdev)
		platform_device_unregister(scu_pdev);

	platform_driver_unregister(&scu_driver);
}
module_exit(scu_exit);

MODULE_ALIAS("platform:scu");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Guenter Roeck");
MODULE_DESCRIPTION("IMS SCU platform driver");
MODULE_DEVICE_TABLE(dmi, scu_device_table);
