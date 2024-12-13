// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2024-2025 Intel Corporation. All rights reserved. */
#include <linux/device.h>
#include <cxl/mailbox.h>
#include "cxl.h"
#include "core.h"
#include "cxlmem.h"

static const uuid_t cxl_exclusive_feats[] = {
	CXL_FEAT_PATROL_SCRUB_UUID,
	CXL_FEAT_ECS_UUID,
	CXL_FEAT_SPPR_UUID,
	CXL_FEAT_HPPR_UUID,
	CXL_FEAT_CACHELINE_SPARING_UUID,
	CXL_FEAT_ROW_SPARING_UUID,
	CXL_FEAT_BANK_SPARING_UUID,
	CXL_FEAT_RANK_SPARING_UUID,
};

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

/*
 * FEAT_DATA_MIN_PAYLOAD_SIZE - min extra number of bytes should be
 * available in the mailbox for storing the actual feature data so that
 * the feature data transfer would work as expected.
 */
#define FEAT_DATA_MIN_PAYLOAD_SIZE 10
int cxl_set_feature(struct cxl_features *features,
		    const uuid_t feat_uuid, u8 feat_version,
		    void *feat_data, size_t feat_data_size,
		    u32 feat_flag, u16 offset, u16 *return_code)
{
	struct cxl_memdev_set_feat_pi {
		struct cxl_mbox_set_feat_hdr hdr;
		u8 feat_data[];
	}  __packed;
	size_t data_in_size, data_sent_size = 0;
	struct cxl_features_state *cfs;
	struct cxl_mbox_cmd mbox_cmd;
	struct cxl_mailbox *cxl_mbox;
	size_t hdr_size;
	int rc = 0;

	if (return_code)
		*return_code = CXL_MBOX_CMD_RC_INPUT;

	cfs = dev_get_drvdata(&features->dev);
	if (!cfs)
		return -EOPNOTSUPP;

	if (!cxl_feature_enabled(cfs, CXL_MBOX_OP_SET_FEATURE))
		return -EOPNOTSUPP;

	cxl_mbox = features->cxl_mbox;

	struct cxl_memdev_set_feat_pi *pi __free(kfree) =
					kmalloc(cxl_mbox->payload_size, GFP_KERNEL);
	pi->hdr.uuid = feat_uuid;
	pi->hdr.version = feat_version;
	feat_flag &= ~CXL_SET_FEAT_FLAG_DATA_TRANSFER_MASK;
	feat_flag |= CXL_SET_FEAT_FLAG_DATA_SAVED_ACROSS_RESET;
	hdr_size = sizeof(pi->hdr);
	/*
	 * Check minimum mbox payload size is available for
	 * the feature data transfer.
	 */
	if (hdr_size + FEAT_DATA_MIN_PAYLOAD_SIZE > cxl_mbox->payload_size)
		return -ENOMEM;

	if ((hdr_size + feat_data_size) <= cxl_mbox->payload_size) {
		pi->hdr.flags = cpu_to_le32(feat_flag |
				       CXL_SET_FEAT_FLAG_FULL_DATA_TRANSFER);
		data_in_size = feat_data_size;
	} else {
		pi->hdr.flags = cpu_to_le32(feat_flag |
				       CXL_SET_FEAT_FLAG_INITIATE_DATA_TRANSFER);
		data_in_size = cxl_mbox->payload_size - hdr_size;
	}

	do {
		pi->hdr.offset = cpu_to_le16(offset + data_sent_size);
		memcpy(pi->feat_data, feat_data + data_sent_size, data_in_size);
		mbox_cmd = (struct cxl_mbox_cmd) {
			.opcode = CXL_MBOX_OP_SET_FEATURE,
			.size_in = hdr_size + data_in_size,
			.payload_in = pi,
		};
		rc = cxl_internal_send_cmd(cxl_mbox, &mbox_cmd);
		if (rc < 0) {
			if (return_code)
				*return_code = mbox_cmd.return_code;
			return rc;
		}

		data_sent_size += data_in_size;
		if (data_sent_size >= feat_data_size) {
			if (return_code)
				*return_code = CXL_MBOX_CMD_RC_SUCCESS;
			return 0;
		}

		if ((feat_data_size - data_sent_size) <= (cxl_mbox->payload_size - hdr_size)) {
			data_in_size = feat_data_size - data_sent_size;
			pi->hdr.flags = cpu_to_le32(feat_flag |
					       CXL_SET_FEAT_FLAG_FINISH_DATA_TRANSFER);
		} else {
			pi->hdr.flags = cpu_to_le32(feat_flag |
					       CXL_SET_FEAT_FLAG_CONTINUE_DATA_TRANSFER);
		}
	} while (true);
}
EXPORT_SYMBOL_NS_GPL(cxl_set_feature, "CXL");

bool is_cxl_feature_exclusive(struct cxl_feat_entry *entry)
{
	for (int i = 0; i < ARRAY_SIZE(cxl_exclusive_feats); i++) {
		if (uuid_equal(&entry->uuid, &cxl_exclusive_feats[i]))
			return true;
	}

	return false;
}
EXPORT_SYMBOL_NS_GPL(is_cxl_feature_exclusive, "CXL");
