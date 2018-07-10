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

#ifndef _FIMG2D_H_
#define _FIMG2D_H_

#define G2D_PLANE_MAX_NR	2

enum e_g2d_color_mode {
	/* COLOR FORMAT */
	G2D_COLOR_FMT_XRGB8888,
	G2D_COLOR_FMT_ARGB8888,
	G2D_COLOR_FMT_RGB565,
	G2D_COLOR_FMT_XRGB1555,
	G2D_COLOR_FMT_ARGB1555,
	G2D_COLOR_FMT_XRGB4444,
	G2D_COLOR_FMT_ARGB4444,
	G2D_COLOR_FMT_PACKED_RGB888,
	G2D_COLOR_FMT_YCbCr444,
	G2D_COLOR_FMT_YCbCr422,
	G2D_COLOR_FMT_YCbCr420,
	/* alpha 8bit */
	G2D_COLOR_FMT_A8,
	/* Luminance 8bit: gray color */
	G2D_COLOR_FMT_L8,
	G2D_COLOR_FMT_MAX,

	G2D_COLOR_FMT_MASK	= 0xF,

	/* COLOR ORDER */
	G2D_ORDER_AXRGB		= (0 << 4),		/* VER4.1 */
	G2D_ORDER_RGBAX		= (1 << 4),		/* VER4.1 */
	G2D_ORDER_AXBGR		= (2 << 4),		/* VER4.1 */
	G2D_ORDER_BGRAX		= (3 << 4),		/* VER4.1 */
	G2D_ORDER_MASK		= (3 << 4),		/* VER4.1 */

	/* Number of YCbCr plane */
	G2D_YCbCr_1PLANE	= (0 << 8),		/* VER4.1 */
	G2D_YCbCr_2PLANE	= (1 << 8),		/* VER4.1 */
	G2D_YCbCr_PLANE_MASK	= (3 << 8),		/* VER4.1 */

	/* Order in YCbCr */
	G2D_YCbCr_ORDER_CrY1CbY0 = (0 << 12),			/* VER4.1 */
	G2D_YCbCr_ORDER_CbY1CrY0 = (1 << 12),			/* VER4.1 */
	G2D_YCbCr_ORDER_Y1CrY0Cb = (2 << 12),			/* VER4.1 */
	G2D_YCbCr_ORDER_Y1CbY0Cr = (3 << 12),			/* VER4.1 */
	G2D_YCbCr_ORDER_CrCb = G2D_YCbCr_ORDER_CrY1CbY0,	/* VER4.1 */
	G2D_YCbCr_ORDER_CbCr = G2D_YCbCr_ORDER_CbY1CrY0,	/* VER4.1 */
	G2D_YCbCr_ORDER_MASK = (3 < 12),			/* VER4.1 */

	/* CSC */
	G2D_CSC_601 = (0 << 16),		/* VER4.1 */
	G2D_CSC_709 = (1 << 16),		/* VER4.1 */
	G2D_CSC_MASK = (1 << 16),		/* VER4.1 */

	/* Valid value range of YCbCr */
	G2D_YCbCr_RANGE_NARROW = (0 << 17),	/* VER4.1 */
	G2D_YCbCr_RANGE_WIDE = (1 << 17),	/* VER4.1 */
	G2D_YCbCr_RANGE_MASK = (1 << 17),	/* VER4.1 */

	G2D_COLOR_MODE_MASK = 0xFFFFFFFF,
};

enum e_g2d_mask_mode {
	/* mask format */
	G2D_MASK_FMT_1BIT,
	G2D_MASK_FMT_4BIT,
	G2D_MASK_FMT_8BIT,
	G2D_MASK_FMT_16BIT_565,
	G2D_MASK_FMT_16BIT_1555,
	G2D_MASK_FMT_16BIT_4444,
	G2D_MASK_FMT_32BIT_8888,
	G2D_MASK_FMT_4BIT_WINCE_AA_FONT,

	/* mask color order */
	G2D_MASK_ORDER_AXRGB			= (0 << 4),
	G2D_MASK_ORDER_RGBAX			= (1 << 4),
	G2D_MASK_ORDER_AXBGR			= (2 << 4),
	G2D_MASK_ORDER_BGRAX			= (3 << 4),

	/* mask type */
	G2D_MASK_TYPE_ALPHA			= (0 << 8),
	G2D_MASK_TYPE_ARGB			= (1 << 8),
	G2D_MASK_TYPE_MIXED			= (2 << 8),
};

enum e_g2d_select_mode {
	G2D_SELECT_MODE_NORMAL,
	G2D_SELECT_MODE_FGCOLOR,
	G2D_SELECT_MODE_BGCOLOR,
};

enum e_g2d_repeat_mode {
	G2D_REPEAT_MODE_REPEAT,
	G2D_REPEAT_MODE_PAD,
	G2D_REPEAT_MODE_REFLECT,
	G2D_REPEAT_MODE_CLAMP,
	G2D_REPEAT_MODE_NONE,
};

enum e_g2d_scale_mode {
	G2D_SCALE_MODE_NONE = 0,
	G2D_SCALE_MODE_NEAREST,
	G2D_SCALE_MODE_BILINEAR,
	G2D_SCALE_MODE_MAX,
};

enum e_g2d_buf_type {
	G2D_IMGBUF_COLOR,
	G2D_IMGBUF_GEM,
	G2D_IMGBUF_USERPTR,
};

enum e_g2d_exec_flag {
	G2D_EXEC_FLAG_ASYNC		= (1 << 0),
};

enum e_g2d_reset_type {
	/*
	 * Issues a local reset, i.e. clearing the G2D context
	 * in userspace. That does not affect the cmdlists that
	 * were already submitted to the kernel.
	 */
	G2D_RESET_LOCAL,
	/*
	 * Issues a global reset, i.e. local reset together with
	 * a reset of the G2D kernel context. This can trigger a
	 * hardware reset of the G2D engine, if necessary.
	 */
	G2D_RESET_GLOBAL,
};

/*
 * A ROP3 is a boolean operation which has three inputs:
 *   source value, destination value and third operand
 *
 * The operation works like this:
 * | output | source |  dest  | third  |
 * |  rop0  |   0    |   0    |   0    |
 * |  rop1  |   0    |   1    |   0    |
 * |  rop2  |   1    |   0    |   0    |
 * |  rop3  |   1    |   1    |   0    |
 * |  rop4  |   0    |   0    |   1    |
 * |  rop5  |   0    |   1    |   1    |
 * |  rop6  |   1    |   0    |   1    |
 * |  rop7  |   1    |   1    |   1    |
 *
 * rop{0..7} are specified by the ROP3 value.
 */
enum e_g2d_rop3_type {
	G2D_ROP3_DST = 0xAA,
	G2D_ROP3_SRC = 0xCC,
	G2D_ROP3_3RD = 0xF0,

	/* Bitwise AND and OR of source and destination. */
	G2D_ROP3_SRC_AND_DST = 0x88,
	G2D_ROP3_SRC_OR_DST = 0xEE,

	/* Bitwise invert of source and destination. */
	G2D_ROP3_INVERT_SRC = 0x33,
	G2D_ROP3_INVERT_DST = 0x55,

	G2D_ROP3_MASK = 0xFF,
};

/*
 * The default for ROP is to only take the RGB part
 * into consideration and ignore the alpha value.
 * G2D_SELECT_ROP_FOR_ALPHA_BLEND also applies the
 * ROP to alpha.
 */
enum e_g2d_select_alpha_src {
	G2D_SELECT_SRC_FOR_ALPHA_BLEND,	/* VER4.1 */
	G2D_SELECT_ROP_FOR_ALPHA_BLEND,	/* VER4.1 */
};

enum e_g2d_third_op_select {
	G2D_THIRD_OP_SELECT_PATTERN,
	G2D_THIRD_OP_SELECT_FG_COLOR,
	G2D_THIRD_OP_SELECT_BG_COLOR,
};

enum e_g2d_transparent_mode {
	G2D_TRANSPARENT_MODE_OPAQUE,
	G2D_TRANSPARENT_MODE_TRANSPARENT,
	G2D_TRANSPARENT_MODE_BLUESCREEN,
	G2D_TRANSPARENT_MODE_MAX,
};

enum e_g2d_color_key_mode {
	G2D_COLORKEY_MODE_DISABLE	= 0,
	G2D_COLORKEY_MODE_SRC_RGBA	= (1<<0),
	G2D_COLORKEY_MODE_DST_RGBA	= (1<<1),
	G2D_COLORKEY_MODE_SRC_YCbCr	= (1<<2),		/* VER4.1 */
	G2D_COLORKEY_MODE_DST_YCbCr	= (1<<3),		/* VER4.1 */
	G2D_COLORKEY_MODE_MASK		= 15,
};

enum e_g2d_alpha_blend_mode {
	G2D_ALPHA_BLEND_MODE_DISABLE,
	G2D_ALPHA_BLEND_MODE_ENABLE,
	G2D_ALPHA_BLEND_MODE_FADING,				/* VER3.0 */
	G2D_ALPHA_BLEND_MODE_MAX,
};

enum e_g2d_op {
	G2D_OP_CLEAR			= 0x00,
	G2D_OP_SRC			= 0x01,
	G2D_OP_DST			= 0x02,
	G2D_OP_OVER			= 0x03,
	G2D_OP_INTERPOLATE		= 0x04,
	G2D_OP_DISJOINT_CLEAR		= 0x10,
	G2D_OP_DISJOINT_SRC		= 0x11,
	G2D_OP_DISJOINT_DST		= 0x12,
	G2D_OP_CONJOINT_CLEAR		= 0x20,
	G2D_OP_CONJOINT_SRC		= 0x21,
	G2D_OP_CONJOINT_DST		= 0x22,
};

/*
 * The G2D_COEFF_MODE_DST_{COLOR,ALPHA} modes both use the ALPHA_REG(0x618)
 * register. The registers fields are as follows:
 * bits 31:8 = color value (RGB order)
 * bits 7:0 = alpha value
 */
enum e_g2d_coeff_mode {
	G2D_COEFF_MODE_ONE,
	G2D_COEFF_MODE_ZERO,
	G2D_COEFF_MODE_SRC_ALPHA,
	G2D_COEFF_MODE_SRC_COLOR,
	G2D_COEFF_MODE_DST_ALPHA,
	G2D_COEFF_MODE_DST_COLOR,
	/* Global Alpha : Set by ALPHA_REG(0x618) */
	G2D_COEFF_MODE_GB_ALPHA,
	/* Global Color and Alpha : Set by ALPHA_REG(0x618) */
	G2D_COEFF_MODE_GB_COLOR,
	/* (1-SRC alpha)/DST Alpha */
	G2D_COEFF_MODE_DISJOINT_S,
	/* (1-DST alpha)/SRC Alpha */
	G2D_COEFF_MODE_DISJOINT_D,
	/* SRC alpha/DST alpha */
	G2D_COEFF_MODE_CONJOINT_S,
	/* DST alpha/SRC alpha */
	G2D_COEFF_MODE_CONJOINT_D,
	/* DST alpha/SRC alpha */
	G2D_COEFF_MODE_MASK
};

enum e_g2d_acoeff_mode {
	G2D_ACOEFF_MODE_A,          /* alpha */
	G2D_ACOEFF_MODE_APGA,	/* alpha + global alpha */
	G2D_ACOEFF_MODE_AMGA,	/* alpha * global alpha */
	G2D_ACOEFF_MODE_MASK
};

enum e_g2d_bitblt_mode {
	G2D_START_BITBLT	= (1 << 0),
	G2D_START_N_HOLD	= (1 << 1),
	G2D_START_CASESEL	= (1 << 2)
};

/**
 * @BLEND_ROUND_0: OUT = (A+1)*B) >> 8
 * @BLEND_ROUND_1: OUT = (A+(A>>7))* B) >> 8
 * @BLEND_ROUND_2: TMP = A*8 + 0x80, OUT = (TMP + (TMP >> 8)) >> 8
 * @BLEND_ROUND_3: TMP = (A*B + C*D + 0x80), OUT = (TMP + (TMP >> 8)) >> 8
 */
enum e_g2d_blend_round_mode {
	G2D_BLEND_ROUND_0,
	G2D_BLEND_ROUND_1,
	G2D_BLEND_ROUND_2,
	G2D_BLEND_ROUND_3  /* initial value */
};

/**
 * @PREMULT_ROUND_0: OUT = (A*B) >> 8
 * @PREMULT_ROUND_1: OUT = (A+1)*B) >> 8
 * @PREMULT_ROUND_2: OUT = (A+(A>>7))* B) >> 8
 * @PREMULT_ROUND_3: TMP = A*8 + 0x80, OUT = (TMP + (TMP >> 8)) >> 8
 */
enum e_g2d_premult_round_mode {
	G2D_PREMULT_ROUND_0,
	G2D_PREMULT_ROUND_1,
	G2D_PREMULT_ROUND_2,
	G2D_PREMULT_ROUND_3  /* initial value */
};

/* YCbCr color value for fast solid fill ops. */
union g2d_ycbcr_val {
	unsigned int val;
	struct {
		/* [0:7] */
		unsigned int Cr:8;
		/* [15:8] */
		unsigned int Cb:8;
		/* [23:16] */
		unsigned int Y:8;

		unsigned int pad:8;
	} data;
};

union g2d_point_val {
	unsigned int val;
	struct {
		/*
		 * Coordinate of Source Image
		 * Range: 0 ~ 8000 (Requirement: SrcLeftX < SrcRightX)
		 * In YCbCr 422 and YCbCr 420 format with even number.
		 */
		unsigned int x:16;
		/*
		 * Y Coordinate of Source Image
		 * Range: 0 ~ 8000 (Requirement: SrcTopY < SrcBottomY)
		 * In YCbCr 420 format with even number.
		 */
		unsigned int y:16;
	} data;
};

/*
 * The unmasked_select field applies to g2d_rop4_val's unmasked_rop3
 * field. The masked_select field to masked_rop3 (ignored if no
 * masking is used.
 */
union g2d_third_op_val {
	unsigned int val;
	struct {
		enum e_g2d_third_op_select	unmasked_select:2;
		unsigned int			reserved1:2;
		enum e_g2d_third_op_select	masked_select:2;
		unsigned int			reserved2:26;
	} data;
};

union g2d_round_mode {
	unsigned int val;
	struct {
		enum e_g2d_blend_round_mode		blend_round:2;
		unsigned int				reserved1:2;
		enum e_g2d_premult_round_mode		premult_round:2;
		unsigned int				reserved2:26;
	} data;
};

union g2d_rop4_val {
	unsigned int val;
	struct {
		enum e_g2d_rop3_type	unmasked_rop3:8;
		enum e_g2d_rop3_type	masked_rop3:8;
		unsigned int		reserved:16;
	} data;
};

union g2d_pattern_size_val {
	unsigned int val;
	struct {
		/* Allowed range for width is 1 ~ 8000 (default = 1). */
		unsigned int		width:13;
		unsigned int		reserved1:3;

		/* Allowed range for height is 1 ~ 8000 (default = 1). */
		unsigned int		height:13;
		unsigned int		reserved2:3;
	} data;
};

union g2d_bitblt_cmd_val {
	unsigned int val;
	struct {
		/* [0:3] */
		unsigned int			mask_rop4_en:1;
		unsigned int			masking_en:1;
		enum e_g2d_select_alpha_src	rop4_alpha_en:1;
		unsigned int			dither_en:1;
		/* [4:7] */
		unsigned int			resolved1:4;
		/* [8:11] */
		unsigned int			cw_en:4;
		/* [12:15] */
		enum e_g2d_transparent_mode	transparent_mode:4;
		/* [16:19] */
		enum e_g2d_color_key_mode	color_key_mode:4;
		/* [20:23] */
		enum e_g2d_alpha_blend_mode	alpha_blend_mode:4;
		/* [24:27] */
		unsigned int src_pre_multiply:1;
		unsigned int pat_pre_multiply:1;
		unsigned int dst_pre_multiply:1;
		unsigned int dst_depre_multiply:1;
		/* [28:31] */
		unsigned int fast_solid_color_fill_en:1;
		unsigned int reserved:3;
	} data;
};

union g2d_blend_func_val {
	unsigned int val;
	struct {
		/* [0:15] */
		enum e_g2d_coeff_mode src_coeff:4;
		enum e_g2d_acoeff_mode src_coeff_src_a:2;
		enum e_g2d_acoeff_mode src_coeff_dst_a:2;
		enum e_g2d_coeff_mode dst_coeff:4;
		enum e_g2d_acoeff_mode dst_coeff_src_a:2;
		enum e_g2d_acoeff_mode dst_coeff_dst_a:2;
		/* [16:19] */
		unsigned int inv_src_color_coeff:1;
		unsigned int resoled1:1;
		unsigned int inv_dst_color_coeff:1;
		unsigned int resoled2:1;
		/* [20:23] */
		unsigned int lighten_en:1;
		unsigned int darken_en:1;
		unsigned int win_ce_src_over_en:2;
		/* [24:31] */
		unsigned int reserved:8;
	} data;
};

struct g2d_image {
	enum e_g2d_select_mode		select_mode;
	enum e_g2d_color_mode		color_mode;
	enum e_g2d_repeat_mode		repeat_mode;
	enum e_g2d_scale_mode		scale_mode;
	unsigned int			xscale;
	unsigned int			yscale;
	unsigned char			rotate_90;
	unsigned char			x_dir;
	unsigned char			y_dir;
	unsigned char			component_alpha;
	unsigned int			width;
	unsigned int			height;
	unsigned int			stride;
	unsigned int			need_free;
	unsigned int			color;
	enum e_g2d_buf_type		buf_type;
	unsigned int			bo[G2D_PLANE_MAX_NR];
	uint64_t			user_ptr[G2D_PLANE_MAX_NR];
	void				*mapped_ptr[G2D_PLANE_MAX_NR];
};

struct g2d_rect {
	uint16_t x;
	uint16_t y;
	uint16_t w;
	uint16_t h;
};

struct g2d_context;

/* Debug API */
int g2d_engine_hang(struct g2d_context *ctx);

struct g2d_context *g2d_init(int fd);
void g2d_fini(struct g2d_context *ctx);
int g2d_reset(struct g2d_context *ctx, enum e_g2d_reset_type type);
void g2d_config_event(struct g2d_context *ctx, void *userdata);
int g2d_exec(struct g2d_context *ctx);
int g2d_exec2(struct g2d_context *ctx, unsigned int flags);
int g2d_solid_fill(struct g2d_context *ctx, struct g2d_image *img,
			unsigned int x, unsigned int y, unsigned int w,
			unsigned int h);
int g2d_solid_fill_multi(struct g2d_context *ctx, struct g2d_image *img,
			const struct g2d_rect *rects, unsigned int num_rects);
int g2d_copy(struct g2d_context *ctx, struct g2d_image *src,
		struct g2d_image *dst, unsigned int src_x,
		unsigned int src_y, unsigned int dst_x, unsigned int dst_y,
		unsigned int w, unsigned int h);
int g2d_copy_multi(struct g2d_context *ctx, struct g2d_image *src,
		struct g2d_image *dst, const struct g2d_rect *src_rects,
		const struct g2d_rect *dst_rects, unsigned int num_rects);
int g2d_copy_rop(struct g2d_context *ctx, struct g2d_image *src,
		struct g2d_image *dst, unsigned int src_x, unsigned int src_y,
		unsigned int dst_x, unsigned dst_y, unsigned int w,
		unsigned int h, union g2d_rop4_val rop4,
		unsigned int use_third_reg);
int g2d_move(struct g2d_context *ctx, struct g2d_image *img,
		unsigned int src_x, unsigned int src_y, unsigned int dst_x,
		unsigned dst_y, unsigned int w, unsigned int h);
int g2d_move_multi(struct g2d_context *ctx, struct g2d_image *img,
		const struct g2d_rect *src_rects, const struct g2d_rect *dst_rects,
		unsigned int num_rects);
int g2d_copy_with_scale(struct g2d_context *ctx, struct g2d_image *src,
				struct g2d_image *dst, unsigned int src_x,
				unsigned int src_y, unsigned int src_w,
				unsigned int src_h, unsigned int dst_x,
				unsigned int dst_y, unsigned int dst_w,
				unsigned int dst_h, unsigned int negative);
int g2d_scale_rop_multi(struct g2d_context *ctx, struct g2d_image *src,
				struct g2d_image *dst, const struct g2d_rect *src_rects,
				const struct g2d_rect *dst_rects, unsigned int num_rects,
				union g2d_rop4_val rop4, unsigned int use_third_op);
int g2d_blend(struct g2d_context *ctx, struct g2d_image *src,
		struct g2d_image *dst, unsigned int src_x,
		unsigned int src_y, unsigned int dst_x, unsigned int dst_y,
		unsigned int w, unsigned int h, enum e_g2d_op op);
int g2d_scale_and_blend(struct g2d_context *ctx, struct g2d_image *src,
		struct g2d_image *dst, unsigned int src_x, unsigned int src_y,
		unsigned int src_w, unsigned int src_h, unsigned int dst_x,
		unsigned int dst_y, unsigned int dst_w, unsigned int dst_h,
		enum e_g2d_op op);
int g2d_userptr_register(struct g2d_context *ctx, void *addr, size_t size,
	unsigned long flags);
int g2d_userptr_unregister(struct g2d_context *ctx, void *addr);
int g2d_userptr_check_idle(struct g2d_context *ctx, void *addr);
#endif /* _FIMG2D_H_ */
