/* exynos_drm.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _EXYNOS_DRM_H_
#define _EXYNOS_DRM_H_

#include "drm.h"

/**
 * User-desired buffer creation information structure.
 *
 * @size: user-desired memory allocation size.
 *	- this size value would be page-aligned internally.
 * @flags: user request for setting memory type or cache attributes.
 * @handle: returned a handle to created gem object.
 *	- this handle will be set by gem module of kernel side.
 */
struct drm_exynos_gem_create {
	uint64_t size;
	unsigned int flags;
	unsigned int handle;
};

/**
 * A structure to gem information.
 *
 * @handle: a handle to gem object created.
 * @flags: flag value including memory type and cache attribute and
 *	this value would be set by driver.
 * @size: size to memory region allocated by gem and this size would
 *	be set by driver.
 */
struct drm_exynos_gem_info {
	unsigned int handle;
	unsigned int flags;
	uint64_t size;
};

/**
 * A structure for user connection request of virtual display.
 *
 * @connection: indicate whether doing connection or not by user.
 * @extensions: if this value is 1 then the vidi driver would need additional
 *	128bytes edid data.
 * @edid: the edid data pointer from user side.
 */
struct drm_exynos_vidi_connection {
	unsigned int connection;
	unsigned int extensions;
	uint64_t edid;
};

/* memory type definitions. */
enum e_drm_exynos_gem_mem_type {
	/* Physically Continuous memory and used as default. */
	EXYNOS_BO_CONTIG	= 0 << 0,
	/* Physically Non-Continuous memory. */
	EXYNOS_BO_NONCONTIG	= 1 << 0,
	/* non-cachable mapping and used as default. */
	EXYNOS_BO_NONCACHABLE	= 0 << 1,
	/* cachable mapping. */
	EXYNOS_BO_CACHABLE	= 1 << 1,
	/* write-combine mapping. */
	EXYNOS_BO_WC		= 1 << 2,
	EXYNOS_BO_MASK		= EXYNOS_BO_NONCONTIG | EXYNOS_BO_CACHABLE |
					EXYNOS_BO_WC
};

/* userptr operation types. */
enum e_drm_exynos_g2d_userptr_op_type {
	/* Register a userspace allocated buffer. */
	G2D_USERPTR_REGISTER,
	/* Unregister a userspace allocated buffer. */
	G2D_USERPTR_UNREGISTER,
	/* Check if a buffer is idle (no G2D command list is using it). */
	G2D_USERPTR_CHECK_IDLE
};

/* userptr flags */
enum e_drm_exynos_g2d_userptr_flags {
	/* G2D engine is allowed to read from the buffer. */
	G2D_USERPTR_FLAG_READ	= 1 << 0,
	/* G2D engine is allowed to write to the buffer. */
	G2D_USERPTR_FLAG_WRITE	= 1 << 1,
	/* G2D engine is allowed to both read and write the buffer. */
	G2D_USERPTR_FLAG_RW =
		G2D_USERPTR_FLAG_READ | G2D_USERPTR_FLAG_WRITE
};

enum drm_exynos_g2d_caps {
	G2D_CAP_USERPTR = (1 << 0),
	G2D_CAP_CMDLIST2 = (1 << 1),
};

struct drm_exynos_g2d_get_ver2 {
	__u32	major;
	__u32	minor;
	__u32	caps;
};

enum drm_exynos_g2d_buf_type {
	G2D_BUF_USERPTR = 1 << 31,
};

enum drm_exynos_g2d_event_type {
	G2D_EVENT_NOT,
	G2D_EVENT_NONSTOP,
	G2D_EVENT_STOP,		/* not yet */
};

/**
 * A structure for issuing userptr operations.
 *
 * @operation: the operation type (register, unregister and check idle).
 * @flags: access flags for buffer registration
 * @user_addr: the address of the userspace allocated buffer.
 * @size: the size of the buffer in bytes.
 */
struct drm_exynos_g2d_userptr_op {
	__u32 operation;
	__u32 flags;
	__u64 user_addr;
	__u64 size;
};

struct drm_exynos_g2d_set_cmdlist {
	__u64					cmd;
	__u64					cmd_buf;
	__u32					cmd_nr;
	__u32					cmd_buf_nr;

	/* for g2d event */
	__u64					event_type;
	__u64					user_data;
};

/*
 * Base commands:
 *
 *  base address (source/destination/pattern/mask)
 *  plane2 base address (source/destination)
 *  stride (source/destination/pattern/mask)
 *  color mode (source/destination/pattern)
 *  mask mode
 *
 *
 * General commands:
 *
 *  left-top/right-bottom (source/destination/mask/cw)
 *  pattern size and offset
 *  direction (source/destination/pattern/mask)
 *  select (source/destination)
 *  scale control (source/mask)
 *  x/y scaling (source/mask)
 *  color registers (foreground/bg/solidfill/bluescreen)
 *  repeat mode (source/mask)
 *  padding color (source/mask)
 *  A8 RGB extension (source/destination)
 *  third operand, ROP4, global alpha
 *  blend function, round mode, rotation
 *  BitBLT command and start
 *
 * cw = clipping window
 *
 *
 * Command stream formatting:
 *
 * The BitBLT start command has to be given explicitly.
 * The general commands buffer has to end with a BitBLT start.
 */

struct drm_exynos_g2d_set_cmdlist2 {
	/*
	 * cmd_base: base commands
	 * cmd: regular commands
	 */
	__u64					cmd_base;
	__u64					cmd;

	/*
	 * A __u16 for the buffer sizes is plenty of space since
	 * we have a much smaller limit for the total amount
	 * of G2D commands anyway.
	 */
	__u16					cmd_base_nr;
	__u16					cmd_nr;

	__u16					flags;

	/* For G2D event handling. */
	__u16					event_type;
	__u64					user_data;
};

struct drm_exynos_g2d_exec {
	__u64					async;
};

#define DRM_EXYNOS_GEM_CREATE		0x00
/* Reserved 0x04 ~ 0x05 for exynos specific gem ioctl */
#define DRM_EXYNOS_GEM_GET		0x04
#define DRM_EXYNOS_VIDI_CONNECTION	0x07

/* G2D */
#define DRM_EXYNOS_G2D_SET_CMDLIST	0x21
#define DRM_EXYNOS_G2D_EXEC		0x22
#define DRM_EXYNOS_G2D_GET_VER2		0x23
#define DRM_EXYNOS_G2D_USERPTR		0x24
#define DRM_EXYNOS_G2D_SET_CMDLIST2	0x25

#define DRM_IOCTL_EXYNOS_GEM_CREATE		DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_EXYNOS_GEM_CREATE, struct drm_exynos_gem_create)

#define DRM_IOCTL_EXYNOS_GEM_GET	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_EXYNOS_GEM_GET,	struct drm_exynos_gem_info)

#define DRM_IOCTL_EXYNOS_VIDI_CONNECTION	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_EXYNOS_VIDI_CONNECTION, struct drm_exynos_vidi_connection)

#define DRM_IOCTL_EXYNOS_G2D_GET_VER2		DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_EXYNOS_G2D_GET_VER2, struct drm_exynos_g2d_get_ver2)
#define DRM_IOCTL_EXYNOS_G2D_SET_CMDLIST	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_EXYNOS_G2D_SET_CMDLIST, struct drm_exynos_g2d_set_cmdlist)
#define DRM_IOCTL_EXYNOS_G2D_EXEC		DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_EXYNOS_G2D_EXEC, struct drm_exynos_g2d_exec)
#define DRM_IOCTL_EXYNOS_G2D_USERPTR		DRM_IOW(DRM_COMMAND_BASE + \
		DRM_EXYNOS_G2D_USERPTR, struct drm_exynos_g2d_userptr_op)
#define DRM_IOCTL_EXYNOS_G2D_SET_CMDLIST2	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_EXYNOS_G2D_SET_CMDLIST2, struct drm_exynos_g2d_set_cmdlist2)

/* EXYNOS specific events */
#define DRM_EXYNOS_G2D_EVENT		0x80000000

struct drm_exynos_g2d_event {
	struct drm_event	base;
	__u64				user_data;
	__u32				tv_sec;
	__u32				tv_usec;
	__u32				cmdlist_no;
	__u32				reserved;
};

#endif
