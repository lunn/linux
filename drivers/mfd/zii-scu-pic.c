// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2012 Guenter Roeck
 * Copyright (C) 2018 Andrew Lunn
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/ihex.h>
#include <linux/init.h>
#include <linux/mfd/core.h>
#include <linux/mfd/zii-scu-pic.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

static const unsigned short normal_i2c[] = { 0x20, I2C_CLIENT_END };

#define NBYTES_BUILD_DATE (32)

struct scu_pic_data {
	struct i2c_client *client;
	struct mutex i2c_lock;

	u8 version_major;
	u8 version_minor;
	char build_date[NBYTES_BUILD_DATE];

	u8 version_major_bootloader;
	u8 version_minor_bootloader;
	bool in_bootloader;
	char build_date_bootloader[NBYTES_BUILD_DATE];
	int update_progress;
	int update_total;
};

/*
 * Note that these accessors implement an I2C protocol which is somewhat
 * non-standard and sub-optimal.
 *
 * The PIC device being accessed as an I2C slave has a firmware implementation
 * which expects to see the device address twice on the bus, hence each of the
 * transfer buffers below includes 'client->addr' as the very first byte.
 *
 * In addition, the new PIC bootloader firmware does not handle I2C restart
 * conditions (possibly due to the polled nature of the slave I2C
 * implementation in the bootloader?).	Thus, all accesses must be decomposed
 * into separate read/write operations bounded by a start and stop condition.
 *
 * This results in bus traffic which looks like:
 *
 *     addressed read:	S addr addr subaddr P S addr addr data P
 *     addressed write: S addr addr subaddr data P
 *
 * Given that there are already units fielded which make use of this existing
 * implementation (and said units are not easily field-upgradable), support for
 * this peculiarity needs to be maintained going forward.
 */

static int scu_pic_read_byte(struct i2c_client const * const client,
			     u8 const reg)
{
	struct device const * const dev = &client->dev;
	u8 buf[2] = { client->addr, reg };
	int err;

	err = i2c_master_send(client, buf, ARRAY_SIZE(buf));
	if (err < 0)
		return err;

	err = i2c_master_recv(client, buf, 1);
	if (err < 0)
		return err;

	dev_dbg(dev, "%s 0x%02x = 0x%02x\n", __func__, reg, buf[0]);
	return buf[0];
}

static int scu_pic_write_byte(struct i2c_client const * const client,
			      u8 const reg, u8 const value)
{
	struct device const * const dev = &client->dev;
	u8 buf[3] = { client->addr, reg, value };
	int err;

	err = i2c_master_send(client, buf, ARRAY_SIZE(buf));
	if (err < 0)
		return err;

	dev_dbg(dev, "%s 0x%02x = 0x%02x\n", __func__, reg, value);
	return 0;
}

int zii_scu_pic_read_byte(struct platform_device *pdev, u8 const reg)
{
	struct scu_pic_data *data = dev_get_drvdata(pdev->dev.parent);
	struct i2c_client *client = data->client;
	int ret;

	mutex_lock(&data->i2c_lock);
	ret = scu_pic_read_byte(client, reg);
	mutex_unlock(&data->i2c_lock);

	return ret;
}
EXPORT_SYMBOL(zii_scu_pic_read_byte);

int zii_scu_pic_write_byte(struct platform_device *pdev,
			   u8 const reg, u8 const value)
{
	struct scu_pic_data *data = dev_get_drvdata(pdev->dev.parent);
	struct i2c_client *client = data->client;
	int ret;

	mutex_lock(&data->i2c_lock);
	ret = scu_pic_write_byte(client, reg, value);
	mutex_unlock(&data->i2c_lock);

	return ret;
}
EXPORT_SYMBOL(zii_scu_pic_write_byte);

static int scu_pic_read_build_date(struct i2c_client const * const client,
				   u8 const reg, u8 * const buf,
				   size_t const nbytes)
{
	struct scu_pic_data * const data = i2c_get_clientdata(client);
	bool have_stx = false;
	int err;
	int i;

	if ((reg != I2C_GET_SCU_PIC_BUILD_DATE) &&
			(reg != I2C_GET_SCU_PIC_BOOTLOADER_BUILD_DATE))
		return -EINVAL;

	/*
	 * Data returned is in the form "\x0221-Oct-14 09:16:05\x03".
	 *
	 * The below parsing logic assumes that the provided buffer is at least
	 * large enough that we'll see an STX before nbytes have been read, at
	 * which point we reset the loop count, and read data until we receive
	 * ETX or the buffer is full.
	 *
	 * This is preferable to looping indefinitely in cases where the I2C
	 * implementation in the PIC firmware returns the same garbage byte ad
	 * infinitum.
	 */
	mutex_lock(&data->i2c_lock);
	i = 0;
	while (i < nbytes) {
		err = scu_pic_read_byte(client, reg);
		if (err < 0) {
			goto exit_unlock;
		} else if (err == 0x02) {
			if (have_stx) {
				err = -EINVAL;
				goto exit_unlock;
			}

			have_stx = true;
			i = 0;
			continue;
		} else if (err == 0x03) {
			if (have_stx) {
				buf[i] = '\0';
				err = 0;
				break;
			}
		} else if (have_stx) {
			buf[i] = err;
		}

		i++;
	}

	buf[nbytes - 1] = '\0';

exit_unlock:
	mutex_unlock(&data->i2c_lock);
	return err;
}

static const struct mfd_cell zii_scu_pic_devs[] = {
	{
		.name = "zii-scu-pic-wdt",
	},
	{
		.name = "zii-scu-pic-leds",
	},
	{
		.name = "zii-scu-pic-hwmon",
	},
};

/* firmware update */

#define SCU_PIC_FIRMWARE_NAME	      "scu_pic.fw"
#define SCU_PIC_APPLICATION_BASE      (0x1000)	/* In units of 16-bit words. */
#define SCU_PIC_APPLICATION_NWORDS    (0x1000)	/* In units of 16-bit words. */
#define SCU_PIC_APPLICATION_END \
	(SCU_PIC_APPLICATION_BASE + SCU_PIC_APPLICATION_NWORDS)

#define BOOTLOAD_REG_ADDRESS_POINTER  (0x01)
#define BOOTLOAD_REG_DATA_DOWNLOAD    (0x02)
#define BOOTLOAD_REG_FLASH_READ	      (0x03)
#define BOOTLOAD_REG_FLASH_ERASE      (0x04)
#define BOOTLOAD_REG_FLASH_WRITE      (0x05)
#define BOOTLOAD_REG_APPLICATION_JUMP (0x06)
#define BOOTLOAD_REG_EMBED_CRC	      (0x07)

static int bootload_set_address_pointer(struct i2c_client const * const client,
					u16 const address)
{
	struct device const * const dev = &client->dev;
	u8 buf[4] = { client->addr, BOOTLOAD_REG_ADDRESS_POINTER,
		      (address >> 8), address };
	int err;

	err = i2c_master_send(client, buf, ARRAY_SIZE(buf));
	if (err < 0)
		return err;

	dev_dbg(dev, "%s: 0x%04x\n", __func__, address);
	return 0;
}

static int bootload_data_download(struct i2c_client const * const client,
				  u8 const * const data, size_t const nbytes)
{
	struct device const * const dev = &client->dev;
	u8 buf[18] = {
		client->addr, BOOTLOAD_REG_DATA_DOWNLOAD,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	};
	int err;

	if (!data || (nbytes > 16))
		return -EINVAL;

	(void)memcpy(&buf[2], data, nbytes);

	err = i2c_master_send(client, buf, ARRAY_SIZE(buf));
	if (err < 0)
		return err;

	dev_dbg(dev, "%s: transferred %zd\n", __func__, nbytes);
	return 0;
}

static int bootload_flash_read(struct i2c_client const * const client,
			       u8 * const data, size_t const nbytes)
{
	struct device const * const dev = &client->dev;
	u8 buf[17] = { client->addr, BOOTLOAD_REG_FLASH_READ };
	int err;

	if ((!data) || (nbytes > 16))
		return -EINVAL;

	err = i2c_master_send(client, buf, 2);
	if (err < 0)
		return err;

	err = i2c_master_recv(client, buf, 16);
	if (err < 0)
		return err;

	(void)memcpy(data, buf, nbytes);

	udelay(100);
	dev_dbg(dev, "%s: read %zd\n", __func__, nbytes);
	return 0;
}

static int bootload_flash_erase(struct i2c_client const * const client)
{
	struct device const * const dev = &client->dev;
	u8 buf[18] = {
		client->addr, BOOTLOAD_REG_DATA_DOWNLOAD,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	};
	int err;

	/*
	 * Erase is implemented as a "write" of blank data values.  The PIC
	 * actually erases on-the-fly as new values are written to flash, but
	 * we blank everything out just to be safe (rather than possibly leave
	 * random opcodes in flash from previous, larger, firmware images).
	 */

	err = i2c_master_send(client, buf, ARRAY_SIZE(buf));
	if (err < 0)
		return err;

	buf[1] = BOOTLOAD_REG_FLASH_WRITE;
	err = i2c_master_send(client, buf, 2);
	if (err < 0)
		return err;

	err = i2c_master_recv(client, buf, 1);
	if (err < 0)
		return err;

	udelay(100);
	dev_dbg(dev, "%s: 0x%02x\n", __func__, buf[0]);
	return buf[0];
}

static int bootload_flash_write(struct i2c_client const * const client)
{
	struct device const * const dev = &client->dev;
	u8 buf[2] = { client->addr, BOOTLOAD_REG_FLASH_WRITE };
	int err;

	err = i2c_master_send(client, buf, ARRAY_SIZE(buf));
	if (err < 0)
		return err;

	err = i2c_master_recv(client, buf, 1);
	if (err < 0)
		return err;

	udelay(100);
	dev_dbg(dev, "%s: 0x%02x\n", __func__, buf[0]);
	return buf[0];
}

static int bootload_embed_crc(struct i2c_client const * const client)
{
	struct device const * const dev = &client->dev;
	u8 buf[2] = { client->addr, BOOTLOAD_REG_EMBED_CRC };
	int err;

	dev_info(dev, "%s: Embedding CRC into PIC EEPROM...\n", __func__);

	err = i2c_master_send(client, buf, ARRAY_SIZE(buf));
	if (err < 0)
		return err;

	err = i2c_master_recv(client, buf, 1);
	if (err < 0)
		return err;

	dev_dbg(dev, "%s: 0x%02x\n", __func__, buf[0]);
	return buf[0];
}

static int load_firmware(struct device * const dev, u8 * const buf,
			 size_t const nbytes_buf)
{
	struct firmware const *fw;
	struct ihex_binrec const *rec;
	size_t nrecords = 0;
	size_t nbytes = 0;
	int err;
	int i;

	/*
	 * The SCU PIC includes 14-bit flash program memory; initialize the
	 * entire buffer with the "erased" value (0x3FFF) prior to loading
	 * firmware chunks into the buffer.
	 */
	for (i = 0; i < nbytes_buf;) {
		buf[i++] = 0xFF;
		buf[i++] = 0x3F;
	}

	/* Obtain a handle to the firmware file. */
	err = request_ihex_firmware(&fw, SCU_PIC_FIRMWARE_NAME, dev);
	if (err) {
		dev_err(dev, "Firmware request for '%s' failed (%d).\n",
				SCU_PIC_FIRMWARE_NAME, err);
		return err;
	}

	rec = (struct ihex_binrec const *)fw->data;
	while (rec) {
		u32 const addr_base = (SCU_PIC_APPLICATION_BASE << 1);
		u32 const addr_end = (SCU_PIC_APPLICATION_END << 1) - 1;
		u32 addr = be32_to_cpu(rec->addr);
		u16 len = be16_to_cpu(rec->len);

		if (((addr + len) < addr_base) || (addr > addr_end)) {
			/* Skip. */
			dev_dbg(dev, "%s: Skipped load of %d bytes @ %04x.\n",
				__func__, len, addr);
		} else {
			u8 const *src = rec->data;
			u8 *dst;

			if (addr < addr_base) {
				int delta = (addr_base - addr);

				addr += delta;
				src += delta;
				len -= delta;
			}

			if ((addr + len) > addr_end)
				len = ((addr_end - addr) + 1);

			dst = buf + (addr - addr_base);

			(void)memcpy(dst, src, len);
			nbytes += len;

			dev_dbg(dev, "%s: Loaded %d bytes @ %04x (offset %04x).\n",
				__func__, len, addr, (addr - addr_base));
		}

		nrecords++;
		rec = ihex_next_binrec(rec);
	}

	dev_info(dev, "%s: Loaded firmware from '%s' (%zu/%zu bytes in %zu records).\n",
		 __func__, SCU_PIC_FIRMWARE_NAME, nbytes, fw->size, nrecords);

	release_firmware(fw);

	return nbytes;
}

/*
 * The bootloader returns "magic" major and minor versions of 'B' and 'L',
 * respectively, when queried.	This allows us to determine when the jump to
 * bootloader mode is complete and the PIC I2C slave interface is again active.
 */

#define BOOTLOADER_MAGIC_MAJOR ('B')
#define BOOTLOADER_MAGIC_MINOR ('L')

static bool exec_bootloader_try(struct device const * const dev)
{
	struct i2c_client const * const client = to_i2c_client(dev);
	struct scu_pic_data * const data = i2c_get_clientdata(client);
	int major, minor;

	scu_pic_write_byte(client, I2C_SET_SCU_PIC_RESET_TO_BOOTLOADER,
				   1);
	usleep_range(50000, 100000);
	major = scu_pic_read_byte(client, I2C_GET_SCU_PIC_FIRMWARE_REV_MAJOR);
	minor = scu_pic_read_byte(client, I2C_GET_SCU_PIC_FIRMWARE_REV_MINOR);
	if (major == BOOTLOADER_MAGIC_MAJOR &&
	    minor == BOOTLOADER_MAGIC_MINOR) {
		dev_info(dev, "%s: Bootloader started successfully.\n",
			 __func__);
		data->in_bootloader = true;
		return true;
	}
	return false;
}

static int exec_bootloader(struct device const * const dev)
{
	int retry = 5;

	dev_info(dev, "%s: Switching to PIC bootloader...\n", __func__);

	do {
		if (exec_bootloader_try(dev))
			break;
	} while (--retry);

	return retry ? 0 : -EIO;
}

static int erase_flash(struct device const * const dev, u16 const address,
		       size_t const nwords)
{
	struct i2c_client const * const client = to_i2c_client(dev);
	struct scu_pic_data *data = i2c_get_clientdata(client);
	size_t nwords_erased;
	int err;

	/* Flash operations are all 8-word multiples and 8-word aligned. */
	WARN_ON(address & 0x7);
	WARN_ON(nwords & 0x7);
	if (address & 0x7 || nwords & 0x7)
		return -EINVAL;

	dev_info(dev, "%s: Erasing firmware flash segment...\n", __func__);

	/* Load 'base address' into the address pointer. */
	err = bootload_set_address_pointer(client, address);
	if (err) {
		dev_err(dev, "Set Address Pointer operation failed.\n");
		goto out;
	}

	/* Erase. */
	for (nwords_erased = 0; nwords_erased < nwords; nwords_erased += 8) {
		err = bootload_flash_erase(client);
		if (err) {
			dev_err(dev, "Erase Flash operation failed.\n");
			goto out;
		}

		data->update_progress += 8;
	}

out:
	return err;
}

static int blank_check_flash(struct device const * const dev, u16 const address,
		       size_t const nwords)
{
	struct i2c_client const * const client = to_i2c_client(dev);
	struct scu_pic_data *data = i2c_get_clientdata(client);
	size_t nwords_checked;
	int err;

	/* Flash operations are all 8-word multiples and 8-word aligned. */
	WARN_ON(address & 0x7);
	WARN_ON(nwords & 0x7);
	if (address & 0x7 || nwords & 0x7)
		return -EINVAL;

	dev_info(dev, "%s: Blank checking firmware flash segment...\n",
		 __func__);

	/* Load 'base address' into the address pointer. */
	err = bootload_set_address_pointer(client, address);
	if (err) {
		dev_err(dev, "Set Address Pointer operation failed.\n");
		goto out;
	}

	/* Blank check. */
	for (nwords_checked = 0; nwords_checked < nwords; nwords_checked += 8) {
		u8 buf[16] = { 0 };
		size_t i;

		err = bootload_flash_read(client, buf, ARRAY_SIZE(buf));
		if (err) {
			dev_err(dev, "Read Flash operation failed.\n");
			goto out;
		}

		for (i = 0; i < ARRAY_SIZE(buf); i += 2) {
			u16 val = ((u16)buf[i + 1] << 8) | (u16)buf[i];

			if (val != 0x3FFF) {
				dev_err(dev, "Flash blank check failed at offset 0x%04lx (read 0x%04x).\n",
					(address + nwords_checked + i), val);
				err = -EIO;
				goto out;
			}
		}

		data->update_progress += 8;
	}

out:
	return err;
}

static int write_flash(struct device const * const dev, u16 const address,
		       size_t const nwords, u8 const * const buf,
		       size_t const nbytes_buf)
{
	struct i2c_client const * const client = to_i2c_client(dev);
	struct scu_pic_data *data = i2c_get_clientdata(client);
	size_t nwords_written;
	int err;

	/* Flash operations are all 8-word multiples and 8-word aligned. */
	WARN_ON(address & 0x7);
	WARN_ON(nwords & 0x7);
	if (address & 0x7 || nwords & 0x7)
		return -EINVAL;

	dev_info(dev, "%s: Writing firmware data...\n", __func__);

	/* Buffer size we're about to write needs to match the segment size. */
	if (nbytes_buf != (nwords << 1)) {
		dev_err(dev, "%s: failed - invalid buffer size (0x%04zx).\n",
			__func__, nbytes_buf);
		err = -EINVAL;
		goto out;
	}

	/* Load 'base address' into the address pointer. */
	err = bootload_set_address_pointer(client, address);
	if (err) {
		dev_err(dev, "Set Address Pointer operation failed.\n");
		goto out;
	}

	/* Write. */
	for (nwords_written = 0; nwords_written < nwords; nwords_written += 8) {
		err = bootload_data_download(client,
					     &buf[nwords_written << 1], 16);
		if (err) {
			dev_err(dev, "Data Download operation failed.\n");
			goto out;
		}

		err = bootload_flash_write(client);
		if (err) {
			dev_err(dev, "Flash Write operation failed.\n");
			goto out;
		}

		data->update_progress += 8;
	}

out:
	return err;
}

static int verify_flash(struct device const * const dev, u16 const address,
			size_t const nwords, u8 const * const buf,
			size_t const nbytes_buf)
{
	struct i2c_client const * const client = to_i2c_client(dev);
	struct scu_pic_data *data = i2c_get_clientdata(client);
	size_t nwords_checked;
	int err;

	/* Flash operations are all 8-word multiples and 8-word aligned. */
	WARN_ON(address & 0x7);
	WARN_ON(nwords & 0x7);
	if (address & 0x7 || nwords & 0x7)
		return -EINVAL;

	dev_info(dev, "%s: Verifying firmware data...\n", __func__);

	/* Buffer size we're about to write needs to match the segment size. */
	if (nbytes_buf != (nwords << 1)) {
		dev_err(dev, "%s: failed - invalid buffer size (0x%04lx).\n",
			__func__, nbytes_buf);
		err = -EINVAL;
		goto out;
	}

	/* Load 'base address' into the address pointer. */
	err = bootload_set_address_pointer(client, address);
	if (err) {
		dev_err(dev, "Set Address Pointer operation failed.\n");
		goto out;
	}

	/* Verify. */
	for (nwords_checked = 0; nwords_checked < nwords; nwords_checked += 8) {
		u8 buf_r[16] = { 0 };
		size_t i;

		err = bootload_flash_read(client, buf_r, ARRAY_SIZE(buf_r));
		if (err) {
			dev_err(dev, "Flash Read operation failed.\n");
			goto out;
		}

		for (i = 0; i < ARRAY_SIZE(buf_r); i += 2) {
			u16 val_w, val_r;

			val_w = ((u16)buf[(nwords_checked << 1) + i + 1] << 8) |
				(u16)buf[(nwords_checked << 1) + i];
			val_r = ((u16)buf_r[i + 1] << 8) | (u16)buf_r[i];

			if (val_r != val_w) {
				dev_err(dev, "Flash verify failed at offset 0x%04lx (expected 0x%04x, read 0x%04x).\n",
					(address + nwords_checked + i),
					val_w, val_r);
				err = -EIO;
				goto out;
			}
		}

		data->update_progress += 8;
	}

out:
	return err;
}

static ssize_t update_firmware(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct scu_pic_data *data = i2c_get_clientdata(client);
	u8 *fw_buf;
	size_t nbytes_fw_buf = (SCU_PIC_APPLICATION_NWORDS << 1);
	int err;

	fw_buf = kmalloc(nbytes_fw_buf, GFP_KERNEL);
	if (!fw_buf)
		return -ENOMEM;

	dev_dbg(dev, "%s: Firmware update started...\n", __func__);
	dev_info(dev, "Firmware flash segment is %u words at offset 0x%04x.\n",
		 SCU_PIC_APPLICATION_NWORDS, SCU_PIC_APPLICATION_BASE);

	err = load_firmware(dev, fw_buf, nbytes_fw_buf);
	if (err < 0)
		goto exit_free;

	mutex_lock(&data->i2c_lock);
	data->update_progress = 0;

	err = exec_bootloader(dev);
	if (err < 0) {
		data->update_progress = err;
		goto exit_unlock;
	}

	err = erase_flash(dev, SCU_PIC_APPLICATION_BASE,
			  SCU_PIC_APPLICATION_NWORDS);
	if (err < 0) {
		data->update_progress = err;
		goto exit_unlock;
	}

	err = blank_check_flash(dev, SCU_PIC_APPLICATION_BASE,
				SCU_PIC_APPLICATION_NWORDS);
	if (err < 0) {
		data->update_progress = err;
		goto exit_unlock;
	}


	err = write_flash(dev, SCU_PIC_APPLICATION_BASE,
			  SCU_PIC_APPLICATION_NWORDS, fw_buf, nbytes_fw_buf);
	if (err < 0) {
		data->update_progress = err;
		goto exit_unlock;
	}

	err = verify_flash(dev, SCU_PIC_APPLICATION_BASE,
			  SCU_PIC_APPLICATION_NWORDS, fw_buf, nbytes_fw_buf);
	if (err < 0) {
		data->update_progress = err;
		goto exit_unlock;
	}

	err = bootload_embed_crc(client);
	if (err < 0) {
		data->update_progress = err;
		goto exit_unlock;
	}

	data->update_progress = 0;

exit_unlock:
	mutex_unlock(&data->i2c_lock);
exit_free:
	kfree(fw_buf);
	return err ? err : 0;
}

static ssize_t update_firmware_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	unsigned long val;
	int err;

	err = kstrtoul(buf, 0, &val);
	if (err)
		return err;
	if (val != 1)
		return -EINVAL;

	err = update_firmware(dev);
	return err ? err : count;
}

static DEVICE_ATTR_WO(update_firmware);

static ssize_t update_firmware_status_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct scu_pic_data *data = i2c_get_clientdata(to_i2c_client(dev));
	int percent_complete;

	if (data->update_progress < 0)
		return sprintf(buf, "%d\n", data->update_progress);

	if (data->update_total != 0)
		percent_complete = (data->update_progress * 100) /
			data->update_total;
	else
		percent_complete = 0;

	return sprintf(buf, "%d\n", percent_complete);
}

static DEVICE_ATTR_RO(update_firmware_status);

static ssize_t build_date_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct scu_pic_data *data = i2c_get_clientdata(to_i2c_client(dev));

	return sprintf(buf, "%s\n",
		       data->build_date[0] ? data->build_date : "Unknown");
}

static DEVICE_ATTR_RO(build_date);

static ssize_t build_date_bootloader_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct scu_pic_data *data = i2c_get_clientdata(to_i2c_client(dev));

	return sprintf(buf, "%s\n",
		       data->build_date_bootloader[0] ?
		       data->build_date_bootloader : "Unknown");
}

static DEVICE_ATTR_RO(build_date_bootloader);

static ssize_t version_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct scu_pic_data *data = i2c_get_clientdata(to_i2c_client(dev));

	return sprintf(buf, "%u.%02u\n", data->version_major,
		       data->version_minor);
}

static DEVICE_ATTR_RO(version);

static ssize_t version_bootloader_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct scu_pic_data *data = i2c_get_clientdata(to_i2c_client(dev));

	return sprintf(buf, "%u.%02u\n", data->version_major_bootloader,
		       data->version_minor_bootloader);
}

static DEVICE_ATTR_RO(version_bootloader);

static struct attribute *zii_scu_pic_attributes_v4[] = {
	&dev_attr_version.attr,
};

static const struct attribute_group zii_scu_pic_group_v4 = {
	.attrs = zii_scu_pic_attributes_v4,
};

static int probe_v4(struct i2c_client * const client)
{
	struct scu_pic_data *data = i2c_get_clientdata(client);
	struct device *dev = &client->dev;
	int err;

	err = sysfs_create_group(&dev->kobj, &zii_scu_pic_group_v4);
	if (err)
		return err;

	dev_info(dev, "Firmware revision %d.%02d.\n",
		 data->version_major, data->version_minor);

	return 0;
}

static void remove_v4(struct i2c_client * const client)
{
	struct device *dev = &client->dev;

	sysfs_remove_group(&dev->kobj, &zii_scu_pic_group_v4);
}

static struct attribute *zii_scu_pic_attributes_v6[] = {
	&dev_attr_version.attr,
	&dev_attr_version_bootloader.attr,
	&dev_attr_build_date.attr,
	&dev_attr_build_date_bootloader.attr,
	&dev_attr_update_firmware.attr,
	&dev_attr_update_firmware_status.attr,
	NULL
};

static const struct attribute_group zii_scu_pic_group_v6 = {
	.attrs = zii_scu_pic_attributes_v6,
};

static int probe_v6(struct i2c_client * const client)
{
	struct scu_pic_data *data = i2c_get_clientdata(client);
	struct device *dev = &client->dev;
	int major, minor;
	int err;

	major = scu_pic_read_byte(client,
				  I2C_GET_SCU_PIC_BOOTLOADER_VERSION_MAJOR);
	minor = scu_pic_read_byte(client,
				  I2C_GET_SCU_PIC_BOOTLOADER_VERSION_MINOR);

	if ((major < 0) || (minor < 0)) {
		dev_err(dev, "Bootloader major %d minor %d\n", major, minor);
		return -ENODEV;
	}

	data->version_major_bootloader = major;
	data->version_minor_bootloader = minor;

	err = scu_pic_read_build_date(client,
				      I2C_GET_SCU_PIC_BUILD_DATE,
				      data->build_date,
				      NBYTES_BUILD_DATE);
	if (err < 0) {
		dev_err(dev, "Failed to read PIC application build date(%d).\n",
			err);
		memset(data->build_date, '\0', NBYTES_BUILD_DATE);
	}

	err = scu_pic_read_build_date(client,
				     I2C_GET_SCU_PIC_BOOTLOADER_BUILD_DATE,
				     data->build_date_bootloader,
				     NBYTES_BUILD_DATE);
	if (err < 0) {
		dev_err(dev, "Failed to read PIC bootloader build date (%d).\n",
			err);
		memset(data->build_date_bootloader, '\0', NBYTES_BUILD_DATE);
	}

	err = sysfs_create_group(&dev->kobj, &zii_scu_pic_group_v6);
	if (err)
		return err;

	dev_info(dev, "Firmware revision %d.%02d, built %s.\n",
		 data->version_major, data->version_minor,
		 data->build_date[0] ?	data->build_date : "Unknown");

	dev_info(dev, "Bootloader revision %d.%02d, built %s.\n",
		 data->version_major_bootloader, data->version_minor_bootloader,
		 data->build_date_bootloader[0] ?
		 data->build_date_bootloader : "Unknown");

	return 0;
}

static void remove_v6(struct i2c_client * const client)
{
	struct device *dev = &client->dev;

	sysfs_remove_group(&dev->kobj, &zii_scu_pic_group_v6);
}

static struct attribute *zii_scu_pic_attributes_bootloader[] = {
	&dev_attr_version_bootloader.attr,
	&dev_attr_build_date_bootloader.attr,
	NULL
};

static const struct attribute_group zii_scu_pic_group_bootloader = {
	.attrs = zii_scu_pic_attributes_bootloader,
};

static int probe_bootloader(struct i2c_client * const client)
{
	struct scu_pic_data *data = i2c_get_clientdata(client);
	struct device *dev = &client->dev;
	int major, minor;
	int err;

	major = scu_pic_read_byte(client,
				  I2C_GET_SCU_PIC_BOOTLOADER_VERSION_MAJOR);
	minor = scu_pic_read_byte(client,
				  I2C_GET_SCU_PIC_BOOTLOADER_VERSION_MINOR);

	if ((major < 0) || (minor < 0)) {
		dev_err(dev, "Bootloader major %d minor %d\n", major, minor);
		return -ENODEV;
	}

	data->version_major_bootloader = major;
	data->version_minor_bootloader = minor;

	err = scu_pic_read_build_date(client,
				     I2C_GET_SCU_PIC_BOOTLOADER_BUILD_DATE,
				     data->build_date_bootloader,
				     NBYTES_BUILD_DATE);
	if (err < 0) {
		dev_err(dev, "Failed to read PIC bootloader build date (%d).\n",
			err);
		memset(data->build_date_bootloader, '\0', NBYTES_BUILD_DATE);
	}

	err = sysfs_create_group(&dev->kobj, &zii_scu_pic_group_v6);
	if (err)
		return err;

	dev_info(dev, "Firmware not present or corrupt.\n");
	dev_info(dev, "Bootloader revision %d.%02d, built %s.\n",
		 data->version_major_bootloader, data->version_minor_bootloader,
		 data->build_date_bootloader[0] ?
		 data->build_date_bootloader : "Unknown");

	return 0;
}

static void remove_bootloader(struct i2c_client * const client)
{
	struct device *dev = &client->dev;

	sysfs_remove_group(&dev->kobj, &zii_scu_pic_group_bootloader);
}


static int scu_pic_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct scu_pic_data *data;
	int major, minor;
	int err;

	major = scu_pic_read_byte(client, I2C_GET_SCU_PIC_FIRMWARE_REV_MAJOR);
	minor = scu_pic_read_byte(client, I2C_GET_SCU_PIC_FIRMWARE_REV_MINOR);

	if (major < 0 || minor < 0) {
		dev_err(dev, "major %d minor %d\n", major, minor);
		return -ENODEV;
	}

	data = devm_kzalloc(dev, sizeof(struct scu_pic_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	client->flags &= ~I2C_CLIENT_PEC;	/* PEC is not supported */

	i2c_set_clientdata(client, data);
	data->client = client;

	mutex_init(&data->i2c_lock);

	data->version_major = major;
	data->version_minor = minor;

	switch (data->version_major) {
	case BOOTLOADER_MAGIC_MAJOR:
		data->version_major = 0;
		data->version_minor = 0;
		data->in_bootloader = true;
		err = probe_bootloader(client);
		break;
	case 4:
	case 5:
		err = probe_v4(client);
		break;
	case 6:
		err = probe_v6(client);
		break;
	default:
		err = -ENODEV;
	}

	if (err) {
		dev_err(dev, "Hardware specific probe failed.\n");
		return err;
	}

	if (!data->in_bootloader)
		return devm_mfd_add_devices(dev, -1, zii_scu_pic_devs,
					    ARRAY_SIZE(zii_scu_pic_devs),
					    NULL, 0, NULL);
	return 0;
}

static int scu_pic_remove(struct i2c_client *client)
{
	struct scu_pic_data * const data = i2c_get_clientdata(client);

	switch (data->version_major) {
	case BOOTLOADER_MAGIC_MAJOR:
		remove_bootloader(client);
		break;
	case 4:
	case 5:
		remove_v4(client);
		break;
	case 6:
		remove_v6(client);
		break;
	}

	return 0;
}

static const struct i2c_device_id scu_pic_id[] = {
	{ "zii_scu_pic", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, scu_pic_id);

static struct i2c_driver scu_pic_driver = {
	.driver = {
		.name = "zii_scu_pic",
	},
	.probe		= scu_pic_probe,
	.remove		= scu_pic_remove,
	.id_table	= scu_pic_id,
	.address_list	= normal_i2c,
};

module_i2c_driver(scu_pic_driver);

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("SCU PIC driver");
MODULE_LICENSE("GPL");
