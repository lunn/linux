// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2012 Guenter Roeck
 * Copyright (C) 2018 Andrew Lunn
 */

#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mfd/zii-scu-pic.h>
#include <linux/module.h>
#include <linux/platform_device.h>

struct zii_scu_pic_data {
	struct platform_device *pdev;
	struct device *hwmon;
	int model;
	int rev;
};

static int zii_scu_pic_temp_get(struct zii_scu_pic_data *data,
			       int channel, long *val)
{
	struct platform_device *pdev = data->pdev;
	int temp_lo;
	int temp_hi;

	switch (channel) {
	case 0:
		temp_hi = zii_scu_pic_read_byte(pdev,
						I2C_GET_SCU_PIC_LOCAL_TEMP);
		temp_lo = 0;

		break;
	case 1:
		temp_hi = zii_scu_pic_read_byte(pdev,
					     I2C_GET_SCU_PIC_REMOTE_TEMP);
		temp_lo = 0;
		break;
	case 2:
		temp_hi = zii_scu_pic_read_byte(pdev,
						I2C_GET_SCU_PIC_LM75_PS_TEMP_H);
		temp_lo = zii_scu_pic_read_byte(pdev,
						I2C_GET_SCU_PIC_LM75_PS_TEMP_L);
		break;
	case 3:
		temp_hi = zii_scu_pic_read_byte(
			pdev, I2C_GET_SCU_PIC_LM75_BOTTOM_AIRFLOW_TEMP_H);
		temp_lo = zii_scu_pic_read_byte(
			pdev, I2C_GET_SCU_PIC_LM75_BOTTOM_AIRFLOW_TEMP_L);
		break;
	case 4:
		temp_hi = zii_scu_pic_read_byte(
			pdev, I2C_GET_SCU_PIC_LM75_TOP_AIRFLOW_TEMP_H);
		temp_lo = zii_scu_pic_read_byte(
			pdev, I2C_GET_SCU_PIC_LM75_TOP_AIRFLOW_TEMP_L);
		break;
	default:
		return -EINVAL;
	}

	*val = ((s16)(temp_hi << 8 | temp_lo) >> 7) * 500;

	return 0;
}

static int zii_scu_pic_fan_get(struct zii_scu_pic_data *data,
			       int channel, long *val)
{
	struct platform_device *pdev = data->pdev;
	int fan;

	switch (channel) {
	case 0:
		fan = zii_scu_pic_read_byte(pdev,
					    I2C_GET_SCU_PIC_FAN1_SPEED);
		break;
	case 1:
		fan = zii_scu_pic_read_byte(pdev,
					    I2C_GET_SCU_PIC_FAN2_SPEED);
		break;
	default:
		return -EINVAL;
	}

	if (fan < 0)
		return fan;

	switch (data->model) {
	case FAN_CONTR_MODEL_ADM1031:
		*val = DIV_ROUND_CLOSEST(11250 * 60, fan);
	case FAN_CONTR_MODEL_MAX6639:
		*val = DIV_ROUND_CLOSEST(8000 * 30, fan);
		break;
	default:
		return -EINVAL;
	}

	if (fan == 255)
		*val = 0;

	return 0;
}

static int zii_scu_pic_read(struct device *dev,
			    enum hwmon_sensor_types type,
			    u32 attr, int channel, long *val)
{
	struct zii_scu_pic_data *data = dev_get_drvdata(dev);

	if (type == hwmon_temp && attr == hwmon_temp_input)
		return zii_scu_pic_temp_get(data, channel, val);

	if (type == hwmon_fan && attr == hwmon_fan_input)
		return zii_scu_pic_fan_get(data, channel, val);

	return -EOPNOTSUPP;
}

static const char *const zii_scu_pic_temp_labels[] = {
	"local",
	"remote",
	"power_supply",
	"front",
	"back",
};

static int zii_scu_pic_string(struct device *dev,
			      enum hwmon_sensor_types type,
			      u32 attr, int channel, const char **str)
{
	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_label:
			*str = zii_scu_pic_temp_labels[channel];
			return 0;
		default:
			return -EOPNOTSUPP;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}

	return -EOPNOTSUPP;
}

static umode_t zii_scu_pic_is_visible(const void *data,
					  enum hwmon_sensor_types type,
					  u32 attr, int channel)
{
	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
		case hwmon_temp_label:
			return 0444;
		}
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_input:
			return 0444;
		}
	default:
		break;
	}

	return 0;
}

static const u32 zii_scu_pic_temp_config_v45[] = {
	HWMON_T_INPUT | HWMON_T_LABEL,
	HWMON_T_INPUT | HWMON_T_LABEL,
	0
};

static const u32 zii_scu_pic_temp_config_v6[] = {
	HWMON_T_INPUT | HWMON_T_LABEL,
	HWMON_T_INPUT | HWMON_T_LABEL,
	HWMON_T_INPUT | HWMON_T_LABEL,
	HWMON_T_INPUT | HWMON_T_LABEL,
	HWMON_T_INPUT | HWMON_T_LABEL,
	0
};

static struct hwmon_channel_info zii_scu_pic_temp = {
	.type = hwmon_temp,
};

static const u32 zii_scu_pic_fan_config[] = {
	HWMON_F_INPUT,
	HWMON_F_INPUT,
	0
};

static const struct hwmon_channel_info zii_scu_pic_fan = {
	.type = hwmon_fan,
	.config = zii_scu_pic_fan_config,
};

static const struct hwmon_channel_info *zii_scu_pic_info[] = {
	&zii_scu_pic_temp,
	&zii_scu_pic_fan,
	NULL,
};

static const struct hwmon_ops zii_scu_pic_ops = {
	.is_visible = zii_scu_pic_is_visible,
	.read = zii_scu_pic_read,
	.read_string = zii_scu_pic_string,
};

static const struct hwmon_chip_info zii_scu_pic_chip_info = {
	.ops = &zii_scu_pic_ops,
	.info = zii_scu_pic_info,
};

static int zii_scu_pic_get_fan_model(struct zii_scu_pic_data *data)
{
	struct platform_device *pdev = data->pdev;
	struct device *dev = &pdev->dev;

	data->model = zii_scu_pic_read_byte(pdev,
					    I2C_GET_SCU_PIC_FAN_CONTR_MODEL);
	if (data->model < 0 || data->model == 0xff) {
		dev_err(dev, "Failed to read fan controller model (%d)\n",
			data->model);
		data->model = FAN_CONTR_MODEL_ADM1031;
	}

	data->rev = zii_scu_pic_read_byte(pdev,
					  I2C_GET_SCU_PIC_FAN_CONTR_REV);
	if (data->rev < 0 || data->rev == 0xff) {
		dev_err(dev, "Failed to read fan controller revision (%d)\n",
			data->rev);
		data->rev = 0;
	}

	dev_info(dev, "Fan controller model 0x%02x, revision 0x%02x.\n",
		 data->model, data->rev);

	return 0;
}

static int zii_scu_pic_hwmon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct zii_scu_pic_data *data;
	int major;
	int err;

	data = devm_kzalloc(&pdev->dev, sizeof(struct zii_scu_pic_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->pdev = pdev;

	major = zii_scu_pic_read_byte(pdev,
				      I2C_GET_SCU_PIC_FIRMWARE_REV_MAJOR);
	switch (major) {
	case 4:
	case 5:
		zii_scu_pic_temp.config = zii_scu_pic_temp_config_v45;
		break;
	case 6:
		zii_scu_pic_temp.config = zii_scu_pic_temp_config_v6;
		break;
	default:
		return -EINVAL;
	}

	err = zii_scu_pic_get_fan_model(data);
	if (err)
		return err;

	platform_set_drvdata(pdev, data);

	data->hwmon = devm_hwmon_device_register_with_info(
		dev, "zii_scu_pic", data,
		&zii_scu_pic_chip_info,
		NULL);

	return PTR_ERR_OR_ZERO(data->hwmon);
}

static int zii_scu_pic_hwmon_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver zii_scu_pic_hwmon_driver = {
	.driver.name	= "zii-scu-pic-hwmon",
	.probe		= zii_scu_pic_hwmon_probe,
	.remove		= zii_scu_pic_hwmon_remove,
};

module_platform_driver(zii_scu_pic_hwmon_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:zii-scu-pic-hwmon");
