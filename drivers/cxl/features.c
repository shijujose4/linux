// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2024-2025 Intel Corporation. All rights reserved. */
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/module.h>
#include <cxl/mailbox.h>
#include <cxl/features.h>
#include "cxl.h"
#include "cxlmem.h"
#include "features.h"

static void cxl_free_feature_entries(void *entries)
{
	kvfree(entries);
}

static int cxl_get_supported_features_count(struct cxl_mailbox *cxl_mbox)
{
	struct cxl_mbox_get_sup_feats_out mbox_out;
	struct cxl_mbox_get_sup_feats_in mbox_in;
	struct cxl_mbox_cmd mbox_cmd;
	int rc;

	memset(&mbox_in, 0, sizeof(mbox_in));
	mbox_in.count = cpu_to_le32(sizeof(mbox_out));
	memset(&mbox_out, 0, sizeof(mbox_out));
	mbox_cmd = (struct cxl_mbox_cmd) {
		.opcode = CXL_MBOX_OP_GET_SUPPORTED_FEATURES,
		.size_in = sizeof(mbox_in),
		.payload_in = &mbox_in,
		.size_out = sizeof(mbox_out),
		.payload_out = &mbox_out,
		.min_out = sizeof(mbox_out),
	};
	rc = cxl_internal_send_cmd(cxl_mbox, &mbox_cmd);
	if (rc < 0)
		return rc;

	return le16_to_cpu(mbox_out.supported_feats);
}

static int get_supported_features(struct cxl_memdev *cxlmd,
				  struct cxl_features_state *cxlfs)
{
	int remain_feats, max_size, max_feats, start, rc, hdr_size;
	struct cxl_mailbox *cxl_mbox = &cxlmd->cxlds->cxl_mbox;
	int feat_size = sizeof(struct cxl_feat_entry);
	struct cxl_mbox_get_sup_feats_in mbox_in;
	struct cxl_feat_entry *entry;
	struct cxl_mbox_cmd mbox_cmd;
	int user_feats = 0;
	int count;

	if (cxlfs->cap < CXL_FEATURES_RO)
		return -EOPNOTSUPP;

	count = cxl_get_supported_features_count(cxl_mbox);
	if (count == 0)
		return -ENOENT;
	if (count < 0)
		return -ENXIO;

	struct cxl_feat_entry *entries __free(kvfree) =
		kvmalloc(count * sizeof(*entries), GFP_KERNEL);
	if (!entries)
		return -ENOMEM;

	struct cxl_mbox_get_sup_feats_out *mbox_out __free(kvfree) =
		kvmalloc(cxl_mbox->payload_size, GFP_KERNEL);
	if (!mbox_out)
		return -ENOMEM;

	hdr_size = struct_size(mbox_out, ents, 0);
	max_size = cxl_mbox->payload_size - hdr_size;
	/* max feat entries that can fit in mailbox max payload size */
	max_feats = max_size / feat_size;
	entry = entries;

	start = 0;
	remain_feats = count;
	do {
		int retrieved, alloc_size, copy_feats;
		int num_entries;

		if (remain_feats > max_feats) {
			alloc_size = struct_size(mbox_out, ents, max_feats);
			remain_feats = remain_feats - max_feats;
			copy_feats = max_feats;
		} else {
			alloc_size = struct_size(mbox_out, ents, remain_feats);
			copy_feats = remain_feats;
			remain_feats = 0;
		}

		memset(&mbox_in, 0, sizeof(mbox_in));
		mbox_in.count = cpu_to_le32(alloc_size);
		mbox_in.start_idx = cpu_to_le16(start);
		memset(mbox_out, 0, alloc_size);
		mbox_cmd = (struct cxl_mbox_cmd) {
			.opcode = CXL_MBOX_OP_GET_SUPPORTED_FEATURES,
			.size_in = sizeof(mbox_in),
			.payload_in = &mbox_in,
			.size_out = alloc_size,
			.payload_out = mbox_out,
			.min_out = hdr_size,
		};
		rc = cxl_internal_send_cmd(cxl_mbox, &mbox_cmd);
		if (rc < 0)
			return rc;

		if (mbox_cmd.size_out <= hdr_size)
			return -ENXIO;

		/*
		 * Make sure retrieved out buffer is multiple of feature
		 * entries.
		 */
		retrieved = mbox_cmd.size_out - hdr_size;
		if (retrieved % feat_size)
			return -ENXIO;

		num_entries = le16_to_cpu(mbox_out->num_entries);
		/*
		 * If the reported output entries * defined entry size !=
		 * retrieved output bytes, then the output package is incorrect.
		 */
		if (num_entries * feat_size != retrieved)
			return -ENXIO;

		memcpy(entry, mbox_out->ents, retrieved);
		if (!is_cxl_feature_exclusive(entry))
			user_feats++;
		entry++;
		/*
		 * If the number of output entries is less than expected, add the
		 * remaining entries to the next batch.
		 */
		remain_feats += copy_feats - num_entries;
		start += num_entries;
	} while (remain_feats);

	cxlfs->num_features = count;
	cxlfs->num_user_features = user_feats;
	cxlfs->entries = no_free_ptr(entries);
	return devm_add_action_or_reset(&cxlmd->dev, cxl_free_feature_entries,
					cxlfs->entries);
}

static void enumerate_feature_cmds(struct cxl_memdev *cxlmd,
				   struct cxl_features_state *cxlfs)
{
	struct cxl_mailbox *cxl_mbox = &cxlmd->cxlds->cxl_mbox;
	int fid;

	fid = cxl_get_feature_command_id(CXL_MBOX_OP_GET_SUPPORTED_FEATURES);
	if (!test_bit(fid, cxl_mbox->feature_cmds))
		return;

	fid = cxl_get_feature_command_id(CXL_MBOX_OP_GET_FEATURE);
	if (!test_bit(fid, cxl_mbox->feature_cmds))
		return;

	cxlfs->cap = CXL_FEATURES_RO;

	fid = cxl_get_feature_command_id(CXL_MBOX_OP_SET_FEATURE);
	if (!test_bit(fid, cxl_mbox->feature_cmds))
		return;

	cxlfs->cap = CXL_FEATURES_RW;
}

static void cxlfs_free(void *_cxlfs)
{
	kfree(_cxlfs);
}
DEFINE_FREE(free_cxlfs, struct cxl_features_state *, if (_T) cxlfs_free(_T))

struct cxl_features_state *_devm_cxlfs_allocate(struct cxl_memdev *cxlmd)
{
	int rc;

	struct cxl_features_state *cxlfs __free(free_cxlfs) =
		kzalloc(sizeof(*cxlfs), GFP_KERNEL);
	if (!cxlfs)
		return NULL;

	cxlfs->cxlmd = cxlmd;

	rc = devm_add_action_or_reset(&cxlmd->dev, cxlfs_free, cxlfs);
	if (rc)
		return NULL;

	return no_free_ptr(cxlfs);
}

void _devm_cxlfs_free(struct cxl_memdev *cxlmd)
{
	kfree(cxlmd->cxlfs);
	/* Set in devm_cxl_add_features(), make sure it's cleared */
	cxlmd->cxlfs = NULL;
}

static void cxl_cxlfs_free(void *_cxlfs)
{
	struct cxl_features_state *cxlfs = _cxlfs;

	devm_cxlfs_free(cxlfs->cxlmd);
}
DEFINE_FREE(cxl_free_cxlfs, struct cxl_features_state *, if (_T) cxl_cxlfs_free(_T))

/**
 * devm_cxl_add_features() - Allocate and initialize features context
 * @cxlmd: CXL memory device
 *
 * Return 0 on success or -errno on failure.
 */
int devm_cxl_add_features(struct cxl_memdev *cxlmd)
{
	int rc;

	struct cxl_features_state *cxlfs __free(cxl_free_cxlfs) =
		devm_cxlfs_allocate(cxlmd);
	if (!cxlfs)
		return -ENOMEM;

	enumerate_feature_cmds(cxlmd, cxlfs);
	rc = get_supported_features(cxlmd, cxlfs);
	if (rc)
		return rc;

	cxlmd->cxlfs = no_free_ptr(cxlfs);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_add_features, "CXL");

MODULE_IMPORT_NS("CXL");
