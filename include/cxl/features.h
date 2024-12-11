/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2024-2025 Intel Corporation. */
#ifndef __CXL_FEATURES_H__
#define __CXL_FEATURES_H__

struct cxl_mailbox;

enum feature_cmds {
	CXL_FEATURE_ID_GET_SUPPORTED_FEATURES = 0,
	CXL_FEATURE_ID_GET_FEATURE,
	CXL_FEATURE_ID_SET_FEATURE,
	CXL_FEATURE_ID_MAX,
};

struct cxl_features {
	int id;
	struct device dev;
	struct cxl_mailbox *cxl_mbox;
};
#define to_cxl_features(dev) container_of(dev, struct cxl_features, dev)

struct cxl_features_state {
	struct cxl_features *features;
	int num_features;
};

struct cxl_features *cxl_features_alloc(struct cxl_mailbox *cxl_mbox,
					struct device *parent);

#endif
