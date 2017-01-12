/*
 * Copyright (C) 2016 - Tobias Jakobi
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 2 of the License,
 * or (at your option) any later version.
 *
 * It is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with it. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <getopt.h>
#include <errno.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include <xf86drm.h>

#include "exynos_drm.h"
#include "exynos_drmif.h"
#include "exynos_fimg2d.h"

enum defaults {
	buf_width = 16,
	buf_height = 8
};

static void set_userspace(void *buf, unsigned elem_size,
							unsigned pos, uint32_t value)
{
	switch (elem_size) {
	case 1:
	{
		uint8_t *buf8 = buf;
		buf8[pos] = (uint8_t)value;
	}
	break;

	case 2:
	{
		uint16_t *buf16 = buf;
		buf16[pos] = (uint16_t)value;
	}
	break;

	case 4:
	{
		uint32_t *buf32 = buf;
		buf32[pos] = (uint32_t)value;
	}
	break;

	default:
		assert(false);
		break;
	}
}

static void fill_userspace(void *buf, unsigned elem_size,
							unsigned buf_size, uint32_t value) {
	unsigned int i;

	switch (elem_size) {
		case 1:
		{
			uint8_t *buf8 = buf;
			uint8_t val8 = (uint8_t)value;

			for (i = 0; i < buf_size; ++i)
				buf8[i] = val8;
		}
		break;

		case 4:
		{
			uint32_t *buf32 = buf;
			uint32_t val32 = value;

			assert(buf_size % 4 == 0);

			for (i = 0; i < buf_size / 4; ++i)
				buf32[i] = val32;
		}
		break;

		default:
			assert(false);
			break;
	}
}

static void print_userspace(void *buf, unsigned elem_size,
							unsigned buf_w, unsigned buf_h) {
	unsigned i, j;
	uint8_t *buf8;
	uint16_t *buf16;
	uint32_t *buf32;

	switch (elem_size) {
	case 1:
		buf8 = buf;

		for (i = 0; i < buf_h; ++i) {
			for (j = 0; j < buf_w; ++j)
				fprintf(stderr, "0x%02x ", (uint32_t)buf8[i * buf_w + j]);
			fprintf(stderr, "\n");
		}
	break;

	case 2:
		buf16 = buf;

		for (i = 0; i < buf_h; ++i) {
			for (j = 0; j < buf_w; ++j)
				fprintf(stderr, "0x%04x ", (uint32_t)buf16[i * buf_w + j]);
			fprintf(stderr, "\n");
		}
	break;

	case 4:
		buf32 = buf;

		for (i = 0; i < buf_h; ++i) {
			for (j = 0; j < buf_w; ++j)
				fprintf(stderr, "0x%08x ", (uint32_t)buf32[i * buf_w + j]);
			fprintf(stderr, "\n");
		}
	break;

	default:
		assert(false);
	break;
	}
}

static int fimg2d_rop(struct g2d_context *ctx, struct exynos_device *dev)
{
	struct g2d_image src_img = { 0 }, dst_img = { 0 };
	struct exynos_bo *src_bo, *dst_bo;
	void *src_buf, *dst_buf;

	union g2d_rop4_val rop4;

	const unsigned num_pixels = buf_width * buf_height;
	int ret = 0;

	src_bo = exynos_bo_create(dev, num_pixels * sizeof(uint8_t), 0);
	dst_bo = exynos_bo_create(dev, num_pixels * sizeof(uint32_t), 0);

	if (!src_bo || !dst_bo) {
		fprintf(stderr, "error: failed to create buffer objects\n");
		ret = -4;

		goto bo_fail;
	}

	src_img.width = buf_width;
	src_img.height = buf_height;
	src_img.stride = buf_width * sizeof(uint8_t);
	src_img.color_mode = G2D_COLOR_FMT_A8;
	src_img.buf_type = G2D_IMGBUF_GEM;
	src_img.bo[0] = src_bo->handle;

	dst_img.width = buf_width;
	dst_img.height = buf_height;
	dst_img.stride = buf_width * sizeof(uint32_t);
	dst_img.color_mode = G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_AXRGB;;
	dst_img.buf_type = G2D_IMGBUF_GEM;
	dst_img.bo[0] = dst_bo->handle;

	src_buf = exynos_bo_map(src_bo);
	dst_buf = exynos_bo_map(dst_bo);

	fill_userspace(src_buf, sizeof(uint8_t), num_pixels * sizeof(uint8_t), 0x80);
	fill_userspace(dst_buf, sizeof(uint32_t), num_pixels * sizeof(uint32_t), 0xffbadf0d);

	rop4.val = 0;
	rop4.data.unmasked_rop3 = G2D_ROP3_DST;

	/*
	 * This combines the alpha values from the source image with the
	 * RGB values from the destination image.
	 */
	ret = g2d_copy_rop(ctx, &src_img, &dst_img, 0, 0, 0, 0, buf_width, buf_height, rop4, 0);

	if (!ret)
		ret = g2d_exec(ctx);

	if (!ret)
		print_userspace(dst_buf, sizeof(uint32_t), buf_width, buf_height);

	rop4.data.unmasked_rop3 = G2D_ROP3_3RD;
	src_img.color = 0xffa4e1d8;

	/*
	 * This extends the alpha values from the source image with the custom color
	 * of the source image and writes the output to the destination image.
	 */
	ret = g2d_copy_rop(ctx, &src_img, &dst_img, 0, 0, 0, 0, buf_width, buf_height, rop4, 1);

	if (!ret)
		ret = g2d_exec(ctx);

	if (!ret)
		print_userspace(dst_buf, sizeof(uint32_t), buf_width, buf_height);

bo_fail:
	exynos_bo_destroy(src_bo);
	exynos_bo_destroy(dst_bo);

	return ret;
}

static int fimg2d_clear2(struct g2d_context *ctx, struct exynos_device *dev)
{
	struct g2d_image img = { 0 };
	struct exynos_bo *bo;
	void *buf;
	struct g2d_rect rects[4];

	const unsigned num_pixels = buf_width * buf_height;
	int ret = 0;

	bo = exynos_bo_create(dev, num_pixels * sizeof(uint32_t), 0);

	if (!bo) {
		fprintf(stderr, "error: failed to create buffer object\n");
		ret = -4;

		goto bo_fail;
	}

	img.width = buf_width;
	img.height = buf_height;
	img.stride = buf_width * sizeof(uint32_t);
	img.color_mode = G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_AXRGB;
	img.buf_type = G2D_IMGBUF_GEM;
	img.bo[0] = bo->handle;

	buf = exynos_bo_map(bo);

	rects[0].x = rects[0].y = 0;
	rects[1].x = 0;
	rects[1].y = buf_height / 2;
	rects[2].x = buf_width / 2;
	rects[2].y = 0;
	rects[3].x = buf_width / 2;
	rects[3].y = buf_height / 2;

	rects[0].w = rects[1].w = rects[2].w = rects[3].w = buf_width / 2;
	rects[0].h = rects[1].h = rects[2].h = rects[3].h = buf_height / 2;

	fprintf(stderr, "Doing 1st buffer clear using solid fill...\n");

	/*
	 * Clear the buffer with a single clear rectangle.
	 */
	img.color = 0xff0aff0b;
	ret = g2d_solid_fill(ctx, &img, 0, 0, buf_width, buf_height);

	if (!ret)
		ret = g2d_exec(ctx);

	if (!ret)
		print_userspace(buf, sizeof(uint32_t), buf_width, buf_height);

	fprintf(stderr, "Doing 2nd buffer clear using solid fill (multi)...\n");

	/*
	 * Clear the buffer with a four clear rectangles.
	 */
	img.color = 0xff88cd22;
	ret = g2d_solid_fill_multi(ctx, &img, rects, 4);

	if (!ret)
		ret = g2d_exec(ctx);

	if (!ret)
		print_userspace(buf, sizeof(uint32_t), buf_width, buf_height);

bo_fail:
	exynos_bo_destroy(bo);

	return ret;
}

static int fimg2d_reset(struct g2d_context *ctx, struct exynos_device *dev)
{
	int ret;

	ret = g2d_reset(ctx, G2D_RESET_LOCAL);
	if (ret < 0) {
		fprintf(stderr, "error: failed to issue local reset\n");
		goto out;
	}

	ret = g2d_engine_hang(ctx);
	if (ret == 0)
		ret = g2d_exec(ctx);

	if (ret < 0) {
		fprintf(stderr, "error: failed to submit \"engine hang\" command\n");
		goto out;
	}

	ret = g2d_reset(ctx, G2D_RESET_GLOBAL);
	if (ret < 0) {
		fprintf(stderr, "error: failed to issue full reset\n");
		goto out;
	}

out:
	return ret;
}

static int fimg2d_ycbcr(struct g2d_context *ctx, struct exynos_device *dev)
{
	struct g2d_image src_img = { 0 }, dst_img = { 0 };
	struct exynos_bo *src_bo, *dst_bo, *plane2_bo;
	void *src_buf, *dst_buf, *plane2_buf;

	const unsigned num_pixels = buf_width * buf_height;
	int ret = 0;

	src_bo = exynos_bo_create(dev, num_pixels * sizeof(uint32_t), 0);
	dst_bo = exynos_bo_create(dev, num_pixels * sizeof(uint16_t), 0);
	plane2_bo = exynos_bo_create(dev, num_pixels * sizeof(uint16_t), 0);

	if (!src_bo || !dst_bo || !plane2_bo) {
		fprintf(stderr, "error: failed to create buffer object\n");
		ret = -4;

		goto fail;
	}

	src_img.width = buf_width;
	src_img.height = buf_height;
	src_img.stride = buf_width * sizeof(uint32_t);
	src_img.color_mode = G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_AXRGB;
	src_img.buf_type = G2D_IMGBUF_GEM;
	src_img.bo[0] = src_bo->handle;

	/* For YCbCr buffers the stride parameter has to match the buffer width. */
	dst_img.width = buf_width;
	dst_img.height = buf_height;
	dst_img.stride = buf_width;
	dst_img.buf_type = G2D_IMGBUF_GEM;
	dst_img.bo[0] = dst_bo->handle;
	dst_img.bo[1] = plane2_bo->handle;

	src_buf = exynos_bo_map(src_bo);
	dst_buf = exynos_bo_map(dst_bo);
	plane2_buf = exynos_bo_map(plane2_bo);

	memset(dst_buf, 0x00, num_pixels * sizeof(uint16_t));
	memset(plane2_buf, 0x00, num_pixels * sizeof(uint16_t));

	src_img.color = 0xff808080;
	ret = g2d_solid_fill(ctx, &src_img, 0, 0, buf_width, buf_height);

	if (!ret)
		ret = g2d_exec(ctx);

	/* Set the first three pixels to different values. */
	set_userspace(src_buf, 4, 0, 0xff000080);
	set_userspace(src_buf, 4, 1, 0xff008000);
	set_userspace(src_buf, 4, 2, 0xff800000);

	if (ret) {
		fprintf(stderr, "error: initial solid fill of source buffer failed\n");
		ret = -5;

		goto fail;
	}

	fprintf(stderr, "\nDoing YCbCr422 (uniplanar) copy...\n");

	dst_img.color_mode = G2D_COLOR_FMT_YCbCr422 | G2D_YCbCr_1PLANE;
	ret = g2d_copy(ctx, &src_img, &dst_img, 2, 0, 2, 0, buf_width - 2, buf_height);

	if (!ret)
		ret = g2d_exec(ctx);

	if (!ret) {
		fprintf(stderr, "YCbCr buffer content (YCbCr):\n");
		print_userspace(dst_buf, sizeof(uint16_t), buf_width, buf_height);
	}

	if (ret) {
		fprintf(stderr, "error: YCbCr422 (uniplanar) pass failed\n");
		ret = -6;

		goto fail;
	}

	memset(dst_buf, 0x00, num_pixels * sizeof(uint16_t));
	memset(plane2_buf, 0x00, num_pixels * sizeof(uint16_t));

	fprintf(stderr, "\nDoing YCbCr444 (biplanar) copy...\n");

	dst_img.color_mode = G2D_COLOR_FMT_YCbCr444 | G2D_YCbCr_2PLANE;
	ret = g2d_copy(ctx, &src_img, &dst_img, 2, 2, 2, 2, buf_width - 2, buf_height - 2);

	if (!ret)
		ret = g2d_exec(ctx);

	if (!ret) {
		fprintf(stderr, "YCbCr buffer content (plane1 / Y):\n");
		print_userspace(dst_buf, sizeof(uint8_t), buf_width, buf_height);

		fprintf(stderr, "YCbCr buffer content (plane2 / CbCr):\n");
		print_userspace(plane2_buf, sizeof(uint16_t), buf_width, buf_height);
	}

	if (ret) {
		fprintf(stderr, "error: YCbCr444 (biplanar) pass failed\n");
		ret = -7;

		goto fail;
	}

	memset(dst_buf, 0x00, num_pixels * sizeof(uint16_t));
	memset(plane2_buf, 0x00, num_pixels * sizeof(uint16_t));

	fprintf(stderr, "\nDoing YCbCr422 (biplanar) copy...\n");

	dst_img.color_mode = G2D_COLOR_FMT_YCbCr422 | G2D_YCbCr_2PLANE;
	ret = g2d_copy(ctx, &src_img, &dst_img, 0, 2, 0, 2, buf_width, buf_height - 2);

	if (!ret)
		ret = g2d_exec(ctx);

	if (!ret) {
		fprintf(stderr, "YCbCr buffer content (plane1 / Y):\n");
		print_userspace(dst_buf, sizeof(uint8_t), buf_width, buf_height);

		fprintf(stderr, "YCbCr buffer content (plane2 / CbCr):\n");
		print_userspace(plane2_buf, sizeof(uint16_t), buf_width / 2, buf_height);
	}

	if (ret) {
		fprintf(stderr, "error: YCbCr422 (biplanar) pass failed\n");
		ret = -7;

		goto fail;
	}

	memset(dst_buf, 0x00, num_pixels * sizeof(uint16_t));
	memset(plane2_buf, 0x00, num_pixels * sizeof(uint16_t));

	fprintf(stderr, "\nDoing YCbCr420 (biplanar) copy...\n");

	dst_img.color_mode = G2D_COLOR_FMT_YCbCr420 | G2D_YCbCr_2PLANE;
	ret = g2d_copy(ctx, &src_img, &dst_img, 0, 0, 0, 0, buf_width, buf_height);

	if (!ret)
		ret = g2d_exec(ctx);

	if (!ret) {
		fprintf(stderr, "YCbCr buffer content (plane1 / Y):\n");
		print_userspace(dst_buf, sizeof(uint8_t), buf_width, buf_height);

		fprintf(stderr, "YCbCr buffer content (plane2 / CbCr):\n");
		print_userspace(plane2_buf, sizeof(uint16_t), buf_width / 2, buf_height / 2);
	}

	if (ret) {
		fprintf(stderr, "error: YCbCr420 (biplanar) pass failed\n");
		ret = -8;
	}

fail:
	exynos_bo_destroy(src_bo);
	exynos_bo_destroy(dst_bo);
	exynos_bo_destroy(plane2_bo);

	return ret;
}

int main(int argc, char **argv)
{
	int fd, ret;

	struct exynos_device *dev;
	struct g2d_context *ctx;

	ret = 0;

	fd = drmOpen("exynos", NULL);
	if (fd < 0) {
		fprintf(stderr, "error: failed to open drm\n");
		ret = -1;

		goto out;
	}

	dev = exynos_device_create(fd);
	if (!dev) {
		fprintf(stderr, "error: failed to create device\n");
		ret = -2;

		goto fail;
	}

	ctx = g2d_init(fd);
	if (!ctx) {
		fprintf(stderr, "error: failed to init G2D\n");
		ret = -3;

		goto g2d_fail;
	}

	ret = fimg2d_rop(ctx, dev);

	if (!ret)
		ret = fimg2d_clear2(ctx, dev);

	if (!ret)
		ret = fimg2d_reset(ctx, dev);

	if (!ret)
		ret = fimg2d_ycbcr(ctx, dev);

	g2d_fini(ctx);

g2d_fail:
	exynos_device_destroy(dev);

fail:
	drmClose(fd);

out:
	return ret;
}
