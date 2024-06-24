/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2024-2025 Intel Corporation. */
#ifndef __CXL_FEATURES_H__
#define __CXL_FEATURES_H__

#include <linux/uuid.h>

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

/* Get Supported Features (0x500h) CXL r3.1 8.2.9.6.1 */
struct cxl_mbox_get_sup_feats_in {
	__le32 count;
	__le16 start_idx;
	u8 reserved[2];
} __packed;

struct cxl_feat_entry {
	uuid_t uuid;
	__le16 id;
	__le16 get_feat_size;
	__le16 set_feat_size;
	__le32 flags;
	u8 get_feat_ver;
	u8 set_feat_ver;
	__le16 effects;
	u8 reserved[18];
} __packed;

struct cxl_mbox_get_sup_feats_out {
	__le16 num_entries;
	__le16 supported_feats;
	u8 reserved[4];
	struct cxl_feat_entry ents[] __counted_by_le(num_entries);
} __packed;

struct cxl_features_state {
	struct cxl_features *features;
	int num_features;
	struct cxl_feat_entry *entries;
};

struct cxl_features *cxl_features_alloc(struct cxl_mailbox *cxl_mbox,
					struct device *parent);
struct cxl_feat_entry *
cxl_get_supported_feature_entry(struct cxl_features *features,
				const uuid_t *feat_uuid);

#endif
