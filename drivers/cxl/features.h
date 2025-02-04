/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2025 Intel Corporation. */
#ifndef __CXL_FEATURES_LOCAL__
#define __CXL_FEATURES_LOCAL__

struct cxl_features_state *_devm_cxlfs_allocate(struct cxl_memdev *cxlmd);
void _devm_cxlfs_free(struct cxl_memdev *cxlmd);

#ifdef CONFIG_CXL_FWCTL
struct cxl_features_state *_devm_cxlfs_fwctl_allocate(struct cxl_memdev *cxlmd);
void _devm_cxlfs_fwctl_free(struct cxl_memdev *cxlmd);
#endif

static inline struct cxl_features_state *
devm_cxlfs_allocate(struct cxl_memdev *cxlmd)
{
#ifdef CONFIG_CXL_FWCTL
	return _devm_cxlfs_fwctl_allocate(cxlmd);
#else
	return _devm_cxlfs_allocate(cxlmd);
#endif
}

static inline void devm_cxlfs_free(struct cxl_memdev *cxlmd)
{
#ifdef CONFIG_CXL_FWCTL
	_devm_cxlfs_fwctl_free(cxlmd);
#else
	_devm_cxlfs_free(cxlmd);
#endif
}

struct cxl_feat_entry;

struct cxl_features_state *devm_cxlfs_allocate(struct cxl_memdev *cxlmd);
void devm_cxlfs_free(struct cxl_memdev *cxlmd);
int devm_cxl_add_features(struct cxl_memdev *cxlmd);
bool is_cxl_feature_exclusive(struct cxl_feat_entry *entry);
bool is_cxl_feature_exclusive_by_uuid(uuid_t *uuid);

#endif
