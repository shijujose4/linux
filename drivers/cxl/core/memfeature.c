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
#include <linux/unaligned.h>
#include <cxl/features.h>
#include <cxl.h>
#include <cxlmem.h>
#include "core.h"

#define CXL_DEV_NUM_RAS_FEATURES	7
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

static int cxl_mem_ps_get_attrs(struct cxl_memdev_state *mds,
				struct cxl_memdev_ps_params *params)
{
	size_t rd_data_size = sizeof(struct cxl_memdev_ps_rd_attrs);
	u16 scrub_cycle_hrs;
	size_t data_size;
	struct cxl_memdev_ps_rd_attrs *rd_attrs __free(kfree) =
						kmalloc(rd_data_size, GFP_KERNEL);
	if (!rd_attrs)
		return -ENOMEM;

	data_size = cxl_get_feature(&mds->cxlds, CXL_FEAT_PATROL_SCRUB_UUID,
				    CXL_GET_FEAT_SEL_CURRENT_VALUE,
				    rd_attrs, rd_data_size);
	if (!data_size)
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

static int cxl_ps_get_attrs(struct device *dev,
			    struct cxl_patrol_scrub_context *cxl_ps_ctx,
			    struct cxl_memdev_ps_params *params)
{
	struct cxl_memdev *cxlmd;
	struct cxl_dev_state *cxlds;
	struct cxl_memdev_state *mds;
	u16 min_scrub_cycle = 0;
	int i, ret;

	if (cxl_ps_ctx->cxlr) {
		struct cxl_region *cxlr = cxl_ps_ctx->cxlr;
		struct cxl_region_params *p = &cxlr->params;

		for (i = p->interleave_ways - 1; i >= 0; i--) {
			struct cxl_endpoint_decoder *cxled = p->targets[i];

			cxlmd = cxled_to_memdev(cxled);
			cxlds = cxlmd->cxlds;
			mds = to_cxl_memdev_state(cxlds);
			ret = cxl_mem_ps_get_attrs(mds, params);
			if (ret)
				return ret;

			if (params->min_scrub_cycle_hrs > min_scrub_cycle)
				min_scrub_cycle = params->min_scrub_cycle_hrs;
		}
		params->min_scrub_cycle_hrs = min_scrub_cycle;
		return 0;
	}
	cxlmd = cxl_ps_ctx->cxlmd;
	cxlds = cxlmd->cxlds;
	mds = to_cxl_memdev_state(cxlds);

	return cxl_mem_ps_get_attrs(mds, params);
}

static int cxl_mem_ps_set_attrs(struct device *dev,
				struct cxl_patrol_scrub_context *cxl_ps_ctx,
				struct cxl_memdev_state *mds,
				struct cxl_memdev_ps_params *params,
				enum cxl_scrub_param param_type)
{
	struct cxl_memdev_ps_wr_attrs wr_attrs;
	struct cxl_memdev_ps_params rd_params;
	int ret;

	ret = cxl_mem_ps_get_attrs(mds, &rd_params);
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

	ret = cxl_set_feature(&mds->cxlds, CXL_FEAT_PATROL_SCRUB_UUID,
			      cxl_ps_ctx->set_version,
			      &wr_attrs, sizeof(wr_attrs),
			      CXL_SET_FEAT_FLAG_DATA_SAVED_ACROSS_RESET);
	if (ret) {
		dev_err(dev, "CXL patrol scrub set feature failed ret=%d\n", ret);
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
	struct cxl_dev_state *cxlds;
	struct cxl_memdev_state *mds;
	int ret, i;

	if (cxl_ps_ctx->cxlr) {
		struct cxl_region *cxlr = cxl_ps_ctx->cxlr;
		struct cxl_region_params *p = &cxlr->params;

		for (i = p->interleave_ways - 1; i >= 0; i--) {
			struct cxl_endpoint_decoder *cxled = p->targets[i];

			cxlmd = cxled_to_memdev(cxled);
			cxlds = cxlmd->cxlds;
			mds = to_cxl_memdev_state(cxlds);
			ret = cxl_mem_ps_set_attrs(dev, cxl_ps_ctx, mds,
						   params, param_type);
			if (ret)
				return ret;
		}
		return 0;
	}
	cxlmd = cxl_ps_ctx->cxlmd;
	cxlds = cxlmd->cxlds;
	mds = to_cxl_memdev_state(cxlds);

	return cxl_mem_ps_set_attrs(dev, cxl_ps_ctx, mds, params, param_type);
}

static int cxl_patrol_scrub_get_enabled_bg(struct device *dev, void *drv_data, bool *enabled)
{
	struct cxl_patrol_scrub_context *ctx = drv_data;
	struct cxl_memdev_ps_params params;
	int ret;

	ret = cxl_ps_get_attrs(dev, ctx, &params);
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

	ret = cxl_ps_get_attrs(dev, ctx, &params);
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

	ret = cxl_ps_get_attrs(dev, ctx, &params);
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

/* CXL DDR5 ECS control definitions */
struct cxl_ecs_context {
	u16 num_media_frus;
	u16 get_feat_size;
	u16 set_feat_size;
	u8 get_version;
	u8 set_version;
	u16 effects;
	struct cxl_memdev *cxlmd;
};

enum {
	CXL_ECS_PARAM_LOG_ENTRY_TYPE,
	CXL_ECS_PARAM_THRESHOLD,
	CXL_ECS_PARAM_MODE,
	CXL_ECS_PARAM_RESET_COUNTER,
};

#define CXL_ECS_LOG_ENTRY_TYPE_MASK	GENMASK(1, 0)
#define CXL_ECS_REALTIME_REPORT_CAP_MASK	BIT(0)
#define CXL_ECS_THRESHOLD_COUNT_MASK	GENMASK(2, 0)
#define CXL_ECS_COUNT_MODE_MASK	BIT(3)
#define CXL_ECS_RESET_COUNTER_MASK	BIT(4)
#define CXL_ECS_RESET_COUNTER	1

enum {
	ECS_THRESHOLD_256 = 256,
	ECS_THRESHOLD_1024 = 1024,
	ECS_THRESHOLD_4096 = 4096,
};

enum {
	ECS_THRESHOLD_IDX_256 = 3,
	ECS_THRESHOLD_IDX_1024 = 4,
	ECS_THRESHOLD_IDX_4096 = 5,
};

static const u16 ecs_supp_threshold[] = {
	[ECS_THRESHOLD_IDX_256] = 256,
	[ECS_THRESHOLD_IDX_1024] = 1024,
	[ECS_THRESHOLD_IDX_4096] = 4096,
};

enum {
	ECS_LOG_ENTRY_TYPE_DRAM = 0x0,
	ECS_LOG_ENTRY_TYPE_MEM_MEDIA_FRU = 0x1,
};

enum cxl_ecs_count_mode {
	ECS_MODE_COUNTS_ROWS = 0,
	ECS_MODE_COUNTS_CODEWORDS = 1,
};

/**
 * struct cxl_ecs_params - CXL memory DDR5 ECS parameter data structure.
 * @log_entry_type: ECS log entry type, per DRAM or per memory media FRU.
 * @threshold: ECS threshold count per GB of memory cells.
 * @count_mode: codeword/row count mode
 *		0 : ECS counts rows with errors
 *		1 : ECS counts codeword with errors
 * @reset_counter: [IN] reset ECC counter to default value.
 */
struct cxl_ecs_params {
	u8 log_entry_type;
	u16 threshold;
	enum cxl_ecs_count_mode count_mode;
	u8 reset_counter;
};

struct cxl_ecs_fru_rd_attrs {
	u8 ecs_cap;
	__le16 ecs_config;
	u8 ecs_flags;
}  __packed;

struct cxl_ecs_rd_attrs {
	u8 ecs_log_cap;
	struct cxl_ecs_fru_rd_attrs fru_attrs[];
}  __packed;

struct cxl_ecs_fru_wr_attrs {
	__le16 ecs_config;
} __packed;

struct cxl_ecs_wr_attrs {
	u8 ecs_log_cap;
	struct cxl_ecs_fru_wr_attrs fru_attrs[];
}  __packed;

/* CXL DDR5 ECS control functions */
static int cxl_mem_ecs_get_attrs(struct device *dev,
				 struct cxl_ecs_context *cxl_ecs_ctx,
				 int fru_id, struct cxl_ecs_params *params)
{
	struct cxl_memdev *cxlmd = cxl_ecs_ctx->cxlmd;
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlds);
	struct cxl_ecs_fru_rd_attrs *fru_rd_attrs;
	size_t rd_data_size;
	u8 threshold_index;
	size_t data_size;
	u16 ecs_config;

	rd_data_size = cxl_ecs_ctx->get_feat_size;

	struct cxl_ecs_rd_attrs *rd_attrs __free(kfree) =
					kmalloc(rd_data_size, GFP_KERNEL);
	if (!rd_attrs)
		return -ENOMEM;

	params->log_entry_type = 0;
	params->threshold = 0;
	params->count_mode = 0;
	data_size = cxl_get_feature(&mds->cxlds, CXL_FEAT_ECS_UUID,
				    CXL_GET_FEAT_SEL_CURRENT_VALUE,
				    rd_attrs, rd_data_size);
	if (!data_size)
		return -EIO;

	fru_rd_attrs = rd_attrs->fru_attrs;
	params->log_entry_type = FIELD_GET(CXL_ECS_LOG_ENTRY_TYPE_MASK,
					   rd_attrs->ecs_log_cap);
	ecs_config = le16_to_cpu(fru_rd_attrs[fru_id].ecs_config);
	threshold_index = FIELD_GET(CXL_ECS_THRESHOLD_COUNT_MASK,
				    ecs_config);
	params->threshold = ecs_supp_threshold[threshold_index];
	params->count_mode = FIELD_GET(CXL_ECS_COUNT_MODE_MASK,
				       ecs_config);
	return 0;
}

static int cxl_mem_ecs_set_attrs(struct device *dev,
				 struct cxl_ecs_context *cxl_ecs_ctx,
				 int fru_id, struct cxl_ecs_params *params,
				 u8 param_type)
{
	struct cxl_memdev *cxlmd = cxl_ecs_ctx->cxlmd;
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlds);
	struct cxl_ecs_fru_rd_attrs *fru_rd_attrs;
	struct cxl_ecs_fru_wr_attrs *fru_wr_attrs;
	size_t rd_data_size, wr_data_size;
	u16 num_media_frus, count;
	size_t data_size;
	u16 ecs_config;
	int ret;

	num_media_frus = cxl_ecs_ctx->num_media_frus;
	rd_data_size = cxl_ecs_ctx->get_feat_size;
	wr_data_size = cxl_ecs_ctx->set_feat_size;
	struct cxl_ecs_rd_attrs *rd_attrs __free(kfree) =
				kmalloc(rd_data_size, GFP_KERNEL);
	if (!rd_attrs)
		return -ENOMEM;

	data_size = cxl_get_feature(&mds->cxlds, CXL_FEAT_ECS_UUID,
				    CXL_GET_FEAT_SEL_CURRENT_VALUE,
				    rd_attrs, rd_data_size);
	if (!data_size)
		return -EIO;

	struct cxl_ecs_wr_attrs *wr_attrs __free(kfree) =
					kmalloc(wr_data_size, GFP_KERNEL);
	if (!wr_attrs)
		return -ENOMEM;

	/*
	 * Fill writable attributes from the current attributes read
	 * for all the media FRUs.
	 */
	fru_rd_attrs = rd_attrs->fru_attrs;
	fru_wr_attrs = wr_attrs->fru_attrs;
	wr_attrs->ecs_log_cap = rd_attrs->ecs_log_cap;
	for (count = 0; count < num_media_frus; count++)
		fru_wr_attrs[count].ecs_config = fru_rd_attrs[count].ecs_config;

	/* Fill attribute to be set for the media FRU */
	ecs_config = le16_to_cpu(fru_rd_attrs[fru_id].ecs_config);
	switch (param_type) {
	case CXL_ECS_PARAM_LOG_ENTRY_TYPE:
		if (params->log_entry_type != ECS_LOG_ENTRY_TYPE_DRAM &&
		    params->log_entry_type != ECS_LOG_ENTRY_TYPE_MEM_MEDIA_FRU) {
			dev_err(dev,
				"Invalid CXL ECS scrub log entry type(%d) to set\n",
			       params->log_entry_type);
			dev_err(dev,
				"Log Entry Type 0: per DRAM  1: per Memory Media FRU\n");
			return -EINVAL;
		}
		wr_attrs->ecs_log_cap = FIELD_PREP(CXL_ECS_LOG_ENTRY_TYPE_MASK,
						   params->log_entry_type);
		break;
	case CXL_ECS_PARAM_THRESHOLD:
		ecs_config &= ~CXL_ECS_THRESHOLD_COUNT_MASK;
		switch (params->threshold) {
		case ECS_THRESHOLD_256:
			ecs_config |= FIELD_PREP(CXL_ECS_THRESHOLD_COUNT_MASK,
						 ECS_THRESHOLD_IDX_256);
			break;
		case ECS_THRESHOLD_1024:
			ecs_config |= FIELD_PREP(CXL_ECS_THRESHOLD_COUNT_MASK,
						 ECS_THRESHOLD_IDX_1024);
			break;
		case ECS_THRESHOLD_4096:
			ecs_config |= FIELD_PREP(CXL_ECS_THRESHOLD_COUNT_MASK,
						 ECS_THRESHOLD_IDX_4096);
			break;
		default:
			dev_err(dev,
				"Invalid CXL ECS scrub threshold count(%d) to set\n",
				params->threshold);
			dev_err(dev,
				"Supported scrub threshold counts: %u, %u, %u\n",
				ECS_THRESHOLD_256, ECS_THRESHOLD_1024, ECS_THRESHOLD_4096);
			return -EINVAL;
		}
		break;
	case CXL_ECS_PARAM_MODE:
		if (params->count_mode != ECS_MODE_COUNTS_ROWS &&
		    params->count_mode != ECS_MODE_COUNTS_CODEWORDS) {
			dev_err(dev,
				"Invalid CXL ECS scrub mode(%d) to set\n",
				params->count_mode);
			dev_err(dev,
				"Supported ECS Modes: 0: ECS counts rows with errors,"
				" 1: ECS counts codewords with errors\n");
			return -EINVAL;
		}
		ecs_config &= ~CXL_ECS_COUNT_MODE_MASK;
		ecs_config |= FIELD_PREP(CXL_ECS_COUNT_MODE_MASK, params->count_mode);
		break;
	case CXL_ECS_PARAM_RESET_COUNTER:
		if (params->reset_counter != CXL_ECS_RESET_COUNTER)
			return -EINVAL;

		ecs_config &= ~CXL_ECS_RESET_COUNTER_MASK;
		ecs_config |= FIELD_PREP(CXL_ECS_RESET_COUNTER_MASK, params->reset_counter);
		break;
	default:
		dev_err(dev, "Invalid CXL ECS parameter to set\n");
		return -EINVAL;
	}
	fru_wr_attrs[fru_id].ecs_config = cpu_to_le16(ecs_config);

	ret = cxl_set_feature(&mds->cxlds, CXL_FEAT_ECS_UUID, cxl_ecs_ctx->set_version,
			      wr_attrs, wr_data_size,
			      CXL_SET_FEAT_FLAG_DATA_SAVED_ACROSS_RESET);
	if (ret) {
		dev_err(dev, "CXL ECS set feature failed ret=%d\n", ret);
		return ret;
	}

	return 0;
}

#define CXL_ECS_GET_ATTR(attrib)						\
static int cxl_ecs_get_##attrib(struct device *dev, void *drv_data,		\
				int fru_id, u32 *val)				\
{										\
	struct cxl_ecs_context *ctx = drv_data;					\
	struct cxl_ecs_params params;						\
	int ret;								\
										\
	ret = cxl_mem_ecs_get_attrs(dev, ctx, fru_id, &params);			\
	if (ret)								\
		return ret;							\
										\
	*val = params.attrib;							\
										\
	return 0;								\
}

CXL_ECS_GET_ATTR(log_entry_type)
CXL_ECS_GET_ATTR(count_mode)
CXL_ECS_GET_ATTR(threshold)

#define CXL_ECS_SET_ATTR(attrib, param_type)						\
static int cxl_ecs_set_##attrib(struct device *dev, void *drv_data,			\
				int fru_id, u32 val)					\
{											\
	struct cxl_ecs_context *ctx = drv_data;						\
	struct cxl_ecs_params params = {						\
		.attrib = val,								\
	};										\
											\
	return cxl_mem_ecs_set_attrs(dev, ctx, fru_id, &params, (param_type));		\
}
CXL_ECS_SET_ATTR(log_entry_type, CXL_ECS_PARAM_LOG_ENTRY_TYPE)
CXL_ECS_SET_ATTR(count_mode, CXL_ECS_PARAM_MODE)
CXL_ECS_SET_ATTR(reset_counter, CXL_ECS_PARAM_RESET_COUNTER)
CXL_ECS_SET_ATTR(threshold, CXL_ECS_PARAM_THRESHOLD)

static const struct edac_ecs_ops cxl_ecs_ops = {
	.get_log_entry_type = cxl_ecs_get_log_entry_type,
	.set_log_entry_type = cxl_ecs_set_log_entry_type,
	.get_mode = cxl_ecs_get_count_mode,
	.set_mode = cxl_ecs_set_count_mode,
	.reset = cxl_ecs_set_reset_counter,
	.get_threshold = cxl_ecs_get_threshold,
	.set_threshold = cxl_ecs_set_threshold,
};

/* CXL memory soft PPR & hard PPR control definitions */
struct cxl_ppr_context {
	uuid_t repair_uuid;
	u8 instance;
	u16 get_feat_size;
	u16 set_feat_size;
	u8 get_version;
	u8 set_version;
	u16 effects;
	struct cxl_memdev *cxlmd;
	enum edac_mem_repair_function repair_function;
	enum edac_mem_repair_persist_mode persist_mode;
	u64 dpa;
	u32 nibble_mask;
};

/**
 * struct cxl_memdev_ppr_params - CXL memory PPR parameter data structure.
 * @op_class: PPR operation class.
 * @op_subclass: PPR operation subclass.
 * @dpa_support: device physical address for PPR support.
 * @media_accessible: memory media is accessible or not during PPR operation.
 * @data_retained: data is retained or not during PPR operation.
 * @dpa: device physical address.
 */
struct cxl_memdev_ppr_params {
	u8 op_class;
	u8 op_subclass;
	bool dpa_support;
	bool media_accessible;
	bool data_retained;
	u64 dpa;
};

enum cxl_ppr_param {
	CXL_PPR_PARAM_DO_QUERY,
	CXL_PPR_PARAM_DO_PPR,
};

/* See CXL rev 3.1 @8.2.9.7.2.1 Table 8-113 sPPR Feature Readable Attributes */
/* See CXL rev 3.1 @8.2.9.7.2.2 Table 8-116 hPPR Feature Readable Attributes */
#define CXL_MEMDEV_PPR_QUERY_RESOURCE_FLAG	BIT(0)

#define CXL_MEMDEV_PPR_DEVICE_INITIATED_MASK	BIT(0)
#define CXL_MEMDEV_PPR_FLAG_DPA_SUPPORT_MASK	BIT(0)
#define CXL_MEMDEV_PPR_FLAG_NIBBLE_SUPPORT_MASK	BIT(1)
#define CXL_MEMDEV_PPR_FLAG_MEM_SPARING_EV_REC_SUPPORT_MASK	BIT(2)

#define CXL_MEMDEV_PPR_RESTRICTION_FLAG_MEDIA_ACCESSIBLE_MASK	BIT(0)
#define CXL_MEMDEV_PPR_RESTRICTION_FLAG_DATA_RETAINED_MASK	BIT(2)

#define CXL_MEMDEV_PPR_SPARING_EV_REC_EN_MASK	BIT(0)

struct cxl_memdev_repair_rd_attrs_hdr {
	u8 max_op_latency;
	__le16 op_cap;
	__le16 op_mode;
	u8 op_class;
	u8 op_subclass;
	u8 rsvd[9];
}  __packed;

struct cxl_memdev_ppr_rd_attrs {
	struct cxl_memdev_repair_rd_attrs_hdr hdr;
	u8 ppr_flags;
	__le16 restriction_flags;
	u8 ppr_op_mode;
}  __packed;

/* See CXL rev 3.1 @8.2.9.7.1.2 Table 8-103 sPPR Maintenance Input Payload */
/* See CXL rev 3.1 @8.2.9.7.1.3 Table 8-104 hPPR Maintenance Input Payload */
struct cxl_memdev_ppr_maintenance_attrs {
	u8 flags;
	__le64 dpa;
	u8 nibble_mask[3];
}  __packed;

static int cxl_mem_ppr_get_attrs(struct device *dev,
				 struct cxl_ppr_context *cxl_ppr_ctx,
				 struct cxl_memdev_ppr_params *params)
{
	struct cxl_memdev *cxlmd = cxl_ppr_ctx->cxlmd;
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlds);
	size_t rd_data_size = sizeof(struct cxl_memdev_ppr_rd_attrs);
	u16 restriction_flags;
	size_t data_size;

	struct cxl_memdev_ppr_rd_attrs *rd_attrs __free(kfree) =
				kmalloc(rd_data_size, GFP_KERNEL);
	if (!rd_attrs)
		return -ENOMEM;

	data_size = cxl_get_feature(&mds->cxlds, cxl_ppr_ctx->repair_uuid,
				    CXL_GET_FEAT_SEL_CURRENT_VALUE,
				    rd_attrs, rd_data_size);
	if (!data_size)
		return -EIO;

	params->op_class = rd_attrs->hdr.op_class;
	params->op_subclass = rd_attrs->hdr.op_subclass;
	params->dpa_support = FIELD_GET(CXL_MEMDEV_PPR_FLAG_DPA_SUPPORT_MASK,
					rd_attrs->ppr_flags);
	restriction_flags = le16_to_cpu(rd_attrs->restriction_flags);
	params->media_accessible = FIELD_GET(CXL_MEMDEV_PPR_RESTRICTION_FLAG_MEDIA_ACCESSIBLE_MASK,
					     restriction_flags) ^ 1;
	params->data_retained = FIELD_GET(CXL_MEMDEV_PPR_RESTRICTION_FLAG_DATA_RETAINED_MASK,
					  restriction_flags) ^ 1;

	return 0;
}

static int cxl_mem_do_ppr_op(struct device *dev,
			     struct cxl_ppr_context *cxl_ppr_ctx,
			     struct cxl_memdev_ppr_params *rd_params,
			     enum cxl_ppr_param param_type)
{
	struct cxl_memdev_ppr_maintenance_attrs maintenance_attrs;
	struct cxl_memdev *cxlmd = cxl_ppr_ctx->cxlmd;
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlds);
	int ret;

	if (!rd_params->media_accessible || !rd_params->data_retained) {
		/* Check if DPA is mapped */
		if (cxl_dpa_to_region(cxlmd, cxl_ppr_ctx->dpa)) {
			dev_err(dev, "CXL can't do PPR as DPA is mapped\n");
			return -EBUSY;
		}
	}
	memset(&maintenance_attrs, 0, sizeof(maintenance_attrs));
	if (param_type == CXL_PPR_PARAM_DO_QUERY)
		maintenance_attrs.flags = CXL_MEMDEV_PPR_QUERY_RESOURCE_FLAG;
	else
		maintenance_attrs.flags = 0;
	maintenance_attrs.dpa = cpu_to_le64(cxl_ppr_ctx->dpa);
	put_unaligned_le24(cxl_ppr_ctx->nibble_mask, maintenance_attrs.nibble_mask);
	ret = cxl_do_maintenance(mds, rd_params->op_class, rd_params->op_subclass,
				 &maintenance_attrs, sizeof(maintenance_attrs));
	if (ret) {
		dev_err(dev, "CXL do PPR failed ret=%d\n", ret);
		up_read(&cxl_region_rwsem);
		cxl_ppr_ctx->nibble_mask = 0;
		cxl_ppr_ctx->dpa = 0;
		return ret;
	}

	return 0;
}

static int cxl_mem_ppr_set_attrs(struct device *dev,
				 struct cxl_ppr_context *cxl_ppr_ctx,
				 enum cxl_ppr_param param_type)
{
	struct cxl_memdev_ppr_params rd_params;
	int ret;

	ret = cxl_mem_ppr_get_attrs(dev, cxl_ppr_ctx, &rd_params);
	if (ret) {
		dev_err(dev, "Get cxlmemdev PPR params failed ret=%d\n",
			ret);
		return ret;
	}

	switch (param_type) {
	case CXL_PPR_PARAM_DO_QUERY:
	case CXL_PPR_PARAM_DO_PPR:
		ret = down_read_interruptible(&cxl_region_rwsem);
		if (ret)
			return ret;
		ret = down_read_interruptible(&cxl_dpa_rwsem);
		if (ret) {
			up_read(&cxl_region_rwsem);
			return ret;
		}
		ret = cxl_mem_do_ppr_op(dev, cxl_ppr_ctx, &rd_params, param_type);
		up_read(&cxl_dpa_rwsem);
		up_read(&cxl_region_rwsem);
		return ret;
	default:
		return -EINVAL;
	}
}

static int cxl_ppr_get_repair_function(struct device *dev, void *drv_data,
				       u32 *repair_function)
{
	struct cxl_ppr_context *cxl_ppr_ctx = drv_data;

	*repair_function = cxl_ppr_ctx->repair_function;

	return 0;
}

static int cxl_ppr_get_persist_mode(struct device *dev, void *drv_data,
				    u32 *persist_mode)
{
	struct cxl_ppr_context *cxl_ppr_ctx = drv_data;

	*persist_mode = cxl_ppr_ctx->persist_mode;

	return 0;
}

static int cxl_ppr_get_dpa_support(struct device *dev, void *drv_data,
				   u32 *dpa_support)
{
	struct cxl_ppr_context *cxl_ppr_ctx = drv_data;
	struct cxl_memdev_ppr_params params;
	int ret;

	ret = cxl_mem_ppr_get_attrs(dev, cxl_ppr_ctx, &params);
	if (ret)
		return ret;

	*dpa_support = params.dpa_support;

	return 0;
}

static int cxl_get_ppr_safe_when_in_use(struct device *dev, void *drv_data,
					u32 *safe)
{
	struct cxl_ppr_context *cxl_ppr_ctx = drv_data;
	struct cxl_memdev_ppr_params params;
	int ret;

	ret = cxl_mem_ppr_get_attrs(dev, cxl_ppr_ctx, &params);
	if (ret)
		return ret;

	*safe = params.media_accessible & params.data_retained;

	return 0;
}

static int cxl_ppr_get_min_dpa(struct device *dev, void *drv_data,
			       u64 *min_dpa)
{
	struct cxl_ppr_context *cxl_ppr_ctx = drv_data;
	struct cxl_memdev *cxlmd = cxl_ppr_ctx->cxlmd;
	struct cxl_dev_state *cxlds = cxlmd->cxlds;

	*min_dpa = cxlds->dpa_res.start;

	return 0;
}

static int cxl_ppr_get_max_dpa(struct device *dev, void *drv_data,
			       u64 *max_dpa)
{
	struct cxl_ppr_context *cxl_ppr_ctx = drv_data;
	struct cxl_memdev *cxlmd = cxl_ppr_ctx->cxlmd;
	struct cxl_dev_state *cxlds = cxlmd->cxlds;

	*max_dpa = cxlds->dpa_res.end;

	return 0;
}

static int cxl_ppr_get_dpa(struct device *dev, void *drv_data,
			   u64 *dpa)
{
	struct cxl_ppr_context *cxl_ppr_ctx = drv_data;

	*dpa = cxl_ppr_ctx->dpa;

	return 0;
}

static int cxl_ppr_set_dpa(struct device *dev, void *drv_data, u64 dpa)
{
	struct cxl_ppr_context *cxl_ppr_ctx = drv_data;
	struct cxl_memdev *cxlmd = cxl_ppr_ctx->cxlmd;
	struct cxl_dev_state *cxlds = cxlmd->cxlds;

	if (!dpa || dpa < cxlds->dpa_res.start || dpa > cxlds->dpa_res.end)
		return -EINVAL;

	cxl_ppr_ctx->dpa = dpa;

	return 0;
}

static int cxl_ppr_get_nibble_mask(struct device *dev, void *drv_data,
				   u64 *nibble_mask)
{
	struct cxl_ppr_context *cxl_ppr_ctx = drv_data;

	*nibble_mask = cxl_ppr_ctx->nibble_mask;

	return 0;
}

static int cxl_ppr_set_nibble_mask(struct device *dev, void *drv_data, u64 nibble_mask)
{
	struct cxl_ppr_context *cxl_ppr_ctx = drv_data;

	cxl_ppr_ctx->nibble_mask = nibble_mask;

	return 0;
}

static int cxl_do_ppr(struct device *dev, void *drv_data, u32 val)
{
	struct cxl_ppr_context *cxl_ppr_ctx = drv_data;
	int ret;

	if (!cxl_ppr_ctx->dpa || val != EDAC_DO_MEM_REPAIR)
		return -EINVAL;

	ret = cxl_mem_ppr_set_attrs(dev, cxl_ppr_ctx, CXL_PPR_PARAM_DO_PPR);

	return ret;
}

static const struct edac_mem_repair_ops cxl_sppr_ops = {
	.get_repair_function = cxl_ppr_get_repair_function,
	.get_persist_mode = cxl_ppr_get_persist_mode,
	.get_dpa_support = cxl_ppr_get_dpa_support,
	.get_repair_safe_when_in_use = cxl_get_ppr_safe_when_in_use,
	.get_min_dpa = cxl_ppr_get_min_dpa,
	.get_max_dpa = cxl_ppr_get_max_dpa,
	.get_dpa = cxl_ppr_get_dpa,
	.set_dpa = cxl_ppr_set_dpa,
	.get_nibble_mask = cxl_ppr_get_nibble_mask,
	.set_nibble_mask = cxl_ppr_set_nibble_mask,
	.do_repair = cxl_do_ppr,
};

/* CXL memory sparing control definitions */
enum cxl_mem_sparing_granularity {
	CXL_MEM_SPARING_CACHELINE,
	CXL_MEM_SPARING_ROW,
	CXL_MEM_SPARING_BANK,
	CXL_MEM_SPARING_RANK,
	CXL_MEM_SPARING_MAX
};

struct cxl_mem_sparing_context {
	uuid_t repair_uuid;
	u8 instance;
	u16 get_feat_size;
	u16 set_feat_size;
	u8 get_version;
	u8 set_version;
	u16 effects;
	struct cxl_memdev *cxlmd;
	enum edac_mem_repair_function repair_function;
	enum edac_mem_repair_persist_mode persist_mode;
	enum cxl_mem_sparing_granularity granularity;
	bool dpa_support;
	u64 dpa;
	u8 channel;
	u8 rank;
	u32 nibble_mask;
	u8 bank_group;
	u8 bank;
	u32 row;
	u16 column;
	u8 sub_channel;
};

struct cxl_memdev_sparing_params {
	u8 op_class;
	u8 op_subclass;
	bool cap_safe_when_in_use;
	bool cap_hard_sparing;
	bool cap_soft_sparing;
};

enum cxl_mem_sparing_param_type {
	CXL_MEM_SPARING_PARAM_DO_QUERY,
	CXL_MEM_SPARING_PARAM_DO_REPAIR,
};

#define CXL_MEMDEV_SPARING_RD_CAP_SAFE_IN_USE_MASK	BIT(0)
#define CXL_MEMDEV_SPARING_RD_CAP_HARD_SPARING_MASK	BIT(1)
#define CXL_MEMDEV_SPARING_RD_CAP_SOFT_SPARING_MASK	BIT(2)

#define CXL_MEMDEV_SPARING_WR_DEVICE_INITIATED_MASK	BIT(0)

#define CXL_MEMDEV_SPARING_QUERY_RESOURCE_FLAG	BIT(0)
#define CXL_MEMDEV_SET_HARD_SPARING_FLAG	BIT(1)
#define CXL_MEMDEV_SPARING_SUB_CHANNEL_VALID_FLAG	BIT(2)
#define CXL_MEMDEV_SPARING_NIB_MASK_VALID_FLAG	BIT(3)

/* See CXL rev 3.1 @8.2.9.7.2.3 Table 8-119 Memory Sparing Feature Readable Attributes */
struct cxl_memdev_sparing_rd_attrs {
	struct cxl_memdev_repair_rd_attrs_hdr hdr;
	u8 rsvd;
	__le16 restriction_flags;
}  __packed;

/* See CXL rev 3.1 @8.2.9.7.1.4 Table 8-105 Memory Sparing Input Payload */
struct cxl_memdev_sparing_in_payload {
	u8 flags;
	u8 channel;
	u8 rank;
	u8 nibble_mask[3];
	u8 bank_group;
	u8 bank;
	u8 row[3];
	__le16 column;
	u8 sub_channel;
}  __packed;

static int cxl_mem_sparing_get_attrs(struct device *dev,
				     struct cxl_mem_sparing_context *cxl_sparing_ctx,
				     struct cxl_memdev_sparing_params *params)
{
	struct cxl_memdev *cxlmd = cxl_sparing_ctx->cxlmd;
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlds);
	size_t rd_data_size = sizeof(struct cxl_memdev_sparing_rd_attrs);
	u16 restriction_flags;
	size_t data_size;
	struct cxl_memdev_sparing_rd_attrs *rd_attrs __free(kfree) =
				kmalloc(rd_data_size, GFP_KERNEL);
	if (!rd_attrs)
		return -ENOMEM;

	data_size = cxl_get_feature(&mds->cxlds, cxl_sparing_ctx->repair_uuid,
				    CXL_GET_FEAT_SEL_CURRENT_VALUE,
				    rd_attrs, rd_data_size);
	if (!data_size)
		return -EIO;

	params->op_class = rd_attrs->hdr.op_class;
	params->op_subclass = rd_attrs->hdr.op_subclass;
	restriction_flags = le16_to_cpu(rd_attrs->restriction_flags);
	params->cap_safe_when_in_use = FIELD_GET(CXL_MEMDEV_SPARING_RD_CAP_SAFE_IN_USE_MASK,
						 restriction_flags) ^ 1;
	params->cap_hard_sparing = FIELD_GET(CXL_MEMDEV_SPARING_RD_CAP_HARD_SPARING_MASK,
					     restriction_flags);
	params->cap_soft_sparing = FIELD_GET(CXL_MEMDEV_SPARING_RD_CAP_SOFT_SPARING_MASK,
					     restriction_flags);

	return 0;
}

static int cxl_mem_do_sparing_op(struct device *dev,
				 struct cxl_mem_sparing_context *cxl_sparing_ctx,
				 struct cxl_memdev_sparing_params *rd_params,
				 enum cxl_mem_sparing_param_type param_type)
{
	struct cxl_memdev_sparing_in_payload sparing_pi;
	struct cxl_memdev *cxlmd = cxl_sparing_ctx->cxlmd;
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlds);
	int ret;

	if (!rd_params->cap_safe_when_in_use && cxl_sparing_ctx->dpa) {
		/* Check if DPA is mapped */
		if (cxl_dpa_to_region(cxlmd, cxl_sparing_ctx->dpa)) {
			dev_err(dev, "CXL can't do sparing as DPA is mapped\n");
			return -EBUSY;
		}
	}
	memset(&sparing_pi, 0, sizeof(sparing_pi));
	if (param_type == CXL_MEM_SPARING_PARAM_DO_QUERY) {
		sparing_pi.flags = CXL_MEMDEV_SPARING_QUERY_RESOURCE_FLAG;
	} else {
		sparing_pi.flags =
			FIELD_PREP(CXL_MEMDEV_SPARING_QUERY_RESOURCE_FLAG, 0);
		/* Do need set hard sparing, sub-channel & nb mask flags for query? */
		if (cxl_sparing_ctx->persist_mode == EDAC_MEM_REPAIR_HARD)
			sparing_pi.flags |=
				FIELD_PREP(CXL_MEMDEV_SET_HARD_SPARING_FLAG, 1);
		if (cxl_sparing_ctx->sub_channel)
			sparing_pi.flags |=
				FIELD_PREP(CXL_MEMDEV_SPARING_SUB_CHANNEL_VALID_FLAG, 1);
		if (cxl_sparing_ctx->nibble_mask)
			sparing_pi.flags |=
				FIELD_PREP(CXL_MEMDEV_SPARING_NIB_MASK_VALID_FLAG, 1);
	}
	/* Common atts for all memory sparing types */
	sparing_pi.channel = cxl_sparing_ctx->channel;
	sparing_pi.rank = cxl_sparing_ctx->rank;
	put_unaligned_le24(cxl_sparing_ctx->nibble_mask, sparing_pi.nibble_mask);

	if (cxl_sparing_ctx->repair_function == EDAC_CACHELINE_MEM_SPARING ||
	    cxl_sparing_ctx->repair_function == EDAC_ROW_MEM_SPARING ||
	    cxl_sparing_ctx->repair_function == EDAC_BANK_MEM_SPARING) {
		sparing_pi.bank_group = cxl_sparing_ctx->bank_group;
		sparing_pi.bank = cxl_sparing_ctx->bank;
	}
	if (cxl_sparing_ctx->repair_function == EDAC_CACHELINE_MEM_SPARING ||
	    cxl_sparing_ctx->repair_function == EDAC_ROW_MEM_SPARING)
		put_unaligned_le24(cxl_sparing_ctx->row, sparing_pi.row);
	if (cxl_sparing_ctx->repair_function == EDAC_CACHELINE_MEM_SPARING) {
		sparing_pi.column = cpu_to_le16(cxl_sparing_ctx->column);
		sparing_pi.sub_channel = cxl_sparing_ctx->sub_channel;
	}

	ret = cxl_do_maintenance(mds, rd_params->op_class, rd_params->op_subclass,
				 &sparing_pi, sizeof(sparing_pi));
	if (ret) {
		dev_err(dev, "CXL do mem sparing failed ret=%d\n", ret);
		cxl_sparing_ctx->dpa = 0;
		cxl_sparing_ctx->nibble_mask = 0;
		cxl_sparing_ctx->bank_group = 0;
		cxl_sparing_ctx->bank = 0;
		cxl_sparing_ctx->rank = 0;
		cxl_sparing_ctx->row = 0;
		cxl_sparing_ctx->column = 0;
		cxl_sparing_ctx->channel = 0;
		cxl_sparing_ctx->sub_channel = 0;
		return ret;
	}

	return 0;
}

static int cxl_mem_sparing_set_attrs(struct device *dev,
				     struct cxl_mem_sparing_context *ctx,
				     enum cxl_mem_sparing_param_type param_type)
{
	struct cxl_memdev_sparing_params rd_params;
	int ret;

	ret = cxl_mem_sparing_get_attrs(dev, ctx, &rd_params);
	if (ret) {
		dev_err(dev, "Get cxlmemdev sparing params failed ret=%d\n",
			ret);
		return ret;
	}

	switch (param_type) {
	case CXL_MEM_SPARING_PARAM_DO_QUERY:
	case CXL_MEM_SPARING_PARAM_DO_REPAIR:
		ret = down_read_interruptible(&cxl_region_rwsem);
		if (ret)
			return ret;
		ret = down_read_interruptible(&cxl_dpa_rwsem);
		if (ret) {
			up_read(&cxl_region_rwsem);
			return ret;
		}
		ret = cxl_mem_do_sparing_op(dev, ctx, &rd_params, param_type);
		up_read(&cxl_dpa_rwsem);
		up_read(&cxl_region_rwsem);
		return ret;
	default:
		return -EINVAL;
	}
}

#define CXL_SPARING_GET_ATTR(attrib, data_type)					\
static int cxl_mem_sparing_get_##attrib(struct device *dev, void *drv_data,	\
					data_type *val)				\
{										\
	struct cxl_mem_sparing_context *ctx = drv_data;				\
										\
	*val = ctx->attrib;							\
										\
	return 0;								\
}
CXL_SPARING_GET_ATTR(repair_function, u32)
CXL_SPARING_GET_ATTR(persist_mode, u32)
CXL_SPARING_GET_ATTR(dpa_support, u32)
CXL_SPARING_GET_ATTR(dpa, u64)
CXL_SPARING_GET_ATTR(nibble_mask, u64)
CXL_SPARING_GET_ATTR(bank_group, u32)
CXL_SPARING_GET_ATTR(bank, u32)
CXL_SPARING_GET_ATTR(rank, u32)
CXL_SPARING_GET_ATTR(row, u64)
CXL_SPARING_GET_ATTR(column, u32)
CXL_SPARING_GET_ATTR(channel, u32)
CXL_SPARING_GET_ATTR(sub_channel, u32)

#define CXL_SPARING_SET_ATTR(attrib, data_type)					\
static int cxl_mem_sparing_set_##attrib(struct device *dev, void *drv_data,	\
					data_type val)				\
{										\
	struct cxl_mem_sparing_context *ctx = drv_data;				\
										\
	ctx->attrib = val;							\
										\
	return 0;								\
}
CXL_SPARING_SET_ATTR(nibble_mask, u64)
CXL_SPARING_SET_ATTR(bank_group, u32)
CXL_SPARING_SET_ATTR(bank, u32)
CXL_SPARING_SET_ATTR(rank, u32)
CXL_SPARING_SET_ATTR(row, u64)
CXL_SPARING_SET_ATTR(column, u32)
CXL_SPARING_SET_ATTR(channel, u32)
CXL_SPARING_SET_ATTR(sub_channel, u32)

static int cxl_mem_sparing_set_persist_mode(struct device *dev, void *drv_data, u32 persist_mode)
{
	struct cxl_mem_sparing_context *ctx = drv_data;

	switch (persist_mode) {
	case EDAC_MEM_REPAIR_SOFT:
		ctx->persist_mode = EDAC_MEM_REPAIR_SOFT;
		return 0;
	case EDAC_MEM_REPAIR_HARD:
		ctx->persist_mode = EDAC_MEM_REPAIR_HARD;
		return 0;
	default:
		return -EINVAL;
	}
}

static int cxl_get_mem_sparing_safe_when_in_use(struct device *dev, void *drv_data,
						u32 *safe)
{
	struct cxl_mem_sparing_context *ctx = drv_data;
	struct cxl_memdev_sparing_params params;
	int ret;

	ret = cxl_mem_sparing_get_attrs(dev, ctx, &params);
	if (ret)
		return ret;

	*safe = params.cap_safe_when_in_use;

	return 0;
}

static int cxl_mem_sparing_get_min_dpa(struct device *dev, void *drv_data,
				       u64 *min_dpa)
{
	struct cxl_mem_sparing_context *ctx = drv_data;
	struct cxl_memdev *cxlmd = ctx->cxlmd;
	struct cxl_dev_state *cxlds = cxlmd->cxlds;

	*min_dpa = cxlds->dpa_res.start;

	return 0;
}

static int cxl_mem_sparing_get_max_dpa(struct device *dev, void *drv_data,
				       u64 *max_dpa)
{
	struct cxl_mem_sparing_context *ctx = drv_data;
	struct cxl_memdev *cxlmd = ctx->cxlmd;
	struct cxl_dev_state *cxlds = cxlmd->cxlds;

	*max_dpa = cxlds->dpa_res.end;

	return 0;
}

static int cxl_mem_sparing_set_dpa(struct device *dev, void *drv_data, u64 dpa)
{
	struct cxl_mem_sparing_context *ctx = drv_data;
	struct cxl_memdev *cxlmd = ctx->cxlmd;
	struct cxl_dev_state *cxlds = cxlmd->cxlds;

	if (!dpa || dpa < cxlds->dpa_res.start || dpa > cxlds->dpa_res.end)
		return -EINVAL;

	ctx->dpa = dpa;

	return 0;
}

static int cxl_do_mem_sparing(struct device *dev, void *drv_data, u32 val)
{
	struct cxl_mem_sparing_context *ctx = drv_data;

	if (val != EDAC_DO_MEM_REPAIR)
		return -EINVAL;

	return cxl_mem_sparing_set_attrs(dev, ctx, CXL_MEM_SPARING_PARAM_DO_REPAIR);
}

#define RANK_OPS \
	.get_repair_function = cxl_mem_sparing_get_repair_function, \
	.get_persist_mode = cxl_mem_sparing_get_persist_mode, \
	.set_persist_mode = cxl_mem_sparing_set_persist_mode, \
	.get_repair_safe_when_in_use = cxl_get_mem_sparing_safe_when_in_use, \
	.get_dpa_support = cxl_mem_sparing_get_dpa_support, \
	.get_min_dpa = cxl_mem_sparing_get_min_dpa, \
	.get_max_dpa = cxl_mem_sparing_get_max_dpa, \
	.get_dpa = cxl_mem_sparing_get_dpa, \
	.set_dpa = cxl_mem_sparing_set_dpa, \
	.get_nibble_mask = cxl_mem_sparing_get_nibble_mask, \
	.set_nibble_mask = cxl_mem_sparing_set_nibble_mask, \
	.get_rank = cxl_mem_sparing_get_rank, \
	.set_rank = cxl_mem_sparing_set_rank, \
	.get_channel = cxl_mem_sparing_get_channel, \
	.set_channel = cxl_mem_sparing_set_channel, \
	.do_repair = cxl_do_mem_sparing

#define BANK_OPS \
	RANK_OPS, \
	.get_bank_group = cxl_mem_sparing_get_bank_group, \
	.set_bank_group = cxl_mem_sparing_set_bank_group, \
	.get_bank = cxl_mem_sparing_get_bank, \
	.set_bank = cxl_mem_sparing_set_bank

#define ROW_OPS \
	BANK_OPS, \
	.get_row = cxl_mem_sparing_get_row, \
	.set_row = cxl_mem_sparing_set_row

#define CACHELINE_OPS \
	ROW_OPS, \
	.get_column = cxl_mem_sparing_get_column, \
	.set_column = cxl_mem_sparing_set_column, \
	.get_sub_channel = cxl_mem_sparing_get_sub_channel, \
	.set_sub_channel = cxl_mem_sparing_set_sub_channel

static const struct edac_mem_repair_ops cxl_rank_sparing_ops = {
	RANK_OPS,
};

static const struct edac_mem_repair_ops cxl_bank_sparing_ops = {
	BANK_OPS,
};

static const struct edac_mem_repair_ops cxl_row_sparing_ops = {
	ROW_OPS,
};

static const struct edac_mem_repair_ops cxl_cacheline_sparing_ops = {
	CACHELINE_OPS,
};

struct cxl_mem_sparing_desc {
	const uuid_t repair_uuid;
	enum edac_mem_repair_function repair_function;
	enum edac_mem_repair_persist_mode persist_mode;
	enum cxl_mem_sparing_granularity granularity;
	const struct edac_mem_repair_ops *repair_ops;
};

static const struct cxl_mem_sparing_desc mem_sparing_desc[] = {
	{
		.repair_uuid = CXL_FEAT_CACHELINE_SPARING_UUID,
		.repair_function = EDAC_CACHELINE_MEM_SPARING,
		.persist_mode = EDAC_MEM_REPAIR_SOFT,
		.granularity = CXL_MEM_SPARING_CACHELINE,
		.repair_ops = &cxl_cacheline_sparing_ops,
	},
	{
		.repair_uuid = CXL_FEAT_ROW_SPARING_UUID,
		.repair_function = EDAC_ROW_MEM_SPARING,
		.persist_mode = EDAC_MEM_REPAIR_SOFT,
		.granularity = CXL_MEM_SPARING_ROW,
		.repair_ops = &cxl_row_sparing_ops,
	},
	{
		.repair_uuid = CXL_FEAT_BANK_SPARING_UUID,
		.repair_function = EDAC_BANK_MEM_SPARING,
		.persist_mode = EDAC_MEM_REPAIR_SOFT,
		.granularity = CXL_MEM_SPARING_BANK,
		.repair_ops = &cxl_bank_sparing_ops,
	},
	{
		.repair_uuid = CXL_FEAT_RANK_SPARING_UUID,
		.repair_function = EDAC_RANK_MEM_SPARING,
		.persist_mode = EDAC_MEM_REPAIR_SOFT,
		.granularity = CXL_MEM_SPARING_RANK,
		.repair_ops = &cxl_rank_sparing_ops,
	},
};

static int cxl_memdev_scrub_init(struct cxl_memdev *cxlmd, struct cxl_region *cxlr,
				 struct edac_dev_feature *ras_feature, u8 scrub_inst)
{
	struct cxl_patrol_scrub_context *cxl_ps_ctx;
	struct cxl_feat_entry feat_entry;
	struct cxl_memdev_state *mds;
	struct cxl_dev_state *cxlds;
	int rc, i;

	if (cxlr) {
		struct cxl_region_params *p = &cxlr->params;

		for (i = p->interleave_ways - 1; i >= 0; i--) {
			struct cxl_endpoint_decoder *cxled = p->targets[i];

			cxlmd = cxled_to_memdev(cxled);
			cxlds = cxlmd->cxlds;
			mds = to_cxl_memdev_state(cxlds);
			memset(&feat_entry, 0, sizeof(feat_entry));
			rc = cxl_get_supported_feature_entry(&mds->cxlds,
							     &CXL_FEAT_PATROL_SCRUB_UUID,
							     &feat_entry);
			if (rc < 0)
				goto feat_unsupported;
			if (!(le32_to_cpu(feat_entry.flags) & CXL_FEAT_ENTRY_FLAG_CHANGABLE))
				goto feat_unsupported;
		}
	} else {
		cxlds = cxlmd->cxlds;
		mds = to_cxl_memdev_state(cxlds);
		rc = cxl_get_supported_feature_entry(&mds->cxlds, &CXL_FEAT_PATROL_SCRUB_UUID,
						     &feat_entry);
		if (rc < 0)
			goto feat_unsupported;

		if (!(le32_to_cpu(feat_entry.flags) & CXL_FEAT_ENTRY_FLAG_CHANGABLE))
			goto feat_unsupported;
	}

	cxl_ps_ctx = devm_kzalloc(&cxlmd->dev, sizeof(*cxl_ps_ctx), GFP_KERNEL);
	if (!cxl_ps_ctx)
		return -ENOMEM;

	*cxl_ps_ctx = (struct cxl_patrol_scrub_context) {
		.get_feat_size = le16_to_cpu(feat_entry.get_feat_size),
		.set_feat_size = le16_to_cpu(feat_entry.set_feat_size),
		.get_version = feat_entry.get_feat_ver,
		.set_version = feat_entry.set_feat_ver,
		.effects = le16_to_cpu(feat_entry.effects),
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

feat_unsupported:
	return -EOPNOTSUPP;
}

static int cxl_memdev_ecs_init(struct cxl_memdev *cxlmd,
			       struct edac_dev_feature *ras_feature)
{
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlds);
	struct cxl_ecs_context *cxl_ecs_ctx;
	struct cxl_feat_entry feat_entry;
	int num_media_frus;
	int rc;

	rc = cxl_get_supported_feature_entry(&mds->cxlds, &CXL_FEAT_ECS_UUID, &feat_entry);
	if (rc < 0)
		goto feat_unsupported;

	if (!(le32_to_cpu(feat_entry.flags) & CXL_FEAT_ENTRY_FLAG_CHANGABLE))
		goto feat_unsupported;
	num_media_frus = (le16_to_cpu(feat_entry.get_feat_size) -
				sizeof(struct cxl_ecs_rd_attrs)) /
				sizeof(struct cxl_ecs_fru_rd_attrs);
	if (!num_media_frus)
		goto feat_unsupported;

	cxl_ecs_ctx = devm_kzalloc(&cxlmd->dev, sizeof(*cxl_ecs_ctx),
				   GFP_KERNEL);
	if (!cxl_ecs_ctx)
		return -ENOMEM;

	*cxl_ecs_ctx = (struct cxl_ecs_context) {
		.get_feat_size = le16_to_cpu(feat_entry.get_feat_size),
		.set_feat_size = le16_to_cpu(feat_entry.set_feat_size),
		.get_version = feat_entry.get_feat_ver,
		.set_version = feat_entry.set_feat_ver,
		.effects = le16_to_cpu(feat_entry.effects),
		.num_media_frus = num_media_frus,
		.cxlmd = cxlmd,
	};

	ras_feature->ft_type = RAS_FEAT_ECS;
	ras_feature->ecs_ops = &cxl_ecs_ops;
	ras_feature->ctx = cxl_ecs_ctx;
	ras_feature->ecs_info.num_media_frus = num_media_frus;

	return 0;

feat_unsupported:
	return -EOPNOTSUPP;
}

static int cxl_memdev_soft_ppr_init(struct cxl_memdev *cxlmd,
				    struct edac_dev_feature *ras_feature,
				    u8 repair_inst)
{
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlds);
	struct cxl_ppr_context *cxl_sppr_ctx;
	struct cxl_feat_entry feat_entry;
	int rc;

	rc = cxl_get_supported_feature_entry(&mds->cxlds, &CXL_FEAT_SPPR_UUID,
					     &feat_entry);
	if (rc < 0)
		goto feat_unsupported;

	if (!(le32_to_cpu(feat_entry.flags) & CXL_FEAT_ENTRY_FLAG_CHANGABLE))
		goto feat_unsupported;

	cxl_sppr_ctx = devm_kzalloc(&cxlmd->dev, sizeof(*cxl_sppr_ctx),
				    GFP_KERNEL);
	if (!cxl_sppr_ctx)
		return -ENOMEM;

	*cxl_sppr_ctx = (struct cxl_ppr_context) {
		.repair_uuid = CXL_FEAT_SPPR_UUID,
		.get_feat_size = le16_to_cpu(feat_entry.get_feat_size),
		.set_feat_size = le16_to_cpu(feat_entry.set_feat_size),
		.get_version = feat_entry.get_feat_ver,
		.set_version = feat_entry.set_feat_ver,
		.effects = le16_to_cpu(feat_entry.effects),
		.cxlmd = cxlmd,
		.repair_function = EDAC_SOFT_PPR,
		.persist_mode = EDAC_MEM_REPAIR_SOFT,
		.instance = repair_inst,
	};

	ras_feature->ft_type = RAS_FEAT_MEM_REPAIR;
	ras_feature->instance = cxl_sppr_ctx->instance;
	ras_feature->mem_repair_ops = &cxl_sppr_ops;
	ras_feature->ctx = cxl_sppr_ctx;

	return 0;

feat_unsupported:
	return -EOPNOTSUPP;
}

static int cxl_memdev_sparing_init(struct cxl_memdev *cxlmd,
				   struct edac_dev_feature *ras_feature,
				   const struct cxl_mem_sparing_desc *desc,
				   u8 repair_inst)
{
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlds);
	struct cxl_mem_sparing_context *cxl_sparing_ctx;
	struct cxl_feat_entry feat_entry;
	int rc;

	rc = cxl_get_supported_feature_entry(&mds->cxlds, &desc->repair_uuid,
					     &feat_entry);
	if (rc < 0)
		goto feat_unsupported;

	if (!(le32_to_cpu(feat_entry.flags) & CXL_FEAT_ENTRY_FLAG_CHANGABLE))
		goto feat_unsupported;

	cxl_sparing_ctx = devm_kzalloc(&cxlmd->dev, sizeof(*cxl_sparing_ctx),
				       GFP_KERNEL);
	if (!cxl_sparing_ctx)
		return -ENOMEM;

	*cxl_sparing_ctx = (struct cxl_mem_sparing_context) {
		.repair_uuid = desc->repair_uuid,
		.get_feat_size = le16_to_cpu(feat_entry.get_feat_size),
		.set_feat_size = le16_to_cpu(feat_entry.set_feat_size),
		.get_version = feat_entry.get_feat_ver,
		.set_version = feat_entry.set_feat_ver,
		.effects = le16_to_cpu(feat_entry.effects),
		.cxlmd = cxlmd,
		.repair_function = desc->repair_function,
		.persist_mode = desc->persist_mode,
		.granularity = desc->granularity,
		.dpa_support = true,
		.instance = repair_inst++,
	};
	ras_feature->ft_type = RAS_FEAT_MEM_REPAIR;
	ras_feature->instance = cxl_sparing_ctx->instance;
	ras_feature->mem_repair_ops = desc->repair_ops;
	ras_feature->ctx = cxl_sparing_ctx;

	return 0;

feat_unsupported:
	return -EOPNOTSUPP;
}

int cxl_mem_ras_features_init(struct cxl_memdev *cxlmd, struct cxl_region *cxlr)
{
	struct edac_dev_feature ras_features[CXL_DEV_NUM_RAS_FEATURES];
	char cxl_dev_name[CXL_DEV_NAME_LEN];
	int num_ras_features = 0;
	u8 repair_inst = 0;
	u8 scrub_inst = 0;
	int rc, i;

	rc = cxl_memdev_scrub_init(cxlmd, cxlr, &ras_features[num_ras_features],
				   scrub_inst);
	if (rc == -EOPNOTSUPP)
		goto feat_scrub_done;
	if (rc < 0)
		return rc;

	scrub_inst++;
	num_ras_features++;

feat_scrub_done:
	if (cxlr) {
		snprintf(cxl_dev_name, sizeof(cxl_dev_name),
			 "cxl_region%d", cxlr->id);
		goto feat_register;
	} else {
		snprintf(cxl_dev_name, sizeof(cxl_dev_name),
			 "%s_%s", "cxl", dev_name(&cxlmd->dev));
	}

	rc = cxl_memdev_ecs_init(cxlmd, &ras_features[num_ras_features]);
	if (rc == -EOPNOTSUPP)
		goto feat_ecs_done;
	if (rc < 0)
		return rc;

	num_ras_features++;

feat_ecs_done:
	rc = cxl_memdev_soft_ppr_init(cxlmd, &ras_features[num_ras_features],
				      repair_inst);
	if (rc == -EOPNOTSUPP)
		goto feat_soft_ppr_done;
	if (rc < 0)
		return rc;

	repair_inst++;
	num_ras_features++;

feat_soft_ppr_done:
	for (i = 0; i < CXL_MEM_SPARING_MAX; i++) {
		rc = cxl_memdev_sparing_init(cxlmd, &ras_features[num_ras_features],
					     &mem_sparing_desc[i], repair_inst);
		if (rc == -EOPNOTSUPP)
			continue;
		if (rc < 0)
			return rc;

		repair_inst++;
		num_ras_features++;
	}

feat_register:
	return edac_dev_register(&cxlmd->dev, cxl_dev_name, NULL,
				 num_ras_features, ras_features);
}
EXPORT_SYMBOL_NS_GPL(cxl_mem_ras_features_init, CXL);
