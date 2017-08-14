/*
 * Marvell 88E6xxx Switch Global 3 Registers (TCAM) support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 * Copyright (c) 2016 Andrew Lunn <andrew@lunn.ch>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "chip.h"
#include "global3.h"

static int mv88e6xxx_g3_read(struct mv88e6xxx_chip *chip, int reg, u16 *val)
{
	int addr = chip->info->global3_addr;

	return mv88e6xxx_read(chip, addr, reg, val);
}

static int mv88e6xxx_g3_write(struct mv88e6xxx_chip *chip, int reg, u16 val)
{
	int addr = chip->info->global3_addr;

	return mv88e6xxx_write(chip, addr, reg, val);
}

static int mv88e6xxx_g3_wait(struct mv88e6xxx_chip *chip, int reg, u16 mask)
{
	int addr = chip->info->global3_addr;

	return mv88e6xxx_wait(chip, addr, reg, mask);
}

static int mv88e6xxx_g3_tcam_set_page0_data(struct mv88e6xxx_chip *chip,
					    u16 page0[32])
{
	int i, err;

	for (i = 2; i < 0x1c; i++) {
		err = mv88e6xxx_g3_write(chip, i, page0[i]);
		if (err)
			return err;
	}

	return 0;
}

static int mv88e6xxx_g3_tcam_set_page1_data(struct mv88e6xxx_chip *chip,
					    u16 page1[32])
{
	int i, err;

	for (i = 2; i < 0x1c; i++) {
		err = mv88e6xxx_g3_write(chip, i, page1[i]);
		if (err)
			return err;
	}

	return 0;
}

static int mv88e6xxx_g3_tcam_set_page2_data(struct mv88e6xxx_chip *chip,
					    u16 page2[32])
{
	int i, err, end;
	// TODO chip->info->tcam_page2_end?

	end = 6;
	//if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_TCAM_1))
		end = 9;

	for (i = 2; i < end; i++) {
		err = mv88e6xxx_g3_write(chip, i, page2[i]);
		if (err)
			return err;
	}

	/* Debug Port */
	return mv88e6xxx_g3_write(chip, GLOBAL3_P2_DEBUG28,
				  page2[GLOBAL3_P2_DEBUG28]);
}

static int mv88e6xxx_g3_tcam_get_page0_data(struct mv88e6xxx_chip *chip,
					    u16 *page0)
{
	int i, err;

	for (i = 2; i < 0x1c; i++) {
		err = mv88e6xxx_g3_read(chip, i, &page0[i]);
		if (err)
			return err;
		//pr_info("page0: %02x %04x\n", i, page0[i]);
	}

	return 0;
}

static int mv88e6xxx_g3_tcam_get_page1_data(struct mv88e6xxx_chip *chip,
					    u16 *page1)
{
	int i, err;

	for (i = 2; i < 0x1c; i++) {
		err = mv88e6xxx_g3_read(chip, i, &page1[i]);
		if (err)
			return err;
	}

	return 0;
}

static int mv88e6xxx_g3_tcam_get_page2_data(struct mv88e6xxx_chip *chip,
					    u16 *page2)
{
	int i, err;

	for (i = 2; i < 6; i++) {
		err = mv88e6xxx_g3_read(chip, i, &page2[i]);
		if (err)
			return err;
	}

	err = mv88e6xxx_g3_read(chip, GLOBAL3_P2_DEBUG28,
				&page2[GLOBAL3_P2_DEBUG28]);
	if (err)
		return err;

	return mv88e6xxx_g3_read(chip, GLOBAL3_P2_DEBUG31,
				 &page2[GLOBAL3_P2_DEBUG31]);
}

static int mv88e6xxx_g3_tcam_wait(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_g3_wait(chip, GLOBAL3_TCAM_OP, GLOBAL3_TCAM_OP_BUSY);
}

/**
 * mv88e6xxx_g3_tcam_flush_all() - Flush all TCAMs from the device
 * @chip: Switch chip to manipulate
 *
 * Flush all entries from the TCAM. This sets the octets/mask at
 * offset 0 for each TCAM entry such that they have the never match
 * value.
 *
 * Return: Error code, or 0
 */
int mv88e6xxx_g3_tcam_flush_all(struct mv88e6xxx_chip *chip)
{
	int err;

	/* Wait until the tcam in ready. */
	err = mv88e6xxx_g3_tcam_wait(chip);
	if (err)
		return err;

	return mv88e6xxx_g3_write(chip, GLOBAL3_TCAM_OP,
				  GLOBAL3_TCAM_OP_FLUSH_ALL);
}

/**
 * mv88e6xxx_g3_tcam_flush_entry() - Flush a single TCAM entry
 * @chip: Switch chip to manipulate
 * entry: Entry to be flushed
 *
 * Flush a single entry from the TCAM. This sets the octets/mask at
 * offset 0 of the TCAM entry such that it has the never match
 * value.
 *
 * Return: Error code, or 0
 */
int mv88e6xxx_g3_tcam_flush_entry(struct mv88e6xxx_chip *chip, u32 entry)
{
	int err;

	/* check if the given pointer is valid */
	if (entry > 0xFE)
		return -EINVAL;

	/* Wait until the tcam in ready. */
	err = mv88e6xxx_g3_tcam_wait(chip);
	if (err)
		return err;

	return mv88e6xxx_g3_write(chip, GLOBAL3_TCAM_OP,
				  GLOBAL3_TCAM_OP_FLUSH_ENTRY | entry);
}

/**
 * mv88e6xxx_g3_tcam_load_entry() - Load a single TCAM entry
 * @chip: Switch chip to manipulate
 * entry: Entry to be loaded
 * @data: Data to be loaded into entry
 *
 * This routine loads a TCAM entry.  The load sequence of TCAM entry
 * is critical. Each TCAM entry is made up of 3 pages of data. All 3
 * pages need to loaded in a particular order for the TCAM to operate
 * correctly while frames are flowing through the switch. Page 2 needs
 * to be loaded first, followed by page 1 and then finally page 0.
 *
 * If the entry is currently valid, it must first be flushed before
 * calling this function
 *
 * Return: Error code, or 0
 */
int mv88e6xxx_g3_tcam_load_entry(struct mv88e6xxx_chip *chip, u32 entry,
				 struct mv88e6xxx_tcam_data *data)
{
	int err;

	/* check if the given pointer is valid */
	if ((entry > 0xFE) || !data)
		return -EINVAL;

	/* Ensure the mode type mask is set, otherwise it never
	 * matches
	 */
	data->page0[2] |= GLOBAL3_P0_KEY1_FRAME_TYPE_MASK;

	/* Load Page 2 first */

	err = mv88e6xxx_g3_tcam_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g3_tcam_set_page2_data(chip, data->page2);
	if (err)
		return err;

	err = mv88e6xxx_g3_write(chip, GLOBAL3_TCAM_OP,
				 GLOBAL3_TCAM_OP_LOAD | GLOBAL3_TCAM_OP_PAGE2 |
				 entry);
	if (err)
		return err;

	/* load Page 1 */

	err = mv88e6xxx_g3_tcam_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g3_tcam_set_page1_data(chip, data->page1);
	if (err)
		return err;

	err = mv88e6xxx_g3_write(chip, GLOBAL3_TCAM_OP,
				 GLOBAL3_TCAM_OP_LOAD | GLOBAL3_TCAM_OP_PAGE1 |
				 entry);
	if (err)
		return err;

	/* load Page 0 */

	err = mv88e6xxx_g3_tcam_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g3_tcam_set_page0_data(chip, data->page0);
	if (err)
		return err;

	return mv88e6xxx_g3_write(chip, GLOBAL3_TCAM_OP,
				  GLOBAL3_TCAM_OP_LOAD | GLOBAL3_TCAM_OP_PAGE0 |
				  entry);
}

/**
 * mv88e6xxx_g3_tcam_read() - Read a TCAM entry
 * @chip: Switch chip to manipulate
 * entry: Entry to be loaded
 * @data: Data read from entry
 *
 * Read a TCAM entry out of the hardware.
 *
 * Return: Error code, or 0
 */
int mv88e6xxx_g3_tcam_read(struct mv88e6xxx_chip *chip, u32 entry,
			   struct mv88e6xxx_tcam_data *data)
{
	int err;

	/* check if the given pointer is valid */
	if ((entry > 0xFE) || !data)
		return -EINVAL;

	err = mv88e6xxx_g3_tcam_wait(chip);
	if (err)
		return err;

	/* Read page 0 */
	err = mv88e6xxx_g3_write(chip, GLOBAL3_TCAM_OP,
				 GLOBAL3_TCAM_OP_READ | GLOBAL3_TCAM_OP_PAGE0 |
				 entry);
	if (err)
		return err;

	/* Wait until the tcam in ready. */
	err = mv88e6xxx_g3_tcam_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g3_tcam_get_page0_data(chip, data->page0);
	if (err)
		return err;

	/* Read page 1 */
	err = mv88e6xxx_g3_write(chip, GLOBAL3_TCAM_OP,
				 GLOBAL3_TCAM_OP_READ | GLOBAL3_TCAM_OP_PAGE1 |
				 entry);
	if (err)
		return err;

	/* Wait until the tcam in ready. */
	err = mv88e6xxx_g3_tcam_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g3_tcam_get_page1_data(chip, data->page1);
	if (err)
		return err;

	/* Read page 2 */
	err = mv88e6xxx_g3_write(chip, GLOBAL3_TCAM_OP,
				 GLOBAL3_TCAM_OP_READ | GLOBAL3_TCAM_OP_PAGE2 |
				 entry);
	if (err)
		return err;

	/* Wait until the tcam in ready. */
	err = mv88e6xxx_g3_tcam_wait(chip);
	if (err)
		return err;

	return mv88e6xxx_g3_tcam_get_page2_data(chip, data->page2);
}

/**
 * mv88e6xxx_g3_tcam_get_next() - Get the next valid TCAM entry
 * @chip: Switch chip to manipulate
 * entry: Entry to be loaded
 * @data: Data read from entry
 *
 * This routine finds the next higher TCAM Entry number that is valid
 * (i.e., any entry whose Page 0 offset 0x02 is not equal to
 * 0x00FF). The TCAM Entry register (bits 7:0) is used as the TCAM
 * entry to start from. To find the lowest number TCAM Entry that is
 * valid, start the Get Next operation with TCAM Entry set to 0xFF.
 *
 * Return: Error code, or 0
 */
int mv88e6xxx_g3_tcam_get_next(struct mv88e6xxx_chip *chip, u32 *entry,
			       struct mv88e6xxx_tcam_data *data)
{
	u16 reg;
	int err;

	/* check if the given pointer is valid */
	if ((*entry > 0xFF) || !data)
		return -EINVAL;

	/* Wait until the tcam in ready. */
	err = mv88e6xxx_g3_tcam_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g3_write(chip, GLOBAL3_TCAM_OP,
				 GLOBAL3_TCAM_OP_GET_NEXT | *entry);

	/* Wait until the tcam in ready. */
	err = mv88e6xxx_g3_tcam_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g3_read(chip, GLOBAL3_TCAM_OP, &reg);
	if (err)
		return err;

	*entry = reg & 0xff;

	if (*entry != 0xff)
		return mv88e6xxx_g3_tcam_read(chip, *entry, data);

	return 0;
}

/**
 * mv88e6xxx_g3_tcam_set_match() - Set an octet of TCAM match data
 * @chip: Switch chip to manipulate
 * @data: TCAM strucuture to set the parameter in
 * @offset: Offset in frame for this octet
 * @octet: Octet of data to match
 * @mask: Mask to apply to octet
 *
 * Set one octet of match data, at the given offset into the frame.
 * The individual pairs of octet bits and mask bits work together as
 * follows:
 *
 *    Mask Octet Meaning
 *	0  0 Don't Care. The octet bit can be a one or a zero for a TCAM hit
 *	     to occur.
 *	1  0 Hit on 0. The octet bit must be a zero for a TCAM hit to occur.
 *	1  1 Hit on 1. The octet bit must be a one for a TCAM hit to occur.
 *	0  1 Never Hit. Used to prevent a TCAM hit from occurring from this
 *	     bit.
 *
 *    The Never Hit value is used to Flush the TCAM or Purge a TCAM
 *    entry.  On a TCAM Flush or Purge, this value it written to the
 *    1st TCAM offset.
 *
 * Offset 0 - 5 is the Destination MAC address
 * Offset 6 - 11 is the Source MAC address
 * Offset 12 - 16 is the VLAN TAG
 *
 *   If the frame has no VLAN TAG, 4 octets of 00 will be inserted in
 *   the place the VLAN tag would be, thus shifting the rest of the
 *   frame down four octets. This allows matches to be performed on
 *   both tagged and untagged frames using one TCAM entry.
 *
 *   When using GLOBAL3_P0_KEY1_FRAME_TYPE_NORNAL, it is possible to
 *   match on tagged, untagged or both types of frames.
 *   * Untagged only - Mask 0xffffffff, value 0x0.
 *   * Tagged Only   - Mask 0xffff0000, value 0x81000000
 *   * Don't care    - Mask 0x0
 *
 * Offset 17 - 18 - Ethernet Type/Length
 * Offset 19 - End -  Rest of frame.
 *
 * Return: Error code, or 0
 */
int mv88e6xxx_g3_tcam_set_match(struct mv88e6xxx_chip *chip,
				struct mv88e6xxx_tcam_data *data,
				unsigned int offset, u8 octet, u8 mask)
{
	u16 word = octet | ((u16)mask << 8);

	if (offset > 47)
		return -EINVAL;
	if (offset > 21) {
		offset -= 22;
		data->page1[2 + offset] = word;
	} else {
		data->page0[6 + offset] = word;
	}

	return 0;
}

/**
 * mv88e6xxx_g3_tcam_get_match() - Get an octet of TCAM match data
 * @chip: Switch chip to manipulate
 * @data: TCAM strucuture to get the match from
 * @offset: Offset in frame for this octet
 * @octet: Octet of match data
 * @mask: Mask apply to octet
 *
 * Return an octet of match data from the TCAM data structure.
 *
 * Return: Error code, or 0
 */
int mv88e6xxx_g3_tcam_get_match(struct mv88e6xxx_chip *chip,
				struct mv88e6xxx_tcam_data *data,
				unsigned int offset, u8 *octet, u8 *mask)
{
	u16 word;

	if (offset > 47)
		return -EINVAL;
	if (offset > 21) {
		offset -= 22;
		word = data->page1[2 + offset];
	} else {
		word = data->page0[6 + offset];
	}

	*mask = (word & 0xff00) >> 8;
	*octet = (word & 0xff);

	return 0;
}

/**
 * mv88e6xxx_g3_tcam_set() - Set a parameter in the TCAM data structure
 * @chip: Switch chip to manipulate
 * @data: TCAM strucuture to set the parameter in
 * @param: ID of parameter to set
 * @value: Value to set the parameter to
 *
 * Given a TCAM data structure, set a parameter to the given value.
 * Where ever possible, only use values as defined in
 * chip.h. These contain the correct bit shifts. In some cases,
 * such as port vectors, PVID, no values are defined. In this case,
 * construct a value as appropriate.
 *
 * A number of parameters include an enable/override bit, or a
 * mask. By default, all such bits are disabled. These will be set as
 * appropriate when the parameter is set.
 *
 * Return: Error code, or 0
 */
int mv88e6xxx_g3_tcam_set(struct mv88e6xxx_chip *chip,
			  struct mv88e6xxx_tcam_data *data,
			  enum mv88e6xxx_tcam_param param, u16 value)
{
	u16 new_value;
	int err = 0;

	switch (param) {
	case MV88E6XXX_P0_KEY1_FRAME_TYPE:
		value |= GLOBAL3_P0_KEY1_FRAME_TYPE_MASK;
		data->page0[GLOBAL3_P0_KEY1] = value;
		break;
	case MV88E6XXX_P0_KEY2_SRC_PORT_VECTOR:
		/* Which ports is this TCAM entry applied to.  Setting
		 * bit 0 means port 0, bit 1 means port 1, etc.
		 *
		 * The meaning of the bits are a bit funky. The mask
		 * bits are ports which should not match, if the value
		 * is 0. Alternatively, if a single port is to be
		 * used, all the mask bits can be set, and a single
		 * bit set in the value section.
		 */
		new_value = (~value <<
			     GLOBAL3_P0_KEY2_SRC_PORT_VECTOR_MASK_SHIFT);
		data->page0[GLOBAL3_P0_KEY2] = new_value;
		break;
	case MV88E6XXX_P0_KEY3_PPRI:
		/* Provider priority bits, when using FRAME_TYPE_PROVIDER */
		new_value = data->page0[GLOBAL3_P0_KEY3] & 0xff0f;
		new_value = value | GLOBAL3_P0_KEY3_PPRI_MASK;
		data->page0[GLOBAL3_P0_KEY3] = new_value;
		break;
	case MV88E6XXX_P0_KEY4_PVID: {
		/* Provider VLAN ID when using FRAME_TYPE_PROVIDER. This
		 * spans two registers.
		 */
		u16 low = (value & 0xff) | GLOBAL3_P0_KEY4_PVID_MASK;
		u16 hi = (value >> 8) | GLOBAL3_P0_KEY3_PVID_MASK;
		u16 new_value;

		data->page0[GLOBAL3_P0_KEY4] = low;
		new_value = data->page0[GLOBAL3_P0_KEY3] & 0xfff0;
		new_value |= hi;
		data->page0[GLOBAL3_P0_KEY3] = new_value;
		break;
	}
	case MV88E6XXX_P2_ACTION1_INTERRUPT:
		/* If the TCAM entry hits, trigger a TCAM interrupt. */
		new_value = data->page2[GLOBAL3_P2_ACTION1];
		new_value &= GLOBAL3_P2_ACTION1_INTERRUPT;
		new_value |= value;
		data->page2[GLOBAL3_P2_ACTION1] = new_value;
		break;
	case MV88E6XXX_P2_ACTION1_INC_TCAM_COUNTER:
		/* If the TCAM entry hits, trigger a TCAM interrupt. */
		new_value = data->page2[GLOBAL3_P2_ACTION1];
		new_value &= GLOBAL3_P2_ACTION1_INC_TCAM_COUNTER;
		new_value |= value;
		data->page2[GLOBAL3_P2_ACTION1] = new_value;
		break;
	case MV88E6XXX_P2_ACTION1_VID:
		/* If the TCAM entry hits, override the VLAN ID in the
		 * frame.
		 */
		new_value = data->page2[GLOBAL3_P2_ACTION1];
		new_value &= ~GLOBAL3_P2_ACTION1_VID_MASK;
		new_value |= GLOBAL3_P2_ACTION1_VID_OVERRIDE;
		new_value |= value;
		data->page2[GLOBAL3_P2_ACTION1] = new_value;
		break;
	case MV88E6XXX_P2_ACTION2_FLOW_ID:
		/* If the TCAM entry hits, provide a flow ID to the
		 * port ingress rate limiter.
		 */
		new_value = data->page2[GLOBAL3_P2_ACTION2];
		new_value &= ~GLOBAL3_P2_ACTION2_FLOW_ID_MASK;
		new_value |= value;
		data->page2[GLOBAL3_P2_ACTION2] = new_value;
		break;
	case MV88E6XXX_P2_ACTION2_QPRI:
		/* If the TCAM entry hits, provide a queue priority
		 * for mapping the frame to a queue in the egress
		 * port.
		 */
		new_value = data->page2[GLOBAL3_P2_ACTION2];
		new_value &= ~GLOBAL3_P2_ACTION2_QPRI_MASK;
		new_value |= GLOBAL3_P2_ACTION2_QPRI_OVERRIDE;
		new_value |= value;
		data->page2[GLOBAL3_P2_ACTION2] = new_value;
		break;
	case MV88E6XXX_P2_ACTION2_FPRI:
		/* If the TCAM entry hits, provide a frame priority
		 * which will be used for the frame if it egresses
		 * from a port.
		 */
		new_value = data->page2[GLOBAL3_P2_ACTION2];
		new_value &= ~GLOBAL3_P2_ACTION2_FPRI_MASK;
		new_value |= GLOBAL3_P2_ACTION2_FPRI_OVERRIDE;
		new_value |= value;
		data->page2[GLOBAL3_P2_ACTION2] = new_value;
		break;
	case MV88E6XXX_P2_ACTION3_DST_PORT_VECTOR:
		/* If the TCAM entry hits, which ports may this frame
		 * egress. If 0, the frame is discarded, and counted
		 * in InFiltered. Setting bit 0 means port 0, bit 1
		 * means port 1, etc.
		 */
		value |= GLOBAL3_P2_ACTION3_DPV_OVERRIDE;
		data->page2[GLOBAL3_P2_ACTION3] = value;
		break;
	case MV88E6XXX_P2_ACTION4_FRAME_ACTION:
		/* If the TCAM entry hits, the frame action data is
		 * assigned to the frame, at the end of the ingress
		 * processing. The modified action data is then passed
		 * to subsequent frame processing blocks.
		 */
		new_value = data->page2[GLOBAL3_P2_ACTION4];
		new_value &= ~GLOBAL3_P2_ACTION4_FRAME_ACTION_MASK;
		new_value |= GLOBAL3_P2_ACTION4_FRAME_ACTION_OVERRIDE;
		new_value |= value;
		data->page2[GLOBAL3_P2_ACTION4] = new_value;
		break;
	case MV88E6XXX_P2_ACTION4_LOAD_BALANCE:
		/* If the TCAM entry hits, the load balance data is
		 * assigned to the frame. It is used to access the
		 * trunking mask table instead of using the source and
		 * destination.
		 */
		new_value = data->page2[GLOBAL3_P2_ACTION4];
		new_value &= ~GLOBAL3_P2_ACTION4_LOAD_BALANCE_MASK;
		new_value |= GLOBAL3_P2_ACTION4_LOAD_BALANCE_OVERRIDE;
		new_value |= value;
		data->page2[GLOBAL3_P2_ACTION4] = new_value;
		break;
	case MV88E6XXX_P2_DEBUG_PORT:
		/* TCAM activities for this TCAM entry are logged into
		 * offset GLOBAL3_P2_DEBUG31. Select which port will
		 * be logged.
		 */
		data->page2[GLOBAL3_P2_DEBUG28] = value;
		break;
	case MV88E6XXX_P2_DEBUG_HIT:
		/* Read only register, set by the switch */
		/* Fall through */
	default:
		err = -EINVAL;
	}

	return err;
}

/**
 * mv88e6xxx_g3_tcam_get() - Get a parameter from the TCAM data structure
 * @chip: Switch chip to manipulate
 * @data: TCAM strucuture to get the parameter from
 * @param: ID of parameter to get
 * @value: Pointer to where to put the value of the parameter
 *
 * Given a TCAM data structure, get a parameter from it.
 *
 * Many parameters have a mask or an enable/override bit which enables
 * the parameter.  For such a parameter, if it is not enabled, *value
 * will be set to MV88E6XXX_TCAM_PARAM_DISABLED.
 *
 * Return: Error code, or 0
 */
int mv88e6xxx_g3_tcam_get(struct mv88e6xxx_chip *chip,
			  struct mv88e6xxx_tcam_data *data,
			  enum mv88e6xxx_tcam_param param, int *value)
{
	int err = 0;

	switch (param) {
	case MV88E6XXX_P0_KEY1_FRAME_TYPE:
		if (!(data->page0[GLOBAL3_P0_KEY1] &
		      GLOBAL3_P0_KEY1_FRAME_TYPE_MASK))
			*value = MV88E6XXX_TCAM_PARAM_DISABLED;
		else
			*value = data->page0[GLOBAL3_P0_KEY1] & 0xc0;
		break;
	case MV88E6XXX_P0_KEY2_SRC_PORT_VECTOR:
		if ((data->page0[GLOBAL3_P0_KEY2] &
		     GLOBAL3_P0_KEY2_SRC_PORT_VECTOR_VALUE_MASK) == 0) {
			*value = ~data->page0[GLOBAL3_P0_KEY2];
			*value >>= GLOBAL3_P0_KEY2_SRC_PORT_VECTOR_MASK_SHIFT;
			*value &= GLOBAL3_P0_KEY2_SRC_PORT_VECTOR_VALUE_MASK;
		} else {
			*value = data->page0[GLOBAL3_P0_KEY2] &
				GLOBAL3_P0_KEY2_SRC_PORT_VECTOR_VALUE_MASK;
		}
		break;
	case MV88E6XXX_P0_KEY3_PPRI:
		if (!(data->page0[GLOBAL3_P0_KEY3] &
		      GLOBAL3_P0_KEY3_PPRI_MASK))
			*value = MV88E6XXX_TCAM_PARAM_DISABLED;
		else
			*value = data->page0[GLOBAL3_P0_KEY3] & 0xf0;
		break;
	case MV88E6XXX_P0_KEY4_PVID: {
		u16 low, hi;

		if (!(data->page0[GLOBAL3_P0_KEY3] &
		      GLOBAL3_P0_KEY3_PVID_MASK) &&
		    !(data->page0[GLOBAL3_P0_KEY4] &
		      GLOBAL3_P0_KEY4_PVID_MASK))
			*value = MV88E6XXX_TCAM_PARAM_DISABLED;
		low = data->page0[GLOBAL3_P0_KEY4] & 0xff;
		hi = data->page0[GLOBAL3_P0_KEY3] & 0xf;
		*value = (hi << 8) | low;
		break;
	}
	case MV88E6XXX_P2_ACTION1_INTERRUPT:
		*value = data->page2[GLOBAL3_P2_ACTION1] &
			GLOBAL3_P2_ACTION1_INTERRUPT;
		break;
	case MV88E6XXX_P2_ACTION1_INC_TCAM_COUNTER:
		*value = data->page2[GLOBAL3_P2_ACTION1] &
			GLOBAL3_P2_ACTION1_INC_TCAM_COUNTER;
		break;
	case MV88E6XXX_P2_ACTION1_VID:
		if (!(data->page2[GLOBAL3_P2_ACTION1] &
		      GLOBAL3_P2_ACTION1_VID_OVERRIDE))
			*value = MV88E6XXX_TCAM_PARAM_DISABLED;
		else
			*value = data->page2[GLOBAL3_P2_ACTION1] & 0xfff;
		break;
	case MV88E6XXX_P2_ACTION2_FLOW_ID:
		*value = data->page2[GLOBAL3_P2_ACTION2] & 0x300;
		break;
	case MV88E6XXX_P2_ACTION2_QPRI:
		if (!(data->page2[GLOBAL3_P2_ACTION2] &
		      GLOBAL3_P2_ACTION2_QPRI_OVERRIDE))
			*value = MV88E6XXX_TCAM_PARAM_DISABLED;
		else
			*value = data->page2[GLOBAL3_P2_ACTION2] & 0x30;
		break;
	case MV88E6XXX_P2_ACTION2_FPRI:
		if (!(data->page2[GLOBAL3_P2_ACTION2] &
		      GLOBAL3_P2_ACTION2_FPRI_OVERRIDE))
			*value = MV88E6XXX_TCAM_PARAM_DISABLED;
		else
			*value = data->page2[GLOBAL3_P2_ACTION2] & 0x7;
		break;
	case MV88E6XXX_P2_ACTION3_DST_PORT_VECTOR:
		if (!(data->page2[GLOBAL3_P2_ACTION3] &
		      GLOBAL3_P2_ACTION3_DPV_OVERRIDE))
			*value = MV88E6XXX_TCAM_PARAM_DISABLED;
		else
			*value = data->page2[GLOBAL3_P2_ACTION3] & 0x7f;
		break;
	case MV88E6XXX_P2_ACTION4_FRAME_ACTION:
		if (!(data->page2[GLOBAL3_P2_ACTION4] &
		      GLOBAL3_P2_ACTION4_FRAME_ACTION_OVERRIDE))
			*value = MV88E6XXX_TCAM_PARAM_DISABLED;
		else
			*value = data->page2[GLOBAL3_P2_ACTION4] & 0x7ff0;
		break;
	case MV88E6XXX_P2_ACTION4_LOAD_BALANCE:
		if (!(data->page2[GLOBAL3_P2_ACTION4] &
		      GLOBAL3_P2_ACTION4_LOAD_BALANCE_OVERRIDE))
			*value = MV88E6XXX_TCAM_PARAM_DISABLED;
		else
			*value = data->page2[GLOBAL3_P2_ACTION4] & 0x7;
		break;
	case MV88E6XXX_P2_DEBUG_PORT:
		*value = data->page2[GLOBAL3_P2_DEBUG28] & 0xf;
		break;
	case MV88E6XXX_P2_DEBUG_HIT:
		*value = data->page2[GLOBAL3_P2_DEBUG31];
	default:
		err = -EINVAL;
	}

	return err;
}
