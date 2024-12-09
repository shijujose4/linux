// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2024,2025 Intel Corporation. All rights reserved. */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <cxl/features.h>

#include "cxl.h"

static int cxl_features_probe(struct device *dev)
{
	struct cxl_features *features = to_cxl_features(dev);
	struct cxl_features_state *cfs __free(kfree) =
		kzalloc(sizeof(*cfs), GFP_KERNEL);

	if (!cfs)
		return -ENOMEM;

	cfs->features = features;
	dev_set_drvdata(dev, no_free_ptr(cfs));

	return 0;
}

static void cxl_features_remove(struct device *dev)
{
	struct cxl_features_state *cfs = dev_get_drvdata(dev);

	kfree(cfs);
}

static struct cxl_driver cxl_features_driver = {
	.name = "cxl_features",
	.probe = cxl_features_probe,
	.remove = cxl_features_remove,
	.id = CXL_DEVICE_FEATURES,
};

module_cxl_driver(cxl_features_driver);

MODULE_DESCRIPTION("CXL: Features");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("CXL");
MODULE_ALIAS_CXL(CXL_DEVICE_FEATURES);
