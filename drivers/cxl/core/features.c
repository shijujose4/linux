// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2024-2025 Intel Corporation. All rights reserved. */
#include <linux/device.h>
#include <cxl/mailbox.h>
#include <cxl/features.h>
#include "cxl.h"
#include "core.h"
#include "cxlmem.h"
#include "features.h"

size_t cxl_get_feature(struct cxl_mailbox *cxl_mbox, const uuid_t *feat_uuid,
		       enum cxl_get_feat_selection selection,
		       void *feat_out, size_t feat_out_size, u16 offset,
		       u16 *return_code)
{
	size_t data_to_rd_size, size_out;
	struct cxl_mbox_get_feat_in pi;
	struct cxl_mbox_cmd mbox_cmd;
	size_t data_rcvd_size = 0;
	int rc;

	if (return_code)
		*return_code = CXL_MBOX_CMD_RC_INPUT;

	if (!feat_out || !feat_out_size)
		return 0;

	size_out = min(feat_out_size, cxl_mbox->payload_size);
	uuid_copy(&pi.uuid, feat_uuid);
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
