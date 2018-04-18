// SPDX-License-Identifier: GPL-2.0+
//
// HWMON driver for RAVE SP
//
// Copyright (C) 2019 Zodiac Inflight Innovations
//
//

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mfd/rave-sp.h>
#include <linux/module.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

static int rave_sp_hwmon_read_string(enum hwmon_sensor_types type,
				     int channel, const char **str,
				     const char * const in[],
				     const char * const temp[],
				     const char * const curr[])
{
	switch (type) {
	case hwmon_in:
		*str = in[channel];
		break;
	case hwmon_temp:
		*str = temp[channel];
		break;
	case hwmon_curr:
		*str = curr[channel];
		break;
	default:
		return -EINVAL;
	}

	return 0;
}


static int rave_sp_hwmon_rmu2_read_string(struct device *dev,
					  enum hwmon_sensor_types type,
					  u32 attr, int channel,
					  const char **str)
{
	static const char * const in[] = {
		"GEN_PHY_1V2",
		NULL,	/* Just kidding, this channel is not connected */
		"GEN_3V3",
		"5V_MAIN",
		"12V_MAIN",
	};

	static const char * const temp[] = {
		"TEMPERATURE",
	};

	return rave_sp_hwmon_read_string(type, channel, str,
					 in, temp, NULL);
}

static int rave_sp_hwmon_rdu1_read_string(struct device *dev,
					  enum hwmon_sensor_types type,
					  u32 attr, int channel,
					  const char **str)
{
	static const char * const in[] = {
		"RMB_28V_FIL",
	};

	static const char * const temp[] = {
		"TEMPERATURE",
		"TEMPERATURE_2"
	};

	static const char * const curr[] = {
		"RMB_28V_CURRENT"
	};

	return rave_sp_hwmon_read_string(type, channel, str,
					 in, temp, curr);
}

static int rave_sp_hwmon_rdu2_read_string(struct device *dev,
					  enum hwmon_sensor_types type,
					  u32 attr, int channel,
					  const char **str)
{
	static const char * const in[] = {
		"RMB_3V3_PMIC",
		"RMB_3V3_MCU",
		"RMB_5V_MAIN",
		"RMB_12V_MAIN",
		"RMB_28V_FIL",
		"RMB_28V_HOTSWAP",
		"DEB_1V8",
		"DEB_3V3",
		"DEB_28V_DEB",
		"DEB_28V_RDU"
	};

	static const char * const temp[] = {
		"TEMPERATURE_RMB",
		"TEMPERATURE_DEB"
	};

	static const char * const curr[] = {
		"RMB_28V_CURRENT"
	};

	return rave_sp_hwmon_read_string(type, channel, str,
					 in, temp, curr);
}

static const struct hwmon_channel_info *rdu1_info[] = {
	HWMON_CHANNEL_INFO(in,    HWMON_I_INPUT | HWMON_I_LABEL),

	HWMON_CHANNEL_INFO(temp,  HWMON_T_INPUT | HWMON_T_LABEL,
				  HWMON_T_INPUT | HWMON_T_LABEL),

	HWMON_CHANNEL_INFO(curr,  HWMON_C_INPUT | HWMON_C_LABEL),
	NULL
};

static const struct hwmon_channel_info *rdu2_info[] = {
	HWMON_CHANNEL_INFO(in,    HWMON_I_INPUT | HWMON_I_LABEL,
				  HWMON_I_INPUT | HWMON_I_LABEL,
				  HWMON_I_INPUT | HWMON_I_LABEL,
				  HWMON_I_INPUT | HWMON_I_LABEL,
				  HWMON_I_INPUT | HWMON_I_LABEL,
				  HWMON_I_INPUT | HWMON_I_LABEL,
				  HWMON_I_INPUT | HWMON_I_LABEL,
				  HWMON_I_INPUT | HWMON_I_LABEL,
				  HWMON_I_INPUT | HWMON_I_LABEL,
				  HWMON_I_INPUT | HWMON_I_LABEL),

	HWMON_CHANNEL_INFO(temp,  HWMON_T_INPUT | HWMON_T_LABEL,
				  HWMON_T_INPUT | HWMON_T_LABEL),

	HWMON_CHANNEL_INFO(curr,  HWMON_C_INPUT | HWMON_C_LABEL),
	NULL
};

static const struct hwmon_channel_info *rmu2_info[] = {
	HWMON_CHANNEL_INFO(in,    HWMON_I_INPUT | HWMON_I_LABEL,
				  HWMON_I_INPUT, /* Not connected */
				  HWMON_I_INPUT | HWMON_I_LABEL,
				  HWMON_I_INPUT | HWMON_I_LABEL,
				  HWMON_I_INPUT | HWMON_I_LABEL),

	HWMON_CHANNEL_INFO(temp,  HWMON_T_INPUT | HWMON_T_LABEL),
	NULL
};

static inline unsigned int le_f88_to_milli(__le16 le_f88)
{
	return le16_to_cpu(le_f88) * 1000 / 256;
}

static inline int le_halfdg_to_mdg(__le16 le_halfdg)
{
	return le16_to_cpu(le_halfdg) * 500;
}

static umode_t rave_sp_hwmon_rdu_is_visible(const void *data,
					     enum hwmon_sensor_types type,
					     u32 attr, int channel)
{
	return 0444;
}

static umode_t rave_sp_hwmon_rmu2_is_visible(const void *data,
					     enum hwmon_sensor_types type,
					     u32 attr, int channel)
{
	if (type == hwmon_in && channel == 1)
		return 0;

	return 0444;
}

static int rave_sp_hwmon_rdu1_read(struct device *dev,
				   enum hwmon_sensor_types type,
				   u32 attr, int channel, long *val)
{
	struct rave_sp *sp = dev_get_drvdata(dev);
	struct rave_sp_status status;
	int ret;

	ret = rave_sp_get_status(sp, &status);
	if (ret)
		return ret;

	switch (type) {
	case hwmon_in:
		*val = le_f88_to_milli(status.voltage_28);
		break;
	case hwmon_temp:
		*val = le_halfdg_to_mdg(status.temp[channel]);
		break;
	case hwmon_curr:
		*val = le16_to_cpu(status.backlight_current);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rave_sp_hwmon_rdu2_read(struct device *dev,
				   enum hwmon_sensor_types type,
				   u32 attr, int channel, long *val)
{
	struct rave_sp *sp = dev_get_drvdata(dev);
	u8 cmd[3];
	__le16 rsp;
	int ret;

	switch (type) {
	case hwmon_in:
		cmd[0] = RAVE_SP_CMD_GET_VOLTAGE;
		break;
	case hwmon_temp:
		cmd[0] = RAVE_SP_CMD_GET_TEMPERATURE;
		break;
	case hwmon_curr:
		cmd[0] = RAVE_SP_CMD_GET_CURRENT;
		break;
	default:
		return -EINVAL;
	}
	/* cmd[1] is going to be filled by rave_sp_exec */
	cmd[2] = channel;

	ret = rave_sp_exec(sp, cmd, sizeof(cmd), &rsp, sizeof(rsp));
	if (ret)
		return ret;

	if (type == hwmon_temp)
		*val = le_halfdg_to_mdg(rsp);
	else
		*val = le_f88_to_milli(rsp);

	return 0;
}

static const struct hwmon_ops rdu1_ops = {
	.is_visible	= rave_sp_hwmon_rdu_is_visible,
	.read		= rave_sp_hwmon_rdu1_read,
	.read_string	= rave_sp_hwmon_rdu1_read_string,
};

static const struct hwmon_ops rdu2_ops = {
	.is_visible	= rave_sp_hwmon_rdu_is_visible,
	.read		= rave_sp_hwmon_rdu2_read,
	.read_string	= rave_sp_hwmon_rdu2_read_string,
};

static struct hwmon_ops rmu2_ops = {
	.is_visible	= rave_sp_hwmon_rmu2_is_visible,
	.read		= rave_sp_hwmon_rdu2_read,
	.read_string	= rave_sp_hwmon_rmu2_read_string,
};

static const struct hwmon_chip_info rdu1_hwmon = {
	.ops	= &rdu1_ops,
	.info	= rdu1_info,
};

static const struct hwmon_chip_info rdu2_hwmon = {
	.ops	= &rdu2_ops,
	.info	= rdu2_info,
};

static const struct hwmon_chip_info rmu2_hwmon = {
	.ops	= &rmu2_ops,
	.info	= rmu2_info,
};

static const struct of_device_id rave_sp_hwmon_of_match[] = {
	{
		.compatible = "zii,rave-sp-hwmon-rdu1",
		.data = &rdu1_hwmon,
	},
	{
		.compatible = "zii,rave-sp-hwmon-rdu2",
		.data = &rdu2_hwmon,
	},
	{
		.compatible = "zii,rave-sp-hwmon-rmu2",
		.data = &rmu2_hwmon,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rave_sp_hwmon_of_match);

static int rave_sp_hwmon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct hwmon_chip_info *info = of_device_get_match_data(dev);
	struct rave_sp *sp = dev_get_drvdata(dev->parent);
	struct device *hwmon_dev;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, KBUILD_MODNAME,
							 sp, info, NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static struct platform_driver rave_sp_hwmon_driver = {
	.probe = rave_sp_hwmon_probe,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = rave_sp_hwmon_of_match,
	},
};
module_platform_driver(rave_sp_hwmon_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrey Vostrikov <andrey.vostrikov@cogentembedded.com>");
MODULE_AUTHOR("Nikita Yushchenko <nikita.yoush@cogentembedded.com>");
MODULE_AUTHOR("Andrey Smirnov <andrew.smirnov@gmail.com>");
MODULE_DESCRIPTION("HWMON driver for RAVE SP connected sensors");
