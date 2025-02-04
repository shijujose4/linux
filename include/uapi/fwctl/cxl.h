/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2024, Intel Corporation
 *
 * These are definitions for the mailbox command interface of CXL subsystem.
 */
#ifndef _UAPI_FWCTL_CXL_H_
#define _UAPI_FWCTL_CXL_H_

#include <linux/types.h>

/**
 * struct fwctl_info_cxl - ioctl(FWCTL_INFO) out_device_data
 * @reserved: Place older for future usages
 */
struct fwctl_info_cxl {
	__u32 reserved;
};
#endif
