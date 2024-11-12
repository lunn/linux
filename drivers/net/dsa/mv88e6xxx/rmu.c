// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Marvell 88E6xxx Switch Remote Management Unit Support
 *
 * Copyright (c) 2022 Mattias Forsblad <mattias.forsblad@gmail.com>
 *
 */

#include <linux/dsa/mv88e6xxx.h>
#include <net/dsa.h>
#include "chip.h"
#include "global1.h"
#include "port.h"
#include "rmu.h"

static const u8 mv88e6xxx_rmu_dest_addr[ETH_ALEN] = {
	0x01, 0x50, 0x43, 0x00, 0x00, 0x00
};

static void mv88e6xxx_rmu_create_l2(struct dsa_switch *ds,
				    struct mv88e6xxx_chip *chip,
				    struct sk_buff *skb,
				    bool edsa)
{
	struct dsa_tagger_data *tagger_data = ds->tagger_data;
	struct ethhdr *eth;
	u8 *header;

	/* Create RMU L2 header. */
	header = skb_push(skb, 2);
	/* Two bytes of EtherType, which is ignored by the switch */
	header[0] = 0;
	header[1] = 0;

	/* Ask tagger to add {E}DSA header */
	tagger_data->rmu_reg2frame(ds, skb);

	/* Insert RMU MAC destination address */
	eth = skb_push(skb, ETH_ALEN * 2);
	memcpy(eth->h_dest, mv88e6xxx_rmu_dest_addr, ETH_ALEN);
	ether_addr_copy(eth->h_source, chip->rmu_master->dev_addr);
	skb_reset_network_header(skb);
}

static void mv88e6xxx_rmu_fill_seqno(struct sk_buff *skb, u32 seqno, int offset)
{
	u8 *dsa_header = skb->data + offset;

	dsa_header[3] = seqno;
}

/* 2 MAC address, 2 byte Ethertype, 2 bytes padding, to DSA header */
static void mv88e6xxx_rmu_fill_seqno_edsa(struct sk_buff *skb, u32 seqno)
{
	mv88e6xxx_rmu_fill_seqno(skb, seqno, ETH_ALEN * 2 + 2 + 2);
}

/* 2 MAC address, to DSA header */
static void mv88e6xxx_rmu_fill_seqno_dsa(struct sk_buff *skb, u32 seqno)
{
	mv88e6xxx_rmu_fill_seqno(skb, seqno, ETH_ALEN * 2);
}

static int mv88e6xxx_rmu_request(struct mv88e6xxx_chip *chip,
				 const void *req, int req_len,
				 void *resp, unsigned int resp_len)
{
	struct sk_buff *skb;
	unsigned char *data;
	bool edsa;

	skb = dev_alloc_skb(64);
	if (!skb)
		return -ENOMEM;

	/* Insert RMU request message */
	data = skb_put(skb, req_len);
	memcpy(data, req, req_len);

	edsa = chip->tag_protocol == DSA_TAG_PROTO_EDSA;

	mv88e6xxx_rmu_create_l2(chip->ds, chip, skb, edsa);
	skb->dev = chip->rmu_master;

	return dsa_inband_request(&chip->rmu_inband, skb,
				  (edsa ? mv88e6xxx_rmu_fill_seqno_edsa :
				   mv88e6xxx_rmu_fill_seqno_dsa),
				  resp, resp_len,
				  MV88E6XXX_RMU_WAIT_TIME_MS);
}

int mv88e6xxx_rmu_stats(struct mv88e6xxx_chip *chip, int port,
			uint64_t *data,
			struct mv88e6xxx_hw_stat *hw_stats,
			int num_hw_stats,
			u16 bank1_select, u16 histogram)
{
	__be16 req[] = {
		MV88E6XXX_RMU_REQ_FORMAT_SOHO,
		MV88E6XXX_RMU_REQ_PAD,
		MV88E6XXX_RMU_REQ_CODE_MIB,
		htons(port),
	};
	struct mv88e6xxx_hw_stat *stat;
	struct mv88e6xxx_rmu_mib_resp resp;
	int i, j, ret;
	int resp_len;
	u64 high;

	if (!chip->rmu_enabled)
		return -EOPNOTSUPP;

	resp_len = sizeof(resp);
	ret = mv88e6xxx_rmu_request(chip, req, sizeof(req),
				    &resp, resp_len);
	if (ret <  0) {
		dev_dbg(chip->dev, "RMU: error for command MIB %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	if (ret < resp_len) {
		dev_err(chip->dev, "RMU: MIB returned wrong length %d %d\n",
			resp_len, ret);
		return -EPROTO;
	}

	if (resp.rmu_header.code != MV88E6XXX_RMU_RESP_CODE_MIB) {
		dev_err(chip->dev, "RMU: MIB returned wrong code %d\n",
			be16_to_cpu(resp.rmu_header.code));
		return -EPROTO;
	}


	for (i = 0, j = 0; i < num_hw_stats; i++) {
		stat = &hw_stats[i];
		if (!(stat->type & chip->info->stats_type))
			continue;

		if (stat->type & STATS_TYPE_PORT) {
			switch (stat->reg) {
			case MV88E6XXX_PORT_IN_DISCARD_LO:
				data[j] = be16_to_cpu(resp.port[0]) << 16;
				data[j] |= be16_to_cpu(resp.port[1]);
				break;
			case MV88E6XXX_PORT_IN_FILTERED:
				data[j] = be16_to_cpu(resp.port[3]);
				break;
			case MV88E6XXX_PORT_OUT_FILTERED:
				data[j] = be16_to_cpu(resp.port[5]);
				break;
			default:
				return -EINVAL;
			}
		}

		if (stat->type & STATS_TYPE_BANK0) {
			data[j] = be32_to_cpu(resp.bank0[stat->reg]);
			if (stat->size == 8) {
				high = be32_to_cpu(resp.bank0[stat->reg + 1]);
				data[j] |= (high << 32);
			}
		}

		if (stat->type & STATS_TYPE_BANK1) {
			/* Not available via RMU, use SMI */
			data[j] = mv88e6xxx_get_ethtool_stat(chip, stat, port,
							     bank1_select, histogram);
		}
		j++;
	}

	return j;
}

int mv88e6xxx_rmu_write(struct mv88e6xxx_chip *chip, int addr, int reg, u16 val)
{
	__be16 req[] = {
		MV88E6XXX_RMU_REQ_FORMAT_SOHO,
		MV88E6XXX_RMU_REQ_PAD,
		MV88E6XXX_RMU_REQ_CODE_REG_RW,
		MV88E6XXX_RMU_REQ_RW_0_WRITE(addr, reg),
		htons(val),
		MV88E6XXX_RMU_REQ_RW_0_END,
		MV88E6XXX_RMU_REQ_RW_1_END,
	};
	struct mv88e6xxx_rmu_header resp;
	int resp_len;
	int ret = -1;

	if (!chip->rmu_enabled || chip->rmu_is_slow)
		return -EOPNOTSUPP;

	resp_len = sizeof(resp);
	ret = mv88e6xxx_rmu_request(chip, req, sizeof(req),
				    &resp, resp_len);
	if (ret <  0) {
		dev_dbg(chip->dev, "RMU: error for command write %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	if (ret < resp_len) {
		dev_err(chip->dev, "RMU: write returned wrong length %d %d\n",
			resp_len, ret);
		return -EPROTO;
	}

	if (resp.code != MV88E6XXX_RMU_RESP_CODE_REG_RW) {
		dev_err(chip->dev, "RMU: write returned wrong code %d\n",
			be16_to_cpu(resp.code));
		return -EPROTO;
	}

	return 0;
}

static void mv88e6xxx_rmu_read_latancy(struct mv88e6xxx_chip *chip,
				       ktime_t latency)
{
	ktime_t average = 0;
	int i;

	if (chip->rmu_samples > ARRAY_SIZE(chip->rmu_read_latancies))
		return;

	chip->rmu_read_latancies[chip->rmu_samples++] = latency;

	if (chip->rmu_samples == ARRAY_SIZE(chip->rmu_read_latancies)) {
		for (i = 0; i < ARRAY_SIZE(chip->rmu_read_latancies); i++)
			average += chip->rmu_read_latancies[i];
		average = average / ARRAY_SIZE(chip->rmu_read_latancies);

		dev_dbg(chip->dev, "RMU %lldus, smi %lldus\n",
			div_u64(average, 1000),
			div_u64(chip->smi_read_latancy, 1000));

		if (chip->smi_read_latancy < average)
			chip->rmu_is_slow = true;

		chip->rmu_samples = U32_MAX;
	}
}

int mv88e6xxx_rmu_read(struct mv88e6xxx_chip *chip, int addr, int reg,
		       u16 *val)
{
	__be16 req[] = {
		MV88E6XXX_RMU_REQ_FORMAT_SOHO,
		MV88E6XXX_RMU_REQ_PAD,
		MV88E6XXX_RMU_REQ_CODE_REG_RW,
		MV88E6XXX_RMU_REQ_RW_0_READ(addr, reg),
		0,
		MV88E6XXX_RMU_REQ_RW_0_END,
		MV88E6XXX_RMU_REQ_RW_1_END,
	};
	struct mv88e6xxx_rmu_rw_resp resp;
	int resp_len;
	ktime_t start;
	int ret;

	if (!chip->rmu_enabled || chip->rmu_is_slow)
		return -EOPNOTSUPP;

	start = ktime_get();

	resp_len = sizeof(resp);
	ret = mv88e6xxx_rmu_request(chip, req, sizeof(req),
				    &resp, resp_len);
	if (ret <  0) {
		dev_dbg(chip->dev, "RMU: error for command read %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	if (ret < resp_len) {
		dev_err(chip->dev, "RMU: read returned wrong length %d %d\n",
			resp_len, ret);
		return -EPROTO;
	}

	if (resp.rmu_header.code != MV88E6XXX_RMU_RESP_CODE_REG_RW) {
		dev_err(chip->dev, "RMU: read returned wrong code %d\n",
			be16_to_cpu(resp.rmu_header.code));
		return -EPROTO;
	}

	mv88e6xxx_rmu_read_latancy(chip, ktime_get() - start);

	*val = ntohs(resp.value);
	return 0;
}

int mv88e6xxx_rmu_wait_bit(struct mv88e6xxx_chip *chip, int addr, int reg,
			   int bit, int val)
{
	__be16 req[] = {
		MV88E6XXX_RMU_REQ_FORMAT_SOHO,
		MV88E6XXX_RMU_REQ_PAD,
		MV88E6XXX_RMU_REQ_CODE_REG_RW,
		val ? MV88E6XXX_RMU_REQ_RW_0_WAIT_1(addr, reg) :
		MV88E6XXX_RMU_REQ_RW_0_WAIT_0(addr, reg),
		htons(bit),
		MV88E6XXX_RMU_REQ_RW_0_END,
		MV88E6XXX_RMU_REQ_RW_1_END,
	};
	struct mv88e6xxx_rmu_header resp;
	int resp_len;
	int ret = -1;

	if (!chip->rmu_enabled || chip->rmu_is_slow)
		return -EOPNOTSUPP;

	resp_len = sizeof(resp);
	ret = mv88e6xxx_rmu_request(chip, req, sizeof(req),
				    &resp, resp_len);
	if (ret <  0) {
		dev_dbg(chip->dev, "RMU: error for command write %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	if (ret < resp_len) {
		dev_err(chip->dev, "RMU: wait bit returned wrong length %d %d\n",
			resp_len, ret);
		return -EPROTO;
	}

	if (resp.code != MV88E6XXX_RMU_RESP_CODE_REG_RW) {
		dev_err(chip->dev, "RMU: wait bit returned wrong code %d\n",
			be16_to_cpu(resp.code));
		return -EPROTO;
	}

	return 0;
}

static int mv88e6xxx_rmu_get_id(struct mv88e6xxx_chip *chip)
{
	const __be16 req[4] = {
		MV88E6XXX_RMU_REQ_FORMAT_GET_ID,
		MV88E6XXX_RMU_REQ_PAD,
		MV88E6XXX_RMU_REQ_CODE_GET_ID,
		MV88E6XXX_RMU_REQ_DATA};
	struct mv88e6xxx_rmu_header resp;
	int resp_len;
	int ret = -1;

	resp_len = sizeof(resp);
	ret = mv88e6xxx_rmu_request(chip, req, sizeof(req),
				    &resp, resp_len);
	if (ret <  0) {
		dev_dbg(chip->dev, "RMU: error for command GET_ID %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	if (ret < resp_len) {
		dev_err(chip->dev, "RMU: GET_ID returned wrong length %d %d\n",
			resp_len, ret);
		return -EPROTO;
	}

	if (resp.code != MV88E6XXX_RMU_RESP_CODE_GOT_ID) {
		dev_dbg(chip->dev, "RMU: GET_ID returned wrong code %d\n",
			be16_to_cpu(resp.code));
		return -EPROTO;
	}

	dev_dbg(chip->dev, "RMU: product ID %4x\n", be16_to_cpu(resp.prodnr));

	return 0;
}

void mv88e6xxx_rmu_conduit_state_change(struct dsa_switch *ds,
					const struct net_device *master,
					bool operational)
{
	struct dsa_port *cpu_dp = master->dsa_ptr;
	struct mv88e6xxx_chip *chip = ds->priv;
	ktime_t start;
	int port;
	int ret;
	u16 id;

	port = dsa_towards_port(ds, cpu_dp->ds->index, cpu_dp->index);

	mv88e6xxx_reg_lock(chip);

	if (operational && chip->info->ops->rmu_enable) {
		ret = chip->info->ops->rmu_enable(chip, port);

		if (ret == -EOPNOTSUPP)
			goto out;

		if (ret < 0) {
			dev_err(chip->dev, "RMU: Unable to enable on port %d %pe",
				port, ERR_PTR(ret));
			goto out;
		}

		chip->rmu_master = (struct net_device *)master;

		/* Get the device ID to prove that the RMU works */
		ret = mv88e6xxx_rmu_get_id(chip);
		if (ret < 0) {
			dev_err(chip->dev, "RMU: Check failed %pe",
				ERR_PTR(ret));
			goto out;
		}

		start = ktime_get();
		ret = mv88e6xxx_port_read(chip, 0, MV88E6XXX_PORT_SWITCH_ID,
					  &id);
		chip->smi_read_latancy = ktime_get() - start;

		chip->rmu_enabled = true;

		dev_dbg(chip->dev, "RMU: Enabled on port %d", port);
	} else {
		if (chip->info->ops->rmu_disable)
			chip->info->ops->rmu_disable(chip);

		chip->rmu_enabled = false;
		chip->rmu_master = NULL;
	}

out:
	mv88e6xxx_reg_unlock(chip);
}

void mv88e6xxx_rmu_frame2reg_handler(struct dsa_switch *ds,
				     struct sk_buff *skb,
				     u8 seqno)
{
	struct mv88e6xxx_rmu_header *rmu_header;
	struct mv88e6xxx_chip *chip = ds->priv;
	unsigned char *ethhdr;
	u8 expected_seqno;
	int resp_len;
	int err = 0;

	/* Check received destination MAC is the masters MAC address*/
	if (!chip->rmu_master)
		goto drop;

	ethhdr = skb_mac_header(skb);
	if (!ether_addr_equal(chip->rmu_master->dev_addr, ethhdr)) {
		dev_dbg_ratelimited(ds->dev, "RMU: mismatching MAC address for request. Rx %pM expecting %pM\n",
				    ethhdr, chip->rmu_master->dev_addr);
		goto drop;
	}

	expected_seqno = dsa_inband_seqno(&chip->rmu_inband);
	if (seqno != expected_seqno) {
		dev_dbg_ratelimited(ds->dev, "RMU: mismatching seqno for request. Rx %d expecting %d\n",
				    seqno, expected_seqno);
		goto drop;
	}

	rmu_header = (struct mv88e6xxx_rmu_header *)(skb->data + 4);
	resp_len = skb->len - 4;
	if (rmu_header->format != MV88E6XXX_RMU_RESP_FORMAT_1 &&
	    rmu_header->format != MV88E6XXX_RMU_RESP_FORMAT_2) {
		dev_dbg_ratelimited(ds->dev, "RMU: invalid format. Rx %d\n",
				    be16_to_cpu(rmu_header->format));
		err = -EPROTO;
	}

	dsa_inband_complete(&chip->rmu_inband, rmu_header, resp_len, err);
drop:
	return;
}
