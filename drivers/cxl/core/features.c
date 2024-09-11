// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2024-2025 Intel Corporation. All rights reserved. */
#include <linux/device.h>
#include <cxl/mailbox.h>
#include "cxl.h"
#include "core.h"
#include "cxlmem.h"

#define CXL_FEATURE_MAX_DEVS 65536
static DEFINE_IDA(cxl_features_ida);

static void cxl_features_release(struct device *dev)
{
	struct cxl_features *features = to_cxl_features(dev);

	ida_free(&cxl_features_ida, features->id);
	kfree(features);
}

static void remove_features_dev(void *dev)
{
	device_unregister(dev);
}

const struct device_type cxl_features_type = {
	.name = "features",
	.release = cxl_features_release,
};
EXPORT_SYMBOL_NS_GPL(cxl_features_type, "CXL");

struct cxl_features *cxl_features_alloc(struct cxl_mailbox *cxl_mbox,
					struct device *parent)
{
	struct device *dev;
	int rc;

	struct cxl_features *features __free(kfree) =
		kzalloc(sizeof(*features), GFP_KERNEL);
	if (!features)
		return ERR_PTR(-ENOMEM);

	rc = ida_alloc_max(&cxl_features_ida, CXL_FEATURE_MAX_DEVS - 1,
			   GFP_KERNEL);
	if (rc < 0)
		return ERR_PTR(rc);

	features->id = rc;
	features->cxl_mbox = cxl_mbox;
	dev = &features->dev;
	device_initialize(dev);
	device_set_pm_not_required(dev);
	dev->parent = parent;
	dev->bus = &cxl_bus_type;
	dev->type = &cxl_features_type;
	rc = dev_set_name(dev, "features%d", features->id);
	if (rc)
		goto err;

	rc = device_add(dev);
	if (rc)
		goto err;

	rc = devm_add_action_or_reset(parent, remove_features_dev, dev);
	if (rc)
		goto err;

	return no_free_ptr(features);

err:
	put_device(dev);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL_NS_GPL(cxl_features_alloc, "CXL");

struct cxl_feat_entry *
cxl_get_supported_feature_entry(struct cxl_features *features,
				const uuid_t *feat_uuid)
{
	struct cxl_feat_entry *feat_entry;
	struct cxl_features_state *cfs;
	int count;

	cfs = dev_get_drvdata(&features->dev);
	if (!cfs)
		return ERR_PTR(-EOPNOTSUPP);

	if (!cfs->num_features)
		return ERR_PTR(-ENOENT);

	/* Check CXL dev supports the feature */
	feat_entry = cfs->entries;
	for (count = 0; count < cfs->num_features;
	     count++, feat_entry++) {
		if (uuid_equal(&feat_entry->uuid, feat_uuid))
			return feat_entry;
	}

	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL_NS_GPL(cxl_get_supported_feature_entry, "CXL");

bool cxl_feature_enabled(struct cxl_features_state *cfs, u16 opcode)
{
	struct cxl_mailbox *cxl_mbox = cfs->features->cxl_mbox;
	struct cxl_mem_command *cmd;

	cmd = cxl_find_feature_command(opcode);
	if (!cmd)
		return false;

	return test_bit(cmd->info.id, cxl_mbox->feature_cmds);
}
EXPORT_SYMBOL_NS_GPL(cxl_feature_enabled, "CXL");

size_t cxl_get_feature(struct cxl_features *features, const uuid_t feat_uuid,
		       enum cxl_get_feat_selection selection,
		       void *feat_out, size_t feat_out_size, u16 offset,
		       u16 *return_code)
{
	size_t data_to_rd_size, size_out;
	struct cxl_features_state *cfs;
	struct cxl_mbox_get_feat_in pi;
	struct cxl_mailbox *cxl_mbox;
	struct cxl_mbox_cmd mbox_cmd;
	size_t data_rcvd_size = 0;
	int rc;

	if (return_code)
		*return_code = CXL_MBOX_CMD_RC_INPUT;

	cfs = dev_get_drvdata(&features->dev);
	if (!cfs)
		return 0;

	if (!cxl_feature_enabled(cfs, CXL_MBOX_OP_GET_FEATURE))
		return 0;

	if (!feat_out || !feat_out_size)
		return 0;

	cxl_mbox = features->cxl_mbox;
	size_out = min(feat_out_size, cxl_mbox->payload_size);
	pi.uuid = feat_uuid;
	pi.selection = selection;
	do {
		data_to_rd_size = min(feat_out_size - data_rcvd_size,
				      cxl_mbox->payload_size);
		pi.offset = cpu_to_le16(offset + data_rcvd_size);
		pi.count = cpu_to_le16(data_to_rd_size);

		mbox_cmd = (struct cxl_mbox_cmd) {
			.opcode = CXL_MBOX_OP_GET_FEATURE,
			.size_in = sizeof(pi),
			.payload_in = &pi,
			.size_out = size_out,
			.payload_out = feat_out + data_rcvd_size,
			.min_out = data_to_rd_size,
		};
		rc = cxl_internal_send_cmd(cxl_mbox, &mbox_cmd);
		if (rc < 0 || !mbox_cmd.size_out) {
			if (return_code)
				*return_code = mbox_cmd.return_code;
			return 0;
		}
		data_rcvd_size += mbox_cmd.size_out;
	} while (data_rcvd_size < feat_out_size);

	if (return_code)
		*return_code = CXL_MBOX_CMD_RC_SUCCESS;

	return data_rcvd_size;
}
EXPORT_SYMBOL_NS_GPL(cxl_get_feature, "CXL");
