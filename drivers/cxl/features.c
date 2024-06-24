// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2024,2025 Intel Corporation. All rights reserved. */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <cxl/mailbox.h>
#include <cxl/features.h>

#include "cxl.h"
#include "cxlmem.h"

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

static int cxl_get_supported_features(struct cxl_features_state *cfs)
{
	int remain_feats, max_size, max_feats, start, rc, hdr_size;
	struct cxl_mailbox *cxl_mbox = cfs->features->cxl_mbox;
	int feat_size = sizeof(struct cxl_feat_entry);
	struct cxl_mbox_get_sup_feats_in mbox_in;
	struct cxl_feat_entry *entry;
	struct cxl_mbox_cmd mbox_cmd;
	struct cxl_mem_command *cmd;
	int count;

	/* Get supported features is optional, need to check */
	cmd = cxl_find_feature_command(CXL_MBOX_OP_GET_SUPPORTED_FEATURES);
	if (!cmd)
		return -EOPNOTSUPP;
	if (!test_bit(cmd->info.id, cxl_mbox->feature_cmds))
		return -EOPNOTSUPP;

	count = cxl_get_supported_features_count(cxl_mbox);
	if (count == 0)
		return 0;
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

	hdr_size = sizeof(*mbox_out);
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
			alloc_size = sizeof(*mbox_out) + max_feats * feat_size;
			remain_feats = remain_feats - max_feats;
			copy_feats = max_feats;
		} else {
			alloc_size = sizeof(*mbox_out) + remain_feats * feat_size;
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
		entry++;
		/*
		 * If the number of output entries is less than expected, add the
		 * remaining entries to the next batch.
		 */
		remain_feats += copy_feats - num_entries;
		start += num_entries;
	} while (remain_feats);

	cfs->num_features = count;
	cfs->entries = no_free_ptr(entries);
	return devm_add_action_or_reset(&cfs->features->dev,
					cxl_free_feature_entries, cfs->entries);
}

static int cxl_features_probe(struct device *dev)
{
	struct cxl_features *features = to_cxl_features(dev);
	int rc;

	struct cxl_features_state *cfs __free(kfree) =
		kzalloc(sizeof(*cfs), GFP_KERNEL);
	if (!cfs)
		return -ENOMEM;

	cfs->features = features;
	rc = cxl_get_supported_features(cfs);
	if (rc)
		return rc;

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
