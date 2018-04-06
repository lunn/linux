// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2012 Guenter Roeck
 * Copyright (C) 2018 Andrew Lunn
 *
 */

#include <linux/init.h>
#include <linux/mfd/zii-scu-pic.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/timer.h>
#include <linux/watchdog.h>

#define SCU_PIC_WDT_TIMEOUT	300		/* 5 minutes */

static int nowayout = WATCHDOG_NOWAYOUT;

module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started");

struct zii_scu_pic_data {
	struct platform_device *pdev;
	struct watchdog_device wdt_dev;
	struct timer_list wdt_timer;
	unsigned long wdt_lastping;
};

static int zii_scu_pic_wdt_ping(struct watchdog_device *wdev)
{
	struct zii_scu_pic_data *data = watchdog_get_drvdata(wdev);
	int err;

	err = zii_scu_pic_read_byte(data->pdev, I2C_GET_SCU_PIC_WDT_STATE);
	mod_timer(&data->wdt_timer,
		  jiffies + min_t(unsigned long, SCU_PIC_WDT_TIMEOUT / 2,
				  wdev->timeout) * HZ);
	data->wdt_lastping = jiffies;

	return (err < 0 ? err : 0);
}

static int zii_scu_pic_wdt_start(struct watchdog_device *wdev)
{
	struct zii_scu_pic_data *data = watchdog_get_drvdata(wdev);
	int err;

	err = zii_scu_pic_write_byte(data->pdev, I2C_SET_SCU_PIC_WDT_STATE, 1);
	mod_timer(&data->wdt_timer, jiffies + min_t(unsigned long,
						    SCU_PIC_WDT_TIMEOUT / 2,
						    wdev->timeout) * HZ);

	return err;
}

static int zii_scu_pic_wdt_stop(struct watchdog_device *wdev)
{
	struct zii_scu_pic_data *data = watchdog_get_drvdata(wdev);
	int err;

	err = zii_scu_pic_write_byte(data->pdev, I2C_SET_SCU_PIC_WDT_STATE, 0);
	del_timer(&data->wdt_timer);

	return err;
}

static int zii_scu_pic_wdt_set_timeout(struct watchdog_device *wdev,
				       unsigned int t)
{
	wdev->timeout = t;
	zii_scu_pic_wdt_ping(wdev);

	return 0;
}

static void zii_scu_pic_wdt_timerfunc(struct timer_list *wdt_timer)
{
	struct zii_scu_pic_data *data = from_timer(data, wdt_timer, wdt_timer);

	if (time_after(jiffies, data->wdt_lastping +
		       data->wdt_dev.timeout * HZ)) {
		pr_crit("Software watchdog timeout: Initiating system reboot.\n");
		emergency_restart();
	}
	zii_scu_pic_wdt_ping(&data->wdt_dev);
}

static const struct watchdog_info zii_scu_pic_wdt_ident = {
	.options	= WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING |
			  WDIOF_SETTIMEOUT,
	.identity	= "ZII SCU Pic Watchdog",
};

static const struct watchdog_ops zii_scu_pic_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= zii_scu_pic_wdt_start,
	.stop		= zii_scu_pic_wdt_stop,
	.ping		= zii_scu_pic_wdt_ping,
	.set_timeout	= zii_scu_pic_wdt_set_timeout,
};

static int zii_scu_pic_wdt_get_reason(struct zii_scu_pic_data *data)
{
	int reason;

	reason = zii_scu_pic_read_byte(data->pdev,
				       I2C_GET_SCU_PIC_RESET_REASON);
	if (reason < 0)
		return reason;

	switch (reason) {
	case ZII_SCU_PIC_RESET_REASON_BROWNOUT:
		data->wdt_dev.bootstatus = WDIOF_POWERUNDER;
		break;
	case ZII_SCU_PIC_RESET_REASON_SW_WATCHDOG:
	case ZII_SCU_PIC_RESET_REASON_HOST_REQUEST:
	case ZII_SCU_PIC_RESET_REASON_HW_WDT_TIMEOUT:
	case ZII_SCU_PIC_RESET_REASON_RESET_TIMER:
		data->wdt_dev.bootstatus = WDIOF_CARDRESET;
		break;
	case ZII_SCU_PIC_RESET_REASON_TEMP_FAULT:
		data->wdt_dev.bootstatus = WDIOF_OVERHEAT;
		break;
	case ZII_SCU_PIC_RESET_REASON_NORMAL:
	case ZII_SCU_PIC_RESET_REASON_HW_WDT_FROM_SLEEP:
	case ZII_SCU_PIC_RESET_REASON_MCLR_FROM_SLEEP:
	case ZII_SCU_PIC_RESET_REASON_MCLR_FROM_RUN:
	case ZII_SCU_PIC_RESET_REASON_UKNOWN_REASON:
	default:
		data->wdt_dev.bootstatus = 0;
		break;
	}

	return 0;
}

static int zii_scu_pic_wdt_probe(struct platform_device *pdev)
{
	struct zii_scu_pic_data *data;
	int err;

	data = devm_kzalloc(&pdev->dev, sizeof(struct zii_scu_pic_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	watchdog_set_drvdata(&data->wdt_dev, data);

	data->pdev = pdev;
	timer_setup(&data->wdt_timer, zii_scu_pic_wdt_timerfunc, 0);

	data->wdt_dev.info = &zii_scu_pic_wdt_ident;
	data->wdt_dev.ops = &zii_scu_pic_wdt_ops;
	data->wdt_dev.timeout = SCU_PIC_WDT_TIMEOUT;
	data->wdt_dev.min_timeout = 1;
	data->wdt_dev.max_timeout = 0xffff;
	watchdog_set_nowayout(&data->wdt_dev, nowayout);
	data->wdt_dev.parent = pdev->dev.parent;

	err = zii_scu_pic_wdt_get_reason(data);
	if (err)
		return err;

	zii_scu_pic_wdt_stop(&data->wdt_dev);

	platform_set_drvdata(pdev, data);

	return watchdog_register_device(&data->wdt_dev);
}

static int zii_scu_pic_wdt_remove(struct platform_device *pdev)
{
	struct zii_scu_pic_data *data = platform_get_drvdata(pdev);

	watchdog_unregister_device(&data->wdt_dev);

	del_timer_sync(&data->wdt_timer);

	return 0;
}

static struct platform_driver zii_scu_pic_wdt_driver = {
	.driver.name	= "zii-scu-pic-wdt",
	.probe		= zii_scu_pic_wdt_probe,
	.remove		= zii_scu_pic_wdt_remove,
};

module_platform_driver(zii_scu_pic_wdt_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:zii-scu-pic-wdt");
