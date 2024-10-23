/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * RAS2 ACPI driver header file
 *
 * (C) Copyright 2014, 2015 Hewlett-Packard Enterprises
 *
 * Copyright (c) 2024 HiSilicon Limited
 */

#ifndef _RAS2_ACPI_H
#define _RAS2_ACPI_H

#include <linux/acpi.h>
#include <linux/auxiliary_bus.h>
#include <linux/mailbox_client.h>
#include <linux/mutex.h>
#include <linux/types.h>

#define RAS2_PCC_CMD_COMPLETE	BIT(0)
#define RAS2_PCC_CMD_ERROR	BIT(2)

/* RAS2 specific PCC commands */
#define RAS2_PCC_CMD_EXEC 0x01

#define RAS2_AUX_DEV_NAME "ras2"
#define RAS2_MEM_DEV_ID_NAME "acpi_ras2_mem"

/* Data structure RAS2 table */
struct ras2_mem_ctx {
	struct auxiliary_device adev;
	/* Lock to provide mutually exclusive access to PCC channel */
	struct mutex lock;
	int id;
	u8 instance;
	bool bg;
	u64 base, size;
	u8 scrub_cycle_hrs, min_scrub_cycle, max_scrub_cycle;
	struct device *dev;
	struct device *scrub_dev;
	void *pcc_subspace;
	struct acpi_ras2_shared_memory __iomem *pcc_comm_addr;
};

int ras2_send_pcc_cmd(struct ras2_mem_ctx *ras2_ctx, u16 cmd);
#endif /* _RAS2_ACPI_H */
