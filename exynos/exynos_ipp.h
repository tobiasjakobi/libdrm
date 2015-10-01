/*
 * Copyright (C) 2017 - Tobias Jakobi
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef _EXYNOS_IPP_H_
#define _EXYNOS_IPP_H_

enum e_ipp_limit_type {
	ipp_limit_size,
	ipp_limit_scale,
};

enum e_ipp_limit_size {
	ipp_limit_buffer,
	ipp_limit_area,
	ipp_limit_rotated,
};

struct ipp_limit {
	enum e_ipp_limit_type type:2;
	enum e_ipp_limit_size size:2;

	/*
	 * Minimum, maximum and alignment for horizontal and
	 * vertical dimensions.
	 */
	unsigned h_min, h_max, h_align;
	unsigned v_min, v_max, v_align;
};

struct ipp_format {
	/*
	 * FourCC of the color format.
	 * See the drm_fourcc header for details.
	 */
	uint32_t fourcc;

	/* Type (source/destination/both) of format. */
	unsigned type_src:1;
	unsigned type_dst:1;

	/*
	 * Format modifier for the color format.
	 * See the drm_fourcc header (Format Modifiers) for details.
	 */
	uint64_t modifier;

	/* Limits that apply to this format. */
	unsigned num_limits[2];
	struct ipp_limit *limits[2];
};

struct ipp_module {
	/* Kernel ID of the IPP module. */
	unsigned id;

	/* Capabilities of the IPP module. */
	unsigned cap_crop:1;
	unsigned cap_rotate:1;
	unsigned cap_scale:1;
	unsigned cap_convert:1;

	/* The formats supported by the IPP module. */
	unsigned num_formats;
	struct ipp_format *formats;
};

struct ipp_buffer {
	/* FourCC of the buffer's color format. */
	uint32_t fourcc;

	unsigned width, height;

	/* See struct drm_mode_fb_cmd2. */
	uint32_t gem_id[4];
	uint32_t offset[4];
	uint32_t pitch[4];
	uint64_t modifier;
};

struct ipp_rect {
	/* Rectangle position. */
	uint32_t x, y;
	/* Rectangle dimensions. */
	uint32_t w, h;
};

struct ipp_context;
struct ipp_task;

/* IPP context initialization and finalization. */
struct ipp_context *ipp_init(int fd);
void ipp_fini(struct ipp_context *ctx);

/* Querying modules. */
unsigned ipp_num_modules(struct ipp_context *ctx);
const struct ipp_module *ipp_get_module(struct ipp_context *ctx,
	unsigned index);

/* Creating, destroying and configuring tasks. */
struct ipp_task *ipp_task_create(unsigned module_idx);
void ipp_task_destroy(struct ipp_task *task);
int ipp_task_config_src(struct ipp_task *task, struct ipp_buffer *buf,
	struct ipp_rect *rect);
int ipp_task_config_dst(struct ipp_task *task, struct ipp_buffer *buf,
	struct ipp_rect *rect);
int ipp_task_config_transf(struct ipp_task *task, uint32_t rotation);
int ipp_task_config_alpha(struct ipp_task *task, uint32_t alpha);

/* Checking and comitting tasks. */
int ipp_check(struct ipp_context *ctx, struct ipp_task *task);
int ipp_commit(struct ipp_context *ctx, struct ipp_task *task,
	void *event_data);

#endif /* _EXYNOS_IPP_H_ */
