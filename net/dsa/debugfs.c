/*
 * net/dsa/debugfs.c - DSA debugfs interface
 * Copyright (c) 2017 Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/debugfs.h>
#include <linux/if_bridge.h>
#include <linux/seq_file.h>
#include <net/dsa.h>
#include <net/switchdev.h>

#include "dsa_priv.h"

#define DSA_SWITCH_FMT	"switch%d"
#define DSA_PORT_FMT	"port%d"

/* DSA module debugfs directory */
static struct dentry *dsa_debugfs_dir;

struct dsa_debugfs_ops {
	int (*read)(struct dsa_switch *ds, int id, struct seq_file *seq);
	int (*write)(struct dsa_switch *ds, int id, char *buf);
};

static int dsa_debugfs_ageing_time_read(struct dsa_switch *ds, int id,
					struct seq_file *seq)
{
	seq_printf(seq, "%d\n", ds->ports[id].ageing_time);

	return 0;
}

static const struct dsa_debugfs_ops dsa_debugfs_ageing_time_ops = {
	.read = dsa_debugfs_ageing_time_read,
};

struct dsa_debugfs_fdb_dump {
	struct switchdev_obj_port_fdb fdb;
	struct seq_file *seq;
};

static int dsa_debugfs_fdb_dump_cb(struct switchdev_obj *obj)
{
	struct switchdev_obj_port_fdb *fdb;
	struct seq_file *seq;
	int i;

	fdb = SWITCHDEV_OBJ_PORT_FDB(obj);
	seq = container_of(fdb, struct dsa_debugfs_fdb_dump, fdb)->seq;

	seq_printf(seq, "vid %d", fdb->vid);
	for (i = 0; i < ETH_ALEN; ++i)
		seq_printf(seq, "%s%02x", i ? ":" : "    ", fdb->addr[i]);
	seq_printf(seq, "    nud 0x%x\n", fdb->ndm_state);

	return 0;
}

static int dsa_debugfs_fdb_read(struct dsa_switch *ds, int id,
				struct seq_file *seq)
{
	struct dsa_debugfs_fdb_dump dump = {
		.fdb.obj.id = SWITCHDEV_OBJ_ID_PORT_FDB,
		.seq = seq,
	};

	if (ds->ops->port_fdb_dump)
		return ds->ops->port_fdb_dump(ds, id, &dump.fdb,
					      dsa_debugfs_fdb_dump_cb);

	return -EOPNOTSUPP;
}

static const struct dsa_debugfs_ops dsa_debugfs_fdb_ops = {
	.read = dsa_debugfs_fdb_read,
};

struct dsa_debugfs_mdb_dump {
	struct switchdev_obj_port_mdb mdb;
	struct seq_file *seq;
};

static int dsa_debugfs_mdb_dump_cb(struct switchdev_obj *obj)
{
	struct switchdev_obj_port_mdb *mdb;
	struct seq_file *seq;
	int i;

	mdb = SWITCHDEV_OBJ_PORT_MDB(obj);
	seq = container_of(mdb, struct dsa_debugfs_mdb_dump, mdb)->seq;

	seq_printf(seq, "vid %d", mdb->vid);
	for (i = 0; i < ETH_ALEN; ++i)
		seq_printf(seq, "%s%02x", i ? ":" : "    ", mdb->addr[i]);
	seq_puts(seq, "\n");

	return 0;
}

static int dsa_debugfs_mdb_read(struct dsa_switch *ds, int id,
				struct seq_file *seq)
{
	struct dsa_debugfs_mdb_dump dump = {
		.mdb.obj.id = SWITCHDEV_OBJ_ID_PORT_MDB,
		.seq = seq,
	};

	if (ds->ops->port_mdb_dump)
		return ds->ops->port_mdb_dump(ds, id, &dump.mdb,
					      dsa_debugfs_mdb_dump_cb);

	return -EOPNOTSUPP;
}

static const struct dsa_debugfs_ops dsa_debugfs_mdb_ops = {
	.read = dsa_debugfs_mdb_read,
};

static int dsa_debugfs_stp_state_read(struct dsa_switch *ds, int id,
					struct seq_file *seq)
{
	switch (ds->ports[id].stp_state) {
	case BR_STATE_DISABLED:
		seq_puts(seq, "disabled\n");
		break;
	case BR_STATE_LISTENING:
		seq_puts(seq, "listening\n");
		break;
	case BR_STATE_LEARNING:
		seq_puts(seq, "learning\n");
		break;
	case BR_STATE_FORWARDING:
		seq_puts(seq, "forwarding\n");
		break;
	case BR_STATE_BLOCKING:
		seq_puts(seq, "blocking\n");
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct dsa_debugfs_ops dsa_debugfs_stp_state_ops = {
	.read = dsa_debugfs_stp_state_read,
};

static int dsa_debugfs_tag_protocol_read(struct dsa_switch *ds, int id,
					 struct seq_file *seq)
{
	enum dsa_tag_protocol proto = DSA_TAG_LAST;

	if (ds->ops->get_tag_protocol)
		proto = ds->ops->get_tag_protocol(ds);

	switch (proto) {
	case DSA_TAG_PROTO_NONE:
		seq_puts(seq, "NONE\n");
		break;
	case DSA_TAG_PROTO_DSA:
		seq_puts(seq, "DSA\n");
		break;
	case DSA_TAG_PROTO_TRAILER:
		seq_puts(seq, "TRAILER\n");
		break;
	case DSA_TAG_PROTO_EDSA:
		seq_puts(seq, "EDSA\n");
		break;
	case DSA_TAG_PROTO_BRCM:
		seq_puts(seq, "BRCM\n");
		break;
	case DSA_TAG_PROTO_QCA:
		seq_puts(seq, "QCA\n");
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static const struct dsa_debugfs_ops dsa_debugfs_tag_protocol_ops = {
	.read = dsa_debugfs_tag_protocol_read,
};

static int dsa_debugfs_tree_read(struct dsa_switch *ds, int id,
				 struct seq_file *seq)
{
	seq_printf(seq, "%d\n", ds->dst->tree);

	return 0;
}

static const struct dsa_debugfs_ops dsa_debugfs_tree_ops = {
	.read = dsa_debugfs_tree_read,
};

struct dsa_debugfs_vlan_dump {
	struct switchdev_obj_port_vlan vlan;
	struct seq_file *seq;
};

static int dsa_debugfs_vlan_dump_cb(struct switchdev_obj *obj)
{
	struct switchdev_obj_port_vlan *vlan;
	struct seq_file *seq;
	int i;

	vlan = SWITCHDEV_OBJ_PORT_VLAN(obj);
	seq = container_of(vlan, struct dsa_debugfs_vlan_dump, vlan)->seq;

	for (i = vlan->vid_begin; i <= vlan->vid_end; ++i) {
		seq_printf(seq, "vid %d", i);
		if (vlan->flags & BRIDGE_VLAN_INFO_PVID)
			seq_puts(seq, "  pvid");
		if (vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED)
			seq_puts(seq, "  untagged");
		seq_puts(seq, "\n");
	}

	return 0;
}

static int dsa_debugfs_vlan_read(struct dsa_switch *ds, int id,
				 struct seq_file *seq)
{
	struct dsa_debugfs_vlan_dump dump = {
		.vlan.obj.id = SWITCHDEV_OBJ_ID_PORT_VLAN,
		.seq = seq,
	};

	if (ds->ops->port_vlan_dump)
		return ds->ops->port_vlan_dump(ds, id, &dump.vlan,
					       dsa_debugfs_vlan_dump_cb);

	return -EOPNOTSUPP;
}

static const struct dsa_debugfs_ops dsa_debugfs_vlan_ops = {
	.read = dsa_debugfs_vlan_read,
};

static int dsa_debugfs_stats_read(struct dsa_switch *ds, int id,
				  struct seq_file *seq)
{
	uint8_t *strings, *string;
	uint64_t *stats;
	int count, i;
	int err = 0;

	if (!(ds->ops->get_strings && ds->ops->get_sset_count &&
	      ds->ops->get_ethtool_stats))
		return -EOPNOTSUPP;

	count = ds->ops->get_sset_count(ds);
	if (count < 0)
		return count;

	strings = kcalloc(count, ETH_GSTRING_LEN, GFP_KERNEL);
	if (!strings)
		return -ENOMEM;

	ds->ops->get_strings(ds, id, strings);

	stats = kcalloc(count, sizeof(uint64_t), GFP_KERNEL);
	if (!stats) {
		err = -ENOMEM;
		goto out_strings;
	}

	ds->ops->get_ethtool_stats(ds, id, stats);

	string = strings;
	for (i=0; i < count; i++) {
		seq_printf(seq, "%-20s: %lld\n", string, stats[i]);
		string += ETH_GSTRING_LEN;
	}

	kfree(stats);
out_strings:
	kfree(strings);

	return err;
}

static const struct dsa_debugfs_ops dsa_debugfs_stats_ops = {
	.read = dsa_debugfs_stats_read,
};

struct dsa_debugfs_priv {
	const struct dsa_debugfs_ops *ops;
	struct dsa_switch *ds;
	int id;
};

static int dsa_debugfs_show(struct seq_file *seq, void *p)
{
	struct dsa_debugfs_priv *priv = seq->private;
	struct dsa_switch *ds = priv->ds;
	int err;

	//mutex_lock(&ds->dst->lock);
	err = priv->ops->read(ds, priv->id, seq);
	//mutex_unlock(&ds->dst->lock);

	return err;
}

static ssize_t dsa_debugfs_write(struct file *file, const char __user *user_buf,
				 size_t count, loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	struct dsa_debugfs_priv *priv = seq->private;
	struct dsa_switch *ds = priv->ds;
	char buf[count + 1];
	int err;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;
	buf[count] = '\0';

	//mutex_lock(&ds->dst->lock);
	err = priv->ops->write(ds, priv->id, buf);
	//mutex_unlock(&ds->dst->lock);

	return err ? err : count;
}

static int dsa_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, dsa_debugfs_show, inode->i_private);
}

static const struct file_operations dsa_debugfs_fops = {
	.open = dsa_debugfs_open,
	.read = seq_read,
	.write = dsa_debugfs_write,
	.llseek = no_llseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int dsa_debugfs_create_file(struct dsa_switch *ds, struct dentry *dir,
				   char *name, int id,
				   const struct dsa_debugfs_ops *ops)
{
	struct dsa_debugfs_priv *priv;
	struct dentry *entry;
	umode_t mode;

	priv = devm_kzalloc(ds->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->ds = ds;
	priv->ops = ops;
	priv->id = id;

	mode = 0;
	if (ops->read)
		mode |= S_IRUGO;
	if (ops->write)
		mode |= S_IWUSR;

	entry = debugfs_create_file(name, mode, dir, priv, &dsa_debugfs_fops);
	if (IS_ERR_OR_NULL(entry))
		return -EFAULT;

	return 0;
}

static int dsa_debugfs_create_symlink_link(struct dsa_switch *ds,
					   struct dentry *dir, int port)
{
	struct dentry *symlink;
	char target[32];

	if (dsa_is_dsa_port(ds, port)) {
		int i, p;

		/* It is currently not possible to distinguish a physical link
		 * between two switches, from a hop, which is sad...
		 */
		for (i = 0; i < DSA_MAX_SWITCHES; ++i)
			if (ds->rtable[i] == port)
				break;

		if (i == DSA_MAX_SWITCHES)
			return -EINVAL;

		p = ds->dst->ds[i]->rtable[ds->index];

		snprintf(target, sizeof(target),
			 "../../" DSA_SWITCH_FMT "/" DSA_PORT_FMT, i, p);
	} else {
		struct net_device *netdev;

		if (dsa_is_cpu_port(ds, port))
			netdev = ds->master_netdev;
		else
			netdev = ds->ports[port].netdev;

		snprintf(target, sizeof(target), "/sys/class/net/%s",
			 netdev_name(netdev));
	}

	symlink = debugfs_create_symlink("link", dir, target);
	if (IS_ERR_OR_NULL(symlink))
		return -EFAULT;

	return 0;
}

static int dsa_debugfs_create_symlink_upstream(struct dsa_switch *ds)
{
	struct dentry *symlink;
	char target[32];

	snprintf(target, sizeof(target), DSA_PORT_FMT, dsa_upstream_port(ds));

	symlink = debugfs_create_symlink("upstream", ds->debugfs_dir, target);
	if (IS_ERR_OR_NULL(symlink))
		return -EFAULT;

	return 0;
}

static int dsa_debugfs_create_port(struct dsa_switch *ds, int port)
{
	struct dentry *dir;
	char name[32];
	int err;

	snprintf(name, sizeof(name), DSA_PORT_FMT, port);

	dir = debugfs_create_dir(name, ds->debugfs_dir);
	if (IS_ERR_OR_NULL(dir))
		return -EFAULT;

	err = dsa_debugfs_create_symlink_link(ds, dir, port);
	if (err)
		return err;

	err = dsa_debugfs_create_file(ds, dir, "ageing_time", port,
				      &dsa_debugfs_ageing_time_ops);
	if (err)
		return err;

	err = dsa_debugfs_create_file(ds, dir, "fdb", port,
				      &dsa_debugfs_fdb_ops);
	if (err)
		return err;

	err = dsa_debugfs_create_file(ds, dir, "mdb", port,
				      &dsa_debugfs_mdb_ops);
	if (err)
		return err;

	err = dsa_debugfs_create_file(ds, dir, "stp_state", port,
				      &dsa_debugfs_stp_state_ops);
	if (err)
		return err;

	err = dsa_debugfs_create_file(ds, dir, "vlan", port,
				      &dsa_debugfs_vlan_ops);
	if (err)
		return err;

	err = dsa_debugfs_create_file(ds, dir, "stats", port,
				      &dsa_debugfs_stats_ops);
	if (err)
		return err;

	return 0;
}

static int dsa_debugfs_create_switch(struct dsa_switch *ds)
{
	char name[32];
	int i, err;

	/* skip if there is no debugfs support */
	if (!dsa_debugfs_dir)
		return 0;

	snprintf(name, sizeof(name), DSA_SWITCH_FMT, ds->index);

	ds->debugfs_dir = debugfs_create_dir(name, dsa_debugfs_dir);
	if (IS_ERR_OR_NULL(ds->debugfs_dir))
		return -EFAULT;

	err = dsa_debugfs_create_symlink_upstream(ds);
	if (err)
		return err;

	err = dsa_debugfs_create_file(ds, ds->debugfs_dir, "tag_protocol", -1,
				      &dsa_debugfs_tag_protocol_ops);
	if (err)
		return err;

	err = dsa_debugfs_create_file(ds, ds->debugfs_dir, "tree", -1,
				      &dsa_debugfs_tree_ops);
	if (err)
		return err;

	for (i = 0; i < DSA_MAX_PORTS; ++i) {
		if (!ds->ports[i].dn)
			continue;

		err = dsa_debugfs_create_port(ds, i);
		if (err)
			return err;
	}

	return 0;
}

static void dsa_debugfs_destroy_switch(struct dsa_switch *ds)
{
	/* handles NULL */
	debugfs_remove_recursive(ds->debugfs_dir);
}

void dsa_debugfs_create_tree(struct dsa_switch_tree *dst)
{
	struct dsa_switch *ds;
	int i, err;

	WARN_ON(!dst->applied);

	for (i = 0; i < DSA_MAX_SWITCHES; ++i) {
		ds = dst->ds[i];
		if (!ds)
			continue;

		err = dsa_debugfs_create_switch(ds);
		if (err) {
			pr_warn("DSA: failed to create debugfs interface for switch %d (%d)\n",
				ds->index, err);
			dsa_debugfs_destroy_tree(dst);
			break;
		}
	}
}

void dsa_debugfs_destroy_tree(struct dsa_switch_tree *dst)
{
	struct dsa_switch *ds;
	int i;

	for (i = 0; i < DSA_MAX_SWITCHES; ++i) {
		ds = dst->ds[i];
		if (!ds)
			continue;

		dsa_debugfs_destroy_switch(ds);
	}
}

void dsa_debugfs_create_module(void)
{
	dsa_debugfs_dir = debugfs_create_dir("dsa", NULL);
	if (IS_ERR(dsa_debugfs_dir)) {
		pr_warn("DSA: failed to create debugfs interface\n");
		dsa_debugfs_dir = NULL;
	}

	if (dsa_debugfs_dir)
		pr_info("DSA: debugfs interface created\n");
}

void dsa_debugfs_destroy_module(void)
{
	/* handles NULL */
	debugfs_remove_recursive(dsa_debugfs_dir);
}
