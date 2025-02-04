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

/**
 * struct fwctl_rpc_cxl - ioctl(FWCTL_RPC) input for CXL
 * @command_id: the defined command id by 'enum feature_cmds'
 * @flags: Flags for the command (input).
 * @op_size: Size of input payload.
 * @reserved1: Reserved. Must be 0s.
 * @in_payload: User address of the hardware op input structure
 */
struct fwctl_rpc_cxl {
	__u32 command_id;
	__u32 flags;
	__u32 op_size;
	__u32 reserved1;
	__aligned_u64 in_payload;
};

/* command_id for CXL mailbox Feature commands */
enum feature_cmds {
	CXL_FEATURE_ID_GET_SUPPORTED_FEATURES = 0,
	CXL_FEATURE_ID_GET_FEATURE,
	CXL_FEATURE_ID_SET_FEATURE,
	CXL_FEATURE_ID_MAX
};

/**
 * struct fwctl_rpc_cxl_out - ioctl(FWCTL_RPC) output for CXL
 * @size: Size of the output payload
 * @retval: Return value from device
 * @payload: Return data from device
 */
struct fwctl_rpc_cxl_out {
	__u32 size;
	__u32 retval;
	__u8 payload[] __counted_by(size);
};

#endif
