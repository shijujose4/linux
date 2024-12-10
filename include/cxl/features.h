/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2024-2025 Intel Corporation. */
#ifndef __CXL_FEATURES_H__
#define __CXL_FEATURES_H__

#include <linux/uuid.h>

#define CXL_FEAT_PATROL_SCRUB_UUID						\
	UUID_INIT(0x96dad7d6, 0xfde8, 0x482b, 0xa7, 0x33, 0x75, 0x77, 0x4e,	\
		  0x06, 0xdb, 0x8a)

#define CXL_FEAT_ECS_UUID							\
	UUID_INIT(0xe5b13f22, 0x2328, 0x4a14, 0xb8, 0xba, 0xb9, 0x69, 0x1e,	\
		  0x89, 0x33, 0x86)

#define CXL_FEAT_SPPR_UUID							\
	UUID_INIT(0x892ba475, 0xfad8, 0x474e, 0x9d, 0x3e, 0x69, 0x2c, 0x91,	\
		  0x75, 0x68, 0xbb)

#define CXL_FEAT_HPPR_UUID							\
	UUID_INIT(0x80ea4521, 0x786f, 0x4127, 0xaf, 0xb1, 0xec, 0x74, 0x59,	\
		  0xfb, 0x0e, 0x24)

#define CXL_FEAT_CACHELINE_SPARING_UUID						\
	UUID_INIT(0x96C33386, 0x91dd, 0x44c7, 0x9e, 0xcb, 0xfd, 0xaf, 0x65,	\
		  0x03, 0xba, 0xc4)

#define CXL_FEAT_ROW_SPARING_UUID						\
	UUID_INIT(0x450ebf67, 0xb135, 0x4f97, 0xa4, 0x98, 0xc2, 0xd5, 0x7f,	\
		  0x27, 0x9b, 0xed)

#define CXL_FEAT_BANK_SPARING_UUID						\
	UUID_INIT(0x78b79636, 0x90ac, 0x4b64, 0xa4, 0xef, 0xfa, 0xac, 0x5d,	\
		  0x18, 0xa8, 0x63)

#define CXL_FEAT_RANK_SPARING_UUID						\
	UUID_INIT(0x34dbaff5, 0x0552, 0x4281, 0x8f, 0x76, 0xda, 0x0b, 0x5e,	\
		  0x7a, 0x76, 0xa7)

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

/* Supported Feature Entry : Payload out attribute flags */
#define CXL_FEAT_ENTRY_FLAG_CHANGABLE  BIT(0)
#define CXL_FEAT_ENTRY_FLAG_DEEPEST_RESET_PERSISTENCE_MASK     GENMASK(3, 1)
#define CXL_FEAT_ENTRY_FLAG_PERSIST_ACROSS_FIRMWARE_UPDATE     BIT(4)
#define CXL_FEAT_ENTRY_FLAG_SUPPORT_DEFAULT_SELECTION  BIT(5)
#define CXL_FEAT_ENTRY_FLAG_SUPPORT_SAVED_SELECTION    BIT(6)

enum cxl_feat_attr_value_persistence {
	CXL_FEAT_ATTR_VALUE_PERSISTENCE_NONE,
	CXL_FEAT_ATTR_VALUE_PERSISTENCE_CXL_RESET,
	CXL_FEAT_ATTR_VALUE_PERSISTENCE_HOT_RESET,
	CXL_FEAT_ATTR_VALUE_PERSISTENCE_WARM_RESET,
	CXL_FEAT_ATTR_VALUE_PERSISTENCE_COLD_RESET,
	CXL_FEAT_ATTR_VALUE_PERSISTENCE_MAX
};

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
	int num_user_features;
	struct cxl_feat_entry *entries;
};

/*
 * Get Feature CXL 3.1 Spec 8.2.9.6.2
 */

/*
 * Get Feature input payload
 * CXL rev 3.1 section 8.2.9.6.2 Table 8-99
 */
enum cxl_get_feat_selection {
	CXL_GET_FEAT_SEL_CURRENT_VALUE,
	CXL_GET_FEAT_SEL_DEFAULT_VALUE,
	CXL_GET_FEAT_SEL_SAVED_VALUE,
	CXL_GET_FEAT_SEL_MAX
};

struct cxl_mbox_get_feat_in {
	uuid_t uuid;
	__le16 offset;
	__le16 count;
	u8 selection;
}  __packed;

/*
 * Set Feature CXL 3.1 Spec 8.2.9.6.3
 */

/*
 * Set Feature input payload
 * CXL rev 3.1 section 8.2.9.6.3 Table 8-101
 */
/* Set Feature : Payload in flags */
#define CXL_SET_FEAT_FLAG_DATA_TRANSFER_MASK	GENMASK(2, 0)
enum cxl_set_feat_flag_data_transfer {
	CXL_SET_FEAT_FLAG_FULL_DATA_TRANSFER,
	CXL_SET_FEAT_FLAG_INITIATE_DATA_TRANSFER,
	CXL_SET_FEAT_FLAG_CONTINUE_DATA_TRANSFER,
	CXL_SET_FEAT_FLAG_FINISH_DATA_TRANSFER,
	CXL_SET_FEAT_FLAG_ABORT_DATA_TRANSFER,
	CXL_SET_FEAT_FLAG_DATA_TRANSFER_MAX
};

#define CXL_SET_FEAT_FLAG_DATA_SAVED_ACROSS_RESET	BIT(3)

struct cxl_mbox_set_feat_hdr {
	uuid_t uuid;
	__le32 flags;
	__le16 offset;
	u8 version;
	u8 rsvd[9];
}  __packed;

bool cxl_feature_enabled(struct cxl_features_state *cfs, u16 opcode);
struct cxl_features *cxl_features_alloc(struct cxl_mailbox *cxl_mbox,
					struct device *parent);
struct cxl_feat_entry *
cxl_get_supported_feature_entry(struct cxl_features *features,
				const uuid_t *feat_uuid);
size_t cxl_get_feature(struct cxl_features *features, const uuid_t feat_uuid,
		       enum cxl_get_feat_selection selection,
		       void *feat_out, size_t feat_out_size, u16 offset,
		       u16 *return_code);
int cxl_set_feature(struct cxl_features *features, const uuid_t feat_uuid,
		    u8 feat_version, void *feat_data, size_t feat_data_size,
		    u32 feat_flag, u16 offset, u16 *return_code);
bool is_cxl_feature_exclusive(struct cxl_feat_entry *entry);

#endif
