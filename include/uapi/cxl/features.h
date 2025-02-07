/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2024,2025, Intel Corporation
 *
 * These are definitions for the mailbox command interface of CXL subsystem.
 */
#ifndef _UAPI_CXL_FEATURES_H_
#define _UAPI_CXL_FEATURES_H_

#include <linux/types.h>
#ifndef __KERNEL__
#include <uuid/uuid.h>
#else
#include <linux/uuid.h>
#endif

/* Get Supported Features (0x500h) CXL r3.2 8.2.9.6.1 */
struct cxl_mbox_get_sup_feats_in {
	__le32 count;
	__le16 start_idx;
	__u8 reserved[2];
} __attribute__ ((__packed__));

/* CXL spec r3.2 Table 8-87 command effects */
#define CXL_CMD_CONFIG_CHANGE_COLD_RESET	BIT(0)
#define CXL_CMD_CONFIG_CHANGE_IMMEDIATE		BIT(1)
#define CXL_CMD_DATA_CHANGE_IMMEDIATE		BIT(2)
#define CXL_CMD_POLICY_CHANGE_IMMEDIATE		BIT(3)
#define CXL_CMD_LOG_CHANGE_IMMEDIATE		BIT(4)
#define CXL_CMD_SECURITY_STATE_CHANGE		BIT(5)
#define CXL_CMD_BACKGROUND			BIT(6)
#define CXL_CMD_BGCMD_ABORT_SUPPORTED		BIT(7)
#define CXL_CMD_EFFECTS_VALID			BIT(9)
#define CXL_CMD_CONFIG_CHANGE_CONV_RESET	BIT(10)
#define CXL_CMD_CONFIG_CHANGE_CXL_RESET		BIT(11)

/*
 * CXL spec r3.2 Table 8-109
 * Get Supported Features Supported Feature Entry
 */
struct cxl_feat_entry {
	uuid_t uuid;
	__le16 id;
	__le16 get_feat_size;
	__le16 set_feat_size;
	__le32 flags;
	__u8 get_feat_ver;
	__u8 set_feat_ver;
	__le16 effects;
	__u8 reserved[18];
} __attribute__ ((__packed__));

/* @flags field for 'struct cxl_feat_entry' */
#define CXL_FEATURE_F_CHANGEABLE		BIT(0)
#define CXL_FEATURE_F_PERSIST_FW_UPDATE		BIT(4)
#define CXL_FEATURE_F_DEFAULT_SEL		BIT(5)
#define CXL_FEATURE_F_SAVED_SEL			BIT(6)

/*
 * CXL spec r3.2 Table 8-108
 * Get supported Features Output Payload
 */
struct cxl_mbox_get_sup_feats_out {
	__le16 num_entries;
	__le16 supported_feats;
	__u8 reserved[4];
	struct cxl_feat_entry ents[] __counted_by_le(num_entries);
} __attribute__ ((__packed__));

/*
 * Get Feature CXL spec r3.2 Spec 8.2.9.6.2
 */

/*
 * Get Feature input payload
 * CXL spec r3.2 section 8.2.9.6.2 Table 8-99
 */
struct cxl_mbox_get_feat_in {
	uuid_t uuid;
	__le16 offset;
	__le16 count;
	__u8 selection;
} __attribute__ ((__packed__));

/* Selection field for 'struct cxl_mbox_get_feat_in' */
enum cxl_get_feat_selection {
	CXL_GET_FEAT_SEL_CURRENT_VALUE,
	CXL_GET_FEAT_SEL_DEFAULT_VALUE,
	CXL_GET_FEAT_SEL_SAVED_VALUE,
	CXL_GET_FEAT_SEL_MAX
};

/*
 * Set Feature CXL spec r3.2  8.2.9.6.3
 */

/*
 * Set Feature input payload
 * CXL spec r3.2 section 8.2.9.6.3 Table 8-101
 */
struct cxl_mbox_set_feat_hdr {
	uuid_t uuid;
	__le32 flags;
	__le16 offset;
	__u8 version;
	__u8 rsvd[9];
} __attribute__ ((__packed__));

/* Set Feature flags field */
enum cxl_set_feat_flag_data_transfer {
	CXL_SET_FEAT_FLAG_FULL_DATA_TRANSFER = 0,
	CXL_SET_FEAT_FLAG_INITIATE_DATA_TRANSFER,
	CXL_SET_FEAT_FLAG_CONTINUE_DATA_TRANSFER,
	CXL_SET_FEAT_FLAG_FINISH_DATA_TRANSFER,
	CXL_SET_FEAT_FLAG_ABORT_DATA_TRANSFER,
	CXL_SET_FEAT_FLAG_DATA_TRANSFER_MAX
};

#define CXL_SET_FEAT_FLAG_DATA_TRANSFER_MASK		GENMASK(2, 0)
#define CXL_SET_FEAT_FLAG_DATA_SAVED_ACROSS_RESET	BIT(3)

#endif
