// SPDX-License-Identifier: GPL-2.0
/*
 * Hwmon client for SATA hard disk drives with temperature sensors
 * (c) 2019 Guenter Roeck
 *
 * Derived from:
 *    Hwmon client for S.M.A.R.T. hard disk drives with temperature sensors.
 *    (C) 2018 Linus Walleij
 *
 *    hwmon: Driver for SCSI/ATA temperature sensors
 *    by Constantin Baranov <const@mimas.ru>, submitted September 2009
 *
 * The primary means to read hard drive temperatures and temperature limits
 * is the SCT Command Transport feature set as specified in ATA8-ACS.
 * It can be used to read the current drive temperature, temperature limits,
 * and historic minimum and maximum temperatures. The SCT Command Transport
 * feature set is documented in "AT Attachment 8 - ATA/ATAPI Command Set
 * (ATA8-ACS)".
 *
 * If the SCT Command Transport feature set is not available, drive temperatures
 * may be readable through SMART attributes. Since SMART attributes are not well
 * defined, this method is only used as fallback mechanism.
 *
 * There are three SMART attributes which may report drive temperatures.
 * Those are defined as follows (from
 * http://www.cropel.com/library/smart-attribute-list.aspx).
 *
 * 190	Temperature	Temperature, monitored by a sensor somewhere inside
 *			the drive. Raw value typicaly holds the actual
 *			temperature (hexadecimal) in its rightmost two digits.
 *
 * 194	Temperature	Temperature, monitored by a sensor somewhere inside
 *			the drive. Raw value typicaly holds the actual
 *			temperature (hexadecimal) in its rightmost two digits.
 *
 * 231	Temperature	Temperature, monitored by a sensor somewhere inside
 *			the drive. Raw value typicaly holds the actual
 *			temperature (hexadecimal) in its rightmost two digits.
 *
 * Wikipedia defines attributes a bit differently.
 *
 * 190	Temperature	Value is equal to (100-temp. °C), allowing manufacturer
 *	Difference or	to set a minimum threshold which corresponds to a
 *	Airflow		maximum temperature. This also follows the convention of
 *	Temperature	100 being a best-case value and lower values being
 *			undesirable. However, some older drives may instead
 *			report raw Temperature (identical to 0xC2) or
 *			Temperature minus 50 here.
 * 194	Temperature or	Indicates the device temperature, if the appropriate
 *	Temperature	sensor is fitted. Lowest byte of the raw value contains
 *	Celsius		the exact temperature value (Celsius degrees).
 * 231	Life Left	Indicates the approximate SSD life left, in terms of
 *	(SSDs) or	program/erase cycles or available reserved blocks.
 *	Temperature	A normalized value of 100 represents a new drive, with
 *			a threshold value at 10 indicating a need for
 *			replacement. A value of 0 may mean that the drive is
 *			operating in read-only mode to allow data recovery.
 *			Previously (pre-2010) occasionally used for Drive
 *			Temperature (more typically reported at 0xC2).
 *
 * Common denominator is that the first raw byte reports the temperature
 * in degrees C on almost all drives. Some drives may report a fractional
 * temperature in the second raw byte.
 *
 * Known exceptions (from libatasmart):
 * - SAMSUNG SV0412H and SAMSUNG SV1204H) report the temperature in 10th
 *   degrees C in the first two raw bytes.
 * - A few Maxtor drives report an unknown or bad value in attribute 194.
 * - Certain Apple SSD drives report an unknown value in attribute 190.
 *   Only certain firmware versions are affected.
 *
 * Those exceptions affect older ATA drives and are currently ignored.
 * Also, the second raw byte (possibly reporting the fractional temperature)
 * is currently ignored.
 *
 * Many drives also report temperature limits in additional SMART data raw
 * bytes. The format of those is not well defined and varies widely.
 * The driver does not currently attempt to report those limits.
 *
 * According to data in smartmontools, attribute 231 is rarely used to report
 * drive temperatures. At the same time, several drives report SSD life left
 * in attribute 231, but do not support temperature sensors. For this reason,
 * attribute 231 is currently ignored.
 *
 * Following above definitions, temperatures are reported as follows.
 *   If SCT Command Transport is supported, it is used to read the
 *   temperature and, if available, temperature limits.
 * - Otherwise, if SMART attribute 194 is supported, it is used to read
 *   the temperature.
 * - Otherwise, if SMART attribute 190 is supported, it is used to read
 *   the temperature.
 */

#include <linux/ata.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/hwmon.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_driver.h>
#include <scsi/scsi_proto.h>

struct satatemp_data {
	struct list_head list;		/* list of instantiated devices */
	struct mutex lock;		/* protect data buffer accesses */
	struct scsi_device *sdev;	/* SCSI device */
	struct device *dev;		/* instantiating device */
	struct device *hwdev;		/* hardware monitoring device */
	u8 smartdata[ATA_SECT_SIZE];	/* local buffer */
	int (*get_temp)(struct satatemp_data *st, u32 attr, long *val);
	bool have_temp_lowest;		/* lowest temp in SCT status */
	bool have_temp_highest;		/* highest temp in SCT status */
	bool have_temp_min;		/* have min temp */
	bool have_temp_max;		/* have max temp */
	bool have_temp_lcrit;		/* have lower critical limit */
	bool have_temp_crit;		/* have critical limit */
	int temp_min;			/* min temp */
	int temp_max;			/* max temp */
	int temp_lcrit;			/* lower critical limit */
	int temp_crit;			/* critical limit */
};

static LIST_HEAD(satatemp_devlist);

#define ATA_MAX_SMART_ATTRS	30
#define SMART_TEMP_PROP_190	190
#define SMART_TEMP_PROP_194	194

#define SCT_STATUS_REQ_ADDR	0xe0
#define  SCT_STATUS_VERSION_LOW		0	/* log byte offsets */
#define  SCT_STATUS_VERSION_HIGH	1
#define  SCT_STATUS_TEMP		200
#define  SCT_STATUS_TEMP_LOWEST		201
#define  SCT_STATUS_TEMP_HIGHEST	202
#define SCT_READ_LOG_ADDR	0xe1
#define  SMART_READ_LOG			0xd5
#define  SMART_WRITE_LOG		0xd6

#define INVALID_TEMP		0x80

#define temp_is_valid(temp)	((temp) != INVALID_TEMP)
#define temp_from_sct(temp)	(((s8)(temp)) * 1000)

static inline bool ata_id_smart_supported(u16 *id)
{
	return id[ATA_ID_COMMAND_SET_1] & BIT(0);
}

static inline bool ata_id_smart_enabled(u16 *id)
{
	return id[ATA_ID_CFS_ENABLE_1] & BIT(0);
}

static int satatemp_scsi_command(struct satatemp_data *st,
				 u8 ata_command, u8 feature,
				 u8 lba_low, u8 lba_mid, u8 lba_high)
{
	static u8 scsi_cmd[MAX_COMMAND_SIZE];
	int data_dir;

	memset(scsi_cmd, 0, sizeof(scsi_cmd));
	scsi_cmd[0] = ATA_16;
	if (ata_command == ATA_CMD_SMART && feature == SMART_WRITE_LOG) {
		scsi_cmd[1] = (5 << 1);	/* PIO Data-out */
		/*
		 * No off.line or cc, write to dev, block count in sector count
		 * field.
		 */
		scsi_cmd[2] = 0x06;
		data_dir = DMA_TO_DEVICE;
	} else {
		scsi_cmd[1] = (4 << 1);	/* PIO Data-in */
		/*
		 * No off.line or cc, read from dev, block count in sector count
		 * field.
		 */
		scsi_cmd[2] = 0x0e;
		data_dir = DMA_FROM_DEVICE;
	}
	scsi_cmd[4] = feature;
	scsi_cmd[6] = 1;	/* 1 sector */
	scsi_cmd[8] = lba_low;
	scsi_cmd[10] = lba_mid;
	scsi_cmd[12] = lba_high;
	scsi_cmd[14] = ata_command;

	return scsi_execute_req(st->sdev, scsi_cmd, data_dir,
				st->smartdata, ATA_SECT_SIZE, NULL, HZ, 5,
				NULL);
}

static int satatemp_ata_command(struct satatemp_data *st, u8 feature, u8 select)
{
	return satatemp_scsi_command(st, ATA_CMD_SMART, feature, select,
				     ATA_SMART_LBAM_PASS, ATA_SMART_LBAH_PASS);
}

static int satatemp_get_smarttemp(struct satatemp_data *st, u32 attr,
				  long *temp)
{
	u8 *buf = st->smartdata;
	bool have_temp = false;
	u8 temp_raw;
	u8 csum;
	int err;
	int i;

	err = satatemp_ata_command(st, ATA_SMART_READ_VALUES, 0);
	if (err)
		return err;

	/* Checksum the read value table */
	csum = 0;
	for (i = 0; i < ATA_SECT_SIZE; i++)
		csum += buf[i];
	if (csum) {
		dev_dbg(&st->sdev->sdev_gendev,
			"checksum error reading SMART values\n");
		return -EIO;
	}

	for (i = 0; i < ATA_MAX_SMART_ATTRS; i++) {
		u8 *attr = buf + i * 12;
		int id = attr[2];

		if (!id)
			continue;

		if (id == SMART_TEMP_PROP_190) {
			temp_raw = attr[7];
			have_temp = true;
		}
		if (id == SMART_TEMP_PROP_194) {
			temp_raw = attr[7];
			have_temp = true;
			break;
		}
	}

	if (have_temp) {
		*temp = temp_raw * 1000;
		return 0;
	}

	return -ENXIO;
}

static int satatemp_get_scttemp(struct satatemp_data *st, u32 attr, long *val)
{
	u8 *buf = st->smartdata;
	int err;

	err = satatemp_ata_command(st, SMART_READ_LOG, SCT_STATUS_REQ_ADDR);
	if (err)
		return err;
	switch (attr) {
	case hwmon_temp_input:
		*val = temp_from_sct(buf[SCT_STATUS_TEMP]);
		break;
	case hwmon_temp_lowest:
		*val = temp_from_sct(buf[SCT_STATUS_TEMP_LOWEST]);
		break;
	case hwmon_temp_highest:
		*val = temp_from_sct(buf[SCT_STATUS_TEMP_HIGHEST]);
		break;
	default:
		err = -EINVAL;
		break;
	}
	return err;
}

static int satatemp_identify(struct satatemp_data *st)
{
	struct scsi_device *sdev = st->sdev;
	u8 *buf = st->smartdata;
	bool is_ata, is_sata;
	bool have_sct_data_table;
	bool have_sct_temp;
	bool have_smart;
	bool have_sct;
	u16 *ata_id;
	u16 version;
	long temp;
	u8 *vpd;
	int err;

	/* bail out immediately if there is no inquiry data */
	if (!sdev->inquiry || sdev->inquiry_len < 16)
		return -ENODEV;

	/*
	 * Inquiry data sanity checks (per SAT-5):
	 * - periheral qualifier must be 0
	 * - peripheral device type must be 0x0 (Direct access block device)
	 * - SCSI Vendor ID is "ATA     "
	 */
	if (sdev->inquiry[0] ||
	    strncmp(&sdev->inquiry[8], "ATA     ", 8))
		return -ENODEV;

	vpd = kzalloc(1024, GFP_KERNEL);
	if (!vpd)
		return -ENOMEM;

	err = scsi_get_vpd_page(sdev, 0x89, vpd, 1024);
	if (err) {
		kfree(vpd);
		return err;
	}

	/*
	 * More sanity checks.
	 * For VPD offsets and values see ANS Project INCITS 557,
	 * "Information technology - SCSI / ATA Translation - 5 (SAT-5)".
	 */
	if (vpd[1] != 0x89 || vpd[2] != 0x02 || vpd[3] != 0x38 ||
	    vpd[36] != 0x34 || vpd[56] != ATA_CMD_ID_ATA) {
		kfree(vpd);
		return -ENODEV;
	}
	ata_id = (u16 *)&vpd[60];
	is_ata = ata_id_is_ata(ata_id);
	is_sata = ata_id_is_sata(ata_id);
	have_sct = ata_id_sct_supported(ata_id);
	have_sct_data_table = ata_id_sct_data_tables(ata_id);
	have_smart = ata_id_smart_supported(ata_id) &&
				ata_id_smart_enabled(ata_id);

	kfree(vpd);

	/* bail out if this is not a SATA device */
	if (!is_ata || !is_sata)
		return -ENODEV;
	if (!have_sct)
		goto skip_sct;

	err = satatemp_ata_command(st, SMART_READ_LOG, SCT_STATUS_REQ_ADDR);
	if (err)
		goto skip_sct;

	version = (buf[SCT_STATUS_VERSION_HIGH] << 8) |
		  buf[SCT_STATUS_VERSION_LOW];
	if (version != 2 && version != 3)
		goto skip_sct;

	have_sct_temp = temp_is_valid(buf[SCT_STATUS_TEMP]);
	if (!have_sct_temp)
		goto skip_sct;

	st->have_temp_lowest = temp_is_valid(buf[SCT_STATUS_TEMP_LOWEST]);
	st->have_temp_highest = temp_is_valid(buf[SCT_STATUS_TEMP_HIGHEST]);

	if (!have_sct_data_table)
		goto skip_sct;

	/* Request and read temperature history table */
	memset(buf, '\0', sizeof(st->smartdata));
	buf[0] = 5;	/* data table command */
	buf[2] = 1;	/* read table */
	buf[4] = 2;	/* temperature history table */

	err = satatemp_ata_command(st, SMART_WRITE_LOG, SCT_STATUS_REQ_ADDR);
	if (err)
		goto skip_sct_data;

	err = satatemp_ata_command(st, SMART_READ_LOG, SCT_READ_LOG_ADDR);
	if (err)
		goto skip_sct_data;

	/*
	 * Temperature limits per AT Attachment 8 -
	 * ATA/ATAPI Command Set (ATA8-ACS)
	 */
	st->have_temp_max = temp_is_valid(buf[6]);
	st->have_temp_crit = temp_is_valid(buf[7]);
	st->have_temp_min = temp_is_valid(buf[8]);
	st->have_temp_lcrit = temp_is_valid(buf[9]);

	st->temp_max = temp_from_sct(buf[6]);
	st->temp_crit = temp_from_sct(buf[7]);
	st->temp_min = temp_from_sct(buf[8]);
	st->temp_lcrit = temp_from_sct(buf[9]);

skip_sct_data:
	if (have_sct_temp) {
		st->get_temp = satatemp_get_scttemp;
		return 0;
	}
skip_sct:
	if (!have_smart)
		return -ENODEV;
	st->get_temp = satatemp_get_smarttemp;
	return satatemp_get_smarttemp(st, hwmon_temp_input, &temp);
}

static int satatemp_read(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long *val)
{
	struct satatemp_data *st = dev_get_drvdata(dev);
	int err = 0;

	if (type != hwmon_temp)
		return -EINVAL;

	switch (attr) {
	case hwmon_temp_input:
	case hwmon_temp_lowest:
	case hwmon_temp_highest:
		mutex_lock(&st->lock);
		err = st->get_temp(st, attr, val);
		mutex_unlock(&st->lock);
		break;
	case hwmon_temp_lcrit:
		*val = st->temp_lcrit;
		break;
	case hwmon_temp_min:
		*val = st->temp_min;
		break;
	case hwmon_temp_max:
		*val = st->temp_max;
		break;
	case hwmon_temp_crit:
		*val = st->temp_crit;
		break;
	default:
		err = -EINVAL;
		break;
	}
	return err;
}

static umode_t satatemp_is_visible(const void *data,
				   enum hwmon_sensor_types type,
				   u32 attr, int channel)
{
	const struct satatemp_data *st = data;

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			return 0444;
		case hwmon_temp_lowest:
			if (st->have_temp_lowest)
				return 0444;
			break;
		case hwmon_temp_highest:
			if (st->have_temp_highest)
				return 0444;
			break;
		case hwmon_temp_min:
			if (st->have_temp_min)
				return 0444;
			break;
		case hwmon_temp_max:
			if (st->have_temp_max)
				return 0444;
			break;
		case hwmon_temp_lcrit:
			if (st->have_temp_lcrit)
				return 0444;
			break;
		case hwmon_temp_crit:
			if (st->have_temp_crit)
				return 0444;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return 0;
}

static const struct hwmon_channel_info *satatemp_info[] = {
	HWMON_CHANNEL_INFO(chip,
			   HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT |
			   HWMON_T_LOWEST | HWMON_T_HIGHEST |
			   HWMON_T_MIN | HWMON_T_MAX |
			   HWMON_T_LCRIT | HWMON_T_CRIT),
	NULL
};

static const struct hwmon_ops satatemp_ops = {
	.is_visible = satatemp_is_visible,
	.read = satatemp_read,
};

static const struct hwmon_chip_info satatemp_chip_info = {
	.ops = &satatemp_ops,
	.info = satatemp_info,
};

/*
 * The device argument points to sdev->sdev_dev. Its parent is
 * sdev->sdev_gendev, which we can use to get the scsi_device pointer.
 */
static int satatemp_add(struct device *dev, struct class_interface *intf)
{
	struct scsi_device *sdev = to_scsi_device(dev->parent);
	struct satatemp_data *st;
	int err;

	st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	st->sdev = sdev;
	st->dev = dev;
	mutex_init(&st->lock);

	if (satatemp_identify(st)) {
		err = -ENODEV;
		goto abort;
	}

	st->hwdev = hwmon_device_register_with_info(dev->parent, "satatemp",
						    st, &satatemp_chip_info,
						    NULL);
	if (IS_ERR(st->hwdev)) {
		err = PTR_ERR(st->hwdev);
		goto abort;
	}

	list_add(&st->list, &satatemp_devlist);
	return 0;

abort:
	kfree(st);
	return err;
}

static void satatemp_remove(struct device *dev, struct class_interface *intf)
{
	struct satatemp_data *st, *tmp;

	list_for_each_entry_safe(st, tmp, &satatemp_devlist, list) {
		if (st->dev == dev) {
			list_del(&st->list);
			hwmon_device_unregister(st->hwdev);
			kfree(st);
			break;
		}
	}
}

static struct class_interface satatemp_interface = {
	.add_dev = satatemp_add,
	.remove_dev = satatemp_remove,
};

static int __init satatemp_init(void)
{
	return scsi_register_interface(&satatemp_interface);
}

static void __exit satatemp_exit(void)
{
	scsi_unregister_interface(&satatemp_interface);
}

module_init(satatemp_init);
module_exit(satatemp_exit);

MODULE_AUTHOR("Guenter Roeck <linus@roeck-us.net>");
MODULE_DESCRIPTION("ATA temperature monitor");
MODULE_LICENSE("GPL");

