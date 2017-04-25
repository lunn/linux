/*
 * Marvell 88E6xxx Switch hardware timestamping support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2017 National Instruments
 *      Erik Hons <erik.hons@ni.com>
 *      Brandon Streiff <brandon.streiff@ni.com>
 *      Dane Wagner <dane.wagner@ni.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "chip.h"
#include "global2.h"
#include "hwtstamp.h"
#include <linux/ptp_classify.h>

static int mv88e6xxx_port_ptp_read(struct mv88e6xxx_chip *chip, int port,
				   int addr, u16 *data, int len)
{
	if (!chip->info->ops->avb_ops->port_ptp_read)
		return -EOPNOTSUPP;

	return chip->info->ops->avb_ops->port_ptp_read(chip, port, addr,
						       data, len);
}

static int mv88e6xxx_port_ptp_write(struct mv88e6xxx_chip *chip, int port,
				    int addr, u16 data)
{
	if (!chip->info->ops->avb_ops->port_ptp_write)
		return -EOPNOTSUPP;

	return chip->info->ops->avb_ops->port_ptp_write(chip, port, addr,
							data);
}

static int mv88e6xxx_ptp_write(struct mv88e6xxx_chip *chip, int addr,
			       u16 data)
{
	if (!chip->info->ops->avb_ops->ptp_write)
		return -EOPNOTSUPP;

	return chip->info->ops->avb_ops->ptp_write(chip, addr, data);
}

/* TX_TSTAMP_TIMEOUT: This limits the time spent polling for a TX
 * timestamp. When working properly, hardware will produce a timestamp
 * within 1ms. Software may enounter delays due to MDIO contention, so
 * the timeout is set accordingly.
 */
#define TX_TSTAMP_TIMEOUT	msecs_to_jiffies(20)

int mv88e6xxx_get_ts_info(struct dsa_switch *ds, int port,
			  struct ethtool_ts_info *info)
{
	struct mv88e6xxx_chip *chip = ds->priv;

	if (!chip->info->ptp_support)
		return -EOPNOTSUPP;

	info->so_timestamping =
		SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE;
	info->phc_index = ptp_clock_index(chip->ptp_clock);
	info->tx_types =
		(1 << HWTSTAMP_TX_OFF) |
		(1 << HWTSTAMP_TX_ON);
	info->rx_filters =
		(1 << HWTSTAMP_FILTER_NONE) |
		(1 << HWTSTAMP_FILTER_PTP_V2_L4_EVENT) |
		(1 << HWTSTAMP_FILTER_PTP_V2_L4_SYNC) |
		(1 << HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ) |
		(1 << HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
		(1 << HWTSTAMP_FILTER_PTP_V2_L2_SYNC) |
		(1 << HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ) |
		(1 << HWTSTAMP_FILTER_PTP_V2_EVENT) |
		(1 << HWTSTAMP_FILTER_PTP_V2_SYNC) |
		(1 << HWTSTAMP_FILTER_PTP_V2_DELAY_REQ);

	return 0;
}

static int mv88e6xxx_set_hwtstamp_config(struct mv88e6xxx_chip *chip, int port,
					 struct hwtstamp_config *config)
{
	struct mv88e6xxx_port_hwtstamp *ps = &chip->port_hwtstamp[port];
	bool tstamp_enable = false;
	u16 port_config0;
	int err;

	/* Prevent the TX/RX paths from trying to interact with the
	 * timestamp hardware while we reconfigure it.
	 */
	clear_bit_unlock(MV88E6XXX_HWTSTAMP_ENABLED, &ps->state);

	/* reserved for future extensions */
	if (config->flags)
		return -EINVAL;

	switch (config->tx_type) {
	case HWTSTAMP_TX_OFF:
		tstamp_enable = false;
		break;
	case HWTSTAMP_TX_ON:
		tstamp_enable = true;
		break;
	default:
		return -ERANGE;
	}

	/* The switch supports timestamping both L2 and L4; one cannot be
	 * disabled independently of the other.
	 */
	switch (config->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		tstamp_enable = false;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		break;
	case HWTSTAMP_FILTER_ALL:
	default:
		config->rx_filter = HWTSTAMP_FILTER_NONE;
		return -ERANGE;
	}

	if (tstamp_enable) {
		/* Disable transportSpecific value matching, so that packets
		 * with either 1588 (0) and 802.1AS (1) will be timestamped.
		 */
		port_config0 = MV88E6XXX_PORT_PTP_CFG0_DISABLE_TSPEC_MATCH;
	} else {
		/* Disable PTP. This disables both RX and TX timestamping. */
		port_config0 = MV88E6XXX_PORT_PTP_CFG0_DISABLE_PTP;
	}

	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_port_ptp_write(chip, port, MV88E6XXX_PORT_PTP_CFG0,
				       port_config0);
	mutex_unlock(&chip->reg_lock);

	if (err < 0)
		return err;

	/* Once hardware has been configured, enable timestamp checks
	 * in the RX/TX paths.
	 */
	if (tstamp_enable)
		set_bit(MV88E6XXX_HWTSTAMP_ENABLED, &ps->state);

	return 0;
}

int mv88e6xxx_port_hwtstamp_set(struct dsa_switch *ds, int port,
				struct ifreq *ifr)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	struct mv88e6xxx_port_hwtstamp *ps = &chip->port_hwtstamp[port];
	struct hwtstamp_config config;
	int err;

	if (!chip->info->ptp_support)
		return -EOPNOTSUPP;

	if (port < 0 || port >= mv88e6xxx_num_ports(chip))
		return -EINVAL;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	err = mv88e6xxx_set_hwtstamp_config(chip, port, &config);
	if (err)
		return err;

	/* Save the chosen configuration to be returned later. */
	memcpy(&ps->tstamp_config, &config, sizeof(config));

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
		-EFAULT : 0;
}

int mv88e6xxx_port_hwtstamp_get(struct dsa_switch *ds, int port,
				struct ifreq *ifr)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	struct mv88e6xxx_port_hwtstamp *ps = &chip->port_hwtstamp[port];
	struct hwtstamp_config *config = &ps->tstamp_config;

	if (!chip->info->ptp_support)
		return -EOPNOTSUPP;

	if (port < 0 || port >= mv88e6xxx_num_ports(chip))
		return -EINVAL;

	return copy_to_user(ifr->ifr_data, config, sizeof(*config)) ?
		-EFAULT : 0;
}

/* Get the start of the PTP header in this skb */
static u8 *_get_ptp_header(struct sk_buff *skb, unsigned int type)
{
	unsigned int offset = 0;
	u8 *data = skb_mac_header(skb);

	if (type & PTP_CLASS_VLAN)
		offset += VLAN_HLEN;

	switch (type & PTP_CLASS_PMASK) {
	case PTP_CLASS_IPV4:
		offset += ETH_HLEN + IPV4_HLEN(data + offset) + UDP_HLEN;
		break;
	case PTP_CLASS_IPV6:
		offset += ETH_HLEN + IP6_HLEN + UDP_HLEN;
		break;
	case PTP_CLASS_L2:
		offset += ETH_HLEN;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	/* Ensure that the entire header is present in this packet. */
	if (skb->len + ETH_HLEN < offset + 34)
		return ERR_PTR(-EINVAL);

	return data + offset;
}

static bool mv88e6xxx_should_tstamp(struct mv88e6xxx_chip *chip, int port,
				    struct sk_buff *skb, unsigned int type)
{
	struct mv88e6xxx_port_hwtstamp *ps = &chip->port_hwtstamp[port];
	u8 *ptp_hdr, *msgtype;
	bool ret;

	if (port < 0 || port >= mv88e6xxx_num_ports(chip))
		return false;

	ptp_hdr = _get_ptp_header(skb, type);
	if (IS_ERR(ptp_hdr))
		return false;

	if (unlikely(type & PTP_CLASS_V1))
		msgtype = ptp_hdr + OFF_PTP_CONTROL;
	else
		msgtype = ptp_hdr;

	ret = test_bit(MV88E6XXX_HWTSTAMP_ENABLED, &ps->state);

	dev_dbg(chip->dev,
		"p%d: PTP message classification 0x%x type 0x%x, tstamp? %d",
		port, type, *msgtype, (int)ret);

	return ret;
}

/* rxtstamp will be called in interrupt context so we don't to do
 * anything like read PTP registers over SMI.
 */
bool mv88e6xxx_port_rxtstamp(struct dsa_switch *ds, int port,
			     struct sk_buff *skb, unsigned int type)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	struct skb_shared_hwtstamps *shhwtstamps;
	__be32 *ptp_rx_ts;
	u8 *ptp_hdr;
	u32 raw_ts;
	u64 ns;

	if (!chip->info->ptp_support)
		return false;

	if (port < 0 || port >= mv88e6xxx_num_ports(chip))
		return false;

	if (!mv88e6xxx_should_tstamp(chip, port, skb, type))
		return false;

	shhwtstamps = skb_hwtstamps(skb);
	memset(shhwtstamps, 0, sizeof(*shhwtstamps));

	/* Because we configured the arrival timestamper to put the counter
	 * into the 32-bit "reserved" field of the PTP header, we can retrieve
	 * the value from the packet directly instead of having to retrieve it
	 * via SMI.
	 */
	ptp_hdr = _get_ptp_header(skb, type);
	if (IS_ERR(ptp_hdr))
		return false;
	ptp_rx_ts = (__be32 *)(ptp_hdr + OFF_PTP_RESERVED);
	raw_ts = __be32_to_cpu(*ptp_rx_ts);
	ns = timecounter_cyc2time(&chip->tstamp_tc, raw_ts);
	shhwtstamps->hwtstamp = ns_to_ktime(ns);

	dev_dbg(chip->dev, "p%d: rxtstamp %llx\n", port, ns);

	return false;
}

static void mv88e6xxx_txtstamp_work(struct work_struct *ugly)
{
	struct mv88e6xxx_port_hwtstamp *ps = container_of(
		ugly, struct mv88e6xxx_port_hwtstamp, tx_tstamp_work);
	struct mv88e6xxx_chip *chip = container_of(
		ps, struct mv88e6xxx_chip, port_hwtstamp[ps->port_id]);
	struct sk_buff *tmp_skb;
	unsigned long tmp_tstamp_start;
	int err;
	u16 departure_block[4];
	u16 tmp_seq_id;

	if (!test_bit(MV88E6XXX_HWTSTAMP_TX_IN_PROGRESS, &ps->state))
		return;

	tmp_skb = ps->tx_skb;
	tmp_seq_id = ps->tx_seq_id;
	tmp_tstamp_start = ps->tx_tstamp_start;

	if (!tmp_skb)
		return;

	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_port_ptp_read(chip, ps->port_id,
				      MV88E6XXX_PORT_PTP_DEP_STS,
				      departure_block,
				      ARRAY_SIZE(departure_block));
	mutex_unlock(&chip->reg_lock);

	if (err)
		goto free_and_clear_skb;

	if (departure_block[0] & MV88E6XXX_PTP_TS_VALID) {
		struct skb_shared_hwtstamps shhwtstamps;
		u64 ns;
		u32 time_raw;
		u16 status;

		/* We have the timestamp; go ahead and clear valid now */
		mutex_lock(&chip->reg_lock);
		mv88e6xxx_port_ptp_write(chip, ps->port_id,
					 MV88E6XXX_PORT_PTP_DEP_STS, 0);
		mutex_unlock(&chip->reg_lock);

		status = departure_block[0] &
				MV88E6XXX_PTP_TS_STATUS_MASK;
		if (status != MV88E6XXX_PTP_TS_STATUS_NORMAL) {
			dev_warn(chip->dev, "p%d: tx timestamp overrun\n",
				 ps->port_id);
			goto free_and_clear_skb;
		}

		if (departure_block[3] != tmp_seq_id) {
			dev_warn(chip->dev, "p%d: unexpected sequence id\n",
				 ps->port_id);
			goto free_and_clear_skb;
		}

		memset(&shhwtstamps, 0, sizeof(shhwtstamps));
		time_raw = ((u32)departure_block[2] << 16) |
			   departure_block[1];
		ns = timecounter_cyc2time(&chip->tstamp_tc, time_raw);
		shhwtstamps.hwtstamp = ns_to_ktime(ns);

		dev_dbg(chip->dev,
			"p%d: txtstamp %llx status 0x%04x skb ID 0x%04x hw ID 0x%04x\n",
			ps->port_id, ktime_to_ns(shhwtstamps.hwtstamp),
			departure_block[0], tmp_seq_id, departure_block[3]);

		/* skb_complete_tx_timestamp() will free up the client to make
		 * another timestamp-able transmit. We have to be ready for it
		 * -- by clearing the ps->tx_skb "flag" -- beforehand.
		 */

		ps->tx_skb = NULL;
		clear_bit_unlock(MV88E6XXX_HWTSTAMP_TX_IN_PROGRESS, &ps->state);

		skb_complete_tx_timestamp(tmp_skb, &shhwtstamps);

	} else {
		if (time_is_before_jiffies(
			    tmp_tstamp_start + TX_TSTAMP_TIMEOUT)) {
			dev_warn(chip->dev, "p%d: clearing tx timestamp hang\n",
				 ps->port_id);
			goto free_and_clear_skb;
		}

		/* The timestamp should be available quickly, while getting it
		 * is high priority and time bounded to only 10ms. A poll is
		 * warranted and this is the nicest way to realize it in a work
		 * item.
		 */

		queue_work(system_highpri_wq, &ps->tx_tstamp_work);
	}

	return;

free_and_clear_skb:
	ps->tx_skb = NULL;
	clear_bit_unlock(MV88E6XXX_HWTSTAMP_TX_IN_PROGRESS, &ps->state);

	dev_kfree_skb_any(tmp_skb);
}

void mv88e6xxx_port_txtstamp(struct dsa_switch *ds, int port,
			     struct sk_buff *clone, unsigned int type)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	struct mv88e6xxx_port_hwtstamp *ps = &chip->port_hwtstamp[port];

	if (!chip->info->ptp_support)
		return;

	if (port < 0 || port >= mv88e6xxx_num_ports(chip))
		goto out;

	if (unlikely(skb_shinfo(clone)->tx_flags & SKBTX_HW_TSTAMP) &&
	    mv88e6xxx_should_tstamp(chip, port, clone, type)) {
		__be16 *seq_ptr = (__be16 *)(_get_ptp_header(clone, type) +
					     OFF_PTP_SEQUENCE_ID);

		if (!test_and_set_bit_lock(MV88E6XXX_HWTSTAMP_TX_IN_PROGRESS,
					   &ps->state)) {
			ps->tx_skb = clone;
			ps->tx_tstamp_start = jiffies;
			ps->tx_seq_id = be16_to_cpup(seq_ptr);

			/* Fetching the timestamp is high-priority work because
			 * 802.1AS bounds the time for a response.
			 *
			 * No need to check result of queue_work(). ps->tx_skb
			 * check ensures work item is not pending (it may be
			 * waiting to exit)
			 */
			queue_work(system_highpri_wq, &ps->tx_tstamp_work);
			return;
		}

		/* Otherwise we're already in progress... */
		dev_dbg(chip->dev,
			"p%d: tx timestamp already in progress, discarding",
			port);
	}

out:
	/* We don't need it after all. */
	kfree_skb(clone);
}

static int mv88e6xxx_hwtstamp_port_setup(struct mv88e6xxx_chip *chip, int port)
{
	struct mv88e6xxx_port_hwtstamp *ps = &chip->port_hwtstamp[port];

	ps->port_id = port;
	INIT_WORK(&ps->tx_tstamp_work, mv88e6xxx_txtstamp_work);

	return mv88e6xxx_port_ptp_write(chip, port, MV88E6XXX_PORT_PTP_CFG0,
					MV88E6XXX_PORT_PTP_CFG0_DISABLE_PTP);
}

static void mv88e6xxx_hwtstamp_port_free(struct mv88e6xxx_chip *chip, int port)
{
	struct mv88e6xxx_port_hwtstamp *ps = &chip->port_hwtstamp[port];

	cancel_work_sync(&ps->tx_tstamp_work);
}

int mv88e6xxx_hwtstamp_setup(struct mv88e6xxx_chip *chip)
{
	int i;
	int err;

	/* Disable timestamping on all ports. */
	for (i = 0; i < mv88e6xxx_num_ports(chip); ++i) {
		err = mv88e6xxx_hwtstamp_port_setup(chip, i);
		if (err)
			return err;
	}

	/* MV88E6XXX_PTP_MSG_TYPE is a mask of PTP message types to
	 * timestamp. This affects all ports that have timestamping enabled,
	 * but the timestamp config is per-port; thus we configure all events
	 * here and only support the HWTSTAMP_FILTER_*_EVENT filter types.
	 */
	err = mv88e6xxx_ptp_write(chip, MV88E6XXX_PTP_MSGTYPE,
				  MV88E6XXX_PTP_MSGTYPE_ALL_EVENT);
	if (err)
		return err;

	/* Each event type will be timestamped using ARRIVAL0. */
	err = mv88e6xxx_ptp_write(chip, MV88E6XXX_PTP_TS_ARRIVAL_PTR, 0x0);
	if (err)
		return err;

	/* Configure the switch to embed the (32-bit) arrival timestamps in
	 * the packets, in the "reserved" field of the PTP header at octet 16
	 * (OFF_PTP_RESERVED), and disable interrupts. When we do the per-port
	 * configuration later, we will also allow overwrites (by not setting
	 * the DISABLE_OVERWRITE bit). This combination lets us handle
	 * back-to-back RX packets easily, because we don't have to do an SMI
	 * access to retrieve the timestamp.
	 */
	for (i = 0; i < mv88e6xxx_num_ports(chip); ++i) {
		u16 val = MV88E6XXX_PORT_PTP_CFG2_EMBED_ARRIVAL;

		err = mv88e6xxx_port_ptp_write(chip, i,
					       MV88E6XXX_PORT_PTP_CFG2, val);
		if (err)
			return err;
	}

	return 0;
}

void mv88e6xxx_hwtstamp_free(struct mv88e6xxx_chip *chip)
{
	int i;

	for (i = 0; i < mv88e6xxx_num_ports(chip); ++i)
		mv88e6xxx_hwtstamp_port_free(chip, i);
}
