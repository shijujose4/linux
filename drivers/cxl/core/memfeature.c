// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CXL memory RAS feature driver.
 *
 * Copyright (c) 2024 HiSilicon Limited.
 *
 *  - Supports functions to configure RAS features of the
 *    CXL memory devices.
 *  - Registers with the EDAC device subsystem driver to expose
 *    the features sysfs attributes to the user for configuring
 *    CXL memory RAS feature.
 */

#include <linux/cleanup.h>
#include <linux/edac.h>
#include <linux/limits.h>
#include <cxl/features.h>
#include <cxl.h>
#include <cxlmem.h>

#define CXL_DEV_NUM_RAS_FEATURES	1
#define CXL_DEV_HOUR_IN_SECS	3600

#define CXL_DEV_NAME_LEN	128

/* CXL memory patrol scrub control functions */
struct cxl_patrol_scrub_context {
	u8 instance;
	u16 get_feat_size;
	u16 set_feat_size;
	u8 get_version;
	u8 set_version;
	u16 effects;
	struct cxl_memdev *cxlmd;
	struct cxl_region *cxlr;
};

/**
 * struct cxl_memdev_ps_params - CXL memory patrol scrub parameter data structure.
 * @enable:     [IN & OUT] enable(1)/disable(0) patrol scrub.
 * @scrub_cycle_changeable: [OUT] scrub cycle attribute of patrol scrub is changeable.
 * @scrub_cycle_hrs:    [IN] Requested patrol scrub cycle in hours.
 *                      [OUT] Current patrol scrub cycle in hours.
 * @min_scrub_cycle_hrs:[OUT] minimum patrol scrub cycle in hours supported.
 */
struct cxl_memdev_ps_params {
	bool enable;
	bool scrub_cycle_changeable;
	u8 scrub_cycle_hrs;
	u8 min_scrub_cycle_hrs;
};

enum cxl_scrub_param {
	CXL_PS_PARAM_ENABLE,
	CXL_PS_PARAM_SCRUB_CYCLE,
};

#define CXL_MEMDEV_PS_SCRUB_CYCLE_CHANGE_CAP_MASK	BIT(0)
#define	CXL_MEMDEV_PS_SCRUB_CYCLE_REALTIME_REPORT_CAP_MASK	BIT(1)
#define	CXL_MEMDEV_PS_CUR_SCRUB_CYCLE_MASK	GENMASK(7, 0)
#define	CXL_MEMDEV_PS_MIN_SCRUB_CYCLE_MASK	GENMASK(15, 8)
#define	CXL_MEMDEV_PS_FLAG_ENABLED_MASK	BIT(0)

struct cxl_memdev_ps_rd_attrs {
	u8 scrub_cycle_cap;
	__le16 scrub_cycle_hrs;
	u8 scrub_flags;
}  __packed;

struct cxl_memdev_ps_wr_attrs {
	u8 scrub_cycle_hrs;
	u8 scrub_flags;
}  __packed;

static int cxl_mem_ps_get_attrs(struct cxl_mailbox *cxl_mbox,
				struct cxl_memdev_ps_params *params)
{
	size_t rd_data_size = sizeof(struct cxl_memdev_ps_rd_attrs);
	u16 scrub_cycle_hrs;
	size_t data_size;
	u16 return_code;
	struct cxl_memdev_ps_rd_attrs *rd_attrs __free(kfree) =
						kmalloc(rd_data_size, GFP_KERNEL);
	if (!rd_attrs)
		return -ENOMEM;

	data_size = cxl_get_feature(cxl_mbox->features, CXL_FEAT_PATROL_SCRUB_UUID,
				    CXL_GET_FEAT_SEL_CURRENT_VALUE,
				    rd_attrs, rd_data_size, 0, &return_code);
	if (!data_size || return_code != CXL_MBOX_CMD_RC_SUCCESS)
		return -EIO;

	params->scrub_cycle_changeable = FIELD_GET(CXL_MEMDEV_PS_SCRUB_CYCLE_CHANGE_CAP_MASK,
						   rd_attrs->scrub_cycle_cap);
	params->enable = FIELD_GET(CXL_MEMDEV_PS_FLAG_ENABLED_MASK,
				   rd_attrs->scrub_flags);
	scrub_cycle_hrs = le16_to_cpu(rd_attrs->scrub_cycle_hrs);
	params->scrub_cycle_hrs = FIELD_GET(CXL_MEMDEV_PS_CUR_SCRUB_CYCLE_MASK,
					    scrub_cycle_hrs);
	params->min_scrub_cycle_hrs = FIELD_GET(CXL_MEMDEV_PS_MIN_SCRUB_CYCLE_MASK,
						scrub_cycle_hrs);

	return 0;
}

static int cxl_ps_get_attrs(struct cxl_patrol_scrub_context *cxl_ps_ctx,
			    struct cxl_memdev_ps_params *params)
{
	struct cxl_memdev *cxlmd;
	u16 min_scrub_cycle = 0;
	int i, ret;

	if (cxl_ps_ctx->cxlr) {
		struct cxl_region *cxlr = cxl_ps_ctx->cxlr;
		struct cxl_region_params *p = &cxlr->params;

		for (i = p->interleave_ways - 1; i >= 0; i--) {
			struct cxl_endpoint_decoder *cxled = p->targets[i];

			cxlmd = cxled_to_memdev(cxled);
			ret = cxl_mem_ps_get_attrs(&cxlmd->cxlds->cxl_mbox, params);
			if (ret)
				return ret;

			if (params->min_scrub_cycle_hrs > min_scrub_cycle)
				min_scrub_cycle = params->min_scrub_cycle_hrs;
		}
		params->min_scrub_cycle_hrs = min_scrub_cycle;
		return 0;
	}
	cxlmd = cxl_ps_ctx->cxlmd;

	return cxl_mem_ps_get_attrs(&cxlmd->cxlds->cxl_mbox, params);
}

static int cxl_mem_ps_set_attrs(struct device *dev,
				struct cxl_patrol_scrub_context *cxl_ps_ctx,
				struct cxl_mailbox *cxl_mbox,
				struct cxl_memdev_ps_params *params,
				enum cxl_scrub_param param_type)
{
	struct cxl_memdev_ps_wr_attrs wr_attrs;
	struct cxl_memdev_ps_params rd_params;
	u16 return_code;
	int ret;

	ret = cxl_mem_ps_get_attrs(cxl_mbox, &rd_params);
	if (ret) {
		dev_err(dev, "Get cxlmemdev patrol scrub params failed ret=%d\n",
			ret);
		return ret;
	}

	switch (param_type) {
	case CXL_PS_PARAM_ENABLE:
		wr_attrs.scrub_flags = FIELD_PREP(CXL_MEMDEV_PS_FLAG_ENABLED_MASK,
						  params->enable);
		wr_attrs.scrub_cycle_hrs = FIELD_PREP(CXL_MEMDEV_PS_CUR_SCRUB_CYCLE_MASK,
						      rd_params.scrub_cycle_hrs);
		break;
	case CXL_PS_PARAM_SCRUB_CYCLE:
		if (params->scrub_cycle_hrs < rd_params.min_scrub_cycle_hrs) {
			dev_err(dev, "Invalid CXL patrol scrub cycle(%d) to set\n",
				params->scrub_cycle_hrs);
			dev_err(dev, "Minimum supported CXL patrol scrub cycle in hour %d\n",
				rd_params.min_scrub_cycle_hrs);
			return -EINVAL;
		}
		wr_attrs.scrub_cycle_hrs = FIELD_PREP(CXL_MEMDEV_PS_CUR_SCRUB_CYCLE_MASK,
						      params->scrub_cycle_hrs);
		wr_attrs.scrub_flags = FIELD_PREP(CXL_MEMDEV_PS_FLAG_ENABLED_MASK,
						  rd_params.enable);
		break;
	}

	ret = cxl_set_feature(cxl_mbox->features, CXL_FEAT_PATROL_SCRUB_UUID,
			      cxl_ps_ctx->set_version,
			      &wr_attrs, sizeof(wr_attrs),
			      CXL_SET_FEAT_FLAG_DATA_SAVED_ACROSS_RESET,
			      0, &return_code);
	if (ret || return_code != CXL_MBOX_CMD_RC_SUCCESS) {
		dev_err(dev, "CXL patrol scrub set feature failed ret=%d return_code=%u\n",
			ret, return_code);
		return ret;
	}

	return 0;
}

static int cxl_ps_set_attrs(struct device *dev,
			    struct cxl_patrol_scrub_context *cxl_ps_ctx,
			    struct cxl_memdev_ps_params *params,
			    enum cxl_scrub_param param_type)
{
	struct cxl_memdev *cxlmd;
	int ret, i;

	if (cxl_ps_ctx->cxlr) {
		struct cxl_region *cxlr = cxl_ps_ctx->cxlr;
		struct cxl_region_params *p = &cxlr->params;

		for (i = p->interleave_ways - 1; i >= 0; i--) {
			struct cxl_endpoint_decoder *cxled = p->targets[i];

			cxlmd = cxled_to_memdev(cxled);
			ret = cxl_mem_ps_set_attrs(dev, cxl_ps_ctx, &cxlmd->cxlds->cxl_mbox,
						   params, param_type);
			if (ret)
				return ret;
		}
		return 0;
	}
	cxlmd = cxl_ps_ctx->cxlmd;

	return cxl_mem_ps_set_attrs(dev, cxl_ps_ctx, &cxlmd->cxlds->cxl_mbox,
				    params, param_type);
}

static int cxl_patrol_scrub_get_enabled_bg(struct device *dev, void *drv_data, bool *enabled)
{
	struct cxl_patrol_scrub_context *ctx = drv_data;
	struct cxl_memdev_ps_params params;
	int ret;

	ret = cxl_ps_get_attrs(ctx, &params);
	if (ret)
		return ret;

	*enabled = params.enable;

	return 0;
}

static int cxl_patrol_scrub_set_enabled_bg(struct device *dev, void *drv_data, bool enable)
{
	struct cxl_patrol_scrub_context *ctx = drv_data;
	struct cxl_memdev_ps_params params = {
		.enable = enable,
	};

	return cxl_ps_set_attrs(dev, ctx, &params, CXL_PS_PARAM_ENABLE);
}

static int cxl_patrol_scrub_read_min_scrub_cycle(struct device *dev, void *drv_data,
						 u32 *min)
{
	struct cxl_patrol_scrub_context *ctx = drv_data;
	struct cxl_memdev_ps_params params;
	int ret;

	ret = cxl_ps_get_attrs(ctx, &params);
	if (ret)
		return ret;
	*min = params.min_scrub_cycle_hrs * CXL_DEV_HOUR_IN_SECS;

	return 0;
}

static int cxl_patrol_scrub_read_max_scrub_cycle(struct device *dev, void *drv_data,
						 u32 *max)
{
	*max = U8_MAX * CXL_DEV_HOUR_IN_SECS; /* Max set by register size */

	return 0;
}

static int cxl_patrol_scrub_read_scrub_cycle(struct device *dev, void *drv_data,
					     u32 *scrub_cycle_secs)
{
	struct cxl_patrol_scrub_context *ctx = drv_data;
	struct cxl_memdev_ps_params params;
	int ret;

	ret = cxl_ps_get_attrs(ctx, &params);
	if (ret)
		return ret;

	*scrub_cycle_secs = params.scrub_cycle_hrs * CXL_DEV_HOUR_IN_SECS;

	return 0;
}

static int cxl_patrol_scrub_write_scrub_cycle(struct device *dev, void *drv_data,
					      u32 scrub_cycle_secs)
{
	struct cxl_patrol_scrub_context *ctx = drv_data;
	struct cxl_memdev_ps_params params = {
		.scrub_cycle_hrs = scrub_cycle_secs / CXL_DEV_HOUR_IN_SECS,
	};

	return cxl_ps_set_attrs(dev, ctx, &params, CXL_PS_PARAM_SCRUB_CYCLE);
}

static const struct edac_scrub_ops cxl_ps_scrub_ops = {
	.get_enabled_bg = cxl_patrol_scrub_get_enabled_bg,
	.set_enabled_bg = cxl_patrol_scrub_set_enabled_bg,
	.get_min_cycle = cxl_patrol_scrub_read_min_scrub_cycle,
	.get_max_cycle = cxl_patrol_scrub_read_max_scrub_cycle,
	.get_cycle_duration = cxl_patrol_scrub_read_scrub_cycle,
	.set_cycle_duration = cxl_patrol_scrub_write_scrub_cycle,
};

static int cxl_memdev_scrub_init(struct cxl_memdev *cxlmd, struct cxl_region *cxlr,
				 struct edac_dev_feature *ras_feature, u8 scrub_inst)
{
	struct cxl_patrol_scrub_context *cxl_ps_ctx;
	struct cxl_feat_entry *feat_entry;
	struct cxl_mailbox *cxl_mbox;
	struct cxl_dev_state *cxlds;
	int i;

	if (cxlr) {
		struct cxl_region_params *p = &cxlr->params;

		for (i = p->interleave_ways - 1; i >= 0; i--) {
			struct cxl_endpoint_decoder *cxled = p->targets[i];

			cxlmd = cxled_to_memdev(cxled);
			cxlds = cxlmd->cxlds;
			cxl_mbox = &cxlds->cxl_mbox;
			feat_entry = cxl_get_supported_feature_entry(cxl_mbox->features,
								     &CXL_FEAT_PATROL_SCRUB_UUID);
			if (IS_ERR(feat_entry))
				return -EOPNOTSUPP;

			if (!(le32_to_cpu(feat_entry->flags) & CXL_FEAT_ENTRY_FLAG_CHANGABLE))
				return -EOPNOTSUPP;
		}
	} else {
		cxlds = cxlmd->cxlds;
		cxl_mbox = &cxlds->cxl_mbox;
		feat_entry = cxl_get_supported_feature_entry(cxl_mbox->features,
							     &CXL_FEAT_PATROL_SCRUB_UUID);
		if (IS_ERR(feat_entry))
			return -EOPNOTSUPP;

		if (!(le32_to_cpu(feat_entry->flags) & CXL_FEAT_ENTRY_FLAG_CHANGABLE))
			return -EOPNOTSUPP;
	}

	cxl_ps_ctx = devm_kzalloc(&cxlmd->dev, sizeof(*cxl_ps_ctx), GFP_KERNEL);
	if (!cxl_ps_ctx)
		return -ENOMEM;

	*cxl_ps_ctx = (struct cxl_patrol_scrub_context) {
		.get_feat_size = le16_to_cpu(feat_entry->get_feat_size),
		.set_feat_size = le16_to_cpu(feat_entry->set_feat_size),
		.get_version = feat_entry->get_feat_ver,
		.set_version = feat_entry->set_feat_ver,
		.effects = le16_to_cpu(feat_entry->effects),
		.instance = scrub_inst,
	};
	if (cxlr)
		cxl_ps_ctx->cxlr = cxlr;
	else
		cxl_ps_ctx->cxlmd = cxlmd;

	ras_feature->ft_type = RAS_FEAT_SCRUB;
	ras_feature->instance = cxl_ps_ctx->instance;
	ras_feature->scrub_ops = &cxl_ps_scrub_ops;
	ras_feature->ctx = cxl_ps_ctx;

	return 0;
}

int cxl_mem_ras_features_init(struct cxl_memdev *cxlmd, struct cxl_region *cxlr)
{
	struct edac_dev_feature ras_features[CXL_DEV_NUM_RAS_FEATURES];
	char cxl_dev_name[CXL_DEV_NAME_LEN];
	int num_ras_features = 0;
	u8 scrub_inst = 0;
	int rc;

	rc = cxl_memdev_scrub_init(cxlmd, cxlr, &ras_features[num_ras_features],
				   scrub_inst);
	if (rc < 0)
		return rc;

	scrub_inst++;
	num_ras_features++;

	if (cxlr)
		snprintf(cxl_dev_name, sizeof(cxl_dev_name),
			 "cxl_region%d", cxlr->id);
	else
		snprintf(cxl_dev_name, sizeof(cxl_dev_name),
			 "%s_%s", "cxl", dev_name(&cxlmd->dev));

	return edac_dev_register(&cxlmd->dev, cxl_dev_name, NULL,
				 num_ras_features, ras_features);
}
EXPORT_SYMBOL_NS_GPL(cxl_mem_ras_features_init, "CXL");
