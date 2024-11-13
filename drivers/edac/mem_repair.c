// SPDX-License-Identifier: GPL-2.0
/*
 * The generic EDAC memory repair driver is designed to control the memory
 * devices with memory repair features, such as Post Package Repair (PPR),
 * memory sparing etc. The common sysfs memory repair interface abstracts
 * the control of various arbitrary memory repair functionalities into a
 * unified set of functions.
 *
 * Copyright (c) 2024 HiSilicon Limited.
 */

#include <linux/edac.h>

enum edac_mem_repair_attributes {
	MEM_REPAIR_FUNCTION,
	MEM_REPAIR_PERSIST_MODE,
	MEM_REPAIR_DPA_SUPPORT,
	MEM_REPAIR_SAFE_IN_USE,
	MEM_REPAIR_HPA,
	MEM_REPAIR_DPA,
	MEM_REPAIR_NIBBLE_MASK,
	MEM_REPAIR_BANK_GROUP,
	MEM_REPAIR_BANK,
	MEM_REPAIR_RANK,
	MEM_REPAIR_ROW,
	MEM_REPAIR_COLUMN,
	MEM_REPAIR_CHANNEL,
	MEM_REPAIR_SUB_CHANNEL,
	MEM_REPAIR_DRY_RUN,
	MEM_DO_REPAIR,
	MEM_REPAIR_MAX_ATTRS
};

struct edac_mem_repair_dev_attr {
	struct device_attribute dev_attr;
	u8 instance;
};

struct edac_mem_repair_context {
	char name[EDAC_FEAT_NAME_LEN];
	struct edac_mem_repair_dev_attr mem_repair_dev_attr[MEM_REPAIR_MAX_ATTRS];
	struct attribute *mem_repair_attrs[MEM_REPAIR_MAX_ATTRS + 1];
	struct attribute_group group;
};

#define TO_MEM_REPAIR_DEV_ATTR(_dev_attr)      \
		container_of(_dev_attr, struct edac_mem_repair_dev_attr, dev_attr)

#define EDAC_MEM_REPAIR_ATTR_SHOW(attrib, cb, type, format)			\
static ssize_t attrib##_show(struct device *ras_feat_dev,			\
			     struct device_attribute *attr, char *buf)		\
{										\
	u8 inst = TO_MEM_REPAIR_DEV_ATTR(attr)->instance;			\
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);		\
	const struct edac_mem_repair_ops *ops =					\
				ctx->mem_repair[inst].mem_repair_ops;		\
	type data;								\
	int ret;								\
										\
	ret = ops->cb(ras_feat_dev->parent, ctx->mem_repair[inst].private,	\
		      &data);							\
	if (ret)								\
		return ret;							\
										\
	return sysfs_emit(buf, format, data);					\
}

EDAC_MEM_REPAIR_ATTR_SHOW(repair_function, get_repair_function, u32, "%u\n")
EDAC_MEM_REPAIR_ATTR_SHOW(persist_mode, get_persist_mode, u32, "%u\n")
EDAC_MEM_REPAIR_ATTR_SHOW(dpa_support, get_dpa_support, u32, "%u\n")
EDAC_MEM_REPAIR_ATTR_SHOW(repair_safe_when_in_use, get_repair_safe_when_in_use, u32, "%u\n")
EDAC_MEM_REPAIR_ATTR_SHOW(hpa, get_hpa, u64, "0x%llx\n")
EDAC_MEM_REPAIR_ATTR_SHOW(dpa, get_dpa, u64, "0x%llx\n")
EDAC_MEM_REPAIR_ATTR_SHOW(nibble_mask, get_nibble_mask, u64, "0x%llx\n")
EDAC_MEM_REPAIR_ATTR_SHOW(bank_group, get_bank_group, u32, "%u\n")
EDAC_MEM_REPAIR_ATTR_SHOW(bank, get_bank, u32, "%u\n")
EDAC_MEM_REPAIR_ATTR_SHOW(rank, get_rank, u32, "%u\n")
EDAC_MEM_REPAIR_ATTR_SHOW(row, get_row, u64, "0x%llx\n")
EDAC_MEM_REPAIR_ATTR_SHOW(column, get_column, u32, "%u\n")
EDAC_MEM_REPAIR_ATTR_SHOW(channel, get_channel, u32, "%u\n")
EDAC_MEM_REPAIR_ATTR_SHOW(sub_channel, get_sub_channel, u32, "%u\n")

#define EDAC_MEM_REPAIR_ATTR_STORE(attrib, cb, type, conv_func)			\
static ssize_t attrib##_store(struct device *ras_feat_dev,			\
			      struct device_attribute *attr,			\
			      const char *buf, size_t len)			\
{										\
	u8 inst = TO_MEM_REPAIR_DEV_ATTR(attr)->instance;			\
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);		\
	const struct edac_mem_repair_ops *ops =					\
				ctx->mem_repair[inst].mem_repair_ops;		\
	type data;								\
	int ret;								\
										\
	ret = conv_func(buf, 0, &data);						\
	if (ret < 0)								\
		return ret;							\
										\
	ret = ops->cb(ras_feat_dev->parent, ctx->mem_repair[inst].private,	\
		      data);							\
	if (ret)								\
		return ret;							\
										\
	return len;								\
}

EDAC_MEM_REPAIR_ATTR_STORE(persist_mode, set_persist_mode, unsigned long, kstrtoul)
EDAC_MEM_REPAIR_ATTR_STORE(hpa, set_hpa, u64, kstrtou64)
EDAC_MEM_REPAIR_ATTR_STORE(dpa, set_dpa, u64, kstrtou64)
EDAC_MEM_REPAIR_ATTR_STORE(nibble_mask, set_nibble_mask, u64, kstrtou64)
EDAC_MEM_REPAIR_ATTR_STORE(bank_group, set_bank_group, unsigned long, kstrtoul)
EDAC_MEM_REPAIR_ATTR_STORE(bank, set_bank, unsigned long, kstrtoul)
EDAC_MEM_REPAIR_ATTR_STORE(rank, set_rank, unsigned long, kstrtoul)
EDAC_MEM_REPAIR_ATTR_STORE(row, set_row, u64, kstrtou64)
EDAC_MEM_REPAIR_ATTR_STORE(column, set_column, unsigned long, kstrtoul)
EDAC_MEM_REPAIR_ATTR_STORE(channel, set_channel, unsigned long, kstrtoul)
EDAC_MEM_REPAIR_ATTR_STORE(sub_channel, set_sub_channel, unsigned long, kstrtoul)

#define EDAC_MEM_REPAIR_DO_OP(attrib, cb)						\
static ssize_t attrib##_store(struct device *ras_feat_dev,				\
			      struct device_attribute *attr,				\
			      const char *buf, size_t len)				\
{											\
	u8 inst = TO_MEM_REPAIR_DEV_ATTR(attr)->instance;				\
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);			\
	const struct edac_mem_repair_ops *ops = ctx->mem_repair[inst].mem_repair_ops;	\
	int ret;									\
											\
	ret = ops->cb(ras_feat_dev->parent, ctx->mem_repair[inst].private);		\
	if (ret)									\
		return ret;								\
											\
	return len;									\
}

EDAC_MEM_REPAIR_DO_OP(dry_run, dry_run)
EDAC_MEM_REPAIR_DO_OP(repair, do_repair)

static umode_t mem_repair_attr_visible(struct kobject *kobj, struct attribute *a, int attr_id)
{
	struct device *ras_feat_dev = kobj_to_dev(kobj);
	struct device_attribute *dev_attr = container_of(a, struct device_attribute, attr);
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);
	u8 inst = TO_MEM_REPAIR_DEV_ATTR(dev_attr)->instance;
	const struct edac_mem_repair_ops *ops = ctx->mem_repair[inst].mem_repair_ops;

	switch (attr_id) {
	case MEM_REPAIR_FUNCTION:
		if (ops->get_repair_function)
			return a->mode;
		break;
	case MEM_REPAIR_PERSIST_MODE:
		if (ops->get_persist_mode) {
			if (ops->set_persist_mode)
				return a->mode;
			else
				return 0444;
		}
		break;
	case MEM_REPAIR_DPA_SUPPORT:
		if (ops->get_dpa_support)
			return a->mode;
		break;
	case MEM_REPAIR_SAFE_IN_USE:
		if (ops->get_repair_safe_when_in_use)
			return a->mode;
		break;
	case MEM_REPAIR_HPA:
		if (ops->get_hpa) {
			if (ops->set_hpa)
				return a->mode;
			else
				return 0444;
		}
		break;
	case MEM_REPAIR_DPA:
		if (ops->get_dpa) {
			if (ops->set_dpa)
				return a->mode;
			else
				return 0444;
		}
		break;
	case MEM_REPAIR_NIBBLE_MASK:
		if (ops->get_nibble_mask) {
			if (ops->set_nibble_mask)
				return a->mode;
			else
				return 0444;
		}
		break;
	case MEM_REPAIR_BANK_GROUP:
		if (ops->get_bank_group) {
			if (ops->set_bank_group)
				return a->mode;
			else
				return 0444;
		}
		break;
	case MEM_REPAIR_BANK:
		if (ops->get_bank) {
			if (ops->set_bank)
				return a->mode;
			else
				return 0444;
		}
		break;
	case MEM_REPAIR_RANK:
		if (ops->get_rank) {
			if (ops->set_rank)
				return a->mode;
			else
				return 0444;
		}
		break;
	case MEM_REPAIR_ROW:
		if (ops->get_row) {
			if (ops->set_row)
				return a->mode;
			else
				return 0444;
		}
		break;
	case MEM_REPAIR_COLUMN:
		if (ops->get_column) {
			if (ops->set_column)
				return a->mode;
			else
				return 0444;
		}
		break;
	case MEM_REPAIR_CHANNEL:
		if (ops->get_channel) {
			if (ops->set_channel)
				return a->mode;
			else
				return 0444;
		}
		break;
	case MEM_REPAIR_SUB_CHANNEL:
		if (ops->get_sub_channel) {
			if (ops->set_sub_channel)
				return a->mode;
			else
				return 0444;
		}
		break;
	case MEM_REPAIR_DRY_RUN:
		if (ops->dry_run)
			return a->mode;
		break;
	case MEM_DO_REPAIR:
		if (ops->do_repair)
			return a->mode;
		break;
	default:
		break;
	}

	return 0;
}

#define EDAC_MEM_REPAIR_ATTR_RO(_name, _instance)       \
	((struct edac_mem_repair_dev_attr) { .dev_attr = __ATTR_RO(_name), \
					     .instance = _instance })

#define EDAC_MEM_REPAIR_ATTR_WO(_name, _instance)       \
	((struct edac_mem_repair_dev_attr) { .dev_attr = __ATTR_WO(_name), \
					     .instance = _instance })

#define EDAC_MEM_REPAIR_ATTR_RW(_name, _instance)       \
	((struct edac_mem_repair_dev_attr) { .dev_attr = __ATTR_RW(_name), \
					     .instance = _instance })

static int mem_repair_create_desc(struct device *dev,
				  const struct attribute_group **attr_groups,
				  u8 instance)
{
	struct edac_mem_repair_context *ctx;
	struct attribute_group *group;
	int i;
	struct edac_mem_repair_dev_attr dev_attr[] = {
		[MEM_REPAIR_FUNCTION] = EDAC_MEM_REPAIR_ATTR_RO(repair_function,
							    instance),
		[MEM_REPAIR_PERSIST_MODE] =
				EDAC_MEM_REPAIR_ATTR_RW(persist_mode, instance),
		[MEM_REPAIR_DPA_SUPPORT] =
				EDAC_MEM_REPAIR_ATTR_RO(dpa_support, instance),
		[MEM_REPAIR_SAFE_IN_USE] =
				EDAC_MEM_REPAIR_ATTR_RO(repair_safe_when_in_use,
							instance),
		[MEM_REPAIR_HPA] = EDAC_MEM_REPAIR_ATTR_RW(hpa, instance),
		[MEM_REPAIR_DPA] = EDAC_MEM_REPAIR_ATTR_RW(dpa, instance),
		[MEM_REPAIR_NIBBLE_MASK] =
				EDAC_MEM_REPAIR_ATTR_RW(nibble_mask, instance),
		[MEM_REPAIR_BANK_GROUP] =
				EDAC_MEM_REPAIR_ATTR_RW(bank_group, instance),
		[MEM_REPAIR_BANK] = EDAC_MEM_REPAIR_ATTR_RW(bank, instance),
		[MEM_REPAIR_RANK] = EDAC_MEM_REPAIR_ATTR_RW(rank, instance),
		[MEM_REPAIR_ROW] = EDAC_MEM_REPAIR_ATTR_RW(row, instance),
		[MEM_REPAIR_COLUMN] = EDAC_MEM_REPAIR_ATTR_RW(column, instance),
		[MEM_REPAIR_CHANNEL] = EDAC_MEM_REPAIR_ATTR_RW(channel, instance),
		[MEM_REPAIR_SUB_CHANNEL] =
				EDAC_MEM_REPAIR_ATTR_RW(sub_channel, instance),
		[MEM_REPAIR_DRY_RUN] = EDAC_MEM_REPAIR_ATTR_WO(dry_run, instance),
		[MEM_DO_REPAIR] = EDAC_MEM_REPAIR_ATTR_WO(repair, instance)
	};

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	for (i = 0; i < MEM_REPAIR_MAX_ATTRS; i++) {
		memcpy(&ctx->mem_repair_dev_attr[i].dev_attr,
		       &dev_attr[i], sizeof(dev_attr[i]));
		ctx->mem_repair_attrs[i] =
				&ctx->mem_repair_dev_attr[i].dev_attr.attr;
	}

	sprintf(ctx->name, "%s%d", "mem_repair", instance);
	group = &ctx->group;
	group->name = ctx->name;
	group->attrs = ctx->mem_repair_attrs;
	group->is_visible  = mem_repair_attr_visible;
	attr_groups[0] = group;

	return 0;
}

/**
 * edac_mem_repair_get_desc - get EDAC memory repair descriptors
 * @dev: client device with memory repair feature
 * @attr_groups: pointer to attribute group container
 * @instance: device's memory repair instance number.
 *
 * Return:
 *  * %0	- Success.
 *  * %-EINVAL	- Invalid parameters passed.
 *  * %-ENOMEM	- Dynamic memory allocation failed.
 */
int edac_mem_repair_get_desc(struct device *dev,
			     const struct attribute_group **attr_groups, u8 instance)
{
	if (!dev || !attr_groups)
		return -EINVAL;

	return mem_repair_create_desc(dev, attr_groups, instance);
}
