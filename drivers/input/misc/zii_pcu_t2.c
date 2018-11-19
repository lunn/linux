/*
 * Driver for Zii Passenger Control Unit Devices
 *
 * Copyright (C) 2017 Zii
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 */


/* Debug info enable: uncomment for verbose debug output in kernel */
/* #define DEBUG */


#include <linux/completion.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/ihex.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/usb/input.h>
#include <linux/usb/cdc.h>
#include <asm/unaligned.h>



/******************************************************
 * Static assert definition that is portable accross different
 * compilers/architectures. If all is ok, it creates a type that
 * declares one element (byte) array. In case of errors,
 * the size of the array is negative, which then triggers compile-time
 * error, and the error message is the name of array in the typedef.
 */
#ifndef STATIC_ASSERT
#define STATIC_ASSERT(cond,errmsg)  \
    typedef u8 STATIC_ASSERT_##errmsg[ (!!(cond))*2-1 ];
#endif



/******************************************************
 * Firmware update definitions
 */
#define ZII_pcu_T2_FIRMWARE_NAME_LEN    (30)
#define ZII_PCU_T2_FIRMWARE_MAIN        "zii_pcu_t2_main.fw"
#define ZII_PCU_T2_FIRMWARE_TOUCHPAD    "zii_pcu_t2_touchpad.fw"
#define ZII_PCU_T2_FIRMWARE_COUNT       (2)

#define ZII_PCU_T2_FIRMWARE_MAIN_START_ADDR     (0x00410000)
#define ZII_PCU_T2_FIRMWARE_TOUCHPAD_START_ADDR (0x00006000)

#define ZII_PCU_T2_FIRMWARE_MAIN_ERASE_TIMEOUT      (5000)
#define ZII_PCU_T2_FIRMWARE_TOUCHPAD_ERASE_TIMEOUT  (11000)

#define ZII_PCU_T2_FIRMWARE_SEGMENT_SIZE    (48)

#define ZII_PCU_T2_DISPLAY_DATA_SIZE        (512)
#define ZII_PCU_T2_DISPLAY_SEGMENT_SIZE     (32)
#define ZII_PCU_T2_DISPLAY_NUM_SEGMENTS     \
            (ZII_PCU_T2_DISPLAY_DATA_SIZE/ZII_PCU_T2_DISPLAY_SEGMENT_SIZE)


#define ZII_PCU_T2_APPLICATION_MODE	    (0)
#define ZII_PCU_T2_BOOTLOADER_MODE		(1)

/* ID of the processor participating in the update */
typedef enum {
    FW_UPDATE_PROC_ID_MAIN,
    FW_UPDATE_PROC_ID_TOUCHPAD,
    FW_UPDATE_PROC_ID_INVALID
} FW_UPDATE_PROC_ID;


/* All about fw update for particular processor */
struct ims_pcu;
typedef struct {
    u8                  processor_id;
    u32                 fw_start_addr;
    u32                 erase_timeout;
    char                fw_name[ZII_pcu_T2_FIRMWARE_NAME_LEN];
    int                 percent_complete;
	struct completion   fw_update_done;
} ZII_PCU_FW_INFO;



/********************************************************
 * PCU button definitions
 */

#define IMS_PCU_KEYMAP_LEN		32

struct ims_pcu_buttons {
	struct input_dev *input;
	char name[32];
	char phys[32];
	unsigned short keymap[IMS_PCU_KEYMAP_LEN];
};

struct ims_pcu_gamepad {
	struct input_dev *input;
	char name[32];
	char phys[32];
};


/* PCU T2 backlight is divided into two groupds: keyboard (kdb), which is RGB
 * based, and game, which is unicolor (white). For each RGB channel, there is
 * a separate backlight LED device whose brightness can be controlled.
 * For compatiblity reasons, there is one keypad device (kbd_backlight) that
 * can be used to drive all 3 RGB channels using the same value for brightness
 * resulting in white LED output.
 */

typedef enum {
    ZII_PCU_T2_BACKLIGHT_RED,
    ZII_PCU_T2_BACKLIGHT_GREEN,
    ZII_PCU_T2_BACKLIGHT_BLUE,
    ZII_PCU_T2_BACKLIGHT_WHITE_TOUCHPAD,
    ZII_PCU_T2_BACKLIGHT_WHITE_KEYPAD,
    ZII_PCU_T2_BACKLIGHT_NUMDEV
} ZII_PCU_T2_BACKLIGHT_ENUM;

/* Forward declaration of type */
struct zii_pcu_t2_backlight;
typedef struct zii_pcu_t2_backlight ZII_PCU_T2_BACKLIGHT;

/* PCU backlight LED device */
struct zii_pcu_t2_backlight_dev {
    struct led_classdev     cdev;
    char                    name[32];
    bool                    registered;
    struct zii_pcu_t2_backlight *backlight;
    ZII_PCU_T2_BACKLIGHT_ENUM channel;
};

/* Set of backlight LED devices for red, green, blue, and white planes */
struct zii_pcu_t2_backlight {
    struct mutex                    led_mutex;
    struct zii_pcu_t2_backlight_dev bl_dev[ZII_PCU_T2_BACKLIGHT_NUMDEV];
};



#define ZII_PCU_T2_PART_NUMBER_LEN		15
#define ZII_PCU_T2_SERIAL_NUMBER_LEN	8
#define ZII_PCU_T2_DOM_LEN			    8
#define ZII_PCU_T2_LRU_REV_LEN          2
#define ZII_PCU_T2_LRU_MOD_LEN          2
#define ZII_PCU_T2_HW_REV_LEN           2

#define IMS_PCU_VERSION_LEN		    (9 + 1)
#define IMS_PCU_BL_RESET_REASON_LEN	(2 + 1)

#define IMS_PCU_BUF_SIZE		128


/***********************************************
 * PCU-T2 get/set info message data structure
 *
 * Note: None of these are null-terminted. Be extremely careful when printing
 * to debug or to string, and ensure the size is taken into account.
 */
struct zii_pcu_t2_info {
    char part_number[ZII_PCU_T2_PART_NUMBER_LEN];
	char date_of_manufacturing[ZII_PCU_T2_DOM_LEN];
	char serial_number[ZII_PCU_T2_SERIAL_NUMBER_LEN];
    char lru_revision[ZII_PCU_T2_LRU_REV_LEN];
    char lru_mod[ZII_PCU_T2_LRU_MOD_LEN];
    char hw_revision[ZII_PCU_T2_HW_REV_LEN];
};



/*************************************************
 * Gaming / Normal mode enumeration. It must fit one byte.
 */

typedef enum __attribute__ ((packed)) {
    ZII_PCU_T2_MODE_NORMAL          = 1,    /* No game pad events */
    ZII_PCU_T2_MODE_GAME_0_BUTTON,
    ZII_PCU_T2_MODE_GAME_2_BUTTON,
    ZII_PCU_T2_MODE_GAME_4_BUTTON,
    ZII_PCU_T2_MODE_INVALID
} ZII_PCU_T2_MODE;

STATIC_ASSERT ((sizeof (ZII_PCU_T2_MODE) == sizeof (u8)),
                    ZII_PCU_T2_MODE_enumeration_must_fit_in_one_byte);


/**************************************************
 * Haptic auto-calibration parameters
 */
typedef struct {
    bool autocal_data_valid;
    struct autocal {
        u8  bemf_gain;
        u8  comp;
        u8  bemf;
    } __attribute__ ((packed)) autocal_data;
} ZII_PCU_T2_HAPTIC_CAL_DATA;

/***************************************************
 * Haptic programmed event button codes
 */
typedef enum
{
	BT_POWER = 0,
	BT_BACK,
	BT_CALL,
	BT_PLAY,
	BT_HOME,
	BT_LIGHT,
	BT_VOLDN,
	BT_VOLUP,
	BT_TOUCH,
	BT_CRADLE,
	BT_TP_GAMEMODE_0,
	BT_TP_GAMEMODE_1,
	BT_TP_GAMEMODE_2,
	BT_TP_GAMEMODE_3,
	BT_TP_GAMEMODE_4,
	BT_TP_GAMEMODE_5,
	BT_TP_GAMEMODE_6,
	BT_TP_GAMEMODE_7,
	BT_NUM_BUTTONS
} ZII_PCU_T2_BUTTON;

/********************************************************
 * Haptic programmed event command format
 */
typedef struct
{
    u8 Pressed;
    u8 Released;
 } __attribute__ ((packed)) ZII_PCU_T2_BUTTON_EVENTS;

typedef struct
{
    ZII_PCU_T2_BUTTON_EVENTS events[BT_NUM_BUTTONS];
} __attribute__ ((packed)) ZII_PCU_T2_PROGRAMMED_EVENTS;


/**************************************************
 * OLED display parameters
 */
typedef struct {
    u8  data[ZII_PCU_T2_DISPLAY_DATA_SIZE];
    u32 count;          /* number of valid bytes in buffer */
    u8  brightness;
    u8  invert;
} __attribute__ ((packed)) ZII_PCU_T2_DISPLAY;

/**************************************************
 * OLED display command parameters
 */
typedef struct {
    u8 action;
    u8 page;
    u8 segment;
    u8 data[ZII_PCU_T2_DISPLAY_SEGMENT_SIZE];
} __attribute__ ((packed)) ZII_PCU_T2_DISPLAY_INFO;

/**************************************************
 * OLED display action codes
 */

#define ZII_PCU_T2_DISPLAY_ACTION_ENABLE_DISPLAY    (0)
#define ZII_PCU_T2_DISPLAY_ACTION_DISABLE_DISPLAY   (1)
#define ZII_PCU_T2_DISPLAY_ACTION_CLEAR_DISPLAY     (2)
#define ZII_PCU_T2_DISPLAY_ACTION_FILL_DISPLAY      (3)
#define ZII_PCU_T2_DISPLAY_ACTION_ENABLE_INVERT     (4)
#define ZII_PCU_T2_DISPLAY_ACTION_DISABLE_INVERT    (5)
#define ZII_PCU_T2_DISPLAY_ACTION_SET_CONTRAST      (6)
#define ZII_PCU_T2_DISPLAY_ACTION_STORE_SEGMENT     (7)
#define ZII_PCU_T2_DISPLAY_ACTION_STORE_AND_WRITE   (8)


struct ims_pcu {
	struct usb_device *udev;
	struct device *dev; /* control interface's device, used for logging */

	unsigned int device_no;

	bool bootloader_mode;

    char calc_crc16_buffer[IMS_PCU_BUF_SIZE];

    char part_number[ZII_PCU_T2_PART_NUMBER_LEN];
	char serial_number[ZII_PCU_T2_SERIAL_NUMBER_LEN];
	char date_of_manufacturing[ZII_PCU_T2_DOM_LEN];
	char lru_revision[ZII_PCU_T2_LRU_REV_LEN];
    char lru_mod[ZII_PCU_T2_LRU_MOD_LEN];
    char hw_revision[ZII_PCU_T2_HW_REV_LEN];

	char fw_version_main[IMS_PCU_VERSION_LEN];
	char fw_version_touchpad[IMS_PCU_VERSION_LEN];
	char bl_version_main[IMS_PCU_VERSION_LEN];
	char bl_version_touchpad[IMS_PCU_VERSION_LEN];
	char reset_reason[IMS_PCU_BL_RESET_REASON_LEN];

	struct usb_interface *ctrl_intf;

	struct usb_endpoint_descriptor *ep_ctrl;
	struct urb *urb_ctrl;
	u8 *urb_ctrl_buf;
	dma_addr_t ctrl_dma;
	size_t max_ctrl_size;

	struct usb_interface *data_intf;

	struct usb_endpoint_descriptor *ep_in;
	struct urb *urb_in;
	u8 *urb_in_buf;
	dma_addr_t read_dma;
	size_t max_in_size;

	struct usb_endpoint_descriptor *ep_out;
	u8 *urb_out_buf;
	size_t max_out_size;

	u8 read_buf[IMS_PCU_BUF_SIZE];
	u8 read_pos;
	bool have_stx;
	bool have_dle;

	u8 cmd_buf[IMS_PCU_BUF_SIZE];
	u8 ack_id;
	u8 expected_response;
	u8 cmd_buf_len;
	struct completion cmd_done;
	struct mutex cmd_mutex;

	ZII_PCU_FW_INFO fw_info;

	struct ims_pcu_buttons buttons;
	struct ims_pcu_gamepad *gamepad;
	ZII_PCU_T2_BACKLIGHT backlight;

    ZII_PCU_T2_MODE mode;   /* Normal or gaming modes of operation */

    ZII_PCU_T2_HAPTIC_CAL_DATA  haptic_data;

    ZII_PCU_T2_DISPLAY display;

	bool setup_complete; /* Input and LED devices have been created */
};



/**********************/
/* CRC-16 calculation */
/**********************/

/* CRC-16 cooefficients */
#define CRC16_CCITT_POLYNOMIAL    0x1021


static uint16_t calc_crc16_for_byte (unsigned short prevValue, unsigned char nextByte)
{
    int i = 0;
    uint16_t shiftReg;
    uint16_t leadingBit;

    /* Perform XOR operation on all the new bits at once */
    shiftReg = prevValue ^ (nextByte << 8);

    /* Loop through each bit of the next byte */
    for(i = 0; i < 8; i++) {

        leadingBit = shiftReg & 0x8000; /* Save the leading bit */
        shiftReg = shiftReg << 1;       /* Shift 1 bit left */

        if (leadingBit == 32768) {      /* 32768 indicates 1000 0000 0000 0000 */

            /* Perform XOR operation with Generator polynomial if the leading bit is 1 */
            shiftReg = shiftReg ^ CRC16_CCITT_POLYNOMIAL;

            /* Remove unwanted values beyond 16 bits. */
            shiftReg = shiftReg & 0xFFFF;
        }
    }

    return shiftReg;
}



static uint16_t calc_crc16_for_buffer (const unsigned char* buf, unsigned long bufSize)
{
    int x = 0;
    unsigned char *p;

    /*Initialize the crc value */
    uint16_t m_crc = 0xFFFF;

    p = (unsigned char*)buf;
    for(x = 0; x < (int)bufSize; x++) {
        m_crc = calc_crc16_for_byte (m_crc, *p++);
    }

    return htons (m_crc);
}


/*********************************************************************
 *             Buttons Input device support                          *
 *********************************************************************/

/* PCU-T2 Normal (PSS) mode button mapping: The array index corresponds to the
 * bitmask returned by PCU firmware.
 */
static const unsigned short ims_pcu_keymap_0[] = {
	[1] = KEY_DISPLAYTOGGLE,
	[2] = KEY_ATTENDANT_TOGGLE,
	[3] = KEY_LIGHTS_TOGGLE,
	[4] = KEY_VOLUMEUP,
	[5] = KEY_VOLUMEDOWN,
	[6] = KEY_BACK,
	[7] = KEY_HOMEPAGE,
	[8] = KEY_PLAYPAUSE,
};

/* PCU-T2 Non-PSS mode button mapping: The array index corresponds to the
 * bitmask returned by PCU firmware.
 */
static const unsigned short ims_pcu_keymap_1[] = {
	[1] = KEY_DISPLAYTOGGLE,
	[2] = KEY_BRIGHTNESSUP,
	[3] = KEY_BRIGHTNESSDOWN,
	[4] = KEY_VOLUMEUP,
	[5] = KEY_VOLUMEDOWN,
	[6] = KEY_BACK,
	[7] = KEY_HOMEPAGE,
	[8] = KEY_PLAYPAUSE,
};

struct ims_pcu_device_info {
	const unsigned short *keymap;
	size_t keymap_len;
};

#define IMS_PCU_DEVINFO(_n)				\
	[_n] = {						\
		.keymap = ims_pcu_keymap_##_n,			\
		.keymap_len = ARRAY_SIZE(ims_pcu_keymap_##_n),	\
	}

static const struct ims_pcu_device_info ims_pcu_device_info[] = {
	IMS_PCU_DEVINFO (0),
	IMS_PCU_DEVINFO (1)
};

static void ims_pcu_buttons_report(struct ims_pcu *pcu,
                                    u32 data, ZII_PCU_T2_MODE mode)
{
	struct ims_pcu_buttons *buttons = &pcu->buttons;
	struct input_dev *input = buttons->input;
	int i;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

    /* In gaming modes, certain buttons are dedicated to game
     * controller. Mask them off and don't report them.
     */
    switch (mode) {

        case ZII_PCU_T2_MODE_NORMAL:
            /* No buttons associated with controller in this mode */
            break;

        case ZII_PCU_T2_MODE_GAME_0_BUTTON:
            /* Play and back */
            data &= ~((1 << 6) | (1 << 8));
            break;

        case ZII_PCU_T2_MODE_GAME_2_BUTTON:
            /* Play, back, attendant and light */
            data &= ~((1 << 6) | (1 << 8) | (1 << 2) | (1 << 3));
            break;

        case ZII_PCU_T2_MODE_GAME_4_BUTTON:
            /* Play, back, attendant, light, volume up/down */
            data &= ~((1 << 6) | (1 << 8)
                    | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5));
            break;

        default:
            dev_err (pcu->dev, "Invalid mode %d ignored", (int) mode);
            break;
    }


	for (i = 0; i < 32; i++) {
		unsigned short keycode = buttons->keymap[i];

		if (keycode != KEY_RESERVED)
			input_report_key(input, keycode, data & (1UL << i));
	}

	input_sync(input);
}

static int ims_pcu_setup_buttons(struct ims_pcu *pcu,
				 const unsigned short *keymap,
				 size_t keymap_len)
{
	struct ims_pcu_buttons *buttons = &pcu->buttons;
	struct input_dev *input;
	int i;
	int error;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

	input = input_allocate_device();
	if (!input) {
		dev_err(pcu->dev,
			"Not enough memory for input input device\n");
		return -ENOMEM;
	}

	snprintf(buttons->name, sizeof(buttons->name),
		 "Zii PCU-T2#%d Button Interface", pcu->device_no);

	usb_make_path(pcu->udev, buttons->phys, sizeof(buttons->phys));
	strlcat(buttons->phys, "/input0", sizeof(buttons->phys));

	memcpy(buttons->keymap, keymap, sizeof(*keymap) * keymap_len);

	input->name = buttons->name;
	input->phys = buttons->phys;
	usb_to_input_id(pcu->udev, &input->id);
	input->dev.parent = &pcu->ctrl_intf->dev;

	input->keycode = buttons->keymap;
	input->keycodemax = ARRAY_SIZE(buttons->keymap);
	input->keycodesize = sizeof(buttons->keymap[0]);

	__set_bit(EV_KEY, input->evbit);
	for (i = 0; i < IMS_PCU_KEYMAP_LEN; i++)
		__set_bit(buttons->keymap[i], input->keybit);
	__clear_bit(KEY_RESERVED, input->keybit);

	error = input_register_device(input);
	if (error) {
		dev_err(pcu->dev,
			"Failed to register buttons input device: %d\n",
			error);
		input_free_device(input);
		return error;
	}

	buttons->input = input;
	return 0;
}

static void ims_pcu_destroy_buttons(struct ims_pcu *pcu)
{
	struct ims_pcu_buttons *buttons = &pcu->buttons;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

	input_unregister_device(buttons->input);
}


/*********************************************************************
 *             Gamepad Input device support                          *
 *********************************************************************/

static void ims_pcu_gamepad_report(struct ims_pcu *pcu,
                                        u32 data, ZII_PCU_T2_MODE mode)
{
	struct ims_pcu_gamepad *gamepad = pcu->gamepad;
	struct input_dev *input = gamepad->input;
	int x = 0;
	int y = 0;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

    /* Back and play map to START regardless of the mode */
    input_report_key (input, BTN_START, (data & (1 << 6)) || (data & (1 << 8)));

    switch (mode) {

        case ZII_PCU_T2_MODE_GAME_0_BUTTON:
            /* No special mapping in this mode */
            break;

        case ZII_PCU_T2_MODE_GAME_2_BUTTON:
            /* Attendant and Light are mapped to A and B, respectively */
            input_report_key (input, BTN_A, data & (1 << 2));
            input_report_key (input, BTN_B, data & (1 << 3));
            break;

        case ZII_PCU_T2_MODE_GAME_4_BUTTON:
            /* Attendant and Light are mapped to A and B, respectively */
            input_report_key (input, BTN_A, data & (1 << 2));
            input_report_key (input, BTN_B, data & (1 << 3));
            /* Volume up/down are mapped to X and Y, respectively */
            input_report_key (input, BTN_X, data & (1 << 4));
            input_report_key (input, BTN_Y, data & (1 << 5));
            break;

        case ZII_PCU_T2_MODE_NORMAL:
            /* Nothing to report in these mode */
            break;

        default:
            dev_err (pcu->dev, "Invalid PCU mode: %d", (int) mode);
            break;
    }


	/* Soft buttons are reported regardless of the mode. However, the
	 * orientation of PCU in 0-button mode is expected to be upright.
	 */
	switch (mode) {
		case ZII_PCU_T2_MODE_GAME_0_BUTTON:
			y = !!(data & (1 << 14)) - !!(data & (1 << 10));
			x = !!(data & (1 << 12)) - !!(data & (1 << 16));
			break;

		case ZII_PCU_T2_MODE_GAME_2_BUTTON:
		case ZII_PCU_T2_MODE_GAME_4_BUTTON:
			x = !!(data & (1 << 14)) - !!(data & (1 << 10));
			y = !!(data & (1 << 16)) - !!(data & (1 << 12));
			break;

		case ZII_PCU_T2_MODE_NORMAL:
			/* Nothing to report in normal mode */
			break;

		default:
			dev_err (pcu->dev, "Invalid PCU mode: %d", (int) mode);
			break;
	}

	input_report_abs(input, ABS_X, x);
	input_report_abs(input, ABS_Y, y);

	input_sync(input);
}

static int ims_pcu_setup_gamepad(struct ims_pcu *pcu)
{
	struct ims_pcu_gamepad *gamepad;
	struct input_dev *input;
	int error;

    dev_dbg (pcu->dev,  "+++ %s", __func__);


	gamepad = kzalloc(sizeof(struct ims_pcu_gamepad), GFP_KERNEL);
	input = input_allocate_device();
	if (!gamepad || !input) {
		dev_err(pcu->dev,
			"Not enough memory for gamepad device\n");
		error = -ENOMEM;
		goto err_free_mem;
	}

	gamepad->input = input;

	snprintf(gamepad->name, sizeof(gamepad->name),
		 "Zii PCU-T2#%d Gamepad Interface", pcu->device_no);

	usb_make_path(pcu->udev, gamepad->phys, sizeof(gamepad->phys));
	strlcat(gamepad->phys, "/input1", sizeof(gamepad->phys));

	input->name = gamepad->name;
	input->phys = gamepad->phys;
	usb_to_input_id(pcu->udev, &input->id);
	input->dev.parent = &pcu->ctrl_intf->dev;

	__set_bit(EV_KEY, input->evbit);
	__set_bit(BTN_A, input->keybit);
	__set_bit(BTN_B, input->keybit);
	__set_bit(BTN_X, input->keybit);
	__set_bit(BTN_Y, input->keybit);
	__set_bit(BTN_START, input->keybit);
	__set_bit(BTN_SELECT, input->keybit);

	__set_bit(EV_ABS, input->evbit);
	input_set_abs_params(input, ABS_X, -1, 1, 0, 0);
	input_set_abs_params(input, ABS_Y, -1, 1, 0, 0);

	error = input_register_device(input);
	if (error) {
		dev_err(pcu->dev,
			"Failed to register gamepad input device: %d\n",
			error);
		goto err_free_mem;
	}

	pcu->gamepad = gamepad;
	return 0;

err_free_mem:
	input_free_device(input);
	kfree(gamepad);
	return -ENOMEM;
}

static void ims_pcu_destroy_gamepad(struct ims_pcu *pcu)
{
	struct ims_pcu_gamepad *gamepad = pcu->gamepad;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

	input_unregister_device(gamepad->input);
	kfree(gamepad);

    pcu->gamepad = NULL;
}


/*********************************************************************
 *             PCU Communication protocol handling                   *
 *********************************************************************/

#define IMS_PCU_PROTOCOL_STX		0x02
#define IMS_PCU_PROTOCOL_ETX		0x03
#define IMS_PCU_PROTOCOL_DLE		0x10

/*********************************************************************
 *  Applicaton mode PCU commands
 */
#define IMS_PCU_CMD_STATUS				0xa0
#define IMS_PCU_CMD_SET_MODE			0xa1
#define IMS_PCU_CMD_PCU_RESET			0xa2
#define IMS_PCU_CMD_RESET_REASON		0xa3
#define IMS_PCU_CMD_GET_DEVICE_ID		0xa4
#define IMS_PCU_CMD_SEND_BUTTONS		0xa5
#define IMS_PCU_CMD_JUMP_TO_BTLDR		0xa6
#define IMS_PCU_CMD_GET_INFO			0xa7
#define IMS_PCU_CMD_SET_INFO			0xa8
#define IMS_PCU_CMD_EEPROM				0xa9
#define IMS_PCU_CMD_GET_FW_VERSION		0xaa
#define IMS_PCU_CMD_GET_BL_VERSION		0xab
#define IMS_PCU_CMD_SET_LED_CONFIG		0xac
#define IMS_PCU_CMD_SET_LED_MODE		0xad
#define IMS_PCU_CMD_GET_LED_BRIGHTNESS	0xae
#define IMS_PCU_CMD_SET_DISPLAY_DATA	0xaf
#define IMS_PCU_CMD_SPECIAL_INFO		0xb0
#define IMS_PCU_CMD_RESERVED			0xb1
#define IMS_PCU_CMD_SET_EVENT_ACTION	0xb2
#define IMS_PCU_CMD_PLAY_HAPTIC_EFFECT	0xb3
#define IMS_PCU_CMD_SET_IMU_MODE        0xb4
#define IMS_PCU_CMD_GET_EVENT_ACTION    0xb5
#define IMS_PCU_CMD_CALIBRATE_HAPTIC	0xbf



/***********************************************************************
 * Application mode PCU responses
 */
#define IMS_PCU_RSP_STATUS					0xc0
#define IMS_PCU_RSP_SET_MODE				0xc1
#define IMS_PCU_RSP_PCU_RESET				0xc2
#define IMS_PCU_RSP_RESET_REASON			0xc3
#define IMS_PCU_RSP_GET_DEVICE_ID			0xc4
#define IMS_PCU_RSP_SEND_BUTTONS			0xc5
#define IMS_PCU_RSP_JUMP_TO_BTLDR			0xc6
#define IMS_PCU_RSP_GET_INFO				0xc7
#define IMS_PCU_RSP_SET_INFO				0xc8
#define IMS_PCU_RSP_EEPROM					0xc9
#define IMS_PCU_RSP_GET_FW_VERSION			0xca
#define IMS_PCU_RSP_GET_BL_VERSION			0xcb
#define IMS_PCU_RSP_SET_LED_CONFIG			0xcc
#define IMS_PCU_RSP_SET_LED_MODE			0xcd
#define IMS_PCU_RSP_GET_LED_BRIGHTNESS	    0xce
#define IMS_PCU_RSP_SET_DISPLAY_DATA		0xcf
#define IMS_PCU_RSP_SPECIAL_INFO			0xd0
#define IMS_PCU_RSP_CMD_INVALID				0xd1
#define IMS_PCU_RSP_SET_EVENT_ACTION	    0xd2
#define IMS_PCU_RSP_PLAY_HAPTIC_EFFECT		0xd3
#define IMS_PCU_RSP_SET_IMU_MODE            0xd4
#define IMS_PCU_RSP_GET_EVENT_ACTION        0xd5
#define IMS_PCU_RSP_CALIBRATE_HAPTIC		0xdf




#define IMS_PCU_RSP_EVNT_BUTTONS	0xe0	/* Unsolicited, button state */



#define IMS_PCU_MIN_PACKET_LEN		3
#define IMS_PCU_DATA_OFFSET		2

#define IMS_PCU_CMD_WRITE_TIMEOUT	100 /* msec */
#define IMS_PCU_CMD_RESPONSE_TIMEOUT	5000 /* msec */

static void ims_pcu_report_events(struct ims_pcu *pcu)
{
	u32 data = get_unaligned_be32(&pcu->read_buf[3]);
	ZII_PCU_T2_MODE mode;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

	/* 
		Get current mode of operation
		NOTE: There is a very small chance that pcu->mode
			is being read as it changed. This will potentially
			cause game buttons to be missed the first press.
     */
    mode = pcu->mode;

    dev_dbg (pcu->dev, "Buttons: 0x%08X", data);
	ims_pcu_buttons_report(pcu, data, mode);

	/* Game pad events reported in gaming mode(s) only */
	if (mode == ZII_PCU_T2_MODE_GAME_0_BUTTON ||
	    mode == ZII_PCU_T2_MODE_GAME_2_BUTTON ||
	    mode == ZII_PCU_T2_MODE_GAME_4_BUTTON)
    {
		ims_pcu_gamepad_report(pcu, data, mode);
    }
}

static void ims_pcu_handle_response(struct ims_pcu *pcu)
{

    dev_dbg (pcu->dev,  "+++ %s", __func__);

	switch (pcu->read_buf[0]) {
	case IMS_PCU_RSP_EVNT_BUTTONS:
		if (likely(pcu->setup_complete))
			ims_pcu_report_events(pcu);
		break;

	default:
		/*
		 * See if we got command completion.
		 * If both the sequence and response code match save
		 * the data and signal completion.
		 */
		if (pcu->read_buf[0] == pcu->expected_response &&
		    pcu->read_buf[1] == (u8) (pcu->ack_id - 1)) {

			memcpy(pcu->cmd_buf, pcu->read_buf, pcu->read_pos);
			pcu->cmd_buf_len = pcu->read_pos;
			complete(&pcu->cmd_done);
		}
		break;
	}
}

static void ims_pcu_process_data(struct ims_pcu *pcu, struct urb *urb)
{
	int i;

	for (i = 0; i < urb->actual_length; i++) {
		u8 data = pcu->urb_in_buf[i];

		/* Skip everything until we get Start Xmit */
		if (!pcu->have_stx && data != IMS_PCU_PROTOCOL_STX)
			continue;

		if (pcu->have_dle) {
			pcu->have_dle = false;
			pcu->read_buf[pcu->read_pos++] = data;
			continue;
		}

		switch (data) {
		case IMS_PCU_PROTOCOL_STX:
			if (pcu->have_stx)
				dev_warn(pcu->dev,
					 "Unexpected STX at byte %d, discarding old data\n",
					 pcu->read_pos);
			pcu->have_stx = true;
			pcu->have_dle = false;
			pcu->read_pos = 0;
			break;

		case IMS_PCU_PROTOCOL_DLE:
			pcu->have_dle = true;
			break;

		case IMS_PCU_PROTOCOL_ETX:
			if (pcu->read_pos < IMS_PCU_MIN_PACKET_LEN) {
				dev_warn(pcu->dev,
					 "Short packet received (%d bytes), ignoring\n",
					 pcu->read_pos);
			} else {
                /* Check CRC on received data */
                uint16_t received_crc16;
                uint16_t calculated_crc16;
                int      crc16_index;

                crc16_index = pcu->read_pos - 2;
                received_crc16 = pcu->read_buf[crc16_index];
                received_crc16 |= ((uint16_t) pcu->read_buf[crc16_index+1]) << 8;

                calculated_crc16 =
                            calc_crc16_for_buffer (pcu->read_buf, crc16_index);

			    if (calculated_crc16 != received_crc16) {
                    dev_warn (pcu->dev,
                        "CRC-16 Error: calculated (0x%X) does not match received (0x%X), ignoring)",
                        calculated_crc16, received_crc16);
			    } else {
				    ims_pcu_handle_response(pcu);
			    }
		    }

			pcu->have_stx = false;
			pcu->have_dle = false;
			pcu->read_pos = 0;
			break;

		default:
			pcu->read_buf[pcu->read_pos++] = data;
			break;
		}
	}
}

static bool ims_pcu_byte_needs_escape(u8 byte)
{
	return byte == IMS_PCU_PROTOCOL_STX ||
	       byte == IMS_PCU_PROTOCOL_ETX ||
	       byte == IMS_PCU_PROTOCOL_DLE;
}

static int ims_pcu_send_cmd_chunk(struct ims_pcu *pcu,
				  u8 command, int chunk, int len)
{
	int error;

	error = usb_bulk_msg(pcu->udev,
			     usb_sndbulkpipe(pcu->udev,
					     pcu->ep_out->bEndpointAddress),
			     pcu->urb_out_buf, len,
			     NULL, IMS_PCU_CMD_WRITE_TIMEOUT);
	if (error < 0) {
		dev_dbg(pcu->dev,
			"Sending 0x%02x command failed at chunk %d: %d\n",
			command, chunk, error);
		return error;
	}

	return 0;
}

static int ims_pcu_send_command(struct ims_pcu *pcu,
				u8 command, const u8 *data, int len)
{
	int error;
	uint16_t crc16 = 0;
	u8 *payload_data;
	int msg_len;
	int esc_msg_len;
    int index;
	int num_chunks = 0;
	int remainder;

    dev_dbg (pcu->dev,  "+++ %s: data: 0x%p, len: %d", __func__, data, len);

    /* Put command, ack ID and data into a single buffer and
     * calculate CRC and add it to the output buffer
     */
    pcu->calc_crc16_buffer[0] = command;
    pcu->calc_crc16_buffer[1] = pcu->ack_id++;
    if (data != NULL && len > 0) {
        memcpy (& pcu->calc_crc16_buffer[2], data, len);
    }
    msg_len = len + 2;
    crc16 = calc_crc16_for_buffer (pcu->calc_crc16_buffer, msg_len);

    /* Add the calculated CRC bytes to the buffer. The CRC buffer is now
     * used as a temporary storage.
     */
    pcu->calc_crc16_buffer[msg_len++] = crc16 & 0x00FF;
    pcu->calc_crc16_buffer[msg_len++] = crc16 >> 8;

    /* Now, go backwards through the buffer and prefix everything with escape
     * sequence where needed.
     */
    payload_data = pcu->calc_crc16_buffer + msg_len - 1;
    esc_msg_len = msg_len;
    for (index = msg_len - 1; index >= 0; index--, payload_data--) {
        /* Check if escape sequence is needed and move payload */
        if (ims_pcu_byte_needs_escape (*payload_data)) {
            esc_msg_len++;
            memmove (payload_data + 1, payload_data, esc_msg_len - index - 1);
            *payload_data = IMS_PCU_PROTOCOL_DLE;
        }
    }

    /* Add start and end of transmission marks */
    pcu->calc_crc16_buffer[esc_msg_len++] = IMS_PCU_PROTOCOL_ETX;
    memmove (& pcu->calc_crc16_buffer[1], & pcu->calc_crc16_buffer[0], esc_msg_len);
    pcu->calc_crc16_buffer[0] = IMS_PCU_PROTOCOL_STX;
    esc_msg_len++;

    /* Calculate how many chunks we need and then send this message.
     * Send each full chunk in a loop, followed by a remainder, if any, at the
     * end.
     */
    num_chunks = esc_msg_len / pcu->max_out_size;

    payload_data = pcu->calc_crc16_buffer;
    for (index = 0; index < num_chunks; index++) {
        memcpy (pcu->urb_out_buf, payload_data, pcu->max_out_size);
        error = ims_pcu_send_cmd_chunk(pcu, command, index, pcu->max_out_size);
		if (error)
			return error;
        payload_data += pcu->max_out_size;
    }

    remainder = esc_msg_len % pcu->max_out_size;

    if (remainder != 0) {
        memcpy (pcu->urb_out_buf, payload_data, esc_msg_len % pcu->max_out_size);
        error = ims_pcu_send_cmd_chunk(pcu, command, index, remainder);
		if (error)
			return error;
    }

    return 0;
}



/*************************************************************************
 * Generic routine to execute command by sending it to PCU.
 */
static int __ims_pcu_execute_command(struct ims_pcu *pcu,
				     u8 command, const void *data, size_t len,
				     u8 expected_response, int response_time)
{
	int error;

	dev_dbg (pcu->dev,  "+++ %s: cmd = 0x%X, rsp = 0x%X",
				__func__, command, expected_response);

	pcu->expected_response = expected_response;
	init_completion(&pcu->cmd_done);

	error = ims_pcu_send_command(pcu, command, data, len);
	if (error)
		return error;

	if (expected_response &&
	    !wait_for_completion_timeout(&pcu->cmd_done,
					 msecs_to_jiffies(response_time))) {
		dev_dbg(pcu->dev, "Command 0x%02x timed out\n", command);
		return -ETIMEDOUT;
	}

	return 0;
}



/************************************************************************
 * Macros to send commands to PCU when PCU is in normal (application) mode.
 */
#define ims_pcu_execute_command(pcu, code, data, len)			\
	__ims_pcu_execute_command(pcu,					\
				  IMS_PCU_CMD_##code, data, len,	\
				  IMS_PCU_RSP_##code,			\
				  IMS_PCU_CMD_RESPONSE_TIMEOUT)

#define ims_pcu_execute_query(pcu, code)				\
	ims_pcu_execute_command(pcu, code, NULL, 0)



/************************************************************************
 * PCU commands when in bootloader mode
 */
#define IMS_PCU_BL_CMD_QUERY_DEVICE         0xA1
#define IMS_PCU_BL_CMD_UNLOCK_CONFIG        0xA2
#define IMS_PCU_BL_CMD_ERASE_APP            0xA3
#define IMS_PCU_BL_CMD_PROGRAM_DEVICE       0xA4
#define IMS_PCU_BL_CMD_PROGRAM_COMPLETE     0xA5
#define IMS_PCU_BL_CMD_READ_APP             0xA6
#define IMS_PCU_BL_CMD_GET_INFO             0xA7
#define IMS_PCU_BL_CMD_SET_INFO             0xA8
#define IMS_PCU_BL_CMD_PCU_RESET            0xA9
#define IMS_PCU_BL_CMD_LAUNCH_APP           0xAA

/* PCU bootloader responses in bootloader mode */
#define IMS_PCU_BL_RSP_QUERY_DEVICE            0xC1
#define IMS_PCU_BL_RSP_UNLOCK_CONFIG           0xC2
#define IMS_PCU_BL_RSP_ERASE_APP               0xC3
#define IMS_PCU_BL_RSP_PROGRAM_DEVICE          0xC4
#define IMS_PCU_BL_RSP_PROGRAM_COMPLETE        0xC5
#define IMS_PCU_BL_RSP_READ_APP                0xC6
#define IMS_PCU_BL_RSP_GET_INFO                0xC7
#define IMS_PCU_BL_RSP_SET_INFO                0xC8
#define IMS_PCU_BL_RSP_PCU_RESET               0xC9
#define IMS_PCU_BL_RSP_LAUNCH_APP              0xCA



#define IMS_PCU_BL_DATA_OFFSET		3



/************************************************************************
 * Generic routine to send commands to PCU in bootloader mode
 */
static int __ims_pcu_execute_bl_command(struct ims_pcu *pcu,
				        u8 command, const void *data, size_t len,
				        u8 expected_response, int response_time)
{
	int error;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

	error = __ims_pcu_execute_command(pcu, command, data,
	            len, expected_response, response_time);
	if (error) {
		dev_err(pcu->dev,
			"Failure when sending 0x%02x command to bootloader, error: %d\n",
			pcu->cmd_buf[0], error);
		return error;
	}

	if (expected_response && pcu->cmd_buf[0] != expected_response) {
		dev_err(pcu->dev,
			"Unexpected response from bootloader: 0x%02x, wanted 0x%02x\n",
			pcu->cmd_buf[2], expected_response);
		return -EINVAL;
	}

	return 0;
}

#define ims_pcu_execute_bl_command(pcu, code, data, len, timeout)	\
	__ims_pcu_execute_bl_command(pcu,				\
				     IMS_PCU_BL_CMD_##code, data, len,	\
				     IMS_PCU_BL_RSP_##code, timeout)	\

#define IMS_PCU_INFO_PART_OFFSET	2
#define IMS_PCU_INFO_DOM_OFFSET		17
#define IMS_PCU_INFO_SERIAL_OFFSET	25


/**********************************************************************
 * Routinte to retrieve info from PCU via communication interface
 */
static int ims_pcu_get_info(struct ims_pcu *pcu)
{
	int error;
	struct zii_pcu_t2_info *pcu_info_data;

	dev_dbg (pcu->dev,  "+++ %s", __func__);

	error = ims_pcu_execute_query(pcu, GET_INFO);
	if (error) {
		dev_err(pcu->dev,
			"GET_INFO command failed, error: %d\n", error);
		return error;
	}

    /* Set up the pointer to get info */
    pcu_info_data = (struct zii_pcu_t2_info *)
                    & pcu->cmd_buf[IMS_PCU_INFO_PART_OFFSET];

    /* Retrieve and store the info */
	memcpy (pcu->part_number,
	                pcu_info_data->part_number, sizeof(pcu->part_number));
	memcpy (pcu->date_of_manufacturing, pcu_info_data->date_of_manufacturing,
	                sizeof(pcu->date_of_manufacturing));
	memcpy (pcu->serial_number,
	                pcu_info_data->serial_number, sizeof(pcu->serial_number));

    memcpy (pcu->lru_revision,
                    pcu_info_data->lru_revision, sizeof (pcu->lru_revision));
    memcpy (pcu->lru_mod, pcu_info_data->lru_mod, sizeof (pcu->lru_mod));
    memcpy (pcu->hw_revision,
                    pcu_info_data->hw_revision, sizeof (pcu->hw_revision));

	return 0;
}



/**********************************************************************
 * Routinte to store PCU info to PCU's non-volatile memory.
 */
static int ims_pcu_set_info(struct ims_pcu *pcu)
{
	int error;
	struct zii_pcu_t2_info *pcu_info_data;


    dev_dbg (pcu->dev,  "+++ %s", __func__);

    /* Set up the pointer to get info */
    pcu_info_data = (struct zii_pcu_t2_info *)
                    & pcu->cmd_buf[IMS_PCU_INFO_PART_OFFSET];

    /* Copy info to the communication buffer. */
	memcpy (pcu_info_data->part_number,
	        pcu->part_number, sizeof(pcu->part_number));
	memcpy (pcu_info_data->date_of_manufacturing,
	        pcu->date_of_manufacturing, sizeof(pcu->date_of_manufacturing));
	memcpy (pcu_info_data->serial_number,
	        pcu->serial_number, sizeof(pcu->serial_number));

    memcpy (pcu_info_data->lru_revision,
            pcu->lru_revision, sizeof (pcu->lru_revision));
    memcpy (pcu_info_data->lru_mod, pcu->lru_mod, sizeof (pcu->lru_mod));
    memcpy (pcu_info_data->hw_revision,
            pcu->hw_revision, sizeof (pcu->hw_revision));


    /* Now send a message to PCU to store the info */
	error = ims_pcu_execute_command(pcu, SET_INFO,
					& pcu->cmd_buf[IMS_PCU_DATA_OFFSET],
					sizeof (struct zii_pcu_t2_info));
	if (error) {
		dev_err(pcu->dev,
			"Failed to update device information, error: %d\n",
			error);
		return error;
	}

	return 0;
}

static int ims_pcu_switch_to_bootloader(struct ims_pcu *pcu)
{
	int error;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

	/* Execute jump to the bootoloader */
	error = ims_pcu_execute_command(pcu, JUMP_TO_BTLDR, NULL, 0);
	if (error) {
		dev_err(pcu->dev,
			"Failure when sending JUMP TO BOOTLOADER command, error: %d\n",
			error);
		return error;
	}

	return 0;
}

/*********************************************************************
 *             Firmware Update handling                              *
 *********************************************************************/

/* Verify processor's upgrade */
static int ims_pcu_verify_firmware(
            struct ims_pcu *pcu,
            const struct firmware *fw,
            ZII_PCU_FW_INFO *fw_info)
{
    unsigned int count = 0;
    u8 cmd_status;
    int error = 0;
    int i = 0;
    const u32 data_segment_size = ZII_PCU_T2_FIRMWARE_SEGMENT_SIZE;
    const u8 *data_to_verify;

    struct verify_device_cmd {
        u8  processor;
        u32 address;
        u8  size;
    } __attribute__ ((packed)) verify_dev_cmd;

    struct verify_device_rsp {
        u8  rsp_id;
        u8  ack_id;
        u8  status;
        u32 address;
        u8  size;
        u8  data[ZII_PCU_T2_FIRMWARE_SEGMENT_SIZE];
    } __attribute__ ((packed)) *pverify_dev_rsp;

    u32 address;
    u32 num_rec;
    u32 rsp_address;
    int remaining_size = fw->size;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

    address = fw_info->fw_start_addr;
    data_to_verify = fw->data;

    num_rec = fw->size / data_segment_size;
    if((fw->size - (num_rec * data_segment_size)) > 0) num_rec++;

    dev_dbg (pcu->dev, "Number of records to verify: %d", num_rec);

    /* read and verify firmware, record by record, from the PCU */
	while (remaining_size) {

        /* set up for next segment */
        verify_dev_cmd.processor = fw_info->processor_id;
        verify_dev_cmd.address   = cpu_to_be32(address);
        verify_dev_cmd.size      = remaining_size > data_segment_size
                                    ? data_segment_size : remaining_size;

        dev_dbg (pcu->dev,
            "Verifying address: 0x%08X, %3d%% count: %d size: %d\n",
            address, fw_info->percent_complete, count, verify_dev_cmd.size);

        /* read back next segment */
        error = ims_pcu_execute_bl_command (pcu, READ_APP,
                        & verify_dev_cmd, sizeof (verify_dev_cmd),
                        5000);
        if (error) {
        dev_err(pcu->dev,
                "Failed to read block at 0x%08x, len %d, error: %d\n",
                    address, verify_dev_cmd.size, error);
        return -EFAULT;
        }

        /* Check status in the response message ... */
        pverify_dev_rsp = (struct verify_device_rsp *)&pcu->cmd_buf;
        cmd_status = pverify_dev_rsp->status;
        if (cmd_status != 0) {
            dev_err (pcu->dev,
                    "Read error at 0x%08x, status: %d\n", address, cmd_status);
            return -EFAULT;
        }

        /* verify returned address matches requested address */
        rsp_address = be32_to_cpu(pverify_dev_rsp->address);
        dev_dbg(pcu->dev, "Comparing rsp_address 0x%8.8x to address 0x%8.8x",
                rsp_address, address);
        if(rsp_address != address) {
            dev_err(pcu->dev, "*** Verify readback address error: got 0x%8.8x, expected 0x%8.8x",
                rsp_address, address);
            return -EFAULT;
        }

        /* verify data byte by byte ... */
        for(i=0; i<verify_dev_cmd.size; i++) {
            if(pverify_dev_rsp->data[i] != data_to_verify[i]) {
                dev_err(pcu->dev, "Verify mismatch at address: 0x%8.8x: got 0x%2.2x, expected 0x%2.2x\n",
                    address+i, pverify_dev_rsp->data[i], data_to_verify[i]);
                return -EFAULT;
            }
        }

        data_to_verify += verify_dev_cmd.size;
        address += verify_dev_cmd.size;
        remaining_size -= verify_dev_cmd.size;

        fw_info->percent_complete = 50 + ((count * 50) / num_rec);
        count++;
    }

    fw_info->percent_complete = 100;

    return error;
}

/* Program processor's application */
static int ims_pcu_flash_firmware(struct ims_pcu *pcu,
        const struct firmware *fw, ZII_PCU_FW_INFO *fw_info)
{

    unsigned int count = 0;
    u8 cmd_status;
    int error;

    const u8 erase_status_ok = 0;
    const u8 erase_status_already_erased = 2;
    const u32 data_segment_size = ZII_PCU_T2_FIRMWARE_SEGMENT_SIZE;
    const u8 *data_to_program;
    struct program_device_msg {
        u8 processor;
        u32 address;
        u8 size;
        u8 data[ZII_PCU_T2_FIRMWARE_SEGMENT_SIZE];
    }  __attribute__ ((packed)) prog_dev_msg;
    u32 num_rec;
    u32 address;
    int remaining_size;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

    /* Send erase command and check response ... */
    dev_dbg (pcu->dev,
        "Erasing processor %d application...", fw_info->processor_id);

    error = ims_pcu_execute_bl_command(pcu, ERASE_APP, & fw_info->processor_id,
            sizeof (fw_info->processor_id), fw_info->erase_timeout);
    if (error) {
        dev_err(pcu->dev,
            "Failed to erase application image, error: %d\n",
            error);
        return error;
    }

    /* Check status ... */
    cmd_status = pcu->cmd_buf[2];
    if (cmd_status != erase_status_ok &&
        cmd_status != erase_status_already_erased)
    {
        dev_err (pcu->dev,
            "Processor %d's booloader failed to erase app: status = %d",
            fw_info->processor_id, cmd_status);
        return -EFAULT;
    }

	dev_dbg (pcu->dev,
			"Processor %d application erased", fw_info->processor_id);

    /* Calculate how many messages need to go across to complete programming */
    num_rec = fw->size / data_segment_size;
    prog_dev_msg.processor = fw_info->processor_id;
    address = fw_info->fw_start_addr;
    data_to_program = fw->data;

    dev_dbg (pcu->dev, "Number of records to flash: %d", num_rec);

    /* Send firmware, record by record, down to PCU ... */
    while (count <= num_rec) {

        dev_dbg (pcu->dev, "Programming address: 0x%08X, count: %d",
                address, count);

        memcpy (prog_dev_msg.data, data_to_program, data_segment_size);
        prog_dev_msg.address = cpu_to_be32 (address);
        prog_dev_msg.size = data_segment_size;

        error = ims_pcu_execute_bl_command (pcu, PROGRAM_DEVICE,
                    & prog_dev_msg, sizeof (prog_dev_msg),
                    5000);

        if (error) {
            dev_err(pcu->dev,
                    "Failed to write block at 0x%08x, len %d, error: %d\n",
                    prog_dev_msg.address, data_segment_size, error);
            return error;
        }

        /* Check status in the respose message ... */
        cmd_status = pcu->cmd_buf[2];
        if (cmd_status != 0) {
            dev_err (pcu->dev,
                    "PCU failed to program block at 0x%08x, status: %d\n",
                    prog_dev_msg.address, cmd_status);
            return -1;
        }

        data_to_program += data_segment_size;
        address += data_segment_size;

        fw_info->percent_complete = (count * 50) / num_rec;
        count++;

    }

    /* Check is there any leftover to finish up the sending ... */
    remaining_size = fw->size - num_rec * data_segment_size;
    if (remaining_size > 0) {

        dev_dbg (pcu->dev,
                "Programming address: 0x%08X, remaining size: %d",
                address, remaining_size);

        memcpy (prog_dev_msg.data, data_to_program, remaining_size);
        prog_dev_msg.address = cpu_to_be32 (address);
        prog_dev_msg.size = remaining_size;

        error = ims_pcu_execute_bl_command (pcu, PROGRAM_DEVICE,
                        & prog_dev_msg, sizeof (prog_dev_msg),
                        5000);
        if (error) {
            dev_err(pcu->dev,
                "Failed to write block at 0x%08x, len %d, error: %d\n",
                prog_dev_msg.address, remaining_size, error);
            return error;
        }

        /* Check status in the respose message ... */
        cmd_status = pcu->cmd_buf[2];
        if (cmd_status != 0) {
            dev_err (pcu->dev,
                    "PCU failed to program block at 0x%08x, status: %d\n",
                    prog_dev_msg.address, cmd_status);
            return -EINVAL;
        }

        /* flash write is only 50%, verify is the second 50% */
        fw_info->percent_complete = 50;

    }

    /* send PROGRAM COMPLETE now to ensure write of last block */
    error = ims_pcu_execute_bl_command(pcu, PROGRAM_COMPLETE,
                & fw_info->processor_id, sizeof (fw_info->processor_id), 2000);
    if (error) {
        dev_err(pcu->dev,
            "Failed to send PROGRAM_COMPLETE, error: %d\n",
            error);
        return error;
    }

    /* perform verification read back */
    error = ims_pcu_verify_firmware(pcu, fw, fw_info);

    if(error) {
        dev_err(pcu->dev, "Verification Failed: Error: %d\n", error);
    }

	return error;
}

static int ims_pcu_handle_firmware_update (
    struct ims_pcu *pcu, const struct firmware *fw, ZII_PCU_FW_INFO *fw_info)
{
	int retval;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

	dev_dbg (pcu->dev, "Updating firmware on processor %d, size: %zu\n",
				fw_info->processor_id, fw->size);

    /* Program firmware and then jump to main application when done. */
	retval = ims_pcu_flash_firmware (pcu, fw, fw_info);
	if (retval)
		fw_info->percent_complete = retval;

	sysfs_notify(&pcu->dev->kobj, NULL, "update_firmware_status");

	return retval;
}


/*********************************************************************
 *             Backlight LED device support                          *
 *********************************************************************/

#define ZII_PCU_T2_LED_ID_HOME          (6)
#define ZII_PCU_T2_LED_ID_TOUCH_0       (8)
#define ZII_PCU_T2_LED_ID_TOUCH_GROUP   (16)
#define ZII_PCU_T2_LED_ID_AB_GROUP      (17)
#define ZII_PCU_T2_LED_ID_ABXY_GROUP    (19)
#define ZII_PCU_T2_LED_ID_MAX           (20)


/* Utility function to get LED brightness */
static int zii_pcu_t2_get_led_brightness (struct ims_pcu *pcu,  u8 led_id,
            ZII_PCU_T2_BACKLIGHT_ENUM channel, enum led_brightness *brightness)
{
    int error = 0;
    struct zii_pcu_t2_led_brightness {
        u8  red;
        u8  green;
        u8  blue;
        u8  white;
    } *b;


    /* Check input id. */
    if (led_id > ZII_PCU_T2_LED_ID_ABXY_GROUP) {
        return -EINVAL;
    }

    /* Lock mutex and execute command to get brightness */
    mutex_lock (& pcu->cmd_mutex);
    error = ims_pcu_execute_command (
                    pcu, GET_LED_BRIGHTNESS, & led_id, sizeof (led_id));
    if (error) {
        dev_err (pcu->dev, "Failed to get LED brighness, error = %d", error);
        error = -EFAULT;
        goto exit;
    }

    b = (struct zii_pcu_t2_led_brightness *) & pcu->cmd_buf[2];
    switch (channel) {
        case ZII_PCU_T2_BACKLIGHT_RED:
            *brightness = (enum led_brightness) b->red;
            break;
        case ZII_PCU_T2_BACKLIGHT_GREEN:
            *brightness = (enum led_brightness) b->green;
            break;
        case ZII_PCU_T2_BACKLIGHT_BLUE:
            *brightness = (enum led_brightness) b->blue;
            break;
        case ZII_PCU_T2_BACKLIGHT_WHITE_TOUCHPAD:
        case ZII_PCU_T2_BACKLIGHT_WHITE_KEYPAD:
            *brightness = (enum led_brightness) b->white;
            break;
        default:
            dev_crit (pcu->dev, "Invalid backlight index: %d", channel);
            *brightness = LED_OFF;
            break;
    }

exit:

    mutex_unlock (& pcu->cmd_mutex);

    return error;
}


/* Utility function to adjust specific channel for RGB LEDs */
static int zii_pcu_t2_led_control (struct ims_pcu *pcu,
    ZII_PCU_T2_BACKLIGHT_ENUM channel,
    enum led_brightness brightness)
{
    ZII_PCU_T2_BACKLIGHT *backlight = & pcu->backlight;
    int error = 0;
    u8 led_id[ZII_PCU_T2_LED_ID_MAX];
    u8 led_id_len = 0;
    int index;

    struct led_config {
        u8  id;
        u8  red;
        u8  green;
        u8  blue;
        u8  white;
        u8  ramp_enable;
        u8  ramp_time;
        u8  blink_period;
        u8  blink_duty_cycle;
    } led_config;

    dev_dbg (pcu->dev, "+++ %s", __func__);

	mutex_lock (& pcu->cmd_mutex);

    /* Initialize generic, channel independent, LED config structure */
    led_config.id = 0;
    led_config.red = backlight->bl_dev[ZII_PCU_T2_BACKLIGHT_RED].cdev.brightness;
    led_config.green = backlight->bl_dev[ZII_PCU_T2_BACKLIGHT_GREEN].cdev.brightness;
    led_config.blue = backlight->bl_dev[ZII_PCU_T2_BACKLIGHT_BLUE].cdev.brightness;
    led_config.white =
		backlight->bl_dev[ZII_PCU_T2_BACKLIGHT_WHITE_TOUCHPAD].cdev.brightness;
    led_config.ramp_enable = 0;
    led_config.ramp_time = 0;
    led_config.blink_period = 0;
    led_config.blink_duty_cycle = 0;

    /* Make adjustmeent depending on the channel */
    switch (channel) {
        case ZII_PCU_T2_BACKLIGHT_RED:
            led_config.red = brightness;
            break;
        case ZII_PCU_T2_BACKLIGHT_GREEN:
            led_config.green = brightness;
            break;
        case ZII_PCU_T2_BACKLIGHT_BLUE:
            led_config.blue = brightness;
            break;
        case ZII_PCU_T2_BACKLIGHT_WHITE_TOUCHPAD:
        case ZII_PCU_T2_BACKLIGHT_WHITE_KEYPAD:
            led_config.white = brightness;
            led_config.red = 0;
            led_config.green = 0;
            led_config.blue = 0;
            break;
        default:
            dev_crit (pcu->dev, "Invalid backlight channel: %d", channel);
            error = -EFAULT;
            break;
    }

    if (error) {
        goto err_unlock_mutex;
    }

    /* Set up the array of LED IDs to control depending on the channel */
    memset (led_id, 0, sizeof (led_id));
    switch (channel) {
		case ZII_PCU_T2_BACKLIGHT_WHITE_TOUCHPAD:
			led_id[0] = ZII_PCU_T2_LED_ID_TOUCH_GROUP;
			led_id_len = 1;
			break;

		case ZII_PCU_T2_BACKLIGHT_WHITE_KEYPAD:
			switch (pcu->mode) {
				case ZII_PCU_T2_MODE_NORMAL:
				case ZII_PCU_T2_MODE_GAME_0_BUTTON:
				/* Do nothing - there are no white only LEDs in this mode */
				led_id_len = 0;
				break;

			case ZII_PCU_T2_MODE_GAME_2_BUTTON:
				/* AB group present */
				led_id[0] = ZII_PCU_T2_LED_ID_AB_GROUP;
				led_id_len = 1;
				break;

			case ZII_PCU_T2_MODE_GAME_4_BUTTON:
				/* ABXY group present */
				led_id[0] = ZII_PCU_T2_LED_ID_ABXY_GROUP;
				led_id_len = 1;
				break;

			default:
				dev_crit (pcu->dev, "Invalid PCU-T2 mode: %d", pcu->mode);
				led_id_len = 0;
				error = -EFAULT;
				break;
			}
			break;

		case ZII_PCU_T2_BACKLIGHT_RED:
		case ZII_PCU_T2_BACKLIGHT_GREEN:
		case ZII_PCU_T2_BACKLIGHT_BLUE:
			/* LED ids range from 0, up to TOUCH_0 button */
			for (index = 0; index < ZII_PCU_T2_LED_ID_TOUCH_0; index++) {
				led_id[index] = index;
			}
			led_id_len = ZII_PCU_T2_LED_ID_TOUCH_0;
			break;

		default:
			dev_crit (pcu->dev, "Invalid backlight channel: %d", channel);
			led_id_len = 0;
			error = -EFAULT;
			break;
    }

    /* Loop through the array of affected LEDs and re-adjust their configs */
    for (index = 0; index < led_id_len && !error; index++) {
        led_config.id = led_id[index];
	error = ims_pcu_execute_command (
                        pcu, SET_LED_CONFIG, & led_config, sizeof (led_config));
	if (error) {
	    dev_err (pcu->dev,
	        "Failed to execute SET_LED_CONFIG command, error = %d", error);
	    }
    }


err_unlock_mutex:

	mutex_unlock(&pcu->cmd_mutex);

    return error;
}


/* Method to set LED brightness */
static void
zii_pcu_t2_backlight_set_brightness (struct led_classdev *cdev,
					                 enum led_brightness value)
{
	struct zii_pcu_t2_backlight_dev *bl_dev =
			container_of (cdev, struct zii_pcu_t2_backlight_dev, cdev);
	ZII_PCU_T2_BACKLIGHT *backlight = bl_dev->backlight;
	struct ims_pcu *pcu =
			container_of (backlight, struct ims_pcu, backlight);
	ZII_PCU_T2_BACKLIGHT_ENUM channel = bl_dev->channel;

    dev_dbg (pcu->dev, "+++ %s", __func__);

    dev_dbg (pcu->dev, "Setting brightness for channel %d: %d", channel, value);

	mutex_lock (& backlight->led_mutex);
    if (zii_pcu_t2_led_control (pcu, channel, value) == 0) {
        bl_dev->cdev.brightness = value;
	}
	mutex_unlock (& backlight->led_mutex);
}


/* Method to get LED brightness */
static enum led_brightness
zii_pcu_t2_backlight_get_brightness (struct led_classdev *cdev)
{
	struct zii_pcu_t2_backlight_dev *bl_dev =
			container_of (cdev, struct zii_pcu_t2_backlight_dev, cdev);
	ZII_PCU_T2_BACKLIGHT *backlight = bl_dev->backlight;
	struct ims_pcu *pcu =
			container_of(backlight, struct ims_pcu, backlight);
    enum led_brightness brightness;

    dev_dbg (pcu->dev, "+++ %s", __func__);

    mutex_lock (& backlight->led_mutex);
    brightness = cdev->brightness;
    mutex_unlock (& backlight->led_mutex);

    dev_dbg (pcu->dev,
        "LED channel %d brightness: %d", bl_dev->channel, brightness);

	return brightness;
}

/* Backlight destroy declaration */
static void zii_pcu_t2_destroy_backlight(struct ims_pcu *pcu);

/* Utility function to setup backlights as LED devices */
static int zii_pcu_t2_setup_backlight(struct ims_pcu *pcu)
{
	ZII_PCU_T2_BACKLIGHT *backlight = & pcu->backlight;
	ZII_PCU_T2_BACKLIGHT_ENUM index;
	int error = 0;
	enum led_brightness brightness;
	u8 led_id;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

    /* Ensure all LED devices are marked as not registered */
    for (index = ZII_PCU_T2_BACKLIGHT_RED;
            index < ZII_PCU_T2_BACKLIGHT_NUMDEV; index++)
    {
        backlight->bl_dev[index].registered = false;
    }

    /* Name all backlight LED devices */
    snprintf (backlight->bl_dev[ZII_PCU_T2_BACKLIGHT_RED].name,
                sizeof (backlight->bl_dev[ZII_PCU_T2_BACKLIGHT_RED].name),
                "pcu%d::%s_backlight_%s", pcu->device_no, "kbd", "red");
    snprintf (backlight->bl_dev[ZII_PCU_T2_BACKLIGHT_GREEN].name,
                sizeof (backlight->bl_dev[ZII_PCU_T2_BACKLIGHT_GREEN].name),
                "pcu%d::%s_backlight_%s", pcu->device_no, "kbd", "green");
    snprintf (backlight->bl_dev[ZII_PCU_T2_BACKLIGHT_BLUE].name,
                sizeof (backlight->bl_dev[ZII_PCU_T2_BACKLIGHT_BLUE].name),
                "pcu%d::%s_backlight_%s", pcu->device_no, "kbd", "blue");
    snprintf (backlight->bl_dev[ZII_PCU_T2_BACKLIGHT_WHITE_TOUCHPAD].name,
                sizeof (backlight->bl_dev[ZII_PCU_T2_BACKLIGHT_WHITE_TOUCHPAD].name),
                "pcu%d::%s_backlight_%s", pcu->device_no, "game", "touchpad");
    snprintf (backlight->bl_dev[ZII_PCU_T2_BACKLIGHT_WHITE_KEYPAD].name,
                sizeof (backlight->bl_dev[ZII_PCU_T2_BACKLIGHT_WHITE_KEYPAD].name),
                "pcu%d::%s_backlight_%s", pcu->device_no, "game", "keypad");

    /* Init mutex */
    mutex_init (& backlight->led_mutex);

    /* Loop through the devices and set them up and register */
    for (index = ZII_PCU_T2_BACKLIGHT_RED;
         index < ZII_PCU_T2_BACKLIGHT_NUMDEV && !error; index++)
    {
        /* While all LEDs can be controlled individually at the firmware level,
         * the driver controls red, green and blue channels for all RGB LEDs
         * together. The same applies for all white LEDs - the driver controls
         * them all at once. So, pick one led from each group and get brigthness.
         */
		switch (index) {
			case ZII_PCU_T2_BACKLIGHT_WHITE_TOUCHPAD:
				led_id = ZII_PCU_T2_LED_ID_TOUCH_GROUP;
				break;

			case ZII_PCU_T2_BACKLIGHT_WHITE_KEYPAD:
				led_id = ZII_PCU_T2_LED_ID_AB_GROUP;
				break;

			default:
				led_id = ZII_PCU_T2_LED_ID_HOME;
				break;
        }

        error = zii_pcu_t2_get_led_brightness (pcu, led_id, index, & brightness);
        if (error) {
            dev_err (pcu->dev, "Failed to turn off RGB LEDs");
            continue;
        }

        backlight->bl_dev[index].channel = index;
        backlight->bl_dev[index].backlight = backlight;
        backlight->bl_dev[index].cdev.max_brightness = LED_FULL;
        backlight->bl_dev[index].cdev.brightness = brightness;
        backlight->bl_dev[index].cdev.name = backlight->bl_dev[index].name;
        backlight->bl_dev[index].cdev.brightness_get =
                        zii_pcu_t2_backlight_get_brightness;
        backlight->bl_dev[index].cdev.brightness_set =
                        zii_pcu_t2_backlight_set_brightness;

        error = led_classdev_register(pcu->dev, & backlight->bl_dev[index].cdev);
        if (error) {
	        dev_err(pcu->dev,
		        "Failed to register backlight device %s, error: %d\n",
		        backlight->bl_dev[index].name, error);
        }

        backlight->bl_dev[index].registered = true;

        dev_dbg (pcu->dev,
            "Registered backlight device: %s", backlight->bl_dev[index].name);
    }

    /* On errors, destroy all backlight planes */
    if (error) {
        zii_pcu_t2_destroy_backlight (pcu);
    }

	return error;
}

static void zii_pcu_t2_destroy_backlight(struct ims_pcu *pcu)
{
	ZII_PCU_T2_BACKLIGHT *backlight = & pcu->backlight;
	ZII_PCU_T2_BACKLIGHT_ENUM index;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

    /* Deregister backlight devices */
    for (index = ZII_PCU_T2_BACKLIGHT_RED;
         index < ZII_PCU_T2_BACKLIGHT_NUMDEV; index++)
    {
        if (backlight->bl_dev[index].registered) {
            led_classdev_unregister (&backlight->bl_dev[index].cdev);
            backlight->bl_dev[index].registered = false;
        }
    }
}


/*********************************************************************
 *             Sysfs attributes handling                             *
 *********************************************************************/

struct ims_pcu_attribute {
	struct device_attribute dattr;
	size_t field_offset;
	int field_length;
};

static ssize_t ims_pcu_attribute_show(struct device *dev,
				      struct device_attribute *dattr,
				      char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct ims_pcu *pcu = usb_get_intfdata(intf);
	struct ims_pcu_attribute *attr =
			container_of(dattr, struct ims_pcu_attribute, dattr);
	char *field = (char *)pcu + attr->field_offset;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

	return scnprintf(buf, PAGE_SIZE, "%.*s\n", attr->field_length, field);
}

static ssize_t ims_pcu_attribute_store(struct device *dev,
				       struct device_attribute *dattr,
				       const char *buf, size_t count)
{

	struct usb_interface *intf = to_usb_interface(dev);
	struct ims_pcu *pcu = usb_get_intfdata(intf);
	struct ims_pcu_attribute *attr =
			container_of(dattr, struct ims_pcu_attribute, dattr);
	char *field = (char *)pcu + attr->field_offset;
	size_t data_len;
	int error;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

	if (count > attr->field_length)
		return -EINVAL;

	data_len = strnlen(buf, attr->field_length);
	if (data_len > attr->field_length)
		return -EINVAL;

	error = mutex_lock_interruptible(&pcu->cmd_mutex);
	if (error)
		return error;

	memset(field, 0, attr->field_length);
	memcpy(field, buf, data_len);

	/* Store the new attribute:
	 * Even if update failed, let's fetch the info again as we just
	 * clobbered one of the fields.
	 */
	error = ims_pcu_set_info(pcu);
    if (error) {
        dev_err (pcu->dev,
            "Failed to set PCU info - ignoring error: %d", error);
    }

	ims_pcu_get_info(pcu);

	mutex_unlock(&pcu->cmd_mutex);

	return error < 0 ? error : count;
}

#define IMS_PCU_ATTR(_field, _mode)					\
struct ims_pcu_attribute ims_pcu_attr_##_field = {			\
	.dattr = __ATTR(_field, _mode,					\
			ims_pcu_attribute_show,				\
			ims_pcu_attribute_store),			\
	.field_offset = offsetof(struct ims_pcu, _field),		\
	.field_length = sizeof(((struct ims_pcu *)NULL)->_field),	\
}

#define IMS_PCU_RO_ATTR(_field)						\
		IMS_PCU_ATTR(_field, S_IRUGO)
#define IMS_PCU_RW_ATTR(_field)						\
		IMS_PCU_ATTR(_field, S_IRUGO | S_IWUSR)

static IMS_PCU_RW_ATTR(part_number);
static IMS_PCU_RW_ATTR(serial_number);
static IMS_PCU_RW_ATTR(date_of_manufacturing);

static IMS_PCU_RO_ATTR (fw_version_main);
static IMS_PCU_RO_ATTR (fw_version_touchpad);
static IMS_PCU_RO_ATTR (bl_version_main);
static IMS_PCU_RO_ATTR (bl_version_touchpad);
static IMS_PCU_RO_ATTR (reset_reason);

static ssize_t ims_pcu_reset_device(struct device *dev,
				    struct device_attribute *dattr,
				    const char *buf, size_t count)
{
	static const u8 reset_byte = 1;
	struct usb_interface *intf = to_usb_interface(dev);
	struct ims_pcu *pcu = usb_get_intfdata(intf);
	int value;
	int error;

    dev_dbg (pcu->dev,  "+++ %s", __func__);


	error = kstrtoint(buf, 0, &value);
	if (error)
		return error;

	if (value != 1)
		return -EINVAL;

	dev_info(pcu->dev, "Attempting to reset device\n");

    if (pcu->bootloader_mode) {
	error = ims_pcu_execute_bl_command (pcu, PCU_RESET, NULL, 0, 5000);
    } else {
	error = ims_pcu_execute_command(pcu, PCU_RESET, &reset_byte, 1);
	}
	if (error) {
		dev_info(pcu->dev,
			 "Failed to reset device, error: %d\n",
			 error);
		return error;
	}

	return count;
}

static DEVICE_ATTR(reset_device, S_IWUSR, NULL, ims_pcu_reset_device);



static ssize_t ims_pcu_update_firmware_store(struct device *dev,
					     struct device_attribute *dattr,
					     const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct ims_pcu *pcu = usb_get_intfdata(intf);
	char   fw_name[ZII_pcu_T2_FIRMWARE_NAME_LEN];
	char   processor[ZII_pcu_T2_FIRMWARE_NAME_LEN];
	const  char *main_proc_name = "main";
	const  char *tp_proc_name = "touchpad";
	const struct firmware *fw = NULL;
    ZII_PCU_FW_INFO *fw_info;
	int error;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

	/* Fill in generic firmware update structure */
	fw_info = & pcu->fw_info;
	fw_info->processor_id = FW_UPDATE_PROC_ID_INVALID;
	fw_info->percent_complete = 0;
	init_completion  (& fw_info->fw_update_done);

	/* Get data and check validity */
	if (count == 0 ||
		(sscanf (buf, "%8s%25s", processor, fw_name) != 2) ||
		(sscanf (buf, "%8s%25s\n", processor, fw_name) != 2))
	{
		return -EINVAL;
	}

	if (strncmp (processor, main_proc_name, strlen (main_proc_name)) == 0) {
		fw_info->processor_id = FW_UPDATE_PROC_ID_MAIN;
		fw_info->fw_start_addr = ZII_PCU_T2_FIRMWARE_MAIN_START_ADDR;
		fw_info->erase_timeout = ZII_PCU_T2_FIRMWARE_MAIN_ERASE_TIMEOUT;
	} else
		if (strncmp (processor, tp_proc_name, strlen (tp_proc_name)) == 0) {
			fw_info->processor_id = FW_UPDATE_PROC_ID_TOUCHPAD;
			fw_info->fw_start_addr = ZII_PCU_T2_FIRMWARE_TOUCHPAD_START_ADDR;
			fw_info->erase_timeout = ZII_PCU_T2_FIRMWARE_TOUCHPAD_ERASE_TIMEOUT;
		} else {
			return -EINVAL;
		}

	/* Request firmare for the processor ... */
	error = request_firmware (& fw, fw_name, pcu->dev);
	if (error) {
		dev_err (pcu->dev,
		    "Firmware %s for processor %d (%s) not found, error: %d",
		    fw_name, fw_info->processor_id, processor, error);
	    return -EINVAL;
	}

	strncpy (fw_info->fw_name, fw_name, ZII_pcu_T2_FIRMWARE_NAME_LEN);

	dev_info (pcu->dev,
		"Firmware update: processor %d (%s), file = %s, size = %zd",
		fw_info->processor_id, processor, fw_info->fw_name, fw->size);

	/* Do firmware update now */
	error = ims_pcu_handle_firmware_update (pcu, fw, fw_info);
	if (error) {
		dev_err (pcu->dev,
		        "Firmware update for %s failed, error: %d",
		        fw_info->fw_name, error);
	}

	release_firmware (fw);
	fw_info->percent_complete = 0;
	complete (& fw_info->fw_update_done);

	sysfs_notify(&pcu->dev->kobj, NULL, "update_firmware_status");

	return error ? -EFAULT : count;
}

static DEVICE_ATTR(update_firmware, S_IWUSR,
		   NULL, ims_pcu_update_firmware_store);

/* Show attribute functions for firmware status */
static ssize_t
ims_pcu_update_firmware_status_show(struct device *dev,
				    struct device_attribute *dattr,
				    char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct ims_pcu *pcu = usb_get_intfdata(intf);

    dev_dbg (pcu->dev,  "+++ %s", __func__);

	return scnprintf(buf, PAGE_SIZE, "%d\n", pcu->fw_info.percent_complete);
}


/* Attributes to show progress of firmware update for both processors */
static DEVICE_ATTR(update_firmware_status, S_IRUGO,
		   ims_pcu_update_firmware_status_show, NULL);


/* Normal mode vs. gaming mode attribute and function handlers */
static ssize_t zii_pcu_t2_mode_show (struct device *dev,
                        struct device_attribute *dattr, char *buf)
{
	struct usb_interface *intf = to_usb_interface (dev);
	struct ims_pcu *pcu = usb_get_intfdata (intf);

    dev_dbg (pcu->dev,  "+++ %s", __func__);

	return scnprintf (buf, PAGE_SIZE, "%d", pcu->mode);
}

static ssize_t zii_pcu_t2_mode_store (struct device *dev,
                struct device_attribute *dattr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface (dev);
	struct ims_pcu *pcu = usb_get_intfdata (intf);
	int error;
	int mode;
	ZII_PCU_T2_MODE current_mode = pcu->mode;

    dev_dbg (pcu->dev,  "+++ %s", __func__);


    /* Get input, and convert it to enumeration */
    if (sscanf (buf, "%d", & mode) != 1 || sscanf (buf, "%d\n", & mode) != 1) {
        error = -EINVAL;
        goto err_set_mode;
    }

    if (mode < ZII_PCU_T2_MODE_NORMAL || mode >= ZII_PCU_T2_MODE_INVALID) {
        error = -EINVAL;
        goto err_set_mode;
    }

    /* Put PCU in the desired mode */
    pcu->mode = (ZII_PCU_T2_MODE) mode;
	error = mutex_lock_interruptible (& pcu->cmd_mutex);
	if (error)
	    goto err_set_mode_unlock_mutex;

    error = ims_pcu_execute_command (pcu,
                            SET_MODE, & pcu->mode, sizeof (pcu->mode));
    if (error)
        dev_err (pcu->dev, "Failed to set mode %d, error = %d", mode, error);

err_set_mode_unlock_mutex:

	mutex_unlock (& pcu->cmd_mutex);

err_set_mode:

    /* Restore mode settings in case there was an error, so the driver is
     * in sync with the PCU.
     */
    if (error)
        pcu->mode = current_mode;

    return error ? error : count;
}

static DEVICE_ATTR (mode, S_IRUSR | S_IWUSR,
                    zii_pcu_t2_mode_show, zii_pcu_t2_mode_store);


/* Haptic feedback attribute and handlers */
static ssize_t zii_pcu_t2_haptic_play_store (struct device *dev,
                struct device_attribute *dattr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface (dev);
	struct ims_pcu *pcu = usb_get_intfdata (intf);
    int error = 0;
    int input;
    u8  haptic_effect = 0;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

     /* Get input and check its validity */
    if (sscanf (buf, "%d", & input) != 1 || sscanf (buf, "%d\n", & input) != 1) {
        error = -EINVAL;
        goto err_haptic_play;
    }

    if (input < 0 || input > 123) {
        error = -EINVAL;
        goto err_haptic_play;
    }

    /* Input is ok, execute the action */
    haptic_effect = (u8) input;
    dev_info (pcu->dev, "Haptic playback: %d", haptic_effect);

    error = mutex_lock_interruptible (& pcu->cmd_mutex);
	if (error)
	    goto err_haptic_play_unlock_mutex;

    error = ims_pcu_execute_command (pcu,
                PLAY_HAPTIC_EFFECT, & haptic_effect, sizeof (haptic_effect));
    if (error)
        dev_err (pcu->dev,
            "Failed to play effect %d, error = %d", haptic_effect, error);

err_haptic_play_unlock_mutex:

	mutex_unlock (& pcu->cmd_mutex);

err_haptic_play:

    return error ? error : count;
}


static ssize_t zii_pcu_t2_haptic_autocal_show (struct device *dev,
                        struct device_attribute *dattr, char *buf)
{
	struct usb_interface *intf = to_usb_interface (dev);
	struct ims_pcu *pcu = usb_get_intfdata (intf);
	ZII_PCU_T2_HAPTIC_CAL_DATA haptic_data;
	int error;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

    /* Check if the autocalibration has been done. If so, print
     * calibration data. If no, return error.
     */
    error = mutex_lock_interruptible (& pcu->cmd_mutex);
	if (error) {
	    dev_err (pcu->dev, "Failed to lock command mutex, error = %d", error);
	    return error;
    }

    haptic_data = pcu->haptic_data;
    mutex_unlock (& pcu->cmd_mutex);

    if (!haptic_data.autocal_data_valid) {
        return -EINVAL;
    }

	return scnprintf (buf, PAGE_SIZE,
	    "BEMF gain: 0x%02X\nComp: 0x%02X\nBEMF: 0x%02X\n",
	    haptic_data.autocal_data.bemf_gain,
	    haptic_data.autocal_data.comp,
	    haptic_data.autocal_data.bemf);
}


static ssize_t zii_pcu_t2_haptic_autocal_store (struct device *dev,
                struct device_attribute *dattr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface (dev);
	struct ims_pcu *pcu = usb_get_intfdata (intf);
	struct autocal *autocal_data_received;
	u8  autocal_status;
    u8  autocal_mode;
	int input;
    int error = 0;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

    /* Get input and check its validity */
    if (sscanf (buf, "%d", & input) != 1 || sscanf (buf, "%d\n", & input) != 1)
    {
        return -EINVAL;
    }

    if (input < 0 || input > 1) {
        return -EINVAL;
    }

    /* All good... Execute auto calibration */
    autocal_mode = (u8) input;
    dev_dbg (pcu->dev, "Haptic calibration mode: %d", autocal_mode);
    error = mutex_lock_interruptible (& pcu->cmd_mutex);
	if (error)
	    goto err_haptic_autocal_unlock_mutex;

    error = ims_pcu_execute_command (pcu,
                CALIBRATE_HAPTIC, & autocal_mode, sizeof (autocal_mode));
    if (error) {
        dev_err (pcu->dev,
            "Failed to calibrate haptic device: mode = %d, error = %d",
            autocal_mode, error);
        goto err_haptic_autocal_unlock_mutex;
    }

    /* check the response received for errors */
    autocal_status = pcu->cmd_buf[2];
    autocal_data_received = (struct autocal *) & pcu->cmd_buf[3];
    if (autocal_status) {
        error = -EINVAL;
        goto err_haptic_autocal_unlock_mutex;
    }

    /* No errors... If mode was 0 (i.e., invalid previous calibration),
     * invalidate the cal data. Otherwise, copy calibration data and
     * make it valid
     */
    if (autocal_mode == 0) {
        pcu->haptic_data.autocal_data.bemf_gain = autocal_data_received->bemf_gain;
        pcu->haptic_data.autocal_data.comp = autocal_data_received->comp;
        pcu->haptic_data.autocal_data.bemf = autocal_data_received->bemf;
        pcu->haptic_data.autocal_data_valid = true;
    } else {
        pcu->haptic_data.autocal_data.bemf_gain = 0;
        pcu->haptic_data.autocal_data.comp = 0;
        pcu->haptic_data.autocal_data.bemf = 0;
        pcu->haptic_data.autocal_data_valid = false;
    }

err_haptic_autocal_unlock_mutex:

	mutex_unlock (& pcu->cmd_mutex);

    return error ? error : count;
}

/****************************************************
 * Display the programmed haptic event table
 *
 * There is one line for each button:
 * bbb ppp rrr
 * Where:
 *  bbb is button index
 *  ppp is event to play on button pressed / switch closed
 *  rrr is event to play on button released / switch  opened
 */

static ssize_t zii_pcu_t2_programmed_event_show (struct device *dev,
                    struct device_attribute *dattr, char *buf)
{
	struct usb_interface *intf = to_usb_interface (dev);
	struct ims_pcu *pcu = usb_get_intfdata (intf);
    ZII_PCU_T2_PROGRAMMED_EVENTS *haptic_ev;
    int error = 0;
    int count = 0;
    int i;
    int n=0;
    char *p;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

    error = mutex_lock_interruptible (& pcu->cmd_mutex);
	if (error) {
        dev_err (pcu->dev, "Failed to lock command mutex, error = %d", error);
	    return error;
    }

    error = ims_pcu_execute_query(pcu, GET_EVENT_ACTION);
	if (error) {
	    goto err_programmed_event_unlock_mutex;
    }

    haptic_ev = (ZII_PCU_T2_PROGRAMMED_EVENTS*)
                    &pcu->cmd_buf[IMS_PCU_INFO_PART_OFFSET];

    /* print the table one line at a time */
    for(i=0, p=buf; i<BT_NUM_BUTTONS; i++)
    {
        n = scnprintf(p, PAGE_SIZE-count, " %3d %3d %3d\n", i,
            haptic_ev->events[i].Pressed, haptic_ev->events[i].Released);
        p += n;
        count += n;
    }

err_programmed_event_unlock_mutex:

	mutex_unlock (& pcu->cmd_mutex);

    return error ? error : count;
}

/****************************************************
 * Set new programmed haptic event for one button
 *
 * Format for request:
 * bbb ppp rrr
 * Where:
 *  bbb is button index  (0..BT_NUM_BUTTONS-1)
 *  ppp is event to play on button pressed / switch closed  (0..126)
 *  rrr is event to play on button released / switch  opened  (0..126)
 */

static ssize_t zii_pcu_t2_programmed_event_store (struct device *dev,
                struct device_attribute *dattr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface (dev);
	struct ims_pcu *pcu = usb_get_intfdata (intf);
    ZII_PCU_T2_PROGRAMMED_EVENTS haptic_ev;
    int error = 0;
    int index;
    int press_code;
    int release_code;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

    /* check for valid request */
    if (sscanf (buf, "%d %d %d", &index, &press_code, &release_code) != 3 ||
        sscanf (buf, "%d %d %d\n", &index, &press_code, &release_code) != 3) {
            return -EINVAL;
    }

    if((index < 0) || (index >= BT_NUM_BUTTONS))   return -EINVAL;
    if((press_code < 0) || (press_code > 126))     return -EINVAL;
    if((release_code < 0) || (release_code > 126)) return -EINVAL;

    /* read the current table */

    error = mutex_lock_interruptible (& pcu->cmd_mutex);
	if (error) {
        dev_err (pcu->dev, "Failed to lock command mutex, error = %d", error);
	    return error;
    }

    error = ims_pcu_execute_query(pcu, GET_EVENT_ACTION);
	if (error) {
        dev_err(pcu->dev, "Failed to read programmed events table, error = %d", error);
	    goto err_programmed_event_unlock_mutex;
    }

    memcpy(&haptic_ev,  &pcu->cmd_buf[IMS_PCU_INFO_PART_OFFSET], sizeof(haptic_ev));

    /* set new events */
    haptic_ev.events[index].Pressed =  press_code;
    haptic_ev.events[index].Released = release_code;

    dev_dbg(pcu->dev, "button %d play %d on press, %d on release", index,
                        haptic_ev.events[index].Pressed,
                        haptic_ev.events[index].Released);

    /* write it back out */
    error = ims_pcu_execute_command (pcu,
                SET_EVENT_ACTION, &haptic_ev, sizeof (haptic_ev));
    if (error) {
        dev_err (pcu->dev,
            "Failed to set new haptic event, error = %d", error);
    }

err_programmed_event_unlock_mutex:

	mutex_unlock (& pcu->cmd_mutex);

    return error ? error : count;
}

/******************************************************
 * Send a display action command - no data
 *
 * pcu    - pointer to pcu struct
 * action - OLED action to perform
 * param  - parameter if needed
 */

static int zii_pcu_execute_display_action(struct ims_pcu *pcu, int action, u8 param)
{
    ZII_PCU_T2_DISPLAY_INFO display_info;

    memset(&display_info, 0, sizeof(display_info));
    display_info.action = action;

    if(action == ZII_PCU_T2_DISPLAY_ACTION_SET_CONTRAST) {
        display_info.data[0] = param;
    }

    dev_dbg(pcu->dev, "+++ %s %d, param %d", __func__, action, param);

    return ims_pcu_execute_command(pcu, SET_DISPLAY_DATA, &display_info, sizeof(display_info));
}


/******************************************************
 * Write Display Data to OLED
 *
 * pcu   - pointer to pcu struct
 * pdata - pointer to display data
 */

static int zii_pcu_execute_write_display_data(struct ims_pcu *pcu, u8 *pdata)
{
    ZII_PCU_T2_DISPLAY_INFO display_info;
    int error = 0;
    int i;
    int page;
    int seg;

    for(i = 0; i < ZII_PCU_T2_DISPLAY_NUM_SEGMENTS; i++) {
        memset(&display_info, 0, sizeof(display_info));
        page = i/4;
        seg = i%4;
        display_info.action =
            (i == (ZII_PCU_T2_DISPLAY_NUM_SEGMENTS-1) ?
            ZII_PCU_T2_DISPLAY_ACTION_STORE_AND_WRITE :
            ZII_PCU_T2_DISPLAY_ACTION_STORE_SEGMENT);
        display_info.page = (u8)page;
        display_info.segment = (u8)seg;
        memcpy(&display_info.data, pdata, ZII_PCU_T2_DISPLAY_SEGMENT_SIZE);
        pdata += ZII_PCU_T2_DISPLAY_SEGMENT_SIZE;

        dev_dbg(pcu->dev, "+++ %s page %d seg %d", __func__, page, seg);

        error = ims_pcu_execute_command(pcu, SET_DISPLAY_DATA,
                                            &display_info, sizeof(display_info));

        if(error) {
            dev_err(pcu->dev, "+++ %s Error writing display data %d", __func__, error);
            return error;
        }
   }

   return error;

}

/******************************************************
 * Setup Display - Initialize it to off
 *
 */

static int zii_pcu_t2_setup_display(struct ims_pcu *pcu)
{
    int error = 0;

    memset(&pcu->display, 0, sizeof(pcu->display));

    if(pcu->bootloader_mode == ZII_PCU_T2_APPLICATION_MODE) {
        error = zii_pcu_execute_display_action(pcu, ZII_PCU_T2_DISPLAY_ACTION_DISABLE_DISPLAY, 0);
        if(error)
            return error;

        error = zii_pcu_execute_display_action(pcu, ZII_PCU_T2_DISPLAY_ACTION_SET_CONTRAST, 0);
        if(error)
            return error;

        error = zii_pcu_execute_write_display_data(pcu, &pcu->display.data[0]);
    }

    return error;

}

/******************************************************
 * Destroy display
 *
 */

static int zii_pcu_t2_destroy_display(struct ims_pcu *pcu)
{

    return (zii_pcu_t2_setup_display(pcu));
}


static ssize_t zii_pcu_t2_display_invert_show (struct device *dev,
        struct device_attribute *dattr, char *buf)
{
    struct usb_interface *intf = to_usb_interface (dev);
    struct ims_pcu *pcu = usb_get_intfdata (intf);
    int error = 0;
    int invert;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

    error = mutex_lock_interruptible (& pcu->cmd_mutex);
	if (error) {
	    dev_err (pcu->dev, "Failed to lock command mutex, error = %d", error);
	    return error;
    }

    invert = pcu->display.invert;
    mutex_unlock (& pcu->cmd_mutex);

    return scnprintf (buf, PAGE_SIZE, "%u\n", invert);

}

static ssize_t zii_pcu_t2_display_invert_store (struct device *dev,
        struct device_attribute *dattr, const char *buf, size_t count)
{
    struct usb_interface *intf = to_usb_interface (dev);
    struct ims_pcu *pcu = usb_get_intfdata (intf);
    int error = 0;
    int input;
    int action;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

    /* Get input and check its validity */
    if (sscanf (buf, "%d", & input) != 1 || sscanf (buf, "%d\n", & input) != 1)
    {
        return -EINVAL;
    }

    if (input < 0 || input > 1) {
        return -EINVAL;
    }

    error = mutex_lock_interruptible (& pcu->cmd_mutex);
	if (error) {
	    dev_err (pcu->dev, "Failed to lock command mutex, error = %d", error);
	    return error;
    }

    pcu->display.invert = (u8)input;
    action = pcu->display.invert ?
            ZII_PCU_T2_DISPLAY_ACTION_ENABLE_INVERT :
            ZII_PCU_T2_DISPLAY_ACTION_DISABLE_INVERT;

    error = zii_pcu_execute_display_action(pcu, action, 0);

    mutex_unlock (& pcu->cmd_mutex);

    return error ? error : count;
}


static ssize_t zii_pcu_t2_display_brightness_show (struct device *dev,
        struct device_attribute *dattr, char *buf)
{
    struct usb_interface *intf = to_usb_interface (dev);
    struct ims_pcu *pcu = usb_get_intfdata (intf);
    int error = 0;
    int brightness;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

    error = mutex_lock_interruptible (& pcu->cmd_mutex);
    if (error) {
        dev_err (pcu->dev, "Failed to lock command mutex, error = %d", error);
        return error;
    }

    brightness =  pcu->display.brightness;
    mutex_unlock (& pcu->cmd_mutex);

    return scnprintf (buf, PAGE_SIZE, "%u\n", brightness);

}

static ssize_t zii_pcu_t2_display_brightness_store (struct device *dev,
        struct device_attribute *dattr, const char *buf, size_t count)
{
    struct usb_interface *intf = to_usb_interface (dev);
    struct ims_pcu *pcu = usb_get_intfdata (intf);
    int error = 0;
    int input;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

    /* Get input and check its validity */
    if (sscanf (buf, "%d", & input) != 1 || sscanf (buf, "%d\n", & input) != 1)
    {
        return -EINVAL;
    }

    if (input < 0 || input > 255) {
        return -EINVAL;
    }

    error = mutex_lock_interruptible (& pcu->cmd_mutex);
	if (error) {
	    dev_err (pcu->dev, "Failed to lock command mutex, error = %d", error);
	    return error;
    }

    pcu->display.brightness = (u8)input;
    if(pcu->display.brightness == 0) {
        /* disable the display */
        error = zii_pcu_execute_display_action(pcu,
                ZII_PCU_T2_DISPLAY_ACTION_DISABLE_DISPLAY, 0);
    } else {
        /* write new brightness value and enable display */
        error = zii_pcu_execute_display_action(pcu,
                ZII_PCU_T2_DISPLAY_ACTION_SET_CONTRAST, pcu->display.brightness);
        if(error == 0) {
            error = zii_pcu_execute_display_action(pcu,
                    ZII_PCU_T2_DISPLAY_ACTION_ENABLE_DISPLAY, 0);
        }
    }

    mutex_unlock (& pcu->cmd_mutex);

    return count;
}

static ssize_t zii_pcu_t2_display_data_store (struct device *dev,
        struct device_attribute *dattr, const char *buf, size_t count)
{
    struct usb_interface *intf = to_usb_interface (dev);
    struct ims_pcu *pcu = usb_get_intfdata (intf);
    int error = 0;
    int bytes_to_copy;
    int space_left;

    error = mutex_lock_interruptible (& pcu->cmd_mutex);
	if (error) {
	    dev_err (pcu->dev, "Failed to lock command mutex, error = %d", error);
	    return error;
    }

    /* append to buffer but don't overfill */
    space_left = ZII_PCU_T2_DISPLAY_DATA_SIZE-pcu->display.count;
    bytes_to_copy = min(space_left, (int)count);

    dev_dbg(pcu->dev,  "+++ %s count = %zd space = %d copy = %d", __func__,
        count, space_left, bytes_to_copy);

    if(bytes_to_copy != count)
    {
        dev_err(pcu->dev,  "+++ %s display buffer truncated", __func__);
    }
    memcpy(&pcu->display.data[pcu->display.count], buf, bytes_to_copy);
    pcu->display.count += bytes_to_copy;

    dev_dbg (pcu->dev,  "+++ %s total = %d / %d", __func__,
        (int)pcu->display.count, (int)ZII_PCU_T2_DISPLAY_DATA_SIZE);

    if((int)pcu->display.count >= (int)ZII_PCU_T2_DISPLAY_DATA_SIZE)
    {
        dev_dbg(pcu->dev, "+++ %s writing to display", __func__);
        error = zii_pcu_execute_write_display_data(pcu, &pcu->display.data[0]);
        if(error)
            return error;

        /* clear display buffer for next image */
        pcu->display.count = 0;
        memset(&pcu->display.data, 0, sizeof(pcu->display.data));
    }

    mutex_unlock (& pcu->cmd_mutex);
    return count;

}


/******************************************************************
 * Show/store methods for application_mode sysfs attribute
 */
static ssize_t zii_pcu_t2_app_mode_show (struct device *dev,
		struct device_attribute *dattr, char *buf)
{
	struct usb_interface *intf = to_usb_interface (dev);
	struct ims_pcu *pcu = usb_get_intfdata (intf);
	int error = 0;
	int app_mode = 0;

	dev_dbg (pcu->dev, "+++ %s", __func__);

	error = mutex_lock_interruptible (& pcu->cmd_mutex);
	if (error) {
		dev_err (pcu->dev, "Failed to lock command mutex, error = %d", error);
		return error;
	}

	app_mode =  pcu->bootloader_mode ? 0 : 1;
	mutex_unlock (& pcu->cmd_mutex);

	return scnprintf (buf, PAGE_SIZE, "%u\n", app_mode);
}


static ssize_t zii_pcu_t2_app_mode_store (struct device *dev,
		struct device_attribute *dattr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface (dev);
	struct ims_pcu *pcu = usb_get_intfdata (intf);
	int error = 0;
	int app_mode = 0;

	dev_dbg (pcu->dev, "+++ %s", __func__);

	/* Get the data from the input */
	if (count == 0 ||
		sscanf (buf, "%d", & app_mode) != 1 ||
		sscanf (buf, "%d\n", & app_mode) != 1)
	{
		return -EINVAL;
	}

	if (app_mode < 0 || app_mode > 1) {
		return -EINVAL;
	}

	error = mutex_lock_interruptible (& pcu->cmd_mutex);
	if (error) {
		dev_err (pcu->dev, "Failed to lock command mutex, error = %d", error);
		return error;
	}

	/* Check if the device is app mode and needs to go to booloader ... */
	if (!pcu->bootloader_mode && app_mode == 0) {
		dev_info (pcu->dev, "Switching from app to bootloader mode");
		error = ims_pcu_switch_to_bootloader (pcu);
	} else {
		/* Check if in bootlaoder mode and PCU needs to jump to app mode */
		if (pcu->bootloader_mode && app_mode == 1) {

			u8 processor = FW_UPDATE_PROC_ID_MAIN;

			dev_info (pcu->dev, "Switching from booloader to app mode");
			error = ims_pcu_execute_bl_command (pcu,
				LAUNCH_APP, & processor, sizeof (processor), 5000);
		}
	}
	mutex_unlock (& pcu->cmd_mutex);

	if (error) {
		dev_err (pcu->dev,
			"Setting app/bootloader mode failed, error = %d", error);
	}

	return error != 0 ? -EFAULT : count;
}


static DEVICE_ATTR (haptic_play, S_IWUSR, NULL, zii_pcu_t2_haptic_play_store);
static DEVICE_ATTR (haptic_autocal, S_IRUSR | S_IWUSR,
            zii_pcu_t2_haptic_autocal_show, zii_pcu_t2_haptic_autocal_store);
static DEVICE_ATTR (haptic_programmed_event, S_IRUSR | S_IWUSR,
            zii_pcu_t2_programmed_event_show,
            zii_pcu_t2_programmed_event_store);
static DEVICE_ATTR (display_brightness, S_IRUSR | S_IWUSR,
            zii_pcu_t2_display_brightness_show, zii_pcu_t2_display_brightness_store);
static DEVICE_ATTR (display_data, S_IWUSR,
            NULL, zii_pcu_t2_display_data_store);
static DEVICE_ATTR(display_invert,  S_IRUSR | S_IWUSR,
             zii_pcu_t2_display_invert_show, zii_pcu_t2_display_invert_store);
static DEVICE_ATTR (application_mode, S_IRUSR | S_IWUSR,
			zii_pcu_t2_app_mode_show, zii_pcu_t2_app_mode_store);


static struct attribute *ims_pcu_attrs[] = {
	&ims_pcu_attr_part_number.dattr.attr,
	&ims_pcu_attr_serial_number.dattr.attr,
	&ims_pcu_attr_date_of_manufacturing.dattr.attr,
	&ims_pcu_attr_fw_version_main.dattr.attr,
	&ims_pcu_attr_fw_version_touchpad.dattr.attr,
	&ims_pcu_attr_bl_version_main.dattr.attr,
	&ims_pcu_attr_bl_version_touchpad.dattr.attr,
	&ims_pcu_attr_reset_reason.dattr.attr,
	&dev_attr_reset_device.attr,
	&dev_attr_application_mode.attr,
	&dev_attr_update_firmware.attr,
	&dev_attr_update_firmware_status.attr,
	&dev_attr_mode.attr,
	&dev_attr_haptic_play.attr,
	&dev_attr_haptic_autocal.attr,
    &dev_attr_haptic_programmed_event.attr,
    &dev_attr_display_invert.attr,
    &dev_attr_display_brightness.attr,
    &dev_attr_display_data.attr,
	NULL
};

static umode_t ims_pcu_is_attr_visible(struct kobject *kobj,
				       struct attribute *attr, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct usb_interface *intf = to_usb_interface(dev);
	struct ims_pcu *pcu = usb_get_intfdata(intf);
	umode_t mode = attr->mode;

	if (pcu->bootloader_mode) {
		if (attr != &dev_attr_update_firmware_status.attr &&
		    attr != &dev_attr_update_firmware.attr &&
		    attr != &dev_attr_reset_device.attr &&
			attr != &dev_attr_application_mode.attr &&
		    attr != &ims_pcu_attr_bl_version_main.dattr.attr &&
		    attr != &ims_pcu_attr_bl_version_touchpad.dattr.attr) {
			mode = 0;
		}
	} else {
		if (attr == &dev_attr_update_firmware_status.attr ||
			attr == &dev_attr_update_firmware.attr)
		{
			mode = 0;
		}
	}

	return mode;
}

static struct attribute_group ims_pcu_attr_group = {
	.is_visible	= ims_pcu_is_attr_visible,
	.attrs		= ims_pcu_attrs,
};

static void ims_pcu_irq(struct urb *urb)
{
	struct ims_pcu *pcu = urb->context;
	int retval, status;


	status = urb->status;

	switch (status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(pcu->dev, "%s - urb shutting down with status: %d\n",
			__func__, status);
		return;
	default:
		dev_dbg(pcu->dev, "%s - nonzero urb status received: %d\n",
			__func__, status);
		goto exit;
	}

	if (urb == pcu->urb_in)
		ims_pcu_process_data(pcu, urb);

exit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval && retval != -ENODEV)
		dev_err(pcu->dev, "%s - usb_submit_urb failed with result %d\n",
			__func__, retval);
}

static int ims_pcu_buffers_alloc(struct ims_pcu *pcu)
{
	int error;

	dev_dbg (pcu->dev,  "+++ %s", __func__);

	pcu->urb_in_buf = usb_alloc_coherent(pcu->udev, pcu->max_in_size,
					     GFP_KERNEL, &pcu->read_dma);
	if (!pcu->urb_in_buf) {
		dev_err(pcu->dev,
			"Failed to allocate memory for read buffer\n");
		return -ENOMEM;
	}

	pcu->urb_in = usb_alloc_urb(0, GFP_KERNEL);
	if (!pcu->urb_in) {
		dev_err(pcu->dev, "Failed to allocate input URB\n");
		error = -ENOMEM;
		goto err_free_urb_in_buf;
	}

	pcu->urb_in->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	pcu->urb_in->transfer_dma = pcu->read_dma;

	usb_fill_bulk_urb(pcu->urb_in, pcu->udev,
			  usb_rcvbulkpipe(pcu->udev,
					  pcu->ep_in->bEndpointAddress),
			  pcu->urb_in_buf, pcu->max_in_size,
			  ims_pcu_irq, pcu);

	/*
	 * We are using usb_bulk_msg() for sending so there is no point
	 * in allocating memory with usb_alloc_coherent().
	 */
	pcu->urb_out_buf = kmalloc(pcu->max_out_size, GFP_KERNEL);
	if (!pcu->urb_out_buf) {
		dev_err(pcu->dev, "Failed to allocate memory for write buffer\n");
		error = -ENOMEM;
		goto err_free_in_urb;
	}

	pcu->urb_ctrl_buf = usb_alloc_coherent(pcu->udev, pcu->max_ctrl_size,
					       GFP_KERNEL, &pcu->ctrl_dma);
	if (!pcu->urb_ctrl_buf) {
		dev_err(pcu->dev,
			"Failed to allocate memory for read buffer\n");
		goto err_free_urb_out_buf;
	}

	pcu->urb_ctrl = usb_alloc_urb(0, GFP_KERNEL);
	if (!pcu->urb_ctrl) {
		dev_err(pcu->dev, "Failed to allocate input URB\n");
		error = -ENOMEM;
		goto err_free_urb_ctrl_buf;
	}

	pcu->urb_ctrl->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	pcu->urb_ctrl->transfer_dma = pcu->ctrl_dma;

	usb_fill_int_urb(pcu->urb_ctrl, pcu->udev,
			  usb_rcvintpipe(pcu->udev,
					 pcu->ep_ctrl->bEndpointAddress),
			  pcu->urb_ctrl_buf, pcu->max_ctrl_size,
			  ims_pcu_irq, pcu, pcu->ep_ctrl->bInterval);

	return 0;

err_free_urb_ctrl_buf:
	usb_free_coherent(pcu->udev, pcu->max_ctrl_size,
			  pcu->urb_ctrl_buf, pcu->ctrl_dma);
err_free_urb_out_buf:
	kfree(pcu->urb_out_buf);
err_free_in_urb:
	usb_free_urb(pcu->urb_in);
err_free_urb_in_buf:
	usb_free_coherent(pcu->udev, pcu->max_in_size,
			  pcu->urb_in_buf, pcu->read_dma);

	return error;
}

static void ims_pcu_buffers_free(struct ims_pcu *pcu)
{
	usb_kill_urb(pcu->urb_in);
	usb_free_urb(pcu->urb_in);

	usb_free_coherent(pcu->udev, pcu->max_out_size,
			  pcu->urb_in_buf, pcu->read_dma);

	kfree(pcu->urb_out_buf);

	usb_kill_urb(pcu->urb_ctrl);
	usb_free_urb(pcu->urb_ctrl);

	usb_free_coherent(pcu->udev, pcu->max_ctrl_size,
			  pcu->urb_ctrl_buf, pcu->ctrl_dma);
}

static const struct usb_cdc_union_desc *
ims_pcu_get_cdc_union_desc(struct usb_interface *intf)
{
	const void *buf = intf->altsetting->extra;
	size_t buflen = intf->altsetting->extralen;
	struct usb_cdc_union_desc *union_desc;

	if (!buf) {
		dev_err(&intf->dev, "Missing descriptor data\n");
		return NULL;
	}

	if (!buflen) {
		dev_err(&intf->dev, "Zero length descriptor\n");
		return NULL;
	}

	while (buflen > 0) {
		union_desc = (struct usb_cdc_union_desc *)buf;

		if (union_desc->bDescriptorType == USB_DT_CS_INTERFACE &&
		    union_desc->bDescriptorSubType == USB_CDC_UNION_TYPE) {
			dev_dbg(&intf->dev, "Found union header\n");

			return union_desc;
		}

		buflen -= union_desc->bLength;
		buf += union_desc->bLength;
	}

	dev_err(&intf->dev, "Missing CDC union descriptor\n");
	return NULL;
}

static int ims_pcu_parse_cdc_data(struct usb_interface *intf, struct ims_pcu *pcu)
{
	const struct usb_cdc_union_desc *union_desc;
	struct usb_host_interface *alt;

	dev_dbg (pcu->dev,  "+++ %s", __func__);

	union_desc = ims_pcu_get_cdc_union_desc(intf);
	if (!union_desc)
		return -EINVAL;

	pcu->ctrl_intf = usb_ifnum_to_if(pcu->udev,
					 union_desc->bMasterInterface0);

	alt = pcu->ctrl_intf->cur_altsetting;
	pcu->ep_ctrl = &alt->endpoint[0].desc;
	pcu->max_ctrl_size = usb_endpoint_maxp(pcu->ep_ctrl);

	pcu->data_intf = usb_ifnum_to_if(pcu->udev,
					 union_desc->bSlaveInterface0);

	alt = pcu->data_intf->cur_altsetting;
	if (alt->desc.bNumEndpoints != 2) {
		dev_err(pcu->dev,
			"Incorrect number of endpoints on data interface (%d)\n",
			alt->desc.bNumEndpoints);
		return -EINVAL;
	}

	pcu->ep_out = &alt->endpoint[1].desc;
	if (!usb_endpoint_is_bulk_out(pcu->ep_out)) {
		dev_err(pcu->dev,
			"First endpoint on data interface is not BULK OUT\n");
		return -EINVAL;
	}

	pcu->max_out_size = usb_endpoint_maxp(pcu->ep_out);
	if (pcu->max_out_size < 8) {
		dev_err(pcu->dev,
			"Max OUT packet size is too small (%zd)\n",
			pcu->max_out_size);
		return -EINVAL;
	}

	pcu->ep_in = &alt->endpoint[0].desc;
	if (!usb_endpoint_is_bulk_in(pcu->ep_in)) {
		dev_err(pcu->dev,
			"Second endpoint on data interface is not BULK IN\n");
		return -EINVAL;
	}

	pcu->max_in_size = usb_endpoint_maxp(pcu->ep_in);
	if (pcu->max_in_size < 8) {
		dev_err(pcu->dev,
			"Max IN packet size is too small (%zd)\n",
			pcu->max_in_size);
		return -EINVAL;
	}

	return 0;
}

static int ims_pcu_start_io(struct ims_pcu *pcu)
{
	int error;

	dev_dbg (pcu->dev,  "+++ %s", __func__);

	error = usb_submit_urb(pcu->urb_ctrl, GFP_KERNEL);
	if (error) {
		dev_err(pcu->dev,
			"Failed to start control IO - usb_submit_urb failed with result: %d\n",
			error);
		return -EIO;
	}

	error = usb_submit_urb(pcu->urb_in, GFP_KERNEL);
	if (error) {
		dev_err(pcu->dev,
			"Failed to start IO - usb_submit_urb failed with result: %d\n",
			error);
		usb_kill_urb(pcu->urb_ctrl);
		return -EIO;
	}

	return 0;
}

static void ims_pcu_stop_io(struct ims_pcu *pcu)
{
	usb_kill_urb(pcu->urb_in);
	usb_kill_urb(pcu->urb_ctrl);
}

static int ims_pcu_line_setup(struct ims_pcu *pcu)
{
	struct usb_host_interface *interface = pcu->ctrl_intf->cur_altsetting;
	struct usb_cdc_line_coding *line = (void *)pcu->cmd_buf;
	int error;

	dev_dbg (pcu->dev,  "+++ %s", __func__);

	memset(line, 0, sizeof(*line));
	line->dwDTERate = cpu_to_le32(57600);
	line->bDataBits = 8;

	error = usb_control_msg(pcu->udev, usb_sndctrlpipe(pcu->udev, 0),
				USB_CDC_REQ_SET_LINE_CODING,
				USB_TYPE_CLASS | USB_RECIP_INTERFACE,
				0, interface->desc.bInterfaceNumber,
				line, sizeof(struct usb_cdc_line_coding),
				5000);
	if (error < 0) {
		dev_err(pcu->dev, "Failed to set line coding, error: %d\n",
			error);
		return error;
	}

	error = usb_control_msg(pcu->udev, usb_sndctrlpipe(pcu->udev, 0),
				USB_CDC_REQ_SET_CONTROL_LINE_STATE,
				USB_TYPE_CLASS | USB_RECIP_INTERFACE,
				0x03, interface->desc.bInterfaceNumber,
				NULL, 0, 5000);
	if (error < 0) {
		dev_err(pcu->dev, "Failed to set line state, error: %d\n",
			error);
		return error;
	}

	dev_dbg (pcu->dev,  "+++ %s", __func__);

	return 0;
}

static int ims_pcu_get_device_info(struct ims_pcu *pcu)
{
	int error;
	char print_buf[IMS_PCU_BUF_SIZE];
	uint16_t ver_major;
	uint8_t ver_minor;

	dev_dbg (pcu->dev,  "+++ %s", __func__);

	error = ims_pcu_get_info(pcu);
	if (error)
		return error;

    /* Get firmware revisions */
	error = ims_pcu_execute_query(pcu, GET_FW_VERSION);
	if (error) {
		dev_err(pcu->dev,
			"GET_FW_VERSION command failed, error: %d\n", error);
		return error;
	}

    ver_major = pcu->cmd_buf[3] << 8 | pcu->cmd_buf[4];
    ver_minor = pcu->cmd_buf[5];
    snprintf (pcu->fw_version_main,
        sizeof(pcu->fw_version_main), "02%02d%02d", ver_major, ver_minor);

    ver_major = pcu->cmd_buf[6] << 8 | pcu->cmd_buf[7];
    ver_minor = pcu->cmd_buf[8];
    snprintf(pcu->fw_version_touchpad,
        sizeof(pcu->fw_version_touchpad), "02%02d%02d", ver_major, ver_minor);

    /* Get bootloader revisions as well */
	error = ims_pcu_execute_query(pcu, GET_BL_VERSION);
	if (error) {
		dev_err(pcu->dev,
			"GET_BL_VERSION command failed, error: %d\n", error);
		return error;
	}

    ver_major = pcu->cmd_buf[3] << 8 | pcu->cmd_buf[4];
    ver_minor = pcu->cmd_buf[5];
	snprintf(pcu->bl_version_main, sizeof(pcu->bl_version_main),
		 "01%02d%02d", ver_major, ver_minor);

    ver_major = pcu->cmd_buf[6] << 8 | pcu->cmd_buf[7];
    ver_minor = pcu->cmd_buf[8];
	snprintf(pcu->bl_version_touchpad, sizeof(pcu->bl_version_touchpad),
		 "01%02d%02d", ver_major, ver_minor);

    /* Check what was the reason for last reset */
	error = ims_pcu_execute_query(pcu, RESET_REASON);
	if (error) {
		dev_err(pcu->dev,
			"RESET_REASON command failed, error: %d\n", error);
		return error;
	}

	snprintf(pcu->reset_reason, sizeof(pcu->reset_reason),
		 "%02x", pcu->cmd_buf[IMS_PCU_DATA_OFFSET]);

    memset (print_buf, 0, sizeof (print_buf));
    strncpy (print_buf, pcu->part_number, sizeof (pcu->part_number));
    dev_info (pcu->dev, "Part number: %s", print_buf);

    memset (print_buf, 0, sizeof (print_buf));
    strncpy (print_buf,
        pcu->date_of_manufacturing, sizeof (pcu->date_of_manufacturing));
    dev_info (pcu->dev, "Manufacturing date: %s", print_buf);

    memset (print_buf, 0, sizeof (print_buf));
    strncpy (print_buf, pcu->serial_number, sizeof (pcu->serial_number));
    dev_info (pcu->dev, "Serial number: %s", print_buf);

	dev_info (pcu->dev, "Main firmware: %s", pcu->fw_version_main);
	dev_info (pcu->dev, "Main bootloader: %s", pcu->bl_version_main);
	dev_info (pcu->dev, "Touchpad firmware: %s", pcu->fw_version_touchpad);
	dev_info (pcu->dev, "Touchpad bootlaoder: %s", pcu->bl_version_touchpad);
	dev_info (pcu->dev, "Reset reason: %s", pcu->reset_reason);

	return 0;
}

static int ims_pcu_identify_type(struct ims_pcu *pcu, u8 *device_id)
{
	int error;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

    /* Get Device ID ... */
	error = ims_pcu_execute_query(pcu, GET_DEVICE_ID);
	if (error) {
		dev_err(pcu->dev,
			"GET_DEVICE_ID command failed, error: %d\n", error);
		return error;
	}

	*device_id = pcu->cmd_buf[2];

    /* Check if the range is ok ... */
    if (*device_id > 1) {
        dev_err (pcu->dev, "Invalid device ID detected: %d", *device_id);
        return -EINVAL;
    }

	dev_dbg (pcu->dev, "Detected device ID: %d\n", *device_id);

	return 0;
}

static int ims_pcu_init_application_mode(struct ims_pcu *pcu)
{
	static atomic_t device_no = ATOMIC_INIT(0);

	const struct ims_pcu_device_info *info;
	u8 device_id;
	u8 enable_button_events = 1;
	int error;

	dev_dbg (pcu->dev,  "+++ %s", __func__);

	error = ims_pcu_get_device_info(pcu);
	if (error) {
		/* Device does not respond to basic queries, hopeless */
		return error;
	}

	error = ims_pcu_identify_type(pcu, &device_id);
	if (error) {
		dev_err(pcu->dev,
			"Failed to identify device, error: %d\n", error);
		/*
		 * Do not signal error, but do not create input nor
		 * backlight devices either, let userspace figure this
		 * out (flash a new firmware?).
		 */
		return 0;
	}


	if (device_id >= ARRAY_SIZE(ims_pcu_device_info) ||
	    !ims_pcu_device_info[device_id].keymap) {
		dev_err(pcu->dev, "Device ID %d is not valid\n", device_id);
        /* Same as above, punt to userspace */
		return 0;
	}

	/* Device appears to be operable, complete initialization */
	pcu->device_no = atomic_inc_return(&device_no) - 1;

	error = zii_pcu_t2_setup_backlight(pcu);
	if (error)
		return error;

    error = zii_pcu_t2_setup_display(pcu);
    if (error)
        goto err_destroy_display;

	info = &ims_pcu_device_info[device_id];
	error = ims_pcu_setup_buttons(pcu, info->keymap, info->keymap_len);
	if (error)
		goto err_destroy_backlight;

    /* Game pad is always present, but gamepad events will go
     * up to the application only if PCU is put in gaming mode.
     */
	error = ims_pcu_setup_gamepad (pcu);
	if (error)
		goto err_destroy_buttons;

    pcu->mode = ZII_PCU_T2_MODE_NORMAL;
    mutex_lock (& pcu->cmd_mutex);
    error =
        ims_pcu_execute_command (pcu, SET_MODE, & pcu->mode, sizeof (pcu->mode));

    mutex_unlock (& pcu->cmd_mutex);
    if (error)
        goto err_destroy_buttons;

    /* Almost done ... tell PCU that it is ok to start sending any
     * button presses.
     */
    mutex_lock (& pcu->cmd_mutex);
    error = ims_pcu_execute_command (pcu,
        SEND_BUTTONS, & enable_button_events, sizeof (enable_button_events));
    mutex_unlock (& pcu->cmd_mutex);
    if (error)
        goto err_destroy_buttons;

    /* PCU is now setup */
	pcu->setup_complete = true;

	return 0;

err_destroy_backlight:
	zii_pcu_t2_destroy_backlight(pcu);
err_destroy_buttons:
	ims_pcu_destroy_buttons(pcu);
err_destroy_display:
    zii_pcu_t2_destroy_display(pcu);

	return error;
}

static void ims_pcu_destroy_application_mode(struct ims_pcu *pcu)
{
    dev_dbg (pcu->dev,  "+++ %s", __func__);

	if (pcu->setup_complete) {
		pcu->setup_complete = false;
		mb(); /* make sure flag setting is not reordered */

		ims_pcu_destroy_gamepad(pcu);
		ims_pcu_destroy_buttons(pcu);
		zii_pcu_t2_destroy_backlight(pcu);
        zii_pcu_t2_destroy_display(pcu);
	}
}

static int ims_pcu_init_bootloader_mode(struct ims_pcu *pcu)
{
	int error;
    uint8_t blver_minor;
    uint16_t blver_major;

    dev_dbg (pcu->dev,  "+++ %s", __func__);

	error = ims_pcu_execute_bl_command(pcu, QUERY_DEVICE, NULL, 0,
					   IMS_PCU_CMD_RESPONSE_TIMEOUT);
	if (error) {
		dev_err(pcu->dev, "Bootloader does not respond, aborting\n");
		return error;
	}

    /* Setup firmware version in sysfs. The main app's version is unknown
     * in bootloader mode.
     */
    pcu->fw_version_main[0] = '\0';
    pcu->fw_version_touchpad[0] = '\0';

    /* The bootloader's info is known - set it up. */
    blver_major = pcu->cmd_buf[4] << 8 | pcu->cmd_buf[5];
    blver_minor = pcu->cmd_buf[6];
    snprintf (pcu->bl_version_main,  sizeof(pcu->bl_version_main),
                "%d.%d", blver_major, blver_minor);

    blver_major = pcu->cmd_buf[7] << 8 | pcu->cmd_buf[8];
    blver_minor = pcu->cmd_buf[9];
    snprintf(pcu->bl_version_touchpad, sizeof(pcu->bl_version_touchpad),
                "%d.%d", blver_major, blver_minor);

    dev_info (pcu->dev,
        "PCU-T2 bootloader versions: main %s, touchpad %s",
        pcu->bl_version_main, pcu->bl_version_touchpad);

	return 0;
}

static void ims_pcu_destroy_bootloader_mode(struct ims_pcu *pcu)
{

    dev_dbg (pcu->dev,  "+++ %s", __func__);

	/* Make sure our initial firmware request has completed */
	wait_for_completion (& pcu->fw_info.fw_update_done);
}

static struct usb_driver ims_pcu_driver;

static int ims_pcu_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct ims_pcu *pcu;
	ZII_PCU_FW_INFO *fw_info;
	int error;

	pcu = kzalloc(sizeof(struct ims_pcu), GFP_KERNEL);
	if (!pcu)
		return -ENOMEM;

	pcu->dev = &intf->dev;
	pcu->udev = udev;
	pcu->bootloader_mode = id->driver_info == ZII_PCU_T2_BOOTLOADER_MODE;
	mutex_init(&pcu->cmd_mutex);
	init_completion(&pcu->cmd_done);

	error = ims_pcu_parse_cdc_data(intf, pcu);
	if (error)
		goto err_free_mem;

	error = usb_driver_claim_interface(&ims_pcu_driver,
					   pcu->data_intf, pcu);
	if (error) {
		dev_err(&intf->dev,
			"Unable to claim corresponding data interface: %d\n",
			error);
		goto err_free_mem;
	}

	usb_set_intfdata(pcu->ctrl_intf, pcu);
	usb_set_intfdata(pcu->data_intf, pcu);

	error = ims_pcu_buffers_alloc(pcu);
	if (error)
		goto err_unclaim_intf;

	error = ims_pcu_start_io(pcu);
	if (error)
		goto err_free_buffers;

	error = ims_pcu_line_setup(pcu);
	if (error)
		goto err_stop_io;

	error = sysfs_create_group(&intf->dev.kobj, &ims_pcu_attr_group);
	if (error)
		goto err_stop_io;

	/* Setup firmware info: generic init */
    fw_info = & pcu->fw_info;
    fw_info->processor_id = FW_UPDATE_PROC_ID_INVALID;
    fw_info->fw_start_addr = 0;
    fw_info->erase_timeout = 0;
    memset (fw_info->fw_name, 0, ZII_pcu_T2_FIRMWARE_NAME_LEN);
    fw_info->percent_complete = 0;
    init_completion (& fw_info->fw_update_done);
    complete (& fw_info->fw_update_done);

    /* Make haptic calibration data invalid */
    pcu->haptic_data.autocal_data_valid = false;
    memset (& pcu->haptic_data.autocal_data, 0, sizeof (struct autocal));

	error = pcu->bootloader_mode ?
			ims_pcu_init_bootloader_mode(pcu) :
			ims_pcu_init_application_mode(pcu);
	if (error)
		goto err_remove_sysfs;

	return 0;

err_remove_sysfs:
	sysfs_remove_group(&intf->dev.kobj, &ims_pcu_attr_group);
err_stop_io:
	ims_pcu_stop_io(pcu);
err_free_buffers:
	ims_pcu_buffers_free(pcu);
err_unclaim_intf:
	usb_driver_release_interface(&ims_pcu_driver, pcu->data_intf);
err_free_mem:
	kfree(pcu);
	return error;
}

static void ims_pcu_disconnect(struct usb_interface *intf)
{
	struct ims_pcu *pcu = usb_get_intfdata(intf);
	struct usb_host_interface *alt = intf->cur_altsetting;

	dev_dbg (pcu->dev, "+++ %s", __func__);

	usb_set_intfdata(intf, NULL);

	/*
	 * See if we are dealing with control or data interface. The cleanup
	 * happens when we unbind primary (control) interface.
	 */
	if (alt->desc.bInterfaceClass != USB_CLASS_COMM)
		return;

	sysfs_remove_group(&intf->dev.kobj, &ims_pcu_attr_group);

	ims_pcu_stop_io(pcu);

	if (pcu->bootloader_mode)
		ims_pcu_destroy_bootloader_mode(pcu);
	else
		ims_pcu_destroy_application_mode(pcu);

	ims_pcu_buffers_free(pcu);
	kfree(pcu);
}

#ifdef CONFIG_PM
static int ims_pcu_suspend(struct usb_interface *intf,
			   pm_message_t message)
{
	struct ims_pcu *pcu = usb_get_intfdata(intf);
	struct usb_host_interface *alt = intf->cur_altsetting;

	if (alt->desc.bInterfaceClass == USB_CLASS_COMM)
		ims_pcu_stop_io(pcu);

	return 0;
}

static int ims_pcu_resume(struct usb_interface *intf)
{
	struct ims_pcu *pcu = usb_get_intfdata(intf);
	struct usb_host_interface *alt = intf->cur_altsetting;
	int retval = 0;

	if (alt->desc.bInterfaceClass == USB_CLASS_COMM) {
		retval = ims_pcu_start_io(pcu);
		if (retval == 0)
			retval = ims_pcu_line_setup(pcu);
	}

	return retval;
}
#endif

static const struct usb_device_id ims_pcu_id_table[] = {
	{
		USB_DEVICE_AND_INTERFACE_INFO(0x2e81, 0x0003,
					USB_CLASS_COMM,
					USB_CDC_SUBCLASS_ACM,
					USB_CDC_ACM_PROTO_AT_V25TER),
		.driver_info = ZII_PCU_T2_APPLICATION_MODE,
	},
	{
		USB_DEVICE_AND_INTERFACE_INFO(0x2e81, 0x0004,
					USB_CLASS_COMM,
					USB_CDC_SUBCLASS_ACM,
					USB_CDC_ACM_PROTO_AT_V25TER),
		.driver_info = ZII_PCU_T2_BOOTLOADER_MODE,
	},
	{ }
};

static struct usb_driver ims_pcu_driver = {
	.name			= "zii_pcu_t2",
	.id_table		= ims_pcu_id_table,
	.probe			= ims_pcu_probe,
	.disconnect		= ims_pcu_disconnect,
#ifdef CONFIG_PM
	.suspend		= ims_pcu_suspend,
	.resume			= ims_pcu_resume,
	.reset_resume		= ims_pcu_resume,
#endif
};

MODULE_DEVICE_TABLE (usb, ims_pcu_id_table);

module_usb_driver(ims_pcu_driver);

MODULE_DESCRIPTION("Zii PCU-T2 driver");
MODULE_AUTHOR("askpdt@pdt.com");
MODULE_LICENSE("GPL");
MODULE_VERSION ("1.7");
