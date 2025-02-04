// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2024-2025 Intel Corporation. All rights reserved. */
#include <linux/device.h>
#include <cxl/mailbox.h>
#include <cxl/features.h>
#include "cxl.h"
#include "cxlmem.h"
#include "features.h"

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

static struct cxl_features_state *devm_cxlfs_allocate(struct cxl_memdev *cxlmd)
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

static void devm_cxlfs_free(struct cxl_memdev *cxlmd)
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
	struct cxl_features_state *cxlfs __free(cxl_free_cxlfs) =
		devm_cxlfs_allocate(cxlmd);
	if (!cxlfs)
		return -ENOMEM;

	enumerate_feature_cmds(cxlmd, cxlfs);

	cxlmd->cxlfs = no_free_ptr(cxlfs);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_add_features, "CXL");
