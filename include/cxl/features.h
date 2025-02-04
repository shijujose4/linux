/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2024-2025 Intel Corporation. */
#ifndef __CXL_FEATURES_H__
#define __CXL_FEATURES_H__

struct cxl_mailbox;

/* Index IDs for CXL mailbox Feature commands */
enum feature_cmds {
	CXL_FEATURE_ID_GET_SUPPORTED_FEATURES = 0,
	CXL_FEATURE_ID_GET_FEATURE,
	CXL_FEATURE_ID_SET_FEATURE,
	CXL_FEATURE_ID_MAX
};

/* Feature commands capability supported by a device */
enum cxl_features_capability {
	CXL_FEATURES_NONE = 0,
	CXL_FEATURES_RO,
	CXL_FEATURES_RW,
};

/**
 * struct cxl_features_state - The Features state for the device
 * @cxlmd: Pointer to cxl mem device
 * @cap: Feature commands capability
 * @num_features: total Features supported by the device
 */
struct cxl_features_state {
	struct cxl_memdev *cxlmd;
	enum cxl_features_capability cap;
	int num_features;
};

#endif
