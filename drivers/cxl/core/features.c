// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2024-2025 Intel Corporation. All rights reserved. */
#include <linux/device.h>
#include <cxl/mailbox.h>
#include "cxl.h"
#include "core.h"

#define CXL_FEATURE_MAX_DEVS 65536
static DEFINE_IDA(cxl_features_ida);

static void cxl_features_release(struct device *dev)
{
	struct cxl_features *features = to_cxl_features(dev);

	ida_free(&cxl_features_ida, features->id);
	kfree(features);
}

static void remove_features_dev(void *dev)
{
	device_unregister(dev);
}

const struct device_type cxl_features_type = {
	.name = "features",
	.release = cxl_features_release,
};
EXPORT_SYMBOL_NS_GPL(cxl_features_type, "CXL");

struct cxl_features *cxl_features_alloc(struct cxl_mailbox *cxl_mbox,
					struct device *parent)
{
	struct device *dev;
	int rc;

	struct cxl_features *features __free(kfree) =
		kzalloc(sizeof(*features), GFP_KERNEL);
	if (!features)
		return ERR_PTR(-ENOMEM);

	rc = ida_alloc_max(&cxl_features_ida, CXL_FEATURE_MAX_DEVS - 1,
			   GFP_KERNEL);
	if (rc < 0)
		return ERR_PTR(rc);

	features->id = rc;
	features->cxl_mbox = cxl_mbox;
	dev = &features->dev;
	device_initialize(dev);
	device_set_pm_not_required(dev);
	dev->parent = parent;
	dev->bus = &cxl_bus_type;
	dev->type = &cxl_features_type;
	rc = dev_set_name(dev, "features%d", features->id);
	if (rc)
		goto err;

	rc = device_add(dev);
	if (rc)
		goto err;

	rc = devm_add_action_or_reset(parent, remove_features_dev, dev);
	if (rc)
		goto err;

	return no_free_ptr(features);

err:
	put_device(dev);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL_NS_GPL(cxl_features_alloc, "CXL");

struct cxl_feat_entry *
cxl_get_supported_feature_entry(struct cxl_features *features,
				const uuid_t *feat_uuid)
{
	struct cxl_feat_entry *feat_entry;
	struct cxl_features_state *cfs;
	int count;

	cfs = dev_get_drvdata(&features->dev);
	if (!cfs)
		return ERR_PTR(-EOPNOTSUPP);

	if (!cfs->num_features)
		return ERR_PTR(-ENOENT);

	/* Check CXL dev supports the feature */
	feat_entry = cfs->entries;
	for (count = 0; count < cfs->num_features;
	     count++, feat_entry++) {
		if (uuid_equal(&feat_entry->uuid, feat_uuid))
			return feat_entry;
	}

	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL_NS_GPL(cxl_get_supported_feature_entry, "CXL");
