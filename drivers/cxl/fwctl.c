// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2025 Intel Corporation. All rights reserved. */
#include <linux/fwctl.h>
#include <linux/device.h>
#include <cxl/features.h>
#include "cxlmem.h"
#include "features.h"

#define to_cxl_features_state(fwctl) container_of(fwctl, struct cxl_features_state, fwctl)

static int cxlctl_open_uctx(struct fwctl_uctx *uctx)
{
	return 0;
}

static void cxlctl_close_uctx(struct fwctl_uctx *uctx)
{
}

static void *cxlctl_info(struct fwctl_uctx *uctx, size_t *length)
{
	struct fwctl_device *fwctl = uctx->fwctl;
	struct cxl_features_state *cxlfs = to_cxl_features_state(fwctl);
	struct fwctl_info_cxl *info;

	if (!cxlfs->num_user_features)
		return ERR_PTR(-EOPNOTSUPP);

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	*length = sizeof(*info);

	return info;
}

static struct cxl_feat_entry *
get_support_feature_info(struct cxl_features_state *cxlfs,
			 const struct fwctl_rpc_cxl *rpc_in)
{
	struct cxl_feat_entry *feat;
	uuid_t uuid;

	if (rpc_in->op_size < sizeof(uuid))
		return ERR_PTR(-EINVAL);

	if (copy_from_user(&uuid, u64_to_user_ptr(rpc_in->in_payload),
			   sizeof(uuid)))
		return ERR_PTR(-EFAULT);

	for (int i = 0; i < cxlfs->num_features; i++) {
		feat = &cxlfs->entries[i];
		if (uuid_equal(&uuid, &feat->uuid))
			return feat;
	}

	return ERR_PTR(-EINVAL);
}

static void *cxlctl_get_supported_features(struct cxl_features_state *cxlfs,
					   const struct fwctl_rpc_cxl *rpc_in,
					   size_t *out_len)
{
	struct cxl_mbox_get_sup_feats_out *feat_out;
	struct cxl_mbox_get_sup_feats_in feat_in;
	struct cxl_feat_entry *pos;
	size_t out_size;
	int requested;
	u32 count;
	u16 start;
	int i;

	if (rpc_in->op_size != sizeof(feat_in))
		return ERR_PTR(-EINVAL);

	if (copy_from_user(&feat_in, u64_to_user_ptr(rpc_in->in_payload),
			   rpc_in->op_size))
		return ERR_PTR(-EFAULT);

	count = le32_to_cpu(feat_in.count);
	start = le16_to_cpu(feat_in.start_idx);
	requested = count / sizeof(*pos);

	/*
	 * Make sure that the total requested number of entries is not greater
	 * than the total number of supported features allowed for userspace.
	 */
	if (start >= cxlfs->num_user_features)
		return ERR_PTR(-EINVAL);

	requested = min_t(int, requested, cxlfs->num_user_features - start);

	out_size = sizeof(struct fwctl_rpc_cxl_out) +
		struct_size(feat_out, ents, requested);

	struct fwctl_rpc_cxl_out *rpc_out __free(kvfree) =
		kvzalloc(out_size, GFP_KERNEL);
	if (!rpc_out)
		return ERR_PTR(-ENOMEM);

	rpc_out->size = struct_size(feat_out, ents, requested);
	feat_out = (struct cxl_mbox_get_sup_feats_out *)rpc_out->payload;
	if (requested == 0) {
		feat_out->num_entries = cpu_to_le16(requested);
		feat_out->supported_feats =
			cpu_to_le16(cxlfs->num_user_features);
		rpc_out->retval = CXL_MBOX_CMD_RC_SUCCESS;
		*out_len = out_size;
		return no_free_ptr(rpc_out);
	}

	for (i = 0, pos = &feat_out->ents[0];
	     i < cxlfs->num_features; i++, pos++) {
		if (i == requested)
			break;

		memcpy(pos, &cxlfs->entries[i], sizeof(*pos));
		/*
		 * If the feature is exclusive, set the set_feat_size to 0 to
		 * indicate that the feature is not changeable.
		 */
		if (is_cxl_feature_exclusive(pos))
			pos->set_feat_size = 0;
	}

	feat_out->num_entries = cpu_to_le16(requested);
	feat_out->supported_feats = cpu_to_le16(cxlfs->num_features);
	rpc_out->retval = CXL_MBOX_CMD_RC_SUCCESS;
	*out_len = out_size;

	return no_free_ptr(rpc_out);
}

static void *cxlctl_get_feature(struct cxl_features_state *cxlfs,
				const struct fwctl_rpc_cxl *rpc_in,
				size_t *out_len)
{
	struct cxl_dev_state *cxlds = cxlfs->cxlmd->cxlds;
	struct cxl_mailbox *cxl_mbox = &cxlds->cxl_mbox;
	struct cxl_mbox_get_feat_in feat_in;
	u16 offset, count, return_code;
	size_t out_size = *out_len;

	if (rpc_in->op_size != sizeof(feat_in))
		return ERR_PTR(-EINVAL);

	if (copy_from_user(&feat_in, u64_to_user_ptr(rpc_in->in_payload),
			   rpc_in->op_size))
		return ERR_PTR(-EFAULT);

	offset = le16_to_cpu(feat_in.offset);
	count = le16_to_cpu(feat_in.count);

	if (!count)
		return ERR_PTR(-EINVAL);

	struct fwctl_rpc_cxl_out *rpc_out __free(kvfree) =
		kvzalloc(out_size, GFP_KERNEL);
	if (!rpc_out)
		return ERR_PTR(-ENOMEM);

	out_size = cxl_get_feature(cxl_mbox, &feat_in.uuid,
				   feat_in.selection, rpc_out->payload,
				   count, offset, &return_code);
	*out_len = sizeof(struct fwctl_rpc_cxl_out);
	if (!out_size) {
		rpc_out->size = 0;
		rpc_out->retval = return_code;
		return no_free_ptr(rpc_out);
	}

	rpc_out->size = out_size;
	rpc_out->retval = CXL_MBOX_CMD_RC_SUCCESS;
	*out_len += out_size;

	return no_free_ptr(rpc_out);
}

static void *cxlctl_set_feature(struct cxl_features_state *cxlfs,
				const struct fwctl_rpc_cxl *rpc_in,
				size_t *out_len)
{
	struct cxl_dev_state *cxlds = cxlfs->cxlmd->cxlds;
	struct cxl_mailbox *cxl_mbox = &cxlds->cxl_mbox;
	size_t out_size, data_size;
	u16 offset, return_code;
	u32 flags;
	int rc;

	if (rpc_in->op_size <= sizeof(struct cxl_mbox_set_feat_hdr))
		return ERR_PTR(-EINVAL);

	struct cxl_mbox_set_feat_in *feat_in __free(kvfree) =
		kvzalloc(rpc_in->op_size, GFP_KERNEL);
	if (!feat_in)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(feat_in, u64_to_user_ptr(rpc_in->in_payload),
			   rpc_in->op_size))
		return ERR_PTR(-EFAULT);

	if (is_cxl_feature_exclusive_by_uuid(&feat_in->hdr.uuid))
		return ERR_PTR(-EPERM);

	offset = le16_to_cpu(feat_in->hdr.offset);
	flags = le32_to_cpu(feat_in->hdr.flags);
	out_size = *out_len;

	struct fwctl_rpc_cxl_out *rpc_out __free(kvfree) =
		kvzalloc(out_size, GFP_KERNEL);
	if (!rpc_out)
		return ERR_PTR(-ENOMEM);

	rpc_out->size = 0;

	data_size = rpc_in->op_size - sizeof(feat_in->hdr);
	rc = cxl_set_feature(cxl_mbox, &feat_in->hdr.uuid,
			     feat_in->hdr.version, feat_in->data,
			     data_size, flags, offset, &return_code);
	if (rc) {
		rpc_out->retval = return_code;
		return no_free_ptr(rpc_out);
	}

	rpc_out->retval = CXL_MBOX_CMD_RC_SUCCESS;
	*out_len = sizeof(*rpc_out);

	return no_free_ptr(rpc_out);
}

static bool cxlctl_validate_set_features(struct cxl_features_state *cxlfs,
					 const struct fwctl_rpc_cxl *rpc_in,
					 enum fwctl_rpc_scope scope)
{
	struct cxl_feat_entry *feat;
	u16 effects, mask;
	u32 flags;

	feat = get_support_feature_info(cxlfs, rpc_in);
	if (IS_ERR(feat))
		return false;

	/* Ensure that the attribute is changeable */
	flags = le32_to_cpu(feat->flags);
	if (!(flags & CXL_FEATURE_F_CHANGEABLE))
		return false;

	effects = le16_to_cpu(feat->effects);

	/*
	 * Reserved bits are set, rejecting since the effects is not
	 * comprehended by the driver.
	 */
	if (effects & CXL_CMD_EFFECTS_RESERVED) {
		dev_warn_once(&cxlfs->cxlmd->dev,
			      "Reserved bits set in the Feature effects field!\n");
		return false;
	}

	/* Currently no user background command support */
	if (effects & CXL_CMD_BACKGROUND)
		return false;

	/* Effects cause immediate change, highest security scope is needed */
	mask = CXL_CMD_CONFIG_CHANGE_IMMEDIATE |
	       CXL_CMD_DATA_CHANGE_IMMEDIATE |
	       CXL_CMD_POLICY_CHANGE_IMMEDIATE |
	       CXL_CMD_LOG_CHANGE_IMMEDIATE;
	if (effects & mask && scope >= FWCTL_RPC_DEBUG_WRITE_FULL)
		return true;

	/* These effects supported for all WRITE scope */
	if ((effects & CXL_CMD_CONFIG_CHANGE_COLD_RESET ||
	     effects & CXL_CMD_CONFIG_CHANGE_CONV_RESET ||
	     effects & CXL_CMD_CONFIG_CHANGE_CXL_RESET) &&
	    scope >= FWCTL_RPC_DEBUG_WRITE)
		return true;

	return false;
}

static bool cxlctl_validate_hw_command(struct cxl_features_state *cxlfs,
				       const struct fwctl_rpc_cxl *rpc_in,
				       enum fwctl_rpc_scope scope,
				       u16 opcode)
{
	if (!cxlfs->num_features)
		return false;

	switch (opcode) {
	case CXL_MBOX_OP_GET_SUPPORTED_FEATURES:
	case CXL_MBOX_OP_GET_FEATURE:
		if (cxlfs->cap < CXL_FEATURES_RO)
			return false;
		if (scope >= FWCTL_RPC_CONFIGURATION)
			return true;
		return false;
	case CXL_MBOX_OP_SET_FEATURE:
		return cxlctl_validate_set_features(cxlfs, rpc_in, scope);
	default:
		return false;
	}
}

static void *cxlctl_handle_commands(struct cxl_features_state *cxlfs,
				    const struct fwctl_rpc_cxl *rpc_in,
				    size_t *out_len, u16 opcode)
{
	switch (opcode) {
	case CXL_MBOX_OP_GET_SUPPORTED_FEATURES:
		return cxlctl_get_supported_features(cxlfs, rpc_in, out_len);
	case CXL_MBOX_OP_GET_FEATURE:
		return cxlctl_get_feature(cxlfs, rpc_in, out_len);
	case CXL_MBOX_OP_SET_FEATURE:
		return cxlctl_set_feature(cxlfs, rpc_in, out_len);
	default:
		return ERR_PTR(-EOPNOTSUPP);
	}
}

static void *cxlctl_fw_rpc(struct fwctl_uctx *uctx, enum fwctl_rpc_scope scope,
			   void *in, size_t in_len, size_t *out_len)
{
	struct fwctl_device *fwctl = uctx->fwctl;
	struct cxl_features_state *cxlfs = to_cxl_features_state(fwctl);
	const struct fwctl_rpc_cxl *rpc_in = in;
	u16 opcode;

	opcode = cxl_get_feature_command_opcode(rpc_in->command_id);
	if (opcode == 0xffff)
		return ERR_PTR(-EINVAL);

	if (!cxlctl_validate_hw_command(cxlfs, rpc_in, scope, opcode))
		return ERR_PTR(-EINVAL);

	return cxlctl_handle_commands(cxlfs, rpc_in, out_len, opcode);
}

static const struct fwctl_ops cxlctl_ops = {
	.device_type = FWCTL_DEVICE_TYPE_CXL,
	.uctx_size = sizeof(struct fwctl_uctx),
	.open_uctx = cxlctl_open_uctx,
	.close_uctx = cxlctl_close_uctx,
	.info = cxlctl_info,
	.fw_rpc = cxlctl_fw_rpc,
};

static void remove_cxlfs(void *_cxlfs)
{
	struct cxl_features_state *cxlfs = _cxlfs;
	struct cxl_memdev *cxlmd = cxlfs->cxlmd;
	struct fwctl_device *fwctl = &cxlfs->fwctl;

	/* Set in devm_cxl_add_features(), make sure it's cleared */
	cxlmd->cxlfs = NULL;
	fwctl_unregister(fwctl);
	fwctl_put(fwctl);
}

DEFINE_FREE(free_cxlfs, struct cxl_features_state *, if (_T) fwctl_put(&_T->fwctl))

static struct cxl_features_state *
__devm_cxlfs_fwctl_allocate(struct cxl_memdev *cxlmd, const struct fwctl_ops *ops)
{
	struct device *dev = &cxlmd->dev;
	int rc;

	struct cxl_features_state *cxlfs __free(free_cxlfs) =
		fwctl_alloc_device(dev, ops, struct cxl_features_state, fwctl);
	if (!cxlfs)
		return NULL;

	cxlfs->cxlmd = cxlmd;
	rc = fwctl_register(&cxlfs->fwctl);
	if (rc)
		return NULL;

	rc = devm_add_action_or_reset(dev, remove_cxlfs, cxlfs);
	if (rc)
		return NULL;

	return no_free_ptr(cxlfs);
}

struct cxl_features_state *_devm_cxlfs_fwctl_allocate(struct cxl_memdev *cxlmd)
{
	return __devm_cxlfs_fwctl_allocate(cxlmd, &cxlctl_ops);
}
EXPORT_SYMBOL_NS_GPL(_devm_cxlfs_fwctl_allocate, "CXL");

void _devm_cxlfs_fwctl_free(struct cxl_memdev *cxlmd)
{
	devm_release_action(&cxlmd->dev, remove_cxlfs, cxlmd);
}
EXPORT_SYMBOL_NS_GPL(_devm_cxlfs_fwctl_free, "CXL");

MODULE_IMPORT_NS("FWCTL");
