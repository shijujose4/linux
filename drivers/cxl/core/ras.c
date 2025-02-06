// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CXL RAS driver.
 *
 * Copyright (c) 2025 HiSilicon Limited.
 *
 */

#include <linux/unaligned.h>
#include <cxlmem.h>

#include "trace.h"

struct cxl_event_gen_media *cxl_find_rec_gen_media(struct cxl_memdev *cxlmd,
						   struct cxl_mem_repair_attrbs *attrbs)
{
	struct cxl_event_gen_media *rec;

	rec = xa_load(&cxlmd->rec_gen_media, attrbs->dpa);
	if (!rec)
		return NULL;

	if (attrbs->repair_type == CXL_PPR)
		return rec;

	return NULL;
}
EXPORT_SYMBOL_NS_GPL(cxl_find_rec_gen_media, "CXL");

struct cxl_event_dram *cxl_find_rec_dram(struct cxl_memdev *cxlmd,
					 struct cxl_mem_repair_attrbs *attrbs)
{
	struct cxl_event_dram *rec;
	u16 validity_flags;

	rec = xa_load(&cxlmd->rec_dram, attrbs->dpa);
	if (!rec)
		return NULL;

	validity_flags = get_unaligned_le16(rec->media_hdr.validity_flags);
	if (!(validity_flags & CXL_DER_VALID_CHANNEL) ||
	    !(validity_flags & CXL_DER_VALID_RANK))
		return NULL;

	switch (attrbs->repair_type) {
	case CXL_PPR:
		if (!(validity_flags & CXL_DER_VALID_NIBBLE) ||
		    get_unaligned_le24(rec->nibble_mask) == attrbs->nibble_mask)
			return rec;
		break;
	case CXL_CACHELINE_SPARING:
		if (!(validity_flags & CXL_DER_VALID_BANK_GROUP) ||
		    !(validity_flags & CXL_DER_VALID_BANK) ||
		    !(validity_flags & CXL_DER_VALID_ROW) ||
		    !(validity_flags & CXL_DER_VALID_COLUMN))
			return NULL;

		if (rec->media_hdr.channel == attrbs->channel &&
		    rec->media_hdr.rank == attrbs->rank &&
		    rec->bank_group == attrbs->bank_group &&
		    rec->bank == attrbs->bank &&
		    get_unaligned_le24(rec->row) == attrbs->row &&
		    get_unaligned_le16(rec->column) == attrbs->column &&
		    (!(validity_flags & CXL_DER_VALID_NIBBLE) ||
		     get_unaligned_le24(rec->nibble_mask) == attrbs->nibble_mask) &&
		    (!(validity_flags & CXL_DER_VALID_SUB_CHANNEL) ||
		     rec->sub_channel == attrbs->sub_channel))
			return rec;
		break;
	case CXL_ROW_SPARING:
		if (!(validity_flags & CXL_DER_VALID_BANK_GROUP) ||
		    !(validity_flags & CXL_DER_VALID_BANK) ||
		    !(validity_flags & CXL_DER_VALID_ROW))
			return NULL;

		if (rec->media_hdr.channel == attrbs->channel &&
		    rec->media_hdr.rank == attrbs->rank &&
		    rec->bank_group == attrbs->bank_group &&
		    rec->bank == attrbs->bank &&
		    get_unaligned_le24(rec->row) == attrbs->row &&
		    (!(validity_flags & CXL_DER_VALID_NIBBLE) ||
		     get_unaligned_le24(rec->nibble_mask) == attrbs->nibble_mask))
			return rec;
		break;
	case CXL_BANK_SPARING:
		if (!(validity_flags & CXL_DER_VALID_BANK_GROUP) ||
		    !(validity_flags & CXL_DER_VALID_BANK))
			return NULL;

		if (rec->media_hdr.channel == attrbs->channel &&
		    rec->media_hdr.rank == attrbs->rank &&
		    rec->bank_group == attrbs->bank_group &&
		    rec->bank == attrbs->bank &&
		    (!(validity_flags & CXL_DER_VALID_NIBBLE) ||
		     get_unaligned_le24(rec->nibble_mask) == attrbs->nibble_mask))
			return rec;
		break;
	case CXL_RANK_SPARING:
		if (rec->media_hdr.channel == attrbs->channel &&
		    rec->media_hdr.rank == attrbs->rank &&
		    (!(validity_flags & CXL_DER_VALID_NIBBLE) ||
		     get_unaligned_le24(rec->nibble_mask) == attrbs->nibble_mask))
			return rec;
		break;
	default:
		return NULL;
	}

	return NULL;
}
EXPORT_SYMBOL_NS_GPL(cxl_find_rec_dram, "CXL");

int cxl_store_rec_gen_media(struct cxl_memdev *cxlmd, union cxl_event *evt)
{
	void *old_rec;
	struct cxl_event_gen_media *rec = kmemdup(&evt->gen_media,
						  sizeof(*rec), GFP_KERNEL);
	if (!rec)
		return -ENOMEM;

	old_rec = xa_store(&cxlmd->rec_gen_media,
			   le64_to_cpu(rec->media_hdr.phys_addr),
			   rec, GFP_KERNEL);
	if (xa_is_err(old_rec))
		return xa_err(old_rec);

	kfree(old_rec);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cxl_store_rec_gen_media, "CXL");

int cxl_store_rec_dram(struct cxl_memdev *cxlmd, union cxl_event *evt)
{
	void *old_rec;
	struct cxl_event_dram *rec = kmemdup(&evt->dram, sizeof(*rec), GFP_KERNEL);

	if (!rec)
		return -ENOMEM;

	old_rec = xa_store(&cxlmd->rec_dram,
			   le64_to_cpu(rec->media_hdr.phys_addr),
			   rec, GFP_KERNEL);
	if (xa_is_err(old_rec))
		return xa_err(old_rec);

	kfree(old_rec);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cxl_store_rec_dram, "CXL");
