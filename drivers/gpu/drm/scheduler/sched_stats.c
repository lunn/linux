/*
 * Copyright 2018 Pengutronix
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <drm/drm_sysfs.h>
#include <drm/gpu_scheduler.h>

#include "sched_stats.h"

struct drm_sched_stats_engines {
	struct kobject		kobj;
};

static struct device_type drm_sched_class_type = {
	.name = "scheduler",
};

static struct device drm_sched_class_device = {
	.type = &drm_sched_class_type,
};

static void drm_sched_stats_rings_kobj_release(struct kobject *kobj)
{
	struct drm_sched_stats_engines *engines =
		container_of(kobj, struct drm_sched_stats_engines, kobj);

	kfree(engines);
}

static struct kobj_type drm_sched_rings_class_type = {
	.release = &drm_sched_stats_rings_kobj_release,
};

static struct drm_sched_stats_engines *rings;


static struct attribute drm_sched_stats_active = {
	.name = "active_us",
	.mode = S_IRUGO
};

static void drm_sched_stats_kobj_release(struct kobject *kobj)
{
	struct drm_gpu_scheduler_stats *stats =
		container_of(kobj, struct drm_gpu_scheduler_stats, kobj);

	kfree(stats);
}

static ssize_t drm_sched_stats_show(struct kobject *kobj,
				 struct attribute *attr,
				 char *buffer)
{
	struct drm_gpu_scheduler_stats *stats =
		container_of(kobj, struct drm_gpu_scheduler_stats, kobj);
	uint64_t val = 0;

	spin_lock_irq(&stats->lock);

	val = stats->active_time_us;
	if (stats->active)
		val += ktime_to_us(ktime_sub(ktime_get(), stats->active_ts));

	spin_unlock_irq(&stats->lock);

	return snprintf(buffer, PAGE_SIZE, "%llu\n", val);
}
static struct attribute *drm_sched_stats_attrs[] = {
	&drm_sched_stats_active,
	NULL
};

static const struct sysfs_ops drm_sched_stats_ops = {
	.show = &drm_sched_stats_show,
};

static struct kobj_type drm_sched_stats_kobj_type = {
	.release = drm_sched_stats_kobj_release,
	.sysfs_ops = &drm_sched_stats_ops,
	.default_attrs = drm_sched_stats_attrs,
};

int drm_sched_stats_init(struct drm_gpu_scheduler *sched)
{
	int ret;

	sched->stats = kzalloc(sizeof(*sched->stats), GFP_KERNEL);
	if (!sched->stats)
		return -ENOMEM;

	spin_lock_init(&sched->stats->lock);

	ret = kobject_init_and_add(&sched->stats->kobj,
				   &drm_sched_stats_kobj_type,
				   &rings->kobj, "%s", sched->name);

	if (ret) {
		pr_warn("could not inititialize stats for drm scheduler %s\n",
			sched->name);
		kfree(sched->stats);
		sched->stats = NULL;
	}

	return 0;
}

void drm_sched_stats_fini(struct drm_gpu_scheduler *sched)
{
	if (!sched->stats)
		return;

	kobject_put(&sched->stats->kobj);
	sched->stats = NULL;
}

int __init drm_sched_stats_class_init(void)
{
	int ret;

	ret = dev_set_name(&drm_sched_class_device, "scheduler");
	if (ret)
		return ret;

	ret = drm_class_device_register(&drm_sched_class_device);
	if (ret)
		return ret;

	rings = kzalloc(sizeof(*rings), GFP_KERNEL);
	if (!rings) {
		ret = -ENOMEM;
		goto unregister_device;
	}
	ret = kobject_init_and_add(&rings->kobj,
				   &drm_sched_rings_class_type,
				   &drm_sched_class_device.kobj,
				   "%s", "rings");
	if (ret) {
		kobject_put(&rings->kobj);
		goto unregister_device;
	}

	return 0;

unregister_device:
	drm_class_device_unregister(&drm_sched_class_device);

	return ret;
}

void __exit drm_sched_stats_class_fini(void)
{
	drm_class_device_unregister(&drm_sched_class_device);
}
