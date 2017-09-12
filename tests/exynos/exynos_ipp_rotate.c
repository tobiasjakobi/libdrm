/*
 * Copyright (C) 2017 Samsung Electronics Co.Ltd
 * Author: Marek Szyprowski <m.szyprowski@samsung.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <xf86drm.h>
#include <drm_fourcc.h>

#include "exynos_drm.h"

struct bo {
	uint8_t *ptr;
	size_t size;
	size_t offset;
	size_t pitch;
	int width;
	int height;
	unsigned int handle;
	unsigned int fourcc;
};

static struct bo*
bo_create(int fd, unsigned int width, unsigned int height,
		  unsigned int bpp, unsigned int fourcc)
{
	struct drm_exynos_gem_create create_arg = { };
	struct drm_exynos_gem_map map_arg = { };
	struct bo *bo;

	bo = calloc(1, sizeof(*bo));
	if (bo == NULL) {
		fprintf(stderr, "failed to allocate buffer object\n");
		return NULL;
	}

	bo->size = width * height * bpp / 8;
	bo->pitch = width * bpp / 8;
	bo->width = width;
	bo->height = height;

	create_arg.size = bo->size;
	create_arg.flags = EXYNOS_BO_NONCONTIG;

	if (drmCommandWriteRead(fd, DRM_EXYNOS_GEM_CREATE, &create_arg,
				sizeof(create_arg))) {
		fprintf(stderr, "cannot create Exynos GEM object (%d): %m\n",
			errno);
		return NULL;
	}

	bo->handle = create_arg.handle;
	bo->fourcc = fourcc;

	map_arg.handle = bo->handle;

	if (drmCommandWriteRead(fd, DRM_EXYNOS_GEM_MAP, &map_arg,
				sizeof(map_arg))) {
		fprintf(stderr, "cannot map Exynos GEM object (%d): %m\n",
			errno);
		return NULL;
	}

	bo->ptr = mmap(0, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		       map_arg.offset);
	if (bo->ptr == MAP_FAILED)
		return NULL;

	return bo;
}

static void
bo_destroy(int fd, struct bo *bo)
{
	struct drm_gem_close arg = { };
	int ret;

	if (bo->ptr)
		munmap(bo->ptr, bo->size);

	arg.handle = bo->handle;
	ret = drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &arg);
	if (ret)
		fprintf(stderr, "failed to destroy dumb buffer (%d): %m\n",
			errno);

	free(bo);
}

static struct bo*
bo_create_xrgb(int fd, unsigned int width, unsigned int height)
{
	return bo_create(fd, width, height, 32, DRM_FORMAT_XRGB8888);
}

struct exynos_drm_ipp_std_task {
	struct drm_exynos_ipp_task_buffer buf[2];
	struct drm_exynos_ipp_task_rect rect[2];
	struct drm_exynos_ipp_task_transform transform;
} __packed;

static int
process_fb(int fd, struct bo *src_bo, int sx, int sy, int sw, int sh,
	       struct bo *dst_bo, int dx, int dy, int dw, int dh, int rotation)
{
	struct exynos_drm_ipp_std_task task = {	};
	struct drm_exynos_ioctl_ipp_commit arg = { };
	uint32_t id = 0; /* hardcoded first available IPP module */

	task.buf[0].id = DRM_EXYNOS_IPP_TASK_BUFFER |
			 DRM_EXYNOS_IPP_TASK_TYPE_SOURCE;
	task.buf[0].fourcc = src_bo->fourcc;
	task.buf[0].width = src_bo->width;
	task.buf[0].height = src_bo->height;
	task.buf[0].pitch[0] = src_bo->pitch;
	task.buf[0].gem_id[0] = src_bo->handle;

	task.buf[1].id = DRM_EXYNOS_IPP_TASK_BUFFER |
			 DRM_EXYNOS_IPP_TASK_TYPE_DESTINATION;
	task.buf[1].fourcc = dst_bo->fourcc;
	task.buf[1].width = dst_bo->width;
	task.buf[1].height = dst_bo->height;
	task.buf[1].pitch[0] = dst_bo->pitch;
	task.buf[1].gem_id[0] = dst_bo->handle;

	task.rect[0].id = DRM_EXYNOS_IPP_TASK_RECTANGLE |
			  DRM_EXYNOS_IPP_TASK_TYPE_SOURCE;
	task.rect[0].x = sx;
	task.rect[0].y = sy;
	task.rect[0].w = sw;
	task.rect[0].h = sh;

	task.rect[1].id = DRM_EXYNOS_IPP_TASK_RECTANGLE |
			  DRM_EXYNOS_IPP_TASK_TYPE_DESTINATION;
	task.rect[1].x = dx;
	task.rect[1].y = dy;
	task.rect[1].w = dw;
	task.rect[1].h = dh;

	task.transform.id = DRM_EXYNOS_IPP_TASK_TRANSFORM;
	task.transform.rotation = rotation;

	arg.flags = 0;
	arg.ipp_id = id;
	arg.params_size = sizeof(task);
	arg.params_ptr = (unsigned long)(&task);
	arg.user_data = 0;

	if (drmCommandWriteRead(fd, DRM_EXYNOS_IPP_COMMIT, &arg, sizeof(arg))) {
		fprintf(stderr, "failed to commit Exynos IPP task (%d): %m\n",
			errno);
		return errno;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	struct bo *buf1, *buf2;
	int fd;
	int err;
	int width = 640;
	int height = 480;
	int val = 1, x, y;

	fd = open("/dev/dri/card0", O_RDWR);

	buf1 = bo_create_xrgb(fd, width, height);
	buf2 = bo_create_xrgb(fd, width, height);

	/* draw test pattern to buffer1 */
	for (y = 0; y < height; y++) {
		uint32_t *p = (uint32_t*)(buf1->ptr + buf1->pitch * y);

		for (x = 0; x < width; x++) {
			if (x > y)
				*p++ = (val++ & 0xffffff) + 0xff000000;
		}
	}

	err = process_fb(fd, buf1, 0, 0, width, height, buf2, 0, 0, width,
			 height, DRM_MODE_ROTATE_180);
	if (!err) {
		printf("Buffer processed, checking processed buffer... ");

		for (y = 0; y < height; y++) {
			uint32_t *p1 = (uint32_t*)(buf1->ptr + buf1->pitch * y);
			uint32_t *p2 = (uint32_t*)(buf2->ptr + buf1->pitch * (height - y - 1));

			for (x = 0; x < width; x++)
				if (*(p1 + x) != *(p2 + width - x - 1)) {
					printf("failed at (%d,%d) %06x != %06x.\n",
						x, y, *(p1 + x),
						*(p2 + width - x - 1));
					goto free;
				}
		}
		printf("okay.\n");
	}

free:
	bo_destroy(fd, buf1);
	bo_destroy(fd, buf2);

	return 0;
}
