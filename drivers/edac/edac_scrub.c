// SPDX-License-Identifier: GPL-2.0
/*
 * Generic EDAC scrub driver supports controlling the memory
 * scrubbers in the system and the common sysfs scrub interface
 * promotes unambiguous access from the userspace.
 *
 * Copyright (c) 2024 HiSilicon Limited.
 */

#define pr_fmt(fmt)     "EDAC SCRUB: " fmt

#include <linux/edac.h>

enum edac_scrub_attributes {
	SCRUB_ADDR_RANGE_BASE,
	SCRUB_ADDR_RANGE_SIZE,
	SCRUB_ENABLE_BACKGROUND,
	SCRUB_ENABLE_ON_DEMAND,
	SCRUB_MIN_CYCLE_DURATION,
	SCRUB_MAX_CYCLE_DURATION,
	SCRUB_CURRENT_CYCLE_DURATION,
	SCRUB_MAX_ATTRS
};

struct edac_scrub_dev_attr {
	struct device_attribute dev_attr;
	u8 instance;
};

struct edac_scrub_context {
	char name[EDAC_FEAT_NAME_LEN];
	struct edac_scrub_dev_attr scrub_dev_attr[SCRUB_MAX_ATTRS];
	struct attribute *scrub_attrs[SCRUB_MAX_ATTRS + 1];
	struct attribute_group group;
};

#define to_scrub_dev_attr(_dev_attr)      \
		container_of(_dev_attr, struct edac_scrub_dev_attr, dev_attr)

static ssize_t addr_range_base_show(struct device *ras_feat_dev,
				    struct device_attribute *attr,
				    char *buf)
{
	u8 inst = ((struct edac_scrub_dev_attr *)to_scrub_dev_attr(attr))->instance;
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);
	const struct edac_scrub_ops *ops = ctx->scrub[inst].scrub_ops;
	u64 base, size;
	int ret;

	ret = ops->read_range(ras_feat_dev->parent, ctx->scrub[inst].private, &base, &size);
	if (ret)
		return ret;

	return sysfs_emit(buf, "0x%llx\n", base);
}

static ssize_t addr_range_size_show(struct device *ras_feat_dev,
				    struct device_attribute *attr,
				    char *buf)
{
	u8 inst = ((struct edac_scrub_dev_attr *)to_scrub_dev_attr(attr))->instance;
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);
	const struct edac_scrub_ops *ops = ctx->scrub[inst].scrub_ops;
	u64 base, size;
	int ret;

	ret = ops->read_range(ras_feat_dev->parent, ctx->scrub[inst].private, &base, &size);
	if (ret)
		return ret;

	return sysfs_emit(buf, "0x%llx\n", size);
}

static ssize_t addr_range_base_store(struct device *ras_feat_dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len)
{
	u8 inst = ((struct edac_scrub_dev_attr *)to_scrub_dev_attr(attr))->instance;
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);
	const struct edac_scrub_ops *ops = ctx->scrub[inst].scrub_ops;
	u64 base, size;
	int ret;

	ret = ops->read_range(ras_feat_dev->parent, ctx->scrub[inst].private, &base, &size);
	if (ret)
		return ret;

	ret = kstrtou64(buf, 0, &base);
	if (ret < 0)
		return ret;

	ret = ops->write_range(ras_feat_dev->parent, ctx->scrub[inst].private, base, size);
	if (ret)
		return ret;

	return len;
}

static ssize_t addr_range_size_store(struct device *ras_feat_dev,
				     struct device_attribute *attr,
				     const char *buf,
				     size_t len)
{
	u8 inst = ((struct edac_scrub_dev_attr *)to_scrub_dev_attr(attr))->instance;
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);
	const struct edac_scrub_ops *ops = ctx->scrub[inst].scrub_ops;
	u64 base, size;
	int ret;

	ret = ops->read_range(ras_feat_dev->parent, ctx->scrub[inst].private, &base, &size);
	if (ret)
		return ret;

	ret = kstrtou64(buf, 0, &size);
	if (ret < 0)
		return ret;

	ret = ops->write_range(ras_feat_dev->parent, ctx->scrub[inst].private, base, size);
	if (ret)
		return ret;

	return len;
}

static ssize_t enable_background_store(struct device *ras_feat_dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	u8 inst = ((struct edac_scrub_dev_attr *)to_scrub_dev_attr(attr))->instance;
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);
	const struct edac_scrub_ops *ops = ctx->scrub[inst].scrub_ops;
	bool enable;
	int ret;

	ret = kstrtobool(buf, &enable);
	if (ret < 0)
		return ret;

	ret = ops->set_enabled_bg(ras_feat_dev->parent, ctx->scrub[inst].private, enable);
	if (ret)
		return ret;

	return len;
}

static ssize_t enable_background_show(struct device *ras_feat_dev,
				      struct device_attribute *attr, char *buf)
{
	u8 inst = ((struct edac_scrub_dev_attr *)to_scrub_dev_attr(attr))->instance;
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);
	const struct edac_scrub_ops *ops = ctx->scrub[inst].scrub_ops;
	bool enable;
	int ret;

	ret = ops->get_enabled_bg(ras_feat_dev->parent, ctx->scrub[inst].private, &enable);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", enable);
}

static ssize_t enable_on_demand_show(struct device *ras_feat_dev,
				     struct device_attribute *attr, char *buf)
{
	u8 inst = ((struct edac_scrub_dev_attr *)to_scrub_dev_attr(attr))->instance;
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);
	const struct edac_scrub_ops *ops = ctx->scrub[inst].scrub_ops;
	bool enable;
	int ret;

	ret = ops->get_enabled_od(ras_feat_dev->parent, ctx->scrub[inst].private, &enable);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", enable);
}

static ssize_t enable_on_demand_store(struct device *ras_feat_dev,
				      struct device_attribute *attr,
				      const char *buf, size_t len)
{
	u8 inst = ((struct edac_scrub_dev_attr *)to_scrub_dev_attr(attr))->instance;
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);
	const struct edac_scrub_ops *ops = ctx->scrub[inst].scrub_ops;
	bool enable;
	int ret;

	ret = kstrtobool(buf, &enable);
	if (ret < 0)
		return ret;

	ret = ops->set_enabled_od(ras_feat_dev->parent, ctx->scrub[inst].private, enable);
	if (ret)
		return ret;

	return len;
}

static ssize_t min_cycle_duration_show(struct device *ras_feat_dev,
				       struct device_attribute *attr,
				       char *buf)
{
	u8 inst = ((struct edac_scrub_dev_attr *)to_scrub_dev_attr(attr))->instance;
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);
	const struct edac_scrub_ops *ops = ctx->scrub[inst].scrub_ops;
	u32 val;
	int ret;

	ret = ops->min_cycle_read(ras_feat_dev->parent, ctx->scrub[inst].private, &val);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", val);
}

static ssize_t max_cycle_duration_show(struct device *ras_feat_dev,
				       struct device_attribute *attr,
				       char *buf)
{
	u8 inst = ((struct edac_scrub_dev_attr *)to_scrub_dev_attr(attr))->instance;
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);
	const struct edac_scrub_ops *ops = ctx->scrub[inst].scrub_ops;
	u32 val;
	int ret;

	ret = ops->max_cycle_read(ras_feat_dev->parent, ctx->scrub[inst].private, &val);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", val);
}

static ssize_t current_cycle_duration_show(struct device *ras_feat_dev,
					   struct device_attribute *attr,
					   char *buf)
{
	u8 inst = ((struct edac_scrub_dev_attr *)to_scrub_dev_attr(attr))->instance;
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);
	const struct edac_scrub_ops *ops = ctx->scrub[inst].scrub_ops;
	u32 val;
	int ret;

	ret = ops->cycle_duration_read(ras_feat_dev->parent, ctx->scrub[inst].private, &val);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", val);
}

static ssize_t current_cycle_duration_store(struct device *ras_feat_dev,
					    struct device_attribute *attr,
					    const char *buf, size_t len)
{
	u8 inst = ((struct edac_scrub_dev_attr *)to_scrub_dev_attr(attr))->instance;
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);
	const struct edac_scrub_ops *ops = ctx->scrub[inst].scrub_ops;
	long val;
	int ret;

	ret = kstrtol(buf, 0, &val);
	if (ret < 0)
		return ret;

	ret = ops->cycle_duration_write(ras_feat_dev->parent, ctx->scrub[inst].private, val);
	if (ret)
		return ret;

	return len;
}

static umode_t scrub_attr_visible(struct kobject *kobj,
				  struct attribute *a, int attr_id)
{
	struct device *ras_feat_dev = kobj_to_dev(kobj);
	struct device_attribute *dev_attr =
				container_of(a, struct device_attribute, attr);
	u8 inst = ((struct edac_scrub_dev_attr *)to_scrub_dev_attr(dev_attr))->instance;
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);
	const struct edac_scrub_ops *ops = ctx->scrub[inst].scrub_ops;

	switch (attr_id) {
	case SCRUB_ADDR_RANGE_BASE:
	case SCRUB_ADDR_RANGE_SIZE:
		if (ops->read_range && ops->write_range)
			return a->mode;
		if (ops->read_range)
			return 0444;
		return 0;
	case SCRUB_ENABLE_BACKGROUND:
		if (ops->get_enabled_bg && ops->set_enabled_bg)
			return a->mode;
		if (ops->get_enabled_bg)
			return 0444;
		return 0;
	case SCRUB_ENABLE_ON_DEMAND:
		if (ops->get_enabled_od && ops->set_enabled_od)
			return a->mode;
		if (ops->get_enabled_od)
			return 0444;
		return 0;
	case SCRUB_MIN_CYCLE_DURATION:
		return ops->min_cycle_read ? a->mode : 0;
	case SCRUB_MAX_CYCLE_DURATION:
		return ops->max_cycle_read ? a->mode : 0;
	case SCRUB_CURRENT_CYCLE_DURATION:
		if (ops->cycle_duration_read && ops->cycle_duration_write)
			return a->mode;
		if (ops->cycle_duration_read)
			return 0444;
		return 0;
	default:
		return 0;
	}
}

#define EDAC_SCRUB_ATTR_RO(_name, _instance)       \
	((struct edac_scrub_dev_attr) { .dev_attr = __ATTR_RO(_name), \
				     .instance = _instance })

#define EDAC_SCRUB_ATTR_WO(_name, _instance)       \
	((struct edac_scrub_dev_attr) { .dev_attr = __ATTR_WO(_name), \
				     .instance = _instance })

#define EDAC_SCRUB_ATTR_RW(_name, _instance)       \
	((struct edac_scrub_dev_attr) { .dev_attr = __ATTR_RW(_name), \
				     .instance = _instance })

static int scrub_create_desc(struct device *scrub_dev,
			     const struct attribute_group **attr_groups,
			     u8 instance)
{
	struct edac_scrub_context *scrub_ctx;
	struct attribute_group *group;
	int i;

	scrub_ctx = devm_kzalloc(scrub_dev, sizeof(*scrub_ctx), GFP_KERNEL);
	if (!scrub_ctx)
		return -ENOMEM;

	group = &scrub_ctx->group;
	scrub_ctx->scrub_dev_attr[0] = EDAC_SCRUB_ATTR_RW(addr_range_base, instance);
	scrub_ctx->scrub_dev_attr[1] = EDAC_SCRUB_ATTR_RW(addr_range_size, instance);
	scrub_ctx->scrub_dev_attr[2] = EDAC_SCRUB_ATTR_RW(enable_background, instance);
	scrub_ctx->scrub_dev_attr[3] = EDAC_SCRUB_ATTR_RW(enable_on_demand, instance);
	scrub_ctx->scrub_dev_attr[4] = EDAC_SCRUB_ATTR_RO(min_cycle_duration, instance);
	scrub_ctx->scrub_dev_attr[5] = EDAC_SCRUB_ATTR_RO(max_cycle_duration, instance);
	scrub_ctx->scrub_dev_attr[6] = EDAC_SCRUB_ATTR_RW(current_cycle_duration, instance);
	for (i = 0; i < SCRUB_MAX_ATTRS; i++)
		scrub_ctx->scrub_attrs[i] = &scrub_ctx->scrub_dev_attr[i].dev_attr.attr;

	sprintf(scrub_ctx->name, "%s%d", "scrub", instance);
	group->name = scrub_ctx->name;
	group->attrs = scrub_ctx->scrub_attrs;
	group->is_visible  = scrub_attr_visible;

	attr_groups[0] = group;

	return 0;
}

/**
 * edac_scrub_get_desc - get EDAC scrub descriptors
 * @scrub_dev: client device, with scrub support
 * @attr_groups: pointer to attrribute group container
 * @instance: device's scrub instance number.
 *
 * Returns 0 on success, error otherwise.
 */
int edac_scrub_get_desc(struct device *scrub_dev,
			const struct attribute_group **attr_groups,
			u8 instance)
{
	if (!scrub_dev || !attr_groups)
		return -EINVAL;

	return scrub_create_desc(scrub_dev, attr_groups, instance);
}
