/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2025 Intel Corporation. */
#ifndef __CXL_FEATURES_LOCAL__
#define __CXL_FEATURES_LOCAL__

struct cxl_feat_entry;

int devm_cxl_add_features(struct cxl_memdev *cxlmd);
bool is_cxl_feature_exclusive(struct cxl_feat_entry *entry);

#endif
