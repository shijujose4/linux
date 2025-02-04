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

static void *cxlctl_fw_rpc(struct fwctl_uctx *uctx, enum fwctl_rpc_scope scope,
			   void *in, size_t in_len, size_t *out_len)
{
	/* Place holder */
	return ERR_PTR(-EOPNOTSUPP);
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
