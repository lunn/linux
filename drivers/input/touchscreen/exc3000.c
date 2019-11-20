// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for I2C connected EETI EXC3000 multiple touch controller
 *
 * Copyright (C) 2017 Ahmet Inan <inan@distec.de>
 * Copyright (C) 2019 Pengutronix <kernel@pengutronix.de>
 * Copyright (C) 2019 Zodiac Inflight Innovations
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/timer.h>
#include <asm/unaligned.h>
#include <linux/firmware.h>
#include <linux/delay.h>

#define EXC3000_NUM_SLOTS		10
#define EXC3000_SLOTS_PER_FRAME		5
#define EXC3000_LEN_FRAME		66
#define EXC3000_LEN_VENDOR_REQUEST	68
#define EXC3000_LEN_POINT		10
#define EXC3000_VENDOR_EVENT		3
#define EXC3000_MT_EVENT		6
#define EXC3000_TIMEOUT_MS		100

struct exc3000_data {
	struct i2c_client *client;
	struct input_dev *input;
	struct touchscreen_properties prop;
	struct timer_list timer;
	u8 buf[2 * EXC3000_LEN_FRAME];
	struct mutex vendor_data_lock;
	struct completion vendor_data_done;
	char *type, *model, *fw_rev;
	int update_status;
};

static void exc3000_report_slots(struct input_dev *input,
				 struct touchscreen_properties *prop,
				 const u8 *buf, int num)
{
	for (; num--; buf += EXC3000_LEN_POINT) {
		if (buf[0] & BIT(0)) {
			input_mt_slot(input, buf[1]);
			input_mt_report_slot_state(input, MT_TOOL_FINGER, true);
			touchscreen_report_pos(input, prop,
					       get_unaligned_le16(buf + 2),
					       get_unaligned_le16(buf + 4),
					       true);
		}
	}
}

static void exc3000_timer(struct timer_list *t)
{
	struct exc3000_data *data = from_timer(data, t, timer);

	input_mt_sync_frame(data->input);
	input_sync(data->input);
}

static inline void exc3000_schedule_timer(struct exc3000_data *data)
{
	mod_timer(&data->timer, jiffies + msecs_to_jiffies(EXC3000_TIMEOUT_MS));
}

static int exc3000_read_frame(struct i2c_client *client, u8 *buf)
{
	int ret;

	ret = i2c_master_send(client, "'", 2);
	if (ret < 0)
		return ret;

	if (ret != 2)
		return -EIO;

	ret = i2c_master_recv(client, buf, EXC3000_LEN_FRAME);
	if (ret < 0)
		return ret;

	if (ret != EXC3000_LEN_FRAME)
		return -EIO;

	if (get_unaligned_le16(buf) != EXC3000_LEN_FRAME)
		return -EINVAL;

	return 0;
}

static int exc3000_handle_mt_event(struct exc3000_data *data)
{
	struct input_dev *input = data->input;
	int ret, total_slots;
	u8 *buf = data->buf;

	total_slots = buf[3];
	if (!total_slots || total_slots > EXC3000_NUM_SLOTS) {
		ret = -EINVAL;
		goto out_fail;
	}

	if (total_slots > EXC3000_SLOTS_PER_FRAME) {
		/* Read 2nd frame to get the rest of the contacts. */
		ret = exc3000_read_frame(data->client, buf + EXC3000_LEN_FRAME);
		if (ret)
			goto out_fail;

		/* 2nd chunk must have number of contacts set to 0. */
		if (buf[EXC3000_LEN_FRAME + 3] != 0) {
			ret = -EINVAL;
			goto out_fail;
		}
	}

	/*
	 * We read full state successfully, no contacts will be "stuck".
	 */
	del_timer_sync(&data->timer);

	while (total_slots > 0) {
		int slots = min(total_slots, EXC3000_SLOTS_PER_FRAME);
		exc3000_report_slots(input, &data->prop, buf + 4, slots);
		total_slots -= slots;
		buf += EXC3000_LEN_FRAME;
	}

	input_mt_sync_frame(input);
	input_sync(input);

	return 0;

out_fail:
	/* Schedule a timer to release "stuck" contacts */
	exc3000_schedule_timer(data);

	return ret;
}

static int exc3000_handle_vendor_event(struct exc3000_data *data)
{
	complete(&data->vendor_data_done);

	return 0;
}

static irqreturn_t exc3000_interrupt(int irq, void *dev_id)
{
	struct exc3000_data *data = dev_id;
	u8 *buf = data->buf;
	int ret;

	ret = exc3000_read_frame(data->client, buf);
	if (ret) {
		/* Schedule a timer to release "stuck" contacts */
		exc3000_schedule_timer(data);
		goto out;
	}

	switch (buf[2]) {
		case EXC3000_MT_EVENT:
			exc3000_handle_mt_event(data);
			break;
		case EXC3000_VENDOR_EVENT:
			exc3000_handle_vendor_event(data);
			break;
		default:
			break;
	}

out:
	return IRQ_HANDLED;
}

static int exc3000_vendor_data_request(struct exc3000_data *data, u8 *request,
				       u8 request_len, u8 *response)
{
	u8 buf[EXC3000_LEN_VENDOR_REQUEST] = { 0x67, 0x00, 0x42, 0x00, 0x03 };
	int ret;

	mutex_lock(&data->vendor_data_lock);

	reinit_completion(&data->vendor_data_done);

	buf[5] = request_len;
	memcpy(&buf[6], request, request_len);

	ret = i2c_master_send(data->client, buf, EXC3000_LEN_VENDOR_REQUEST);
	if (ret < 0)
		goto out_unlock;

	if (response) {
		wait_for_completion(&data->vendor_data_done);
		memcpy(response, &data->buf[4], data->buf[3]);
		ret = data->buf[3];
	}

out_unlock:
	mutex_unlock(&data->vendor_data_lock);

	return ret;
}
static int exc3000_populate_device_info(struct exc3000_data *data)
{
	struct device *dev = &data->client->dev;
	u8 response[EXC3000_LEN_FRAME];
	int ret;

	/* query type info */
	ret = exc3000_vendor_data_request(data, (u8[]){0x46}, 1, response);
	if (ret < 0)
		return -ENODEV;

	if (data->type)
		devm_kfree(dev, data->type);
	data->type = devm_kmemdup(dev, &response[1], ret - 1, GFP_KERNEL);

	/* query model info */
	ret = exc3000_vendor_data_request(data, (u8[]){0x45}, 1, response);
	if (ret < 0)
		return -ENODEV;

	if (data->model)
		devm_kfree(dev, data->model);
	data->model = devm_kmemdup(dev, &response[1], ret - 1, GFP_KERNEL);

	/* query bootloader info */
	ret = exc3000_vendor_data_request(data,
					  (u8[]){0x39, 0x02}, 2, response);
	if (ret < 0)
		return -ENODEV;

	/*
	 * If the bootloader version is non-zero then the device is in
	 * bootloader mode and won't answer a query for the application FW
	 * version, so we just use the bootloader version info.
	 */
	if (response[2] || response[3]) {
		char bl_version[8];

		snprintf(bl_version, 8, "%d.%d", response[2], response[3]);
		if (data->fw_rev)
			devm_kfree(dev, data->fw_rev);
		data->fw_rev = devm_kmemdup(dev, bl_version,
					    strlen(bl_version), GFP_KERNEL);
	} else {
		/* query application firmware version */
		ret = exc3000_vendor_data_request(data,
						  (u8[]){0x44}, 1, response);
		if (ret < 0)
			return -ENODEV;

		if (data->fw_rev)
			devm_kfree(dev, data->fw_rev);
		data->fw_rev = devm_kmemdup(dev, &response[1],
					    ret - 1, GFP_KERNEL);
	}

	dev_info(&data->client->dev,
		 "found type %s, model %s, firmware revision %s",
		 data->type, data->model, data->fw_rev);

	return 0;
}

static ssize_t exc3000_sysfs_type_show(struct device *dev,
				       struct device_attribute *dattr,
				       char *buf)
{
	struct exc3000_data *data = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%s\n", data->type);
}
static DEVICE_ATTR(type, 0444, exc3000_sysfs_type_show, NULL);

static ssize_t exc3000_sysfs_model_show(struct device *dev,
					struct device_attribute *dattr,
					char *buf)
{
	struct exc3000_data *data = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%s\n", data->model);
}
static DEVICE_ATTR(model, 0444, exc3000_sysfs_model_show, NULL);

static ssize_t exc3000_sysfs_fw_rev_show(struct device *dev,
					 struct device_attribute *dattr,
					 char *buf)
{
	struct exc3000_data *data = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%s\n", data->fw_rev);
}
static DEVICE_ATTR(fw_rev, 0444, exc3000_sysfs_fw_rev_show, NULL);

static void exc3000_generate_unlock_response(u8 *challenge, u8 *response)
{
	u8 op, rot, sum;
	int i;

	op = challenge[0] + challenge[3];
	rot = challenge[1] + challenge[2];
	sum = challenge[0] + challenge[1] + challenge[2] + challenge[3];

	for (i = 0; i < 4; i++) {
		if ((op >> i) & 0x1) {
			response[i] = sum + challenge[(rot + i) & 0x3];
		} else {
			response[i] = sum - challenge[(rot + i) & 0x3];
		}
	}
}

static int exc3000_firmware_update(struct exc3000_data *data,
				   const struct firmware *fw)
{
	struct device *dev = &data->client->dev;
	u8 resp[EXC3000_LEN_FRAME];
	int ret, i;

	dev_info(dev, "starting firmware update\n");

	/* 1: check device state */
	ret = exc3000_vendor_data_request(data, (u8[]){0x39, 0x02}, 2, resp);
	if (ret < 0)
		goto out;

	/* 2: switch state from app to bootloader mode if necessary */
	if (!resp[2] && !resp[3]) {
		u8 unlock_req[6] = { 0x3a, 0xfc };

		dev_dbg(dev, "device in app mode, switching to bootloader\n");

		/* 2.1 request unlock challenge */
		ret = exc3000_vendor_data_request(data,
						  (u8[]){0x3a, 0xfb}, 2, resp);
		if (ret < 0)
			goto out;

		/* 2.2 generate and send response */
		exc3000_generate_unlock_response(&resp[2], &unlock_req[2]);
		ret = exc3000_vendor_data_request(data, unlock_req, 6, resp);
		if (ret < 0)
			goto out;

		if (resp[2] != 0x01) {
			dev_err(dev, "device unlock failed, aborting\n");
			ret = -EINVAL;
			goto out;
		}

		/* 2.3 unknown, but required and invariant data */
		ret = exc3000_vendor_data_request(data,
						  (u8[]){0x3a, 0xfe, 0x34,
						         0x43, 0xcc}, 5, resp);
		if (ret < 0)
			goto out;

		/* 2.4 reset controller */
		ret = exc3000_vendor_data_request(data, (u8[]){0x3a, 0xff},
						  2, NULL);
		if (ret < 0)
			goto out;

		/* wait for controller init after reset */
		msleep(500);

		/* 2.5: check communication after reset */
		ret = exc3000_vendor_data_request(data, (u8[]){0x39, 0x01},
						  2, resp);
		if (ret < 0)
			goto out;

		if (resp[1] != 0x02) {
			dev_err(dev, "device ping request NACK, aborting\n");
			ret = -EINVAL;
			goto out;
		}

		/* 2.6: check device mode again */
		ret = exc3000_vendor_data_request(data, (u8[]){0x39, 0x02},
						  2, resp);
		if (ret < 0)
			goto out;

		if (!resp[2] && !resp[3]) {
			dev_err(dev, "device still app mode, aborting\n");
			ret = -EINVAL;
			goto out;
		}
	}

	/* 3: start firmware upload */
	dev_dbg(dev, "start firmware upload\n");
	ret = exc3000_vendor_data_request(data, (u8[]){0x3a, 0x04}, 2, resp);
	if (ret < 0)
		goto out;

	if (resp[2] != 0x01) {
		dev_err(dev, "firmware update start NACK, aborting\n");
		ret = -EINVAL;
		goto out;
	}

	/* 4: upload firmware */
	for (i = 0x56; i < fw->size; i += 36) {
		u8 fw_chunk[37] = { 0x3a, 0x01, fw->data[i],
				    fw->data[i+1],fw->data[i+34] };

		memcpy(&fw_chunk[5], &fw->data[i+2], 32);
		ret = exc3000_vendor_data_request(data, fw_chunk, 37, resp);
		if (ret < 0)
			goto out;

		if (resp[2] != fw->data[i] || resp[3] != fw->data[i+1] ||
		    resp[4] != fw->data[i+34]) {
			dev_err(dev,
				"firmware update readback wrong, aborting\n");
			ret = -EINVAL;
			goto out;
		}

		data->update_status = DIV_ROUND_UP(i * 100, fw->size);
	}

	/* 5: end firmware upload */
	ret = exc3000_vendor_data_request(data,
					  (u8[]){0x3a, 0x05, fw->data[0x37],
						fw->data[0x38], fw->data[0x39],
						fw->data[0x1f], fw->data[0x20]},
					  7, resp);
	if (ret < 0)
		goto out;

	if (resp[2] != 0x01) {
		dev_err(dev, "firmware update end NACK, aborting\n");
		ret = -EINVAL;
		goto out;
	}

	/* 6: switch back to app mode */
	ret = exc3000_vendor_data_request(data, (u8[]){0x3a, 0xff}, 2, NULL);
	if (ret < 0)
		goto out;

	/* wait for controller init after reset */
	msleep(500);

	/* 7: check communication */
	ret = exc3000_vendor_data_request(data, (u8[]){0x39, 0x01}, 2, resp);
	if (ret < 0)
		goto out;

	if (resp[1] != 0x02) {
		dev_err(dev, "device ping request NACK, aborting\n");
		ret = -EINVAL;
		goto out;
	}

	/* 8: check if we are in app mode again */
	ret = exc3000_vendor_data_request(data, (u8[]){0x39, 0x02}, 2, resp);
	if (ret < 0)
		goto out;

	if (resp[2] || resp[3]) {
		dev_err(dev, "device still bootloader mode, aborting\n");
		ret = -EINVAL;
		goto out;
	}

	dev_info(dev, "firmware update complete\n");

	exc3000_populate_device_info(data);

	data->update_status = 0;

	return 0;

out:
	data->update_status = ret;
	return ret;
}

static ssize_t exc3000_update_fw_store(struct device *dev,
				       struct device_attribute *dattr,
				       const char *buf, size_t count)
{
	struct exc3000_data *data = dev_get_drvdata(dev);
	char fw_name[NAME_MAX];
	const struct firmware *fw;
	size_t copy_count = count;
	int ret;

	if (count == 0 || count >= NAME_MAX)
		return -EINVAL;

	if (buf[count - 1] == '\0' || buf[count - 1] == '\n')
		copy_count -= 1;

	strncpy(fw_name, buf, copy_count);
	fw_name[copy_count] = '\0';

	ret = request_firmware(&fw, fw_name, dev);
	if (ret)
		return ret;

	dev_info(dev, "Flashing %s\n", fw_name);

	ret = exc3000_firmware_update(data, fw);

	release_firmware(fw);

	return ret ?: count;
}
static DEVICE_ATTR(update_fw, 0200, NULL, exc3000_update_fw_store);

static ssize_t exc3000_update_fw_status_show(struct device *dev,
					     struct device_attribute *dattr,
					     char *buf)
{
	struct exc3000_data *data = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", data->update_status);
}
static DEVICE_ATTR(update_fw_status, 0444, exc3000_update_fw_status_show, NULL);

static struct attribute *exc3000_attrs[] = {
	&dev_attr_type.attr,
	&dev_attr_model.attr,
	&dev_attr_fw_rev.attr,
	&dev_attr_update_fw.attr,
	&dev_attr_update_fw_status.attr,
	NULL
};

static const struct attribute_group exc3000_attr_group = {
	.attrs = exc3000_attrs,
};

static int exc3000_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct exc3000_data *data;
	struct input_dev *input;
	int error;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	timer_setup(&data->timer, exc3000_timer, 0);
	mutex_init(&data->vendor_data_lock);
	init_completion(&data->vendor_data_done);

	error = devm_request_threaded_irq(&client->dev, client->irq,
					  NULL, exc3000_interrupt, IRQF_ONESHOT,
					  client->name, data);
	if (error)
		return error;

	error = exc3000_populate_device_info(data);
	if (error)
		return error;

	dev_set_drvdata(&client->dev, data);
	error = sysfs_create_group(&client->dev.kobj, &exc3000_attr_group);
	if (error)
		return error;

	input = devm_input_allocate_device(&client->dev);
	if (!input)
		return -ENOMEM;

	data->input = input;

	input->name = "EETI EXC3000 Touch Screen";
	input->id.bustype = BUS_I2C;

	input_set_abs_params(input, ABS_MT_POSITION_X, 0, 4095, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, 4095, 0, 0);
	touchscreen_parse_properties(input, true, &data->prop);

	error = input_mt_init_slots(input, EXC3000_NUM_SLOTS,
				    INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (error)
		return error;

	error = input_register_device(input);
	if (error)
		return error;

	return 0;
}

int exc3000_remove(struct i2c_client *client)
{
	sysfs_remove_group(&client->dev.kobj, &exc3000_attr_group);

	return 0;
}

static const struct i2c_device_id exc3000_id[] = {
	{ "exc3000", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, exc3000_id);

#ifdef CONFIG_OF
static const struct of_device_id exc3000_of_match[] = {
	{ .compatible = "eeti,exc3000" },
	{ }
};
MODULE_DEVICE_TABLE(of, exc3000_of_match);
#endif

static struct i2c_driver exc3000_driver = {
	.driver = {
		.name	= "exc3000",
		.of_match_table = of_match_ptr(exc3000_of_match),
	},
	.id_table	= exc3000_id,
	.probe		= exc3000_probe,
	.remove		= exc3000_remove,
};

module_i2c_driver(exc3000_driver);

MODULE_AUTHOR("Ahmet Inan <inan@distec.de>");
MODULE_DESCRIPTION("I2C connected EETI EXC3000 multiple touch controller driver");
MODULE_LICENSE("GPL v2");
