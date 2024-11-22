// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SCU board driver
 *
 * Copyright (c) 2012, 2014 Guenter Roeck <linux@roeck-us.net>
 */
#include <asm/byteorder.h>
#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_platform.h>
#include <linux/nvmem-consumer.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/processor.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/version.h>

/* Embedded dtbo symbols created by cmd_wrap_S_dtb in scripts/Makefile.lib */
extern char __dtbo_scu_begin[];
extern char __dtbo_scu_end[];

#define NAMEPLATE 		"nameplate"

enum scu_version { scu1, scu2, scu3, scu4, scu4c, unknown };

struct __packed eeprom_data {
	__le16 length;				/* 0 - 1 */
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

struct scu_data;

struct scu_platform_data {
	const char *board_type;
	const char *lru_part_number;
	const char *board_part_number;
	const char *board_dash_number;
	int eeprom_len;
};

struct scu_data {
	struct device *dev;			/* SCU platform device */
	struct proc_dir_entry *rave_proc_dir;
	struct mutex write_lock;
	const struct scu_platform_data *pdata;
	bool have_write_magic;
	struct eeprom_data eeprom;
	struct nvmem_device *nvmem;
	bool eeprom_accessible;
	bool eeprom_valid;
	int ovcs_id;
	struct notifier_block scu_nvmem_notifier_nb;
};

#define SCU_EEPROM_LEN_EEPROM	36
#define SCU_EEPROM_LEN_GEN3	75		/* Preliminary */

#define SCU_LRU_PARTNUM_GEN3	"00-5013"
#define SCU_LRU_PARTNUM_GEN4	"00-5031"
#define SCU_LRU_PARTNUM_GEN4_COPPER "00-5032"

#define SCU_ZII_BOARD_PARTNUM	"05-0041"
#define SCU_ZII_BOARD_DASHNUM_SCU4  "11"
#define SCU_ZII_BOARD_DASHNUM_SCU4_COPPER  "12"

#define SCU_WRITE_MAGIC		5482328594ULL

/* sysfs */

static unsigned char scu_get_checksum(unsigned char *ptr, int len)
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

	strscpy(buffer, in, sizeof(buffer));
	/* do not copy newline from input string */
	cp = strchr(buffer, '\n');
	if (cp)
		*cp = '\0';

	if (len > sizeof(buffer))
		len = sizeof(buffer);

	mutex_lock(&data->write_lock);
	strscpy(out, buffer, len);

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
			       buf,
			       data->eeprom.board_updated_date_of_manufacture,
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

static int scu_load_overlay(struct scu_data *data)
{
	u32 dtbo_size = __dtbo_scu_end - __dtbo_scu_begin;
	void *dtbo_start = __dtbo_scu_begin;
	int err;

	err = of_overlay_fdt_apply(dtbo_start, dtbo_size, &data->ovcs_id,
				   NULL);
	if (err)
		dev_err(data->dev, "Error applying overlay %d\n", err);

	return err;
}

static void scu_unload_overlay(struct scu_data *data)
{
	of_overlay_remove(&data->ovcs_id);
}
static struct scu_platform_data scu_platform_data[] = {
	[scu4] = {
		.board_type = "SCU4 x86",
		.lru_part_number = SCU_LRU_PARTNUM_GEN4,
		.board_part_number = SCU_ZII_BOARD_PARTNUM,
		.board_dash_number = SCU_ZII_BOARD_DASHNUM_SCU4,
		.eeprom_len = SCU_EEPROM_LEN_GEN3,
	},

	[scu4c] = {
		.board_type = "SCU4 Copper x86",
		.lru_part_number = SCU_LRU_PARTNUM_GEN4_COPPER,
		.board_part_number = SCU_ZII_BOARD_PARTNUM,
		.board_dash_number = SCU_ZII_BOARD_DASHNUM_SCU4_COPPER,
		.eeprom_len = SCU_EEPROM_LEN_GEN3,
	},
	[unknown] = {
		.board_type = "UNKNOWN",
		.eeprom_len = SCU_EEPROM_LEN_GEN3,
	},
};

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
	int i, len, err;

	data->nvmem = nvmem;

	ptr = (unsigned char *)&data->eeprom;
	/* Read Data structure from EEPROM */
	err = nvmem_device_read(nvmem, 0x300, sizeof(data->eeprom), ptr);
	if (err <= 0) {
		dev_err(data->dev, "Failed to read eeprom data %d\n", err);
		goto error;
	}

	/* EEPROM is accessible, permit write access to it */
	data->eeprom_accessible = true;

	/* Special case - eeprom not programmed */
	if (data->eeprom.length == cpu_to_le16(0xffff) &&
	    data->eeprom.checksum == 0xff) {
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

unprogrammed:
	data->pdata = pdata;
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

	return;
done:
	err = sysfs_create_group(&data->dev->kobj, &scu_eeprom_group);
	if (err)
		dev_err(data->dev, "Unable to create eeprom group\n");

	err = sysfs_create_bin_file(&data->dev->kobj,
				    &scu_eeprom_test_scratchpad_file);
	if (err)
		dev_err(data->dev, "Unable to create scratchpad\n");
}

static const char * const scu_modules[] = {
	"kempld-core",
	"i2c-kempld",
	"lpc_ich",
	"gpio_ich",
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

static int scu_nvmem_notifier_cb(struct notifier_block *nb,
				 unsigned long event,
				 void *data)
{
	struct scu_data *scu_data = container_of(nb, struct scu_data,
						 scu_nvmem_notifier_nb);
	struct nvmem_device *nvmem = data;

	if (event == NVMEM_ADD && !strncmp(nvmem_dev_name(nvmem),
					   NAMEPLATE, strlen(NAMEPLATE)))
		scu_populate_unit_info(nvmem, scu_data);

	return notifier_from_errno(0);
}

static int scu_probe(struct platform_device *pdev)
{
	struct proc_dir_entry *rave_board_type;
	struct device *dev = &pdev->dev;
	struct scu_data *data;
	int ret;

	scu_request_modules(true);

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);

	data->dev = dev;
	data->scu_nvmem_notifier_nb.notifier_call = scu_nvmem_notifier_cb;
	nvmem_register_notifier(&data->scu_nvmem_notifier_nb);

	mutex_init(&data->write_lock);

	data->rave_proc_dir = proc_mkdir("rave", NULL);
	if (!data->rave_proc_dir) {
		ret = -ENODEV;
		dev_err(dev, "Error creating proc directory\n");
		goto error_unregister;
	}
	rave_board_type = proc_create_data("board_type", 0, data->rave_proc_dir,
					   &scu_proc_ops, data);
	if (rave_board_type == NULL) {
		ret = -ENODEV;
		dev_err(dev, "Error creating proc board_type\n");
		goto error_remove;
	}

	scu_load_overlay(data);

	ret = sysfs_create_group(&dev->kobj, &scu_base_group);
	if (ret) {
		dev_err(dev, "Failed to create sysfs group\n");
		goto error_group;
	}

	return 0;

error_group:
	scu_unload_overlay(data);
error_remove:
	proc_remove(data->rave_proc_dir);
error_unregister:
	nvmem_unregister_notifier(&data->scu_nvmem_notifier_nb);
	return ret;
}

static int __exit scu_remove(struct platform_device *pdev)
{
	struct scu_data *data = platform_get_drvdata(pdev);

	nvmem_unregister_notifier(&data->scu_nvmem_notifier_nb);

	sysfs_remove_bin_file(&data->dev->kobj,
			      &scu_eeprom_test_scratchpad_file);
	sysfs_remove_group(&pdev->dev.kobj, &scu_eeprom_group);
	sysfs_remove_group(&pdev->dev.kobj, &scu_base_group);

	scu_unload_overlay(data);

	proc_remove(data->rave_proc_dir);

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
