/* SPDX-License-Identifier: GPL-2.0+ */

/*
 * Core definitions for RAVE SP MFD driver.
 *
 * Copyright (C) 2017 Zodiac Inflight Innovations
 */

#ifndef _LINUX_RAVE_SP_H_
#define _LINUX_RAVE_SP_H_

#include <linux/notifier.h>

enum rave_sp_command {
	RAVE_SP_CMD_GET_FIRMWARE_VERSION	= 0x20,
	RAVE_SP_CMD_GET_BOOTLOADER_VERSION	= 0x21,
	RAVE_SP_CMD_GET_VOLTAGE			= 0x22,
	RAVE_SP_CMD_GET_CURRENT			= 0x23,
	RAVE_SP_CMD_GET_TEMPERATURE		= 0x24,
	RAVE_SP_CMD_BOOT_SOURCE			= 0x26,
	RAVE_SP_CMD_LED_CONTROL			= 0x28,
	RAVE_SP_CMD_GET_ETC			= 0x2A,
	RAVE_SP_CMD_GET_BOARD_COPPER_REV	= 0x2B,
	RAVE_SP_CMD_GET_GPIO_STATE		= 0x2F,

	RAVE_SP_CMD_STATUS			= 0xA0,
	RAVE_SP_CMD_SW_WDT			= 0xA1,
	RAVE_SP_CMD_PET_WDT			= 0xA2,
	RAVE_SP_CMD_RMB_EEPROM			= 0xA4,
	RAVE_SP_CMD_SET_BACKLIGHT		= 0xA6,
	RAVE_SP_CMD_RESET			= 0xA7,
	RAVE_SP_CMD_RESET_REASON		= 0xA8,
	RAVE_SP_CMD_PWR_LED			= 0xA9,

	RAVE_SP_CMD_JUMP_TO_BOOTLOADER		= 0xB0,
	RAVE_SP_CMD_BOOTLOADER			= 0xB1,
	RAVE_SP_CMD_REQ_COPPER_REV		= 0xB6,
	RAVE_SP_CMD_GET_I2C_DEVICE_STATUS	= 0xBA,
	RAVE_SP_CMD_GET_SP_SILICON_REV		= 0xB9,
	RAVE_SP_CMD_CONTROL_EVENTS		= 0xBB,

	RAVE_SP_EVNT_BASE			= 0xE0,
};

struct rave_sp;

static inline unsigned long rave_sp_action_pack(u8 event, u8 value)
{
	return ((unsigned long)value << 8) | event;
}

static inline u8 rave_sp_action_unpack_event(unsigned long action)
{
	return action;
}

static inline u8 rave_sp_action_unpack_value(unsigned long action)
{
	return action >> 8;
}

int rave_sp_exec(struct rave_sp *sp,
		 void *__data,  size_t data_size,
		 void *reply_data, size_t reply_data_size);

struct device;
int devm_rave_sp_register_event_notifier(struct device *dev,
					 struct notifier_block *nb);

bool rave_sp_is_in_bootloader_mode(struct rave_sp *sp);

struct rave_sp_version {
	u8     hardware;
	__le16 major;
	u8     minor;
	u8     letter[2];
} __packed;

struct rave_sp_status {
	struct rave_sp_version bootloader_version;
	struct rave_sp_version firmware_version;
	u16 rdu_eeprom_flag;
	u16 dds_eeprom_flag;
	u8  pic_flag;
	u8  orientation;
	u32 etc;
	s16 temp[2];
	u8  backlight_info;
	__le16 backlight_current;
	u8  dip_switch;
	u8  host_interrupt;
	u16 voltage_28;
	u8  i2c_device_status;
	u8  power_status;
	u8  general_status;
	u8  deprecated1;
	u8  power_led_status;
	u8  deprecated2;
	u8  periph_power_shutoff;
} __packed;

int rave_sp_get_status(struct rave_sp *sp, struct rave_sp_status *status);

#endif /* _LINUX_RAVE_SP_H_ */
