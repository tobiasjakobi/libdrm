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

#include <xf86drm.h>

#include "exynos_drm.h"
#include "exynos_drmif.h"
#include "exynos_fimg2d.h"

enum defaults {
	buf_width = 16,
	buf_height = 8
};

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

	g2d_fini(ctx);

g2d_fail:
	exynos_device_destroy(dev);

fail:
	drmClose(fd);

out:
	return ret;
}
