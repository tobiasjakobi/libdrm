/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <sys/mman.h>
#include <linux/stddef.h>

#include <xf86drm.h>

#include "libdrm_macros.h"
#include "exynos_drm.h"
#include "fimg2d_reg.h"
#include "exynos_fimg2d.h"

#define		SET_BF(val, sc, si, scsa, scda, dc, di, dcsa, dcda) \
			val.data.src_coeff = sc;		\
			val.data.inv_src_color_coeff = si;	\
			val.data.src_coeff_src_a = scsa;	\
			val.data.src_coeff_dst_a = scda;	\
			val.data.dst_coeff = dc;		\
			val.data.inv_dst_color_coeff = di;	\
			val.data.dst_coeff_src_a = dcsa;	\
			val.data.dst_coeff_dst_a = dcda;

#define MIN(a, b)	((a) < (b) ? (a) : (b))

#define MSG_PREFIX "exynos/fimg2d: "

#define G2D_MAX_CMD_NR		64
#define G2D_MAX_BASE_CMD_NR	16
#define G2D_MAX_CMDLIST_NR	64

struct drm_exynos_g2d_cmd {
	__u32	offset;
	__u32	data;
};

struct g2d_context {
	int				fd;
	unsigned int			major;
	unsigned int			minor;
	unsigned int			caps;
	struct drm_exynos_g2d_cmd	cmd[G2D_MAX_CMD_NR];
	struct drm_exynos_g2d_cmd	cmd_base[G2D_MAX_BASE_CMD_NR];
	unsigned int			cmd_nr;
	unsigned int			cmd_base_nr;
	unsigned int			cmdlist_nr;
	void				*event_userdata;
};

enum g2d_base_addr_reg {
	g2d_dst = 0,
	g2d_src
};

enum e_g2d_dir_mode {
	G2D_DIR_MODE_POSITIVE = 0,
	G2D_DIR_MODE_NEGATIVE = 1
};

union g2d_direction_val {
	unsigned int val[2];
	struct {
		/* SRC_MSK_DIRECT_REG [0:1] (source) */
		enum e_g2d_dir_mode		src_x_direction:1;
		enum e_g2d_dir_mode		src_y_direction:1;

		/* SRC_MSK_DIRECT_REG [2:3] */
		unsigned int			reversed1:2;

		/* SRC_MSK_DIRECT_REG [4:5] (mask) */
		enum e_g2d_dir_mode		mask_x_direction:1;
		enum e_g2d_dir_mode		mask_y_direction:1;

		/* SRC_MSK_DIRECT_REG [6:31] */
		unsigned int			padding1:26;

		/* DST_PAT_DIRECT_REG [0:1] (destination) */
		enum e_g2d_dir_mode		dst_x_direction:1;
		enum e_g2d_dir_mode		dst_y_direction:1;

		/* DST_PAT_DIRECT_REG [2:3] */
		unsigned int			reversed2:2;

		/* DST_PAT_DIRECT_REG [4:5] (pattern) */
		enum e_g2d_dir_mode		pat_x_direction:1;
		enum e_g2d_dir_mode		pat_y_direction:1;

		/* DST_PAT_DIRECT_REG [6:31] */
		unsigned int			padding2:26;
	} data;
};

static unsigned int g2d_get_scaling(unsigned int src, unsigned int dst)
{
	/*
	 * The G2D hw scaling factor is a normalized inverse of the scaling factor.
	 * For example: When source width is 100 and destination width is 200
	 * (scaling of 2x), then the hw factor is NC * 100 / 200.
	 * The normalization factor (NC) is 2^16 = 0x10000.
	 */

	return ((src << 16) / dst);
}

static unsigned int g2d_get_blend_op(enum e_g2d_op op)
{
	union g2d_blend_func_val val;

	val.val = 0;

	/*
	 * The switch statement is missing the default branch since
	 * we assume that the caller checks the blending operation
	 * via g2d_validate_blending_op() first.
	 */
	switch (op) {
	case G2D_OP_CLEAR:
	case G2D_OP_DISJOINT_CLEAR:
	case G2D_OP_CONJOINT_CLEAR:
		SET_BF(val, G2D_COEFF_MODE_ZERO, 0, 0, 0, G2D_COEFF_MODE_ZERO,
				0, 0, 0);
		break;
	case G2D_OP_SRC:
	case G2D_OP_DISJOINT_SRC:
	case G2D_OP_CONJOINT_SRC:
		SET_BF(val, G2D_COEFF_MODE_ONE, 0, 0, 0, G2D_COEFF_MODE_ZERO,
				0, 0, 0);
		break;
	case G2D_OP_DST:
	case G2D_OP_DISJOINT_DST:
	case G2D_OP_CONJOINT_DST:
		SET_BF(val, G2D_COEFF_MODE_ZERO, 0, 0, 0, G2D_COEFF_MODE_ONE,
				0, 0, 0);
		break;
	case G2D_OP_OVER:
		SET_BF(val, G2D_COEFF_MODE_ONE, 0, 0, 0,
				G2D_COEFF_MODE_SRC_ALPHA, 1, 0, 0);
		break;
	case G2D_OP_INTERPOLATE:
		SET_BF(val, G2D_COEFF_MODE_SRC_ALPHA, 0, 0, 0,
				G2D_COEFF_MODE_SRC_ALPHA, 1, 0, 0);
		break;
	}

	return val.val;
}

/*
 * g2d_check_space - check if command buffers have enough space left.
 *
 * @ctx: a pointer to g2d_context structure.
 * @num_cmds: number of (regular) commands.
 * @num_base_cmds: number of base commands.
 */
static unsigned int g2d_check_space(const struct g2d_context *ctx,
	unsigned int num_cmds, unsigned int num_base_cmds)
{
	if (ctx->cmd_nr + num_cmds >= G2D_MAX_CMD_NR ||
	    ctx->cmd_base_nr + num_base_cmds >= G2D_MAX_BASE_CMD_NR)
		return 1;
	else
		return 0;
}

/*
 * g2d_validate_select_mode - validate select mode.
 *
 * @mode: the mode to validate
 *
 * Returns zero for an invalid mode and one otherwise.
 */
static int g2d_validate_select_mode(
	enum e_g2d_select_mode mode)
{
	switch (mode) {
	case G2D_SELECT_MODE_NORMAL:
	case G2D_SELECT_MODE_FGCOLOR:
	case G2D_SELECT_MODE_BGCOLOR:
		return 1;
	}

	return 0;
}

/*
 * g2d_validate_blending_op - validate blending operation.
 *
 * @operation: the operation to validate
 *
 * Returns zero for an invalid mode and one otherwise.
 */
static int g2d_validate_blending_op(
	enum e_g2d_op operation)
{
	switch (operation) {
	case G2D_OP_CLEAR:
	case G2D_OP_SRC:
	case G2D_OP_DST:
	case G2D_OP_OVER:
	case G2D_OP_INTERPOLATE:
	case G2D_OP_DISJOINT_CLEAR:
	case G2D_OP_DISJOINT_SRC:
	case G2D_OP_DISJOINT_DST:
	case G2D_OP_CONJOINT_CLEAR:
	case G2D_OP_CONJOINT_SRC:
	case G2D_OP_CONJOINT_DST:
		return 1;
	}

	return 0;
}

static int is_g2d_base_cmd(unsigned long cmd)
{
	switch (cmd & ~(G2D_BUF_USERPTR)) {
	/* source */
	case SRC_BASE_ADDR_REG:
	case SRC_STRIDE_REG:
	case SRC_COLOR_MODE_REG:
	case SRC_PLANE2_BASE_ADDR_REG:
		return 1;

	/* destination */
	case DST_BASE_ADDR_REG:
	case DST_STRIDE_REG:
	case DST_COLOR_MODE_REG:
	case DST_PLANE2_BASE_ADDR_REG:
		return 1;

	/* pattern */
	case PAT_BASE_ADDR_REG:
	case PAT_COLOR_MODE_REG:
	case PAT_STRIDE_REG:
		return 1;

	/* mask */
	case MASK_BASE_ADDR_REG:
	case MASK_STRIDE_REG:
	case MASK_MODE_REG:
		return 1;

	default:
		return 0;
	}
}

static int is_g2d_cmd(unsigned long cmd)
{
	switch (cmd & ~(G2D_BUF_USERPTR)) {
	/* command */
	case BITBLT_START_REG:
	case BITBLT_COMMAND_REG:
	case BLEND_FUNCTION_REG:
	case ROUND_MODE_REG:
		return 1;

	/* parameter settings */
	case ROTATE_REG:
	case SRC_MASK_DIRECT_REG:
	case DST_PAT_DIRECT_REG:
		return 1;

	/* source */
	case SRC_SELECT_REG:
	case SRC_LEFT_TOP_REG:
	case SRC_RIGHT_BOTTOM_REG:
	case SRC_REPEAT_MODE_REG:
	case SRC_PAD_VALUE_REG:
	case SRC_A8_RGB_EXT_REG:
	case SRC_SCALE_CTRL_REG:
	case SRC_XSCALE_REG:
	case SRC_YSCALE_REG:
		return 1;

	/* destination */
	case DST_SELECT_REG:
	case DST_LEFT_TOP_REG:
	case DST_RIGHT_BOTTOM_REG:
	case DST_A8_RGB_EXT_REG:
		return 1;

	/* pattern */
	case PAT_SIZE_REG:
	case PAT_OFFSET_REG:
		return 1;

	/* mask */
	case MASK_LEFT_TOP_REG:
	case MASK_RIGHT_BOTTOM_REG:
	case MASK_REPEAT_MODE_REG:
	case MASK_PAD_VALUE_REG:
	case MASK_SCALE_CTRL_REG:
	case MASK_XSCALE_REG:
	case MASK_YSCALE_REG:
		return 1;

	/* third operand, ROP and alpha setting */
	case THIRD_OPERAND_REG:
	case ROP4_REG:
	case ALPHA_REG:
		return 1;

	/* color setting */
	case FG_COLOR_REG:
	case BG_COLOR_REG:
	case BS_COLOR_REG:
	case SF_COLOR_REG:
		return 1;

	default:
		return 0;
	}
}

/*
 * g2d_add_base_cmd - add a base command to the command buffer.
 *
 * @ctx: pointer to a g2d_context structure.
 * @cmd: base command.
 * @value: argument to the base command.
 *
 * The caller has to make sure that the commands buffers have enough space
 * left to hold the command. Use g2d_check_space() to ensure this.
 */
static void g2d_add_base_cmd(struct g2d_context *ctx, unsigned long cmd,
			unsigned long value)
{
	assert(is_g2d_base_cmd(cmd));
	assert(ctx->cmd_base_nr < G2D_MAX_BASE_CMD_NR);

	ctx->cmd_base[ctx->cmd_base_nr].offset = cmd;
	ctx->cmd_base[ctx->cmd_base_nr].data = value;
	ctx->cmd_base_nr++;
}

/*
 * g2d_add_cmd - add a regular command to the command buffer.
 *
 * @ctx: pointer to a g2d_context structure.
 * @cmd: regular command.
 * @value: argument to the regular command.
 *
 * The caller has to make sure that the commands buffers have enough space
 * left to hold the command. Use g2d_check_space() to ensure this.
 */
static void g2d_add_cmd(struct g2d_context *ctx, unsigned long cmd,
			unsigned long value)
{
	assert(is_g2d_cmd(cmd));
	assert(ctx->cmd_nr < G2D_MAX_CMD_NR);

	ctx->cmd[ctx->cmd_nr].offset = cmd;
	ctx->cmd[ctx->cmd_nr].data = value;
	ctx->cmd_nr++;
}

/*
 * g2d_add_base_addr - helper function to set dst/src base address register.
 *
 * @ctx: a pointer to g2d_context structure.
 * @img: a pointer to the dst/src g2d_image structure.
 * @reg: the register that should be set.
 */
static void g2d_add_base_addr(struct g2d_context *ctx, struct g2d_image *img,
			enum g2d_base_addr_reg reg)
{
	const unsigned long cmd = (reg == g2d_dst) ?
		DST_BASE_ADDR_REG : SRC_BASE_ADDR_REG;

	if (img->buf_type == G2D_IMGBUF_USERPTR)
		g2d_add_base_cmd(ctx, cmd | G2D_BUF_USERPTR,
				(unsigned long)&img->user_ptr[0]);
	else
		g2d_add_base_cmd(ctx, cmd, img->bo[0]);
}

/*
 * g2d_set_direction - setup direction register (useful for overlapping blits).
 *
 * @ctx: a pointer to g2d_context structure.
 * @dir: a pointer to the g2d_direction_val structure.
 */
static void g2d_set_direction(struct g2d_context *ctx,
			const union g2d_direction_val *dir)
{
	g2d_add_cmd(ctx, SRC_MASK_DIRECT_REG, dir->val[0]);
	g2d_add_cmd(ctx, DST_PAT_DIRECT_REG, dir->val[1]);
}

/*
 * g2d_flush - submit all commands and values in user side command buffer
 *		to command queue aware of fimg2d dma.
 *
 * @ctx: a pointer to g2d_context structure.
 *
 * This function should be called after all commands and values to user
 * side command buffer are set. It submits that buffer to the kernel side driver.
 */
static int g2d_flush(struct g2d_context *ctx)
{
	int ret;
	struct drm_exynos_g2d_set_cmdlist2 cmdlist = {0};

	if (ctx->cmd_nr == 0 && ctx->cmd_base_nr == 0)
		return 0;

	if (ctx->cmdlist_nr >= G2D_MAX_CMDLIST_NR) {
		fprintf(stderr, MSG_PREFIX "command list overflow.\n");
		return -EINVAL;
	}

	cmdlist.cmd_base = (uint64_t)(uintptr_t)&ctx->cmd_base[0];
	cmdlist.cmd = (uint64_t)(uintptr_t)&ctx->cmd[0];
	cmdlist.cmd_base_nr = ctx->cmd_base_nr;
	cmdlist.cmd_nr = ctx->cmd_nr;

	if (ctx->event_userdata) {
		cmdlist.event_type = G2D_EVENT_NONSTOP;
		cmdlist.user_data = (uint64_t)(uintptr_t)(ctx->event_userdata);
		ctx->event_userdata = NULL;
	} else {
		cmdlist.event_type = G2D_EVENT_NOT;
		cmdlist.user_data = 0;
	}

	ctx->cmd_nr = 0;
	ctx->cmd_base_nr = 0;

	ret = drmIoctl(ctx->fd, DRM_IOCTL_EXYNOS_G2D_SET_CMDLIST2, &cmdlist);
	if (ret < 0) {
		fprintf(stderr, MSG_PREFIX "failed to set cmdlist.\n");
		return ret;
	}

	ctx->cmdlist_nr++;

	return ret;
}

/**
 * g2d_init - create a new g2d context and get hardware version.
 *
 * @fd: a file descriptor to an opened drm device.
 */
drm_public struct g2d_context *g2d_init(int fd)
{
	struct drm_exynos_g2d_get_ver2 ver;
	struct g2d_context *ctx;
	int ret;

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx) {
		fprintf(stderr, MSG_PREFIX "failed to allocate context.\n");
		return NULL;
	}

	ctx->fd = fd;

	ret = drmIoctl(fd, DRM_IOCTL_EXYNOS_G2D_GET_VER2, &ver);
	if (ret < 0) {
		fprintf(stderr, MSG_PREFIX "failed to get version.\n");
		free(ctx);
		return NULL;
	}

	if (!(ver.caps & G2D_CAP_CMDLIST2)) {
		fprintf(stderr, MSG_PREFIX "G2D driver doesn't support cmdlist2.\n");
		free(ctx);
		return NULL;
	}

	ctx->major = ver.major;
	ctx->minor = ver.minor;
	ctx->caps = ver.caps;

	printf(MSG_PREFIX "G2D version (%d.%d).\n", ctx->major, ctx->minor);

	if (ctx->caps & G2D_CAP_USERPTR)
		printf(MSG_PREFIX "G2D driver supports userptr.\n");

	return ctx;
}

drm_public void g2d_fini(struct g2d_context *ctx)
{
	free(ctx);
}

/**
 * g2d_config_event - setup userdata configuration for a g2d event.
 *		The next invocation of a g2d call (e.g. g2d_solid_fill) is
 *		then going to flag the command buffer as 'nonstop'.
 *		Completion of the command buffer execution can then be
 *		determined by using drmHandleEvent on the DRM fd.
 *		The userdata is 'consumed' in the process.
 *
 * @ctx: a pointer to g2d_context structure.
 * @userdata: a pointer to the user data
 */
drm_public void g2d_config_event(struct g2d_context *ctx, void *userdata)
{
	ctx->event_userdata = userdata;
}

/**
 * g2d_exec - start the dma to process all commands summited by g2d_flush().
 *
 * @ctx: a pointer to g2d_context structure.
 */
drm_public int g2d_exec(struct g2d_context *ctx)
{
	struct drm_exynos_g2d_exec exec;
	int ret;

	if (ctx->cmdlist_nr == 0)
		return -EINVAL;

	exec.async = 0;

	ret = drmIoctl(ctx->fd, DRM_IOCTL_EXYNOS_G2D_EXEC, &exec);
	if (ret < 0) {
		fprintf(stderr, MSG_PREFIX "failed to execute.\n");
		return ret;
	}

	ctx->cmdlist_nr = 0;

	return ret;
}

/**
 * g2d_solid_fill - fill given buffer with given color data.
 *
 * @ctx: a pointer to g2d_context structure.
 * @img: a pointer to g2d_image structure including image and buffer
 *	information.
 * @x: x start position to buffer filled with given color data.
 * @y: y start position to buffer filled with given color data.
 * @w: width value to buffer filled with given color data.
 * @h: height value to buffer filled with given color data.
 */
drm_public int
g2d_solid_fill(struct g2d_context *ctx, struct g2d_image *img,
			unsigned int x, unsigned int y, unsigned int w,
			unsigned int h)
{
	union g2d_bitblt_cmd_val bitblt;
	union g2d_point_val pt;

	if (g2d_check_space(ctx, 6, 3))
		return -ENOSPC;

	g2d_add_base_addr(ctx, img, g2d_dst);
	g2d_add_base_cmd(ctx, DST_COLOR_MODE_REG, img->color_mode);
	g2d_add_base_cmd(ctx, DST_STRIDE_REG, img->stride);

	g2d_add_cmd(ctx, DST_SELECT_REG, G2D_SELECT_MODE_NORMAL);

	if (x + w > img->width)
		w = img->width - x;
	if (y + h > img->height)
		h = img->height - y;

	pt.data.x = x;
	pt.data.y = y;
	g2d_add_cmd(ctx, DST_LEFT_TOP_REG, pt.val);

	pt.data.x = x + w;
	pt.data.y = y + h;
	g2d_add_cmd(ctx, DST_RIGHT_BOTTOM_REG, pt.val);

	g2d_add_cmd(ctx, SF_COLOR_REG, img->color);

	bitblt.val = 0;
	bitblt.data.fast_solid_color_fill_en = 1;
	g2d_add_cmd(ctx, BITBLT_COMMAND_REG, bitblt.val);

	g2d_add_cmd(ctx, BITBLT_START_REG, G2D_START_BITBLT);

	return g2d_flush(ctx);
}

/**
 * g2d_solid_fill_multi - fill given buffer with given color data.
 *
 * @ctx: a pointer to g2d_context structure.
 * @img: a pointer to g2d_image structure including image and buffer
 *	information.
 * @rects: pointer to an array of g2d_rect structures.
 * @num_rects: number of rectangle objects in array.
 *
 * Empty rectangles are silently ignored.
 */
drm_public int
g2d_solid_fill_multi(struct g2d_context *ctx, struct g2d_image *img,
			const struct g2d_rect *rects, unsigned int num_rects)
{
	union g2d_bitblt_cmd_val bitblt;
	union g2d_point_val pt;
	unsigned int i;

	if (num_rects == 0)
		return 0;

	if (g2d_check_space(ctx, 3 + 3 * num_rects, 3))
		return -ENOSPC;

	g2d_add_base_addr(ctx, img, g2d_dst);
	g2d_add_base_cmd(ctx, DST_COLOR_MODE_REG, img->color_mode);
	g2d_add_base_cmd(ctx, DST_STRIDE_REG, img->stride);

	g2d_add_cmd(ctx, DST_SELECT_REG, G2D_SELECT_MODE_NORMAL);
	g2d_add_cmd(ctx, SF_COLOR_REG, img->color);

	bitblt.val = 0;
	bitblt.data.fast_solid_color_fill_en = 1;
	g2d_add_cmd(ctx, BITBLT_COMMAND_REG, bitblt.val);

	for (i = 0; i < num_rects; ++i) {
		unsigned int x, y, w, h;

		x = rects[i].x;
		y = rects[i].y;
		w = rects[i].w;
		h = rects[i].h;

		if (x + w > img->width)
			w = img->width - x;
		if (y + h > img->height)
			h = img->height - y;

		if (w == 0 || h == 0)
			continue;

		pt.data.x = x;
		pt.data.y = y;
		g2d_add_cmd(ctx, DST_LEFT_TOP_REG, pt.val);

		pt.data.x = x + w;
		pt.data.y = y + h;
		g2d_add_cmd(ctx, DST_RIGHT_BOTTOM_REG, pt.val);

		g2d_add_cmd(ctx, BITBLT_START_REG, G2D_START_BITBLT | G2D_START_CASESEL);

	}

	return g2d_flush(ctx);
}

/**
 * g2d_copy - copy contents in source buffer to destination buffer.
 *
 * @ctx: a pointer to g2d_context structure.
 * @src: a pointer to g2d_image structure including image and buffer
 *	information to source.
 * @dst: a pointer to g2d_image structure including image and buffer
 *	information to destination.
 * @src_x: x start position to source buffer.
 * @src_y: y start position to source buffer.
 * @dst_x: x start position to destination buffer.
 * @dst_y: y start position to destination buffer.
 * @w: width value to source and destination buffers.
 * @h: height value to source and destination buffers.
 */
drm_public int
g2d_copy(struct g2d_context *ctx, struct g2d_image *src,
		struct g2d_image *dst, unsigned int src_x, unsigned int src_y,
		unsigned int dst_x, unsigned dst_y, unsigned int w,
		unsigned int h)
{
	union g2d_rop4_val rop4;
	union g2d_point_val pt;
	unsigned int src_w, src_h, dst_w, dst_h;

	src_w = w;
	src_h = h;
	if (src_x + src->width > w)
		src_w = src->width - src_x;
	if (src_y + src->height > h)
		src_h = src->height - src_y;

	dst_w = w;
	dst_h = w;
	if (dst_x + dst->width > w)
		dst_w = dst->width - dst_x;
	if (dst_y + dst->height > h)
		dst_h = dst->height - dst_y;

	w = MIN(src_w, dst_w);
	h = MIN(src_h, dst_h);

	if (w <= 0 || h <= 0) {
		fprintf(stderr, MSG_PREFIX "invalid width or height.\n");
		return -EINVAL;
	}

	if (g2d_check_space(ctx, 8, 6))
		return -ENOSPC;

	g2d_add_base_addr(ctx, src, g2d_src);
	g2d_add_base_cmd(ctx, SRC_COLOR_MODE_REG, src->color_mode);
	g2d_add_base_cmd(ctx, SRC_STRIDE_REG, src->stride);

	g2d_add_base_addr(ctx, dst, g2d_dst);
	g2d_add_base_cmd(ctx, DST_COLOR_MODE_REG, dst->color_mode);
	g2d_add_base_cmd(ctx, DST_STRIDE_REG, dst->stride);

	g2d_add_cmd(ctx, SRC_SELECT_REG, G2D_SELECT_MODE_NORMAL);
	g2d_add_cmd(ctx, DST_SELECT_REG, G2D_SELECT_MODE_BGCOLOR);

	pt.data.x = src_x;
	pt.data.y = src_y;
	g2d_add_cmd(ctx, SRC_LEFT_TOP_REG, pt.val);
	pt.data.x = src_x + w;
	pt.data.y = src_y + h;
	g2d_add_cmd(ctx, SRC_RIGHT_BOTTOM_REG, pt.val);

	pt.data.x = dst_x;
	pt.data.y = dst_y;
	g2d_add_cmd(ctx, DST_LEFT_TOP_REG, pt.val);
	pt.data.x = dst_x + w;
	pt.data.y = dst_y + h;
	g2d_add_cmd(ctx, DST_RIGHT_BOTTOM_REG, pt.val);

	rop4.val = 0;
	rop4.data.unmasked_rop3 = G2D_ROP3_SRC;
	g2d_add_cmd(ctx, ROP4_REG, rop4.val);

	g2d_add_cmd(ctx, BITBLT_START_REG, G2D_START_BITBLT);

	return g2d_flush(ctx);
}

/**
 * g2d_copy_rop - copy contents from source buffer to destination buffer
 *		while applying a raster operaton (ROP4).
 *
 * @ctx: a pointer to g2d_context structure.
 * @src: a pointer to g2d_image structure including image and buffer
 *	information to source.
 * @dst: a pointer to g2d_image structure including image and buffer
 *	information to destination.
 * @src_x: x start position to source buffer.
 * @src_y: y start position to source buffer.
 * @dst_x: x start position to destination buffer.
 * @dst_y: y start position to destination buffer.
 * @w: width value to source and destination buffers.
 * @h: height value to source and destination buffers.
 * @rop4: raster operation definition.
 * @use_third_op: setup the third operand register to use the
 *	color of the source image.
 */
drm_public int
g2d_copy_rop(struct g2d_context *ctx, struct g2d_image *src,
		struct g2d_image *dst, unsigned int src_x, unsigned int src_y,
		unsigned int dst_x, unsigned dst_y, unsigned int w,
		unsigned int h, union g2d_rop4_val rop4,
		unsigned int use_third_op)
{
	union g2d_point_val pt;
	unsigned int src_w, src_h, dst_w, dst_h;

	union g2d_bitblt_cmd_val bitblt_cmd;

	src_w = w;
	src_h = h;
	if (src_x + src->width > w)
		src_w = src->width - src_x;
	if (src_y + src->height > h)
		src_h = src->height - src_y;

	dst_w = w;
	dst_h = w;
	if (dst_x + dst->width > w)
		dst_w = dst->width - dst_x;
	if (dst_y + dst->height > h)
		dst_h = dst->height - dst_y;

	w = MIN(src_w, dst_w);
	h = MIN(src_h, dst_h);

	if (w <= 0 || h <= 0) {
		fprintf(stderr, MSG_PREFIX "invalid width or height.\n");
		return -EINVAL;
	}

	/* Validate ROP4: */
	if (rop4.val & 0xffff0000) {
		fprintf(stderr, MSG_PREFIX "invalid ROP4 value.\n");
		return -EINVAL;
	}

	/*
	 * The masked ROP needs correctly configured mask
	 * registers, otherwise the G2D engine crashes.
	 */
	if (rop4.data.masked_rop3 != 0) {
		fprintf(stderr, MSG_PREFIX "masked ROP not implemented yet.\n");
		return -EINVAL;
	}

	if (g2d_check_space(ctx, 9 + (use_third_op ? 2 : 0), 6))
		return -ENOSPC;

	g2d_add_base_addr(ctx, src, g2d_src);
	g2d_add_base_cmd(ctx, SRC_COLOR_MODE_REG, src->color_mode);
	g2d_add_base_cmd(ctx, SRC_STRIDE_REG, src->stride);

	g2d_add_base_addr(ctx, dst, g2d_dst);
	g2d_add_base_cmd(ctx, DST_COLOR_MODE_REG, dst->color_mode);
	g2d_add_base_cmd(ctx, DST_STRIDE_REG, dst->stride);

	g2d_add_cmd(ctx, SRC_SELECT_REG, G2D_SELECT_MODE_NORMAL);
	g2d_add_cmd(ctx, DST_SELECT_REG, G2D_SELECT_MODE_NORMAL);

	pt.data.x = src_x;
	pt.data.y = src_y;
	g2d_add_cmd(ctx, SRC_LEFT_TOP_REG, pt.val);
	pt.data.x = src_x + w;
	pt.data.y = src_y + h;
	g2d_add_cmd(ctx, SRC_RIGHT_BOTTOM_REG, pt.val);

	pt.data.x = dst_x;
	pt.data.y = dst_y;
	g2d_add_cmd(ctx, DST_LEFT_TOP_REG, pt.val);
	pt.data.x = dst_x + w;
	pt.data.y = dst_y + h;
	g2d_add_cmd(ctx, DST_RIGHT_BOTTOM_REG, pt.val);

	/*
	 * When using the third operand register, configure it to use
	 * the color from the foreground color register.
	 * The fg color is set from the color of the source G2D image.
	 */
	if (use_third_op) {
		union g2d_third_op_val third_op;

		third_op.val = 0;
		third_op.data.unmasked_select = G2D_THIRD_OP_SELECT_FG_COLOR;
		third_op.data.masked_select = G2D_THIRD_OP_SELECT_FG_COLOR;

		g2d_add_cmd(ctx, FG_COLOR_REG, src->color);
		g2d_add_cmd(ctx, THIRD_OPERAND_REG, third_op.val);
	}

	g2d_add_cmd(ctx, ROP4_REG, rop4.val);

	/*
	 * The raster operation can either apply to the alpha channel as
	 * well, or simply copy it from the source. Use the component_alpha
	 * flag from the source G2D image to select the mode.
	 */
	bitblt_cmd.val = 0;
	bitblt_cmd.data.rop4_alpha_en = src->component_alpha ?
		G2D_SELECT_ROP_FOR_ALPHA_BLEND : G2D_SELECT_SRC_FOR_ALPHA_BLEND;

	g2d_add_cmd(ctx, BITBLT_COMMAND_REG, bitblt_cmd.val);

	g2d_add_cmd(ctx, BITBLT_START_REG, G2D_START_BITBLT);

	return g2d_flush(ctx);
}

/**
 * g2d_move - copy content inside single buffer.
 *	Similar to libc's memmove() this copies a rectangular
 *	region of the provided buffer to another location, while
 *	properly handling the situation where source and
 *	destination rectangle overlap.
 *
 * @ctx: a pointer to g2d_context structure.
 * @img: a pointer to g2d_image structure providing
 *	buffer information.
 * @src_x: x position of source rectangle.
 * @src_y: y position of source rectangle.
 * @dst_x: x position of destination rectangle.
 * @dst_y: y position of destination rectangle.
 * @w: width of rectangle to move.
 * @h: height of rectangle to move.
 */
drm_public int
g2d_move(struct g2d_context *ctx, struct g2d_image *img,
		unsigned int src_x, unsigned int src_y,
		unsigned int dst_x, unsigned dst_y, unsigned int w,
		unsigned int h)
{
	union g2d_rop4_val rop4;
	union g2d_point_val pt;
	union g2d_direction_val dir;
	unsigned int src_w, src_h, dst_w, dst_h;

	src_w = w;
	src_h = h;
	if (src_x + img->width > w)
		src_w = img->width - src_x;
	if (src_y + img->height > h)
		src_h = img->height - src_y;

	dst_w = w;
	dst_h = w;
	if (dst_x + img->width > w)
		dst_w = img->width - dst_x;
	if (dst_y + img->height > h)
		dst_h = img->height - dst_y;

	w = MIN(src_w, dst_w);
	h = MIN(src_h, dst_h);

	if (w == 0 || h == 0) {
		fprintf(stderr, MSG_PREFIX "invalid width or height.\n");
		return -EINVAL;
	}

	if (g2d_check_space(ctx, 10, 6))
		return -ENOSPC;

	g2d_add_base_addr(ctx, img, g2d_src);
	g2d_add_base_cmd(ctx, SRC_COLOR_MODE_REG, img->color_mode);
	g2d_add_base_cmd(ctx, SRC_STRIDE_REG, img->stride);

	g2d_add_base_addr(ctx, img, g2d_dst);
	g2d_add_base_cmd(ctx, DST_COLOR_MODE_REG, img->color_mode);
	g2d_add_base_cmd(ctx, DST_STRIDE_REG, img->stride);

	g2d_add_cmd(ctx, DST_SELECT_REG, G2D_SELECT_MODE_BGCOLOR);
	g2d_add_cmd(ctx, SRC_SELECT_REG, G2D_SELECT_MODE_NORMAL);

	dir.val[0] = dir.val[1] = 0;

	if (dst_x >= src_x)
		dir.data.src_x_direction = dir.data.dst_x_direction = 1;
	if (dst_y >= src_y)
		dir.data.src_y_direction = dir.data.dst_y_direction = 1;

	g2d_set_direction(ctx, &dir);

	pt.data.x = src_x;
	pt.data.y = src_y;
	g2d_add_cmd(ctx, SRC_LEFT_TOP_REG, pt.val);
	pt.data.x = src_x + w;
	pt.data.y = src_y + h;
	g2d_add_cmd(ctx, SRC_RIGHT_BOTTOM_REG, pt.val);

	pt.data.x = dst_x;
	pt.data.y = dst_y;
	g2d_add_cmd(ctx, DST_LEFT_TOP_REG, pt.val);
	pt.data.x = dst_x + w;
	pt.data.y = dst_y + h;
	g2d_add_cmd(ctx, DST_RIGHT_BOTTOM_REG, pt.val);

	rop4.val = 0;
	rop4.data.unmasked_rop3 = G2D_ROP3_SRC;
	g2d_add_cmd(ctx, ROP4_REG, rop4.val);

	g2d_add_cmd(ctx, BITBLT_START_REG, G2D_START_BITBLT);

	return g2d_flush(ctx);
}

/**
 * g2d_copy_with_scale - copy contents in source buffer to destination buffer
 *	scaling up or down properly.
 *
 * @ctx: a pointer to g2d_context structure.
 * @src: a pointer to g2d_image structure including image and buffer
 *	information to source.
 * @dst: a pointer to g2d_image structure including image and buffer
 *	information to destination.
 * @src_x: x start position to source buffer.
 * @src_y: y start position to source buffer.
 * @src_w: width value to source buffer.
 * @src_h: height value to source buffer.
 * @dst_x: x start position to destination buffer.
 * @dst_y: y start position to destination buffer.
 * @dst_w: width value to destination buffer.
 * @dst_h: height value to destination buffer.
 * @negative: indicate that it uses color negative to source and
 *	destination buffers.
 */
drm_public int
g2d_copy_with_scale(struct g2d_context *ctx, struct g2d_image *src,
				struct g2d_image *dst, unsigned int src_x,
				unsigned int src_y, unsigned int src_w,
				unsigned int src_h, unsigned int dst_x,
				unsigned int dst_y, unsigned int dst_w,
				unsigned int dst_h, unsigned int negative)
{
	union g2d_rop4_val rop4;
	union g2d_point_val pt;
	unsigned int scale, repeat_pad;
	unsigned int scale_x, scale_y;

	/* Sanitize this parameter to facilitate space computation below. */
	if (negative)
		negative = 1;

	if (src_w == dst_w && src_h == dst_h)
		scale = 0;
	else {
		scale = 1;
		scale_x = g2d_get_scaling(src_w, dst_w);
		scale_y = g2d_get_scaling(src_h, dst_h);
	}

	repeat_pad = src->repeat_mode == G2D_REPEAT_MODE_PAD ? 1 : 0;

	if (src_x + src_w > src->width)
		src_w = src->width - src_x;
	if (src_y + src_h > src->height)
		src_h = src->height - src_y;

	if (dst_x + dst_w > dst->width)
		dst_w = dst->width - dst_x;
	if (dst_y + dst_h > dst->height)
		dst_h = dst->height - dst_y;

	if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
		fprintf(stderr, MSG_PREFIX "invalid width or height.\n");
		return -EINVAL;
	}

	if (g2d_check_space(ctx, 9 + scale * 3 + negative + repeat_pad, 6))
		return -ENOSPC;

	g2d_add_base_addr(ctx, src, g2d_src);
	g2d_add_base_cmd(ctx, SRC_COLOR_MODE_REG, src->color_mode);
	g2d_add_base_cmd(ctx, SRC_STRIDE_REG, src->stride);

	g2d_add_base_addr(ctx, dst, g2d_dst);
	g2d_add_base_cmd(ctx, DST_COLOR_MODE_REG, dst->color_mode);
	g2d_add_base_cmd(ctx, DST_STRIDE_REG, dst->stride);

	g2d_add_cmd(ctx, SRC_SELECT_REG, G2D_SELECT_MODE_NORMAL);
	g2d_add_cmd(ctx, DST_SELECT_REG, G2D_SELECT_MODE_BGCOLOR);

	g2d_add_cmd(ctx, SRC_REPEAT_MODE_REG, src->repeat_mode);
	if (repeat_pad)
		g2d_add_cmd(ctx, SRC_PAD_VALUE_REG, dst->color);

	rop4.val = 0;
	rop4.data.unmasked_rop3 = G2D_ROP3_SRC;

	if (negative) {
		g2d_add_cmd(ctx, BG_COLOR_REG, 0x00FFFFFF);
		rop4.data.unmasked_rop3 ^= G2D_ROP3_DST;
	}

	g2d_add_cmd(ctx, ROP4_REG, rop4.val);

	if (scale) {
		g2d_add_cmd(ctx, SRC_SCALE_CTRL_REG, G2D_SCALE_MODE_BILINEAR);
		g2d_add_cmd(ctx, SRC_XSCALE_REG, scale_x);
		g2d_add_cmd(ctx, SRC_YSCALE_REG, scale_y);
	}

	pt.data.x = src_x;
	pt.data.y = src_y;
	g2d_add_cmd(ctx, SRC_LEFT_TOP_REG, pt.val);
	pt.data.x = src_x + src_w;
	pt.data.y = src_y + src_h;
	g2d_add_cmd(ctx, SRC_RIGHT_BOTTOM_REG, pt.val);

	pt.data.x = dst_x;
	pt.data.y = dst_y;
	g2d_add_cmd(ctx, DST_LEFT_TOP_REG, pt.val);
	pt.data.x = dst_x + dst_w;
	pt.data.y = dst_y + dst_h;
	g2d_add_cmd(ctx, DST_RIGHT_BOTTOM_REG, pt.val);

	g2d_add_cmd(ctx, BITBLT_START_REG, G2D_START_BITBLT);

	return g2d_flush(ctx);
}

/**
 * g2d_scale_rop_multi - apply ROP with scaling to multiple rectangles.
 *
 * @ctx: a pointer to g2d_context structure.
 * @src: a pointer to g2d_image structure including image and buffer
 *	information to source.
 * @dst: a pointer to g2d_image structure including image and buffer
 *	information to destination.
 * @src_rects: pointer to an array of g2d_rect structures acting as
 *	blitting sources.
 * @dst_rects: pointer to an array of g2d_rect structures acting as
 *	blitting destinations.
 * @num_rects: number of rectangle objects in array
 * @rop4: raster operation definition.
 * @use_third_op: setup the third operand register to use the
 *	color of the source image.
 */
drm_public int
g2d_scale_rop_multi(struct g2d_context *ctx, struct g2d_image *src,
		struct g2d_image *dst, const struct g2d_rect *src_rects,
		const struct g2d_rect *dst_rects, unsigned int num_rects,
		union g2d_rop4_val rop4, unsigned int use_third_op)
{
	union g2d_bitblt_cmd_val bitblt_cmd;
	unsigned int i, repeat_pad;

	repeat_pad = src->repeat_mode == G2D_REPEAT_MODE_PAD ? 1 : 0;

	if (rop4.val & 0xffff0000) {
		fprintf(stderr, MSG_PREFIX "invalid ROP4 value.\n");
		return -EINVAL;
	}

	if (rop4.data.masked_rop3 != 0) {
		fprintf(stderr, MSG_PREFIX "masked ROP not implemented yet.\n");
		return -EINVAL;
	}

	if (g2d_check_space(ctx, 6 + num_rects * 7 + repeat_pad + (use_third_op ? 2 : 0), 6))
		return -ENOSPC;

	g2d_add_base_addr(ctx, src, g2d_src);
	g2d_add_base_cmd(ctx, SRC_COLOR_MODE_REG, src->color_mode);
	g2d_add_base_cmd(ctx, SRC_STRIDE_REG, src->stride);

	g2d_add_base_addr(ctx, dst, g2d_dst);
	g2d_add_base_cmd(ctx, DST_COLOR_MODE_REG, dst->color_mode);
	g2d_add_base_cmd(ctx, DST_STRIDE_REG, dst->stride);

	g2d_add_cmd(ctx, SRC_SELECT_REG, G2D_SELECT_MODE_NORMAL);
	g2d_add_cmd(ctx, SRC_SCALE_CTRL_REG, G2D_SCALE_MODE_BILINEAR);
	g2d_add_cmd(ctx, SRC_REPEAT_MODE_REG, src->repeat_mode);
	g2d_add_cmd(ctx, DST_SELECT_REG, G2D_SELECT_MODE_NORMAL);

	if (repeat_pad)
		g2d_add_cmd(ctx, SRC_PAD_VALUE_REG, dst->color);

	if (use_third_op) {
		union g2d_third_op_val third_op;

		third_op.val = 0;
		third_op.data.unmasked_select = G2D_THIRD_OP_SELECT_FG_COLOR;
		third_op.data.masked_select = G2D_THIRD_OP_SELECT_FG_COLOR;

		g2d_add_cmd(ctx, FG_COLOR_REG, src->color);
		g2d_add_cmd(ctx, THIRD_OPERAND_REG, third_op.val);
	}

	bitblt_cmd.val = 0;
	bitblt_cmd.data.rop4_alpha_en = src->component_alpha ?
		G2D_SELECT_ROP_FOR_ALPHA_BLEND : G2D_SELECT_SRC_FOR_ALPHA_BLEND;

	g2d_add_cmd(ctx, BITBLT_COMMAND_REG, bitblt_cmd.val);
	g2d_add_cmd(ctx, ROP4_REG, rop4.val);

	for (i = 0; i < num_rects; ++i) {
		union g2d_point_val pt;

		unsigned int src_x, src_y, src_w, src_h;
		unsigned int dst_x, dst_y, dst_w, dst_h;
		unsigned int scale_x, scale_y;

		src_x = src_rects[i].x;
		src_y = src_rects[i].y;
		src_w = src_rects[i].w;
		src_h = src_rects[i].h;

		dst_x = dst_rects[i].x;
		dst_y = dst_rects[i].y;
		dst_w = dst_rects[i].w;
		dst_h = dst_rects[i].h;

		scale_x = g2d_get_scaling(src_w, dst_w);
		scale_y = g2d_get_scaling(src_h, dst_h);

		if (src_x + src->width > src_w)
			src_w = src->width - src_x;
		if (src_y + src->height > src_h)
			src_h = src->height - src_y;

		if (src_w == 0 || src_h == 0)
			continue;

		if (dst_x + dst->width > dst_w)
			dst_w = dst->width - dst_x;
		if (dst_y + dst->height > dst_h)
			dst_h = dst->height - dst_y;

		if (dst_w == 0 || dst_h == 0)
			continue;

		g2d_add_cmd(ctx, SRC_XSCALE_REG, scale_x);
		g2d_add_cmd(ctx, SRC_YSCALE_REG, scale_y);

		pt.data.x = src_x;
		pt.data.y = src_y;
		g2d_add_cmd(ctx, SRC_LEFT_TOP_REG, pt.val);
		pt.data.x += src_w;
		pt.data.y += src_h;
		g2d_add_cmd(ctx, SRC_RIGHT_BOTTOM_REG, pt.val);

		pt.data.x = dst_x;
		pt.data.y = dst_y;
		g2d_add_cmd(ctx, DST_LEFT_TOP_REG, pt.val);
		pt.data.x += dst_w;
		pt.data.y += dst_h;
		g2d_add_cmd(ctx, DST_RIGHT_BOTTOM_REG, pt.val);

		g2d_add_cmd(ctx, BITBLT_START_REG, G2D_START_BITBLT | G2D_START_CASESEL);
	}

	return g2d_flush(ctx);
}


/**
 * g2d_blend - blend image data in source and destination buffers.
 *
 * @ctx: a pointer to g2d_context structure.
 * @src: a pointer to g2d_image structure including image and buffer
 *	information to source.
 * @dst: a pointer to g2d_image structure including image and buffer
 *	information to destination.
 * @src_x: x start position to source buffer.
 * @src_y: y start position to source buffer.
 * @dst_x: x start position to destination buffer.
 * @dst_y: y start position to destination buffer.
 * @w: width value to source and destination buffer.
 * @h: height value to source and destination buffer.
 * @op: blend operation type.
 */
drm_public int
g2d_blend(struct g2d_context *ctx, struct g2d_image *src,
		struct g2d_image *dst, unsigned int src_x,
		unsigned int src_y, unsigned int dst_x, unsigned int dst_y,
		unsigned int w, unsigned int h, enum e_g2d_op op)
{
	union g2d_point_val pt;
	union g2d_bitblt_cmd_val bitblt;
	union g2d_blend_func_val blend;
	unsigned int basecmd_space, cmd_space;
	unsigned int src_w, src_h, dst_w, dst_h;

	src_w = w;
	src_h = h;
	if (src_x + w > src->width)
		src_w = src->width - src_x;
	if (src_y + h > src->height)
		src_h = src->height - src_y;

	dst_w = w;
	dst_h = h;
	if (dst_x + w > dst->width)
		dst_w = dst->width - dst_x;
	if (dst_y + h > dst->height)
		dst_h = dst->height - dst_y;

	w = MIN(src_w, dst_w);
	h = MIN(src_h, dst_h);

	if (w <= 0 || h <= 0) {
		fprintf(stderr, MSG_PREFIX "invalid width or height.\n");
		return -EINVAL;
	}

	if (!g2d_validate_select_mode(src->select_mode)) {
		fprintf(stderr , MSG_PREFIX "invalid select mode for source.\n");
		return -EINVAL;
	}

	if (!g2d_validate_blending_op(op)) {
		fprintf(stderr , MSG_PREFIX "unsupported blending operation.\n");
		return -EINVAL;
	}

	if (src->select_mode == G2D_SELECT_MODE_NORMAL) {
		basecmd_space = 6;
		cmd_space = 9;
	} else {
		basecmd_space = 3;
		cmd_space = 10;
	}

	if (g2d_check_space(ctx, cmd_space, basecmd_space))
		return -ENOSPC;

	/*
	 * Setting the color mode is only necessary for normal select
	 * mode, since the color mode of foreground/background is
	 * determined by the destination color mode.
	 */
	switch (src->select_mode) {
	case G2D_SELECT_MODE_NORMAL:
		g2d_add_base_addr(ctx, src, g2d_src);
		g2d_add_base_cmd(ctx, SRC_COLOR_MODE_REG, src->color_mode);
		g2d_add_base_cmd(ctx, SRC_STRIDE_REG, src->stride);
		break;
	case G2D_SELECT_MODE_FGCOLOR:
		g2d_add_cmd(ctx, FG_COLOR_REG, src->color);
		break;
	case G2D_SELECT_MODE_BGCOLOR:
		g2d_add_cmd(ctx, BG_COLOR_REG, src->color);
		break;
	}

	g2d_add_base_addr(ctx, dst, g2d_dst);
	g2d_add_base_cmd(ctx, DST_COLOR_MODE_REG, dst->color_mode);
	g2d_add_base_cmd(ctx, DST_STRIDE_REG, dst->stride);

	g2d_add_cmd(ctx, SRC_SELECT_REG, src->select_mode);

	if (op == G2D_OP_SRC || op == G2D_OP_CLEAR)
		g2d_add_cmd(ctx, DST_SELECT_REG, G2D_SELECT_MODE_BGCOLOR);
	else
		g2d_add_cmd(ctx, DST_SELECT_REG, G2D_SELECT_MODE_NORMAL);

	bitblt.val = 0;
	bitblt.data.alpha_blend_mode = G2D_ALPHA_BLEND_MODE_ENABLE;
	blend.val = g2d_get_blend_op(op);
	g2d_add_cmd(ctx, BITBLT_COMMAND_REG, bitblt.val);
	g2d_add_cmd(ctx, BLEND_FUNCTION_REG, blend.val);

	pt.data.x = src_x;
	pt.data.y = src_y;
	g2d_add_cmd(ctx, SRC_LEFT_TOP_REG, pt.val);
	pt.data.x = src_x + w;
	pt.data.y = src_y + h;
	g2d_add_cmd(ctx, SRC_RIGHT_BOTTOM_REG, pt.val);

	pt.data.x = dst_x;
	pt.data.y = dst_y;
	g2d_add_cmd(ctx, DST_LEFT_TOP_REG, pt.val);
	pt.data.x = dst_x + w;
	pt.data.y = dst_y + h;
	g2d_add_cmd(ctx, DST_RIGHT_BOTTOM_REG, pt.val);

	g2d_add_cmd(ctx, BITBLT_START_REG, G2D_START_BITBLT);

	return g2d_flush(ctx);
}

/**
 * g2d_scale_and_blend - apply scaling to source buffer and then blend to destination buffer
 *
 * @ctx: a pointer to g2d_context structure.
 * @src: a pointer to g2d_image structure including image and buffer
 *	information to source.
 * @dst: a pointer to g2d_image structure including image and buffer
 *	information to destination.
 * @src_x: x start position to source buffer.
 * @src_y: y start position to source buffer.
 * @src_w: width value to source buffer.
 * @src_h: height value to source buffer.
 * @dst_x: x start position to destination buffer.
 * @dst_y: y start position to destination buffer.
 * @dst_w: width value to destination buffer.
 * @dst_h: height value to destination buffer.
 * @op: blend operation type.
 */
drm_public int
g2d_scale_and_blend(struct g2d_context *ctx, struct g2d_image *src,
		struct g2d_image *dst, unsigned int src_x, unsigned int src_y,
		unsigned int src_w, unsigned int src_h, unsigned int dst_x,
		unsigned int dst_y, unsigned int dst_w, unsigned int dst_h,
		enum e_g2d_op op)
{
	union g2d_point_val pt;
	union g2d_bitblt_cmd_val bitblt;
	union g2d_blend_func_val blend;
	unsigned int scale, basecmd_space, cmd_space;
	unsigned int scale_x, scale_y;

	if (src_w == dst_w && src_h == dst_h)
		scale = 0;
	else {
		scale = 1;
		scale_x = g2d_get_scaling(src_w, dst_w);
		scale_y = g2d_get_scaling(src_h, dst_h);
	}

	if (src_x + src_w > src->width)
		src_w = src->width - src_x;
	if (src_y + src_h > src->height)
		src_h = src->height - src_y;

	if (dst_x + dst_w > dst->width)
		dst_w = dst->width - dst_x;
	if (dst_y + dst_h > dst->height)
		dst_h = dst->height - dst_y;

	if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
		fprintf(stderr, MSG_PREFIX "invalid width or height.\n");
		return -EINVAL;
	}

	if (!g2d_validate_select_mode(src->select_mode)) {
		fprintf(stderr , MSG_PREFIX "invalid select mode for source.\n");
		return -EINVAL;
	}

	if (!g2d_validate_blending_op(op)) {
		fprintf(stderr , MSG_PREFIX "unsupported blending operation.\n");
		return -EINVAL;
	}

	if (src->select_mode == G2D_SELECT_MODE_NORMAL) {
		basecmd_space = 6;
		cmd_space = 9;
	} else {
		basecmd_space = 3;
		cmd_space = 10;
	}

	cmd_space += scale * 3;

	if (g2d_check_space(ctx, cmd_space, basecmd_space))
		return -ENOSPC;

	/*
	 * Setting the color mode is only necessary for normal select
	 * mode, since the color mode of foreground/background is
	 * determined by the destination color mode.
	 */
	switch (src->select_mode) {
	case G2D_SELECT_MODE_NORMAL:
		g2d_add_base_addr(ctx, src, g2d_src);
		g2d_add_base_cmd(ctx, SRC_COLOR_MODE_REG, src->color_mode);
		g2d_add_base_cmd(ctx, SRC_STRIDE_REG, src->stride);
		break;
	case G2D_SELECT_MODE_FGCOLOR:
		g2d_add_cmd(ctx, FG_COLOR_REG, src->color);
		break;
	case G2D_SELECT_MODE_BGCOLOR:
		g2d_add_cmd(ctx, BG_COLOR_REG, src->color);
		break;
	}

	g2d_add_base_addr(ctx, dst, g2d_dst);
	g2d_add_base_cmd(ctx, DST_COLOR_MODE_REG, dst->color_mode);
	g2d_add_base_cmd(ctx, DST_STRIDE_REG, dst->stride);

	g2d_add_cmd(ctx, SRC_SELECT_REG, src->select_mode);

	if (op == G2D_OP_SRC || op == G2D_OP_CLEAR)
		g2d_add_cmd(ctx, DST_SELECT_REG, G2D_SELECT_MODE_BGCOLOR);
	else
		g2d_add_cmd(ctx, DST_SELECT_REG, G2D_SELECT_MODE_NORMAL);

	if (scale) {
		g2d_add_cmd(ctx, SRC_SCALE_CTRL_REG, G2D_SCALE_MODE_BILINEAR);
		g2d_add_cmd(ctx, SRC_XSCALE_REG, scale_x);
		g2d_add_cmd(ctx, SRC_YSCALE_REG, scale_y);
	}

	bitblt.val = 0;
	bitblt.data.alpha_blend_mode = G2D_ALPHA_BLEND_MODE_ENABLE;
	blend.val = g2d_get_blend_op(op);
	g2d_add_cmd(ctx, BITBLT_COMMAND_REG, bitblt.val);
	g2d_add_cmd(ctx, BLEND_FUNCTION_REG, blend.val);

	pt.data.x = src_x;
	pt.data.y = src_y;
	g2d_add_cmd(ctx, SRC_LEFT_TOP_REG, pt.val);
	pt.data.x = src_x + src_w;
	pt.data.y = src_y + src_h;
	g2d_add_cmd(ctx, SRC_RIGHT_BOTTOM_REG, pt.val);

	pt.data.x = dst_x;
	pt.data.y = dst_y;
	g2d_add_cmd(ctx, DST_LEFT_TOP_REG, pt.val);
	pt.data.x = dst_x + dst_w;
	pt.data.y = dst_y + dst_h;
	g2d_add_cmd(ctx, DST_RIGHT_BOTTOM_REG, pt.val);

	g2d_add_cmd(ctx, BITBLT_START_REG, G2D_START_BITBLT);

	return g2d_flush(ctx);
}

/**
 * g2d_userptr_register - register an userspace allocated buffer
 *
 * @ctx: a pointer to g2d_context structure.
 * @addr: address of the userspace buffer.
 * @size: size of the buffer in bytes.
 * @flags: flags indicating buffer access (read/write).
 *
 * Registering a null pointer is silently ignored.
 */
drm_public int
g2d_userptr_register(struct g2d_context *ctx, void *addr, size_t size,
	unsigned long flags)
{
	struct drm_exynos_g2d_userptr_op req = { 0 };

	if (!addr)
		return 0;

	req.operation = G2D_USERPTR_REGISTER;
	req.flags = flags;
	req.user_addr = (uint64_t)(uintptr_t)addr;
	req.size = size;

	return drmIoctl(ctx->fd, DRM_IOCTL_EXYNOS_G2D_USERPTR, &req);
}

/**
 * g2d_userptr_unregister - unregister an userspace allocated buffer
 *
 * @ctx: a pointer to g2d_context structure.
 * @addr: address of the userspace buffer.
 *
 * Unregistering a null pointer is silently ignored.
 */
drm_public int
g2d_userptr_unregister(struct g2d_context *ctx, void *addr)
{
	struct drm_exynos_g2d_userptr_op req = { 0 };

	if (!addr)
		return 0;

	req.operation = G2D_USERPTR_UNREGISTER;
	req.user_addr = (uint64_t)(uintptr_t)addr;

	return drmIoctl(ctx->fd, DRM_IOCTL_EXYNOS_G2D_USERPTR, &req);
}

/**
 * g2d_userptr_check_idle - check if an userspace allocated buffer is idle
 *
 * @ctx: a pointer to g2d_context structure.
 * @addr: address of the userspace buffer
 *
 * Returns zero (0) if the userptr is currently used by a G2D command
 * list, and one (1) if it is unused.
 * A negative value indicates that the userptr was not found.
 *
 * Checking a null pointer is silently ignored (the null buffer
 * is always idle).
 */
drm_public int
g2d_userptr_check_idle(struct g2d_context *ctx, void *addr)
{
	struct drm_exynos_g2d_userptr_op req = { 0 };

	if (!addr)
		return 1;

	req.operation = G2D_USERPTR_CHECK_IDLE;
	req.user_addr = (uint64_t)(uintptr_t)addr;

	return drmIoctl(ctx->fd, DRM_IOCTL_EXYNOS_G2D_USERPTR, &req);
}
