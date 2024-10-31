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
#include <cxl.h>
#include <cxlmem.h>
#include "core.h"

#define CXL_DEV_NUM_RAS_FEATURES	3
#define CXL_DEV_HOUR_IN_SECS	3600

#define CXL_SCRUB_NAME_LEN	128

/* CXL memory patrol scrub control definitions */
static const uuid_t cxl_patrol_scrub_uuid =
	UUID_INIT(0x96dad7d6, 0xfde8, 0x482b, 0xa7, 0x33, 0x75, 0x77, 0x4e, 0x06, 0xdb, 0x8a);

/* CXL memory patrol scrub control functions */
struct cxl_patrol_scrub_context {
	u8 instance;
	u16 get_feat_size;
	u16 set_feat_size;
	u8 get_version;
	u8 set_version;
	u16 set_effects;
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
	u16 scrub_cycle_hrs;
	u16 min_scrub_cycle_hrs;
};

enum cxl_scrub_param {
	CXL_PS_PARAM_ENABLE,
	CXL_PS_PARAM_SCRUB_CYCLE,
};

#define	CXL_MEMDEV_PS_SCRUB_CYCLE_CHANGE_CAP_MASK	BIT(0)
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
	size_t data_size;
	struct cxl_memdev_ps_rd_attrs *rd_attrs __free(kfree) =
						kmalloc(rd_data_size, GFP_KERNEL);
	if (!rd_attrs)
		return -ENOMEM;

	data_size = cxl_get_feature(mds, cxl_patrol_scrub_uuid,
				    CXL_GET_FEAT_SEL_CURRENT_VALUE,
				    rd_attrs, rd_data_size);
	if (!data_size)
		return -EIO;

	params->scrub_cycle_changeable = FIELD_GET(CXL_MEMDEV_PS_SCRUB_CYCLE_CHANGE_CAP_MASK,
						   rd_attrs->scrub_cycle_cap);
	params->enable = FIELD_GET(CXL_MEMDEV_PS_FLAG_ENABLED_MASK,
				   rd_attrs->scrub_flags);
	params->scrub_cycle_hrs = FIELD_GET(CXL_MEMDEV_PS_CUR_SCRUB_CYCLE_MASK,
					    rd_attrs->scrub_cycle_hrs);
	params->min_scrub_cycle_hrs = FIELD_GET(CXL_MEMDEV_PS_MIN_SCRUB_CYCLE_MASK,
						rd_attrs->scrub_cycle_hrs);

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

	ret = cxl_set_feature(mds, cxl_patrol_scrub_uuid,
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
static const uuid_t cxl_ecs_uuid =
	UUID_INIT(0xe5b13f22, 0x2328, 0x4a14, 0xb8, 0xba, 0xb9, 0x69, 0x1e, 0x89, 0x33, 0x86);

struct cxl_ecs_context {
	u16 num_media_frus;
	u16 get_feat_size;
	u16 set_feat_size;
	u8 get_version;
	u8 set_version;
	u16 set_effects;
	struct cxl_memdev *cxlmd;
};

enum {
	CXL_ECS_PARAM_LOG_ENTRY_TYPE,
	CXL_ECS_PARAM_THRESHOLD,
	CXL_ECS_PARAM_MODE,
	CXL_ECS_PARAM_RESET_COUNTER,
};

#define	CXL_ECS_LOG_ENTRY_TYPE_MASK	GENMASK(1, 0)
#define	CXL_ECS_REALTIME_REPORT_CAP_MASK	BIT(0)
#define	CXL_ECS_THRESHOLD_COUNT_MASK	GENMASK(2, 0)
#define	CXL_ECS_COUNT_MODE_MASK	BIT(3)
#define	CXL_ECS_RESET_COUNTER_MASK	BIT(4)

enum {
	ECS_THRESHOLD_256 = 3,
	ECS_THRESHOLD_1024 = 4,
	ECS_THRESHOLD_4096 = 5,
};

static const u16 ecs_supp_threshold[] = {
	[ECS_THRESHOLD_256] = 256,
	[ECS_THRESHOLD_1024] = 1024,
	[ECS_THRESHOLD_4096] = 4096,
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

	rd_data_size = cxl_ecs_ctx->get_feat_size;

	struct cxl_ecs_rd_attrs *rd_attrs __free(kfree) =
					kmalloc(rd_data_size, GFP_KERNEL);
	if (!rd_attrs)
		return -ENOMEM;

	params->log_entry_type = 0;
	params->threshold = 0;
	params->count_mode = 0;
	data_size = cxl_get_feature(mds, cxl_ecs_uuid,
				    CXL_GET_FEAT_SEL_CURRENT_VALUE,
				    rd_attrs, rd_data_size);
	if (!data_size)
		return -EIO;

	fru_rd_attrs = rd_attrs->fru_attrs;
	params->log_entry_type = FIELD_GET(CXL_ECS_LOG_ENTRY_TYPE_MASK,
					   rd_attrs->ecs_log_cap);
	threshold_index = FIELD_GET(CXL_ECS_THRESHOLD_COUNT_MASK,
				    fru_rd_attrs[fru_id].ecs_config);
	params->threshold = ecs_supp_threshold[threshold_index];
	params->count_mode = FIELD_GET(CXL_ECS_COUNT_MODE_MASK,
				       fru_rd_attrs[fru_id].ecs_config);
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
	int ret;

	num_media_frus = cxl_ecs_ctx->num_media_frus;
	rd_data_size = cxl_ecs_ctx->get_feat_size;
	wr_data_size = cxl_ecs_ctx->set_feat_size;
	struct cxl_ecs_rd_attrs *rd_attrs __free(kfree) =
				kmalloc(rd_data_size, GFP_KERNEL);
	if (!rd_attrs)
		return -ENOMEM;

	data_size = cxl_get_feature(mds, cxl_ecs_uuid,
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
		fru_wr_attrs[fru_id].ecs_config &= ~CXL_ECS_THRESHOLD_COUNT_MASK;
		switch (params->threshold) {
		case 256:
			fru_wr_attrs[fru_id].ecs_config |= FIELD_PREP(CXL_ECS_THRESHOLD_COUNT_MASK,
								  ECS_THRESHOLD_256);
			break;
		case 1024:
			fru_wr_attrs[fru_id].ecs_config |= FIELD_PREP(CXL_ECS_THRESHOLD_COUNT_MASK,
								  ECS_THRESHOLD_1024);
			break;
		case 4096:
			fru_wr_attrs[fru_id].ecs_config |= FIELD_PREP(CXL_ECS_THRESHOLD_COUNT_MASK,
								  ECS_THRESHOLD_4096);
			break;
		default:
			dev_err(dev,
				"Invalid CXL ECS scrub threshold count(%d) to set\n",
				params->threshold);
			dev_err(dev,
				"Supported scrub threshold counts: %u, %u, %u\n",
				ecs_supp_threshold[ECS_THRESHOLD_256],
				ecs_supp_threshold[ECS_THRESHOLD_1024],
				ecs_supp_threshold[ECS_THRESHOLD_4096]);
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
		fru_wr_attrs[fru_id].ecs_config &= ~CXL_ECS_COUNT_MODE_MASK;
		fru_wr_attrs[fru_id].ecs_config |= FIELD_PREP(CXL_ECS_COUNT_MODE_MASK,
							      params->count_mode);
		break;
	case CXL_ECS_PARAM_RESET_COUNTER:
		if (params->reset_counter != 1)
			return -EINVAL;

		fru_wr_attrs[fru_id].ecs_config &= ~CXL_ECS_RESET_COUNTER_MASK;
		fru_wr_attrs[fru_id].ecs_config |= FIELD_PREP(CXL_ECS_RESET_COUNTER_MASK,
							      params->reset_counter);
		break;
	default:
		dev_err(dev, "Invalid CXL ECS parameter to set\n");
		return -EINVAL;
	}

	ret = cxl_set_feature(mds, cxl_ecs_uuid, cxl_ecs_ctx->set_version,
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
/* See CXL rev 3.1 @8.2.9.7.2 Table 8-110 Maintenance Operation */
static const uuid_t cxl_sppr_uuid =
	UUID_INIT(0x892ba475, 0xfad8, 0x474e, 0x9d, 0x3e, 0x69, 0x2c, 0x91, 0x75, 0x68, 0xbb);

static const uuid_t cxl_hppr_uuid =
	UUID_INIT(0x80ea4521, 0x786f, 0x4127, 0xaf, 0xb1, 0xec, 0x74, 0x59, 0xfb, 0x0e, 0x24);

struct cxl_ppr_context {
	uuid_t repair_uuid;
	u8 instance;
	u16 get_feat_size;
	u16 set_feat_size;
	u8 get_version;
	u8 set_version;
	u16 set_effects;
	struct cxl_memdev *cxlmd;
	enum edac_mem_repair_type repair_type;
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
#define	CXL_MEMDEV_PPR_QUERY_RESOURCE_FLAG BIT(0)

#define CXL_MEMDEV_PPR_DEVICE_INITIATED_MASK BIT(0)
#define CXL_MEMDEV_PPR_FLAG_DPA_SUPPORT_MASK BIT(0)
#define CXL_MEMDEV_PPR_FLAG_NIBBLE_SUPPORT_MASK BIT(1)
#define CXL_MEMDEV_PPR_FLAG_MEM_SPARING_EV_REC_SUPPORT_MASK BIT(2)

#define CXL_MEMDEV_PPR_RESTRICTION_FLAG_MEDIA_ACCESSIBLE_MASK BIT(0)
#define CXL_MEMDEV_PPR_RESTRICTION_FLAG_DATA_RETAINED_MASK BIT(2)

#define CXL_MEMDEV_PPR_SPARING_EV_REC_EN_MASK BIT(0)

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

/* See CXL rev 3.1 @8.2.9.7.2.1 Table 8-114 sPPR Feature Writable Attributes */
/* See CXL rev 3.1 @8.2.9.7.2.2 Table 8-117 hPPR Feature Writable Attributes */
struct cxl_memdev_ppr_wr_attrs {
	__le16 op_mode;
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
	size_t data_size;
	struct cxl_memdev_ppr_rd_attrs *rd_attrs __free(kfree) =
				kmalloc(rd_data_size, GFP_KERNEL);
	if (!rd_attrs)
		return -ENOMEM;

	data_size = cxl_get_feature(mds, cxl_ppr_ctx->repair_uuid,
				    CXL_GET_FEAT_SEL_CURRENT_VALUE,
				    rd_attrs, rd_data_size);
	if (!data_size)
		return -EIO;

	params->op_class = rd_attrs->hdr.op_class;
	params->op_subclass = rd_attrs->hdr.op_subclass;
	params->dpa_support = FIELD_GET(CXL_MEMDEV_PPR_FLAG_DPA_SUPPORT_MASK,
					rd_attrs->ppr_flags);
	params->media_accessible = FIELD_GET(CXL_MEMDEV_PPR_RESTRICTION_FLAG_MEDIA_ACCESSIBLE_MASK,
					     rd_attrs->restriction_flags) ^ 1;
	params->data_retained = FIELD_GET(CXL_MEMDEV_PPR_RESTRICTION_FLAG_DATA_RETAINED_MASK,
					  rd_attrs->restriction_flags) ^ 1;

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
	maintenance_attrs.dpa = cxl_ppr_ctx->dpa;
	*((u32 *)&maintenance_attrs.nibble_mask[0]) = cxl_ppr_ctx->nibble_mask;
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

static int cxl_ppr_get_repair_type(struct device *dev, void *drv_data,
				   u32 *repair_type)
{
	struct cxl_ppr_context *cxl_ppr_ctx = drv_data;

	*repair_type = cxl_ppr_ctx->repair_type;

	return 0;
}

static int cxl_ppr_get_persist_mode_avail(struct device *dev, void *drv_data,
					  char *buf)
{
	return sysfs_emit(buf, "%u\n", EDAC_MEM_REPAIR_SOFT);
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

static int cxl_get_ppr_dpa(struct device *dev, void *drv_data,
			   u64 *dpa)
{
	struct cxl_ppr_context *cxl_ppr_ctx = drv_data;

	*dpa = cxl_ppr_ctx->dpa;

	return 0;
}

static int cxl_set_ppr_dpa(struct device *dev, void *drv_data, u64 dpa)
{
	struct cxl_ppr_context *cxl_ppr_ctx = drv_data;

	if (!dpa)
		return -EINVAL;

	cxl_ppr_ctx->dpa = dpa;

	return 0;
}

static int cxl_get_ppr_nibble_mask(struct device *dev, void *drv_data,
				   u64 *nibble_mask)
{
	struct cxl_ppr_context *cxl_ppr_ctx = drv_data;

	*nibble_mask = cxl_ppr_ctx->nibble_mask;

	return 0;
}

static int cxl_set_ppr_nibble_mask(struct device *dev, void *drv_data, u64 nibble_mask)
{
	struct cxl_ppr_context *cxl_ppr_ctx = drv_data;

	cxl_ppr_ctx->nibble_mask = nibble_mask;

	return 0;
}

static int cxl_do_query_ppr(struct device *dev, void *drv_data)
{
	struct cxl_ppr_context *cxl_ppr_ctx = drv_data;

	if (!cxl_ppr_ctx->dpa)
		return -EINVAL;

	return cxl_mem_ppr_set_attrs(dev, cxl_ppr_ctx, CXL_PPR_PARAM_DO_QUERY);
}

static int cxl_do_ppr(struct device *dev, void *drv_data)
{
	struct cxl_ppr_context *cxl_ppr_ctx = drv_data;
	int ret;

	if (!cxl_ppr_ctx->dpa)
		return -EINVAL;

	ret = cxl_mem_ppr_set_attrs(dev, cxl_ppr_ctx, CXL_PPR_PARAM_DO_PPR);

	return ret;
}

static const struct edac_mem_repair_ops cxl_sppr_ops = {
	.get_repair_type = cxl_ppr_get_repair_type,
	.get_persist_mode_avail = cxl_ppr_get_persist_mode_avail,
	.get_persist_mode = cxl_ppr_get_persist_mode,
	.get_dpa_support = cxl_ppr_get_dpa_support,
	.get_repair_safe_when_in_use = cxl_get_ppr_safe_when_in_use,
	.get_dpa = cxl_get_ppr_dpa,
	.set_dpa = cxl_set_ppr_dpa,
	.get_nibble_mask = cxl_get_ppr_nibble_mask,
	.set_nibble_mask = cxl_set_ppr_nibble_mask,
	.do_query = cxl_do_query_ppr,
	.do_repair = cxl_do_ppr,
};

int cxl_mem_ras_features_init(struct cxl_memdev *cxlmd, struct cxl_region *cxlr)
{
	struct edac_dev_feature ras_features[CXL_DEV_NUM_RAS_FEATURES];
	struct cxl_patrol_scrub_context *cxl_ps_ctx;
	char cxl_dev_name[CXL_SCRUB_NAME_LEN];
	struct cxl_ppr_context *cxl_sppr_ctx;
	struct cxl_ecs_context *cxl_ecs_ctx;
	struct cxl_feat_entry feat_entry;
	struct cxl_memdev_state *mds;
	struct cxl_dev_state *cxlds;
	int num_ras_features = 0;
	int num_media_frus;
	u8 repair_inst = 0;
	u8 scrub_inst = 0;
	int rc, i;

	if (cxlr) {
		struct cxl_region_params *p = &cxlr->params;

		for (i = p->interleave_ways - 1; i >= 0; i--) {
			struct cxl_endpoint_decoder *cxled = p->targets[i];

			cxlmd = cxled_to_memdev(cxled);
			cxlds = cxlmd->cxlds;
			mds = to_cxl_memdev_state(cxlds);
			memset(&feat_entry, 0, sizeof(feat_entry));
			rc = cxl_get_supported_feature_entry(mds, &cxl_patrol_scrub_uuid,
							     &feat_entry);
			if (rc < 0)
				return rc;
			if (!(feat_entry.attr_flags & CXL_FEAT_ENTRY_FLAG_CHANGABLE))
				return -EOPNOTSUPP;
		}
	} else {
		cxlds = cxlmd->cxlds;
		mds = to_cxl_memdev_state(cxlds);
		rc = cxl_get_supported_feature_entry(mds, &cxl_patrol_scrub_uuid,
						     &feat_entry);
		if (rc < 0)
			goto feat_scrub_done;

		if (!(feat_entry.attr_flags & CXL_FEAT_ENTRY_FLAG_CHANGABLE))
			goto feat_scrub_done;
	}

	cxl_ps_ctx = devm_kzalloc(&cxlmd->dev, sizeof(*cxl_ps_ctx), GFP_KERNEL);
	if (!cxl_ps_ctx)
		return -ENOMEM;

	*cxl_ps_ctx = (struct cxl_patrol_scrub_context) {
		.get_feat_size = feat_entry.get_feat_size,
		.set_feat_size = feat_entry.set_feat_size,
		.get_version = feat_entry.get_feat_ver,
		.set_version = feat_entry.set_feat_ver,
		.set_effects = feat_entry.set_effects,
		.instance = scrub_inst++,
	};
	if (cxlr) {
		snprintf(cxl_dev_name, sizeof(cxl_dev_name),
			 "cxl_region%d", cxlr->id);
		cxl_ps_ctx->cxlr = cxlr;
	} else {
		snprintf(cxl_dev_name, sizeof(cxl_dev_name),
			 "%s_%s", "cxl", dev_name(&cxlmd->dev));
		cxl_ps_ctx->cxlmd = cxlmd;
	}

	ras_features[num_ras_features].ft_type = RAS_FEAT_SCRUB;
	ras_features[num_ras_features].instance = cxl_ps_ctx->instance;
	ras_features[num_ras_features].scrub_ops = &cxl_ps_scrub_ops;
	ras_features[num_ras_features].ctx = cxl_ps_ctx;
	num_ras_features++;

feat_scrub_done:
	if (!cxlr) {
		rc = cxl_get_supported_feature_entry(mds, &cxl_ecs_uuid,
						     &feat_entry);
		if (rc < 0)
			goto feat_ecs_done;

		if (!(feat_entry.attr_flags & CXL_FEAT_ENTRY_FLAG_CHANGABLE))
			goto feat_ecs_done;
		num_media_frus = (feat_entry.get_feat_size - sizeof(struct cxl_ecs_rd_attrs)) /
					sizeof(struct cxl_ecs_fru_rd_attrs);
		if (!num_media_frus)
			goto feat_ecs_done;

		cxl_ecs_ctx = devm_kzalloc(&cxlmd->dev, sizeof(*cxl_ecs_ctx),
					   GFP_KERNEL);
		if (!cxl_ecs_ctx)
			goto feat_ecs_done;
		*cxl_ecs_ctx = (struct cxl_ecs_context) {
			.get_feat_size = feat_entry.get_feat_size,
			.set_feat_size = feat_entry.set_feat_size,
			.get_version = feat_entry.get_feat_ver,
			.set_version = feat_entry.set_feat_ver,
			.set_effects = feat_entry.set_effects,
			.num_media_frus = num_media_frus,
			.cxlmd = cxlmd,
		};

		ras_features[num_ras_features].ft_type = RAS_FEAT_ECS;
		ras_features[num_ras_features].ecs_ops = &cxl_ecs_ops;
		ras_features[num_ras_features].ctx = cxl_ecs_ctx;
		ras_features[num_ras_features].ecs_info.num_media_frus =
								num_media_frus;
		num_ras_features++;
	}

feat_ecs_done:
	/* CXL sPPR */
	rc = cxl_get_supported_feature_entry(mds, &cxl_sppr_uuid,
					     &feat_entry);
	if (rc < 0)
		goto feat_sppr_done;

	if (!(feat_entry.attr_flags & CXL_FEAT_ENTRY_FLAG_CHANGABLE))
		goto feat_sppr_done;

	cxl_sppr_ctx = devm_kzalloc(&cxlmd->dev, sizeof(*cxl_sppr_ctx),
				    GFP_KERNEL);
	if (!cxl_sppr_ctx)
		goto feat_sppr_done;
	*cxl_sppr_ctx = (struct cxl_ppr_context) {
		.repair_uuid = cxl_sppr_uuid,
		.get_feat_size = feat_entry.get_feat_size,
		.set_feat_size = feat_entry.set_feat_size,
		.get_version = feat_entry.get_feat_ver,
		.set_version = feat_entry.set_feat_ver,
		.set_effects = feat_entry.set_effects,
		.cxlmd = cxlmd,
		.repair_type = EDAC_TYPE_SPPR,
		.persist_mode = EDAC_MEM_REPAIR_SOFT,
		.instance = repair_inst++,
	};

	ras_features[num_ras_features].ft_type = RAS_FEAT_MEM_REPAIR;
	ras_features[num_ras_features].instance = cxl_sppr_ctx->instance;
	ras_features[num_ras_features].mem_repair_ops = &cxl_sppr_ops;
	ras_features[num_ras_features].ctx = cxl_sppr_ctx;
	num_ras_features++;

feat_sppr_done:
	return edac_dev_register(&cxlmd->dev, cxl_dev_name, NULL,
				 num_ras_features, ras_features);
}
EXPORT_SYMBOL_NS_GPL(cxl_mem_ras_features_init, CXL);
