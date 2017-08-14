/*
 * net/dsa/debugfs.c - DSA debugfs interface
 * Copyright (c) 2017 Savoir-faire Linux, Inc.
 *	Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/debugfs.h>

#include "dsa_priv.h"

#define DSA_SWITCH_FMT	"switch%d"
#define DSA_PORT_FMT	"port%d"

/* DSA module debugfs directory */
static struct dentry *dsa_debugfs_dir;

static int dsa_debugfs_create_port(struct dsa_switch *ds, int port)
{
	struct dentry *dir;
	char name[32];

	snprintf(name, sizeof(name), DSA_PORT_FMT, port);

	dir = debugfs_create_dir(name, ds->debugfs_dir);
	if (IS_ERR_OR_NULL(dir))
		return -EFAULT;

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

	for (i = 0; i < ds->num_ports; i++) {
		if (ds->enabled_port_mask & BIT(i)) {
			err = dsa_debugfs_create_port(ds, i);
			if (err)
				return err;
		}
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

	for (i = 0; i < DSA_MAX_SWITCHES; i++) {
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

	for (i = 0; i < DSA_MAX_SWITCHES; i++) {
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
