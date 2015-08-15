/*
 * Copyright (C) 2015 - Tobias Jakobi
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
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include <sys/mman.h>
#include <xf86drm.h>

#include "exynos_drm.h"
#include "exynos_drmif.h"
#include "exynos_fimg2d.h"

enum copy_direction {
	copy_userptr_to_gem,
	copy_gem_to_userptr
};

const uint32_t flag_combinations[] = {
	EXYNOS_BO_CONTIG | EXYNOS_BO_NONCACHABLE,
	EXYNOS_BO_CONTIG | EXYNOS_BO_CACHABLE,
	EXYNOS_BO_CONTIG | EXYNOS_BO_WC,
	EXYNOS_BO_NONCONTIG | EXYNOS_BO_NONCACHABLE,
	EXYNOS_BO_NONCONTIG | EXYNOS_BO_CACHABLE,
	EXYNOS_BO_NONCONTIG | EXYNOS_BO_WC
};

#define NUM_FLAG_COMBS (sizeof(flag_combinations) / sizeof(flag_combinations[0]))

static const char *flag_string(uint32_t f)
{
	switch (f) {
	case EXYNOS_BO_CONTIG | EXYNOS_BO_NONCACHABLE:
		return "contiguous/non-cachable";
	case EXYNOS_BO_CONTIG | EXYNOS_BO_CACHABLE:
		return "contiguous/cachable";
	case EXYNOS_BO_CONTIG | EXYNOS_BO_WC:
		return "contiguous/write-combining";
	case EXYNOS_BO_NONCONTIG | EXYNOS_BO_NONCACHABLE:
		return "non-contiguous/non-cachable";
	case EXYNOS_BO_NONCONTIG | EXYNOS_BO_CACHABLE:
		return "non-contiguous/cachable";
	case EXYNOS_BO_NONCONTIG | EXYNOS_BO_WC:
		return "non-contiguous/write-combining";
	}

	assert(0);
	return NULL;
}

/*
 * Fill a userspace buffer with random content.
 */
static void fill_userspace_random(void *buf, unsigned buf_size)
{
	unsigned int i;
	uint32_t *data;

	assert(buf_size % 4 == 0);
	data = buf;

	for (i = 0; i < buf_size / 4; ++i)
		data[i] = rand();
}

/*
 * Fill a GEM buffer with random content.
 */
static int fill_gem_random(struct exynos_bo *bo)
{
	void *buf;

	buf = exynos_bo_map(bo);
	if (buf == NULL)
		return -1;

	fill_userspace_random(buf, bo->size);

	if (exynos_bo_unmap(bo))
		return -2;

	return 0;
}

/*
 * Compare a GEM buffer with a userspace buffer.
 * The size is given by the GEM buffer.
 */
static int memcmp_gem_userspace(struct exynos_bo *bo, const void *ubuf)
{
	void *buf;
	int ret;

	buf = exynos_bo_map(bo);
	if (buf == NULL)
		return -1;

	ret = memcmp(buf, ubuf, bo->size);

	if (exynos_bo_unmap(bo))
		return -2;

	return ret;
}

/*
 * Compare two GEM buffers.
 * The size of the buffers has to match.
 */
static int memcmp_gem_gem(struct exynos_bo *bo1, struct exynos_bo *bo2)
{
	void *buf1, *buf2;
	int ret;

	assert(bo1->size == bo2->size);

	buf1 = exynos_bo_map(bo1);
	buf2 = exynos_bo_map(bo2);
	if (buf1 == NULL || buf2 == NULL)
		return -1;

	ret = memcmp(buf1, buf2, bo1->size);

	if (exynos_bo_unmap(bo1) || exynos_bo_unmap(bo2))
		return -2;

	return ret;
}

/*
 * Verify G2D copy operation from GEM to GEM buffer.
 */
static int copy_gem_gem(struct exynos_device *dev,
			struct g2d_context *ctx, unsigned buf_width, 
			unsigned buf_height, unsigned iterations, unsigned num_buffers,
			uint32_t src_gem_flags, uint32_t dst_gem_flags)
{
	struct g2d_image *img;
	struct exynos_bo **buffers;

	unsigned long bufsize;
	unsigned i, j, halfbuf;
	int ret = 0;

	assert(num_buffers % 2 == 0);

	buffers = calloc(num_buffers, sizeof(struct exynos_bo*));
	if (buffers == NULL) {
		fprintf(stderr, "error: buffers buffers allocation failed.\n");
		ret = -4;
		goto out;
	}

	bufsize = buf_width * buf_height * 4;
	halfbuf = num_buffers / 2;

	for (i = 0; i < num_buffers; ++i) {
		buffers[i] = exynos_bo_create(dev, bufsize,
			i < halfbuf ? src_gem_flags : dst_gem_flags);

		if (buffers[i] == NULL) {
			fprintf(stderr, "error: allocation of buffer %u failed.\n", i);
			ret = -5;
			break;
		}
	}

	if (i != num_buffers) {
		while (i-- > 0)
			exynos_bo_destroy(buffers[i]);
		goto fail_alloc;
	}

	img = calloc(num_buffers, sizeof(struct g2d_image));
	if (img == NULL) {
		fprintf(stderr, "error: G2D image allocation failed.\n");
		ret = -6;
		goto fail_img;
	}

	for (i = 0; i < num_buffers; ++i) {
		img[i].width = buf_width;
		img[i].height = buf_height;
		img[i].stride = buf_width * 4;
		img[i].color_mode = G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_AXRGB;
		img[i].buf_type = G2D_IMGBUF_GEM;
		img[i].bo[0] = buffers[i]->handle;
	}

	srand(time(NULL));

	printf("starting G2D GEM/GEM test\n");
	printf("source GEM = %s\n", flag_string(src_gem_flags));
	printf("destination GEM = %s\n", flag_string(dst_gem_flags));
	printf("buffer width = %u, buffer height = %u, iterations = %u\n",
		buf_width, buf_height, iterations);

	for (i = 0; i < iterations; ++i) {
		for (j = 0; j < halfbuf; ++j) {
			if (fill_gem_random(buffers[j])) {
				fprintf(stderr, "error: buffer fill %u failed in iteration %u\n", j, i);
				ret = -7;
				goto fail_test;
			}
		}

		for (j = 0; j < halfbuf; ++j) {
			ret = g2d_copy(ctx, &img[j], &img[j + halfbuf],
				0, 0, 0, 0, buf_width, buf_height);

			if (ret == 0)
				ret = g2d_exec(ctx);

			if (ret != 0) {
				fprintf(stderr, "error: buffer copy %u failed in iteration %u\n", j, i);
				ret = -8;
				goto fail_test;
			}
		}

		for (j = 0; j < halfbuf; ++j) {
			if (memcmp_gem_gem(buffers[j], buffers[j + halfbuf])) {
				fprintf(stderr, "error: buffer test %u failed in iteration %u\n", j, i);
				ret = -9;
				goto fail_test;
			}
		}
	}

	printf("test passed successfully\n\n");

fail_test:
	free(img);

fail_img:
	for (i = 0; i < num_buffers; ++i)
		exynos_bo_destroy(buffers[i]);

fail_alloc:
	free(buffers);

out:
	return ret;
}

/*
 * Verify G2D copy operation between userptr and GEM buffer.
 */
static int copy_userptr_gem(struct exynos_device *dev,
			struct g2d_context *ctx, unsigned buf_width,
			unsigned buf_height, unsigned iterations, unsigned num_buffers,
			enum copy_direction dir, uint32_t gem_flags)
{
	struct g2d_image *img;
	void **userspace_buffers;
	struct exynos_bo **gem_buffers;

	unsigned long bufsize, userptr_flags;
	unsigned i, j, halfbuf;
	int ret = 0;

	assert(num_buffers % 2 == 0);
	halfbuf = num_buffers / 2;

	userspace_buffers = calloc(halfbuf, sizeof(void*));
	if (userspace_buffers == NULL) {
		fprintf(stderr, "error: userspace buffers allocation failed.\n");
		ret = -4;
		goto out;
	}

	gem_buffers = calloc(halfbuf, sizeof(struct exynos_bo*));
	if (gem_buffers == NULL) {
		fprintf(stderr, "error: GEM buffers allocation failed.\n");
		free(userspace_buffers);
		ret = -4;
		goto out;
	}

	bufsize = buf_width * buf_height * 4;
	userptr_flags = (dir == copy_userptr_to_gem) ?
		G2D_USERPTR_FLAG_READ : G2D_USERPTR_FLAG_WRITE;

	for (i = 0; i < halfbuf; ++i) {
		userspace_buffers[i] = malloc(bufsize);

		if (userspace_buffers[i] == NULL) {
			fprintf(stderr, "error: allocation of userspace aligned buffer %u failed.\n", i);
			ret = -5;
			break;
		}
	}

	if (i != halfbuf) {
		while (i-- > 0)
			free(userspace_buffers[i]);
		goto fail_alloc;
	}

	for (i = 0; i < halfbuf; ++i) {
		gem_buffers[i] = exynos_bo_create(dev, bufsize, gem_flags);

		if (gem_buffers[i] == NULL) {
			fprintf(stderr, "error: allocation of GEM buffer %u failed.\n", i);
			ret = -5;
			break;
		}
	}

	if (i != halfbuf) {
		while (i-- > 0)
			exynos_bo_destroy(gem_buffers[i]);
		goto fail_alloc;
	}

	for (i = 0; i < halfbuf; ++i) {
		ret = g2d_userptr_register(ctx, userspace_buffers[i], bufsize, userptr_flags);

		if (ret) {
			fprintf(stderr, "error: userptr registration of buffer %u failed (%d).\n", i, ret);
			ret = -6;
			break;
		}
	}

	if (i != halfbuf) {
		while (i-- > 0)
			g2d_userptr_unregister(ctx, userspace_buffers[i]);
		goto fail_register;
	}

	img = calloc(num_buffers, sizeof(struct g2d_image));
	if (img == NULL) {
		fprintf(stderr, "error: G2D image allocation failed.\n");
		ret = -7;
		goto fail_img;
	}

	for (i = 0; i < num_buffers; ++i) {
		img[i].width = buf_width;
		img[i].height = buf_height;
		img[i].stride = buf_width * 4;
		img[i].color_mode = G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_AXRGB;

		if (i < halfbuf) {
			if (dir == copy_userptr_to_gem) {
				img[i].buf_type = G2D_IMGBUF_USERPTR;
				img[i].user_ptr[0] = (uint64_t)(uintptr_t)userspace_buffers[i];
			} else {
				img[i].buf_type = G2D_IMGBUF_GEM;
				img[i].bo[0] = gem_buffers[i]->handle;
			}
		} else {
			if (dir == copy_userptr_to_gem) {
				img[i].buf_type = G2D_IMGBUF_GEM;
				img[i].bo[0] = gem_buffers[i - halfbuf]->handle;
			} else {
				img[i].buf_type = G2D_IMGBUF_USERPTR;
				img[i].user_ptr[0] = (uint64_t)(uintptr_t)userspace_buffers[i - halfbuf];
			}
		}
	}

	srand(time(NULL));

	printf("starting G2D userptr/GEM test\n");
	printf("buffer width = %u, buffer height = %u, iterations = %u\n",
		buf_width, buf_height, iterations);
	printf("copy direction = %s\n", dir == copy_userptr_to_gem ?
		"userptr -> GEM" : "GEM -> userptr");
	printf("GEM buffer = %s\n", flag_string(gem_flags));

	for (i = 0; i < iterations; ++i) {
		for (j = 0; j < halfbuf; ++j) {
			if (dir == copy_userptr_to_gem) {
				fill_userspace_random(userspace_buffers[j], bufsize);
			} else {
				if (fill_gem_random(gem_buffers[j])) {
					fprintf(stderr, "error: buffer fill %u failed in iteration %u\n", j, i);
					ret = -8;
					goto fail_test;
				}
			}
		}

		for (j = 0; j < halfbuf; ++j) {
			ret = g2d_copy(ctx, &img[j], &img[j + halfbuf],
				0, 0, 0, 0, buf_width, buf_height);

			if (ret == 0)
				ret = g2d_exec(ctx);

			if (ret != 0) {
				fprintf(stderr, "error: buffer copy %u failed in iteration %u\n", j, i);
				ret = -9;
				goto fail_test;
			}
		}
		for (j = 0; j < halfbuf; ++j) {
			if (memcmp_gem_userspace(gem_buffers[j], userspace_buffers[j])) {
				fprintf(stderr, "error: buffer test %u failed in iteration %u\n", j, i);
				ret = -10;
				goto fail_test;
			}
		}
	}

	printf("test passed successfully\n\n");

fail_test:
	free(img);

fail_img:
	for (i = 0; i < halfbuf; ++i)
		g2d_userptr_unregister(ctx, userspace_buffers[i]);

fail_register:
	for (i = 0; i < halfbuf; ++i) {
		free(userspace_buffers[i]);
		exynos_bo_destroy(gem_buffers[i]);
	}

fail_alloc:
	free(userspace_buffers);
	free(gem_buffers);

out:
	return ret;
}

/*
 * Verify G2D copy operation from userptr to userptr.
 */
static int copy_userptr_userptr(struct g2d_context *ctx, unsigned buf_width, 
			unsigned buf_height, unsigned iterations, unsigned num_buffers)
{
	struct g2d_image *img;
	void **buffers;

	unsigned long bufsize, userptr_flags;
	unsigned i, j, halfbuf;
	int ret = 0;

	assert(num_buffers % 2 == 0);

	buffers = calloc(num_buffers, sizeof(void*));
	if (buffers == NULL) {
		fprintf(stderr, "error: buffers allocation failed.\n");
		ret = -4;
		goto out;
	}

	bufsize = buf_width * buf_height * 4;
	halfbuf = num_buffers / 2;

	for (i = 0; i < num_buffers; ++i) {
		buffers[i] = malloc(bufsize);

		if (buffers[i] == NULL) {
			fprintf(stderr, "error: allocation of aligned buffer %u failed.\n", i);
			ret = -5;
			break;
		}
	}

	if (i != num_buffers) {
		while (i-- > 0)
			free(buffers[i]);
		goto fail_alloc;
	}

	for (i = 0; i < num_buffers; ++i) {
		userptr_flags = (i < halfbuf) ? G2D_USERPTR_FLAG_READ : G2D_USERPTR_FLAG_WRITE;

		ret = g2d_userptr_register(ctx, buffers[i], bufsize, userptr_flags);

		if (ret) {
			fprintf(stderr, "error: userptr registration of buffer %u failed (%d).\n", i, ret);
			ret = -6;
			break;
		}
	}

	if (i != num_buffers) {
		while (i-- > 0)
			g2d_userptr_unregister(ctx, buffers[i]);
		goto fail_register;
	}

	img = calloc(num_buffers, sizeof(struct g2d_image));
	if (img == NULL) {
		fprintf(stderr, "error: G2D image allocation failed.\n");
		ret = -7;
		goto fail_img;
	}

	for (i = 0; i < num_buffers; ++i) {
		img[i].width = buf_width;
		img[i].height = buf_height;
		img[i].stride = buf_width * 4;
		img[i].color_mode = G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_AXRGB;
		img[i].buf_type = G2D_IMGBUF_USERPTR;
		img[i].user_ptr[0] = (uint64_t)(uintptr_t)buffers[i];
	}

	srand(time(NULL));

	printf("starting G2D userptr/userptr test\n");
	printf("buffer width = %u, buffer height = %u, iterations = %u\n",
		buf_width, buf_height, iterations);

	for (i = 0; i < iterations; ++i) {
		/* Fill the first half of the buffers with random content. */
		for (j = 0; j < halfbuf; ++j)
			fill_userspace_random(buffers[j], bufsize);

		/* Copy first half of buffers to second half. */
		for (j = 0; j < halfbuf; ++j) {
			ret = g2d_copy(ctx, &img[j], &img[j + halfbuf],
				0, 0, 0, 0, buf_width, buf_height);

			if (ret == 0)
				ret = g2d_exec(ctx);

			if (ret != 0) {
				fprintf(stderr, "error: buffer copy %u failed in iteration %u\n", j, i);
				ret = -8;
				goto fail_test;
			}
		}

		/* Check if buffers were copied without any errors. */
		for (j = 0; j < halfbuf; ++j) {
			if (memcmp(buffers[j], buffers[j + halfbuf], bufsize)) {
				fprintf(stderr, "error: buffer test %u failed in iteration %u\n", j, i);
				ret = -9;
				goto fail_test;
			}
		}
	}

	printf("test passed successfully\n\n");

fail_test:
	free(img);

fail_img:
	for (i = 0; i < num_buffers; ++i)
		g2d_userptr_unregister(ctx, buffers[i]);

fail_register:
	for (i = 0; i < num_buffers; ++i)
		free(buffers[i]);

fail_alloc:
	free(buffers);

out:
	return ret;
}

static void usage(const char *name)
{
	fprintf(stderr, "usage: %s [-ibwh]\n\n", name);

	fprintf(stderr, "\t-i <number of iterations>\n");
	fprintf(stderr, "\t-n <number of buffers> (default = 16)\n\n");

	fprintf(stderr, "\t-w <buffer width> (default = 512)\n");
	fprintf(stderr, "\t-h <buffer height> (default = 512)\n\n");

	exit(0);
}

int main(int argc, char **argv)
{
	int fd, ret, c, parsefail;

	struct exynos_device *dev;
	struct g2d_context *ctx;

	unsigned int iters = 0, numbuf = 16;
	unsigned int bufw = 512, bufh = 512;
	unsigned int i, j;

	ret = 0;
	parsefail = 0;

	while ((c = getopt(argc, argv, "i:n:w:h:")) != -1) {
		switch (c) {
		case 'i':
			if (sscanf(optarg, "%u", &iters) != 1)
				parsefail = 1;
			break;
		case 'n':
			if (sscanf(optarg, "%u", &numbuf) != 1)
				parsefail = 1;
			break;
		case 'w':
			if (sscanf(optarg, "%u", &bufw) != 1)
				parsefail = 1;
			break;
		case 'h':
			if (sscanf(optarg, "%u", &bufh) != 1)
				parsefail = 1;
			break;
		default:
			parsefail = 1;
			break;
		}
	}

	if (parsefail || (argc == 1) || (iters == 0))
		usage(argv[0]);

	if (bufw < 2 || bufw > 4096 || bufh < 2 || bufh > 4096) {
		fprintf(stderr, "error: buffer width/height should be in the range 2 to 4096.\n");
		ret = -1;

		goto out;
	}

	if (numbuf == 0 || (numbuf % 2 != 0)) {
		fprintf(stderr, "error: number of buffers has to be strictly positive and even.\n");
		ret = -1;

		goto out;
	}

	fd = drmOpen("exynos", NULL);
	if (fd < 0) {
		fprintf(stderr, "error: failed to open drm\n");
		ret = -1;

		goto out;
	}

	dev = exynos_device_create(fd);
	if (dev == NULL) {
		fprintf(stderr, "error: failed to create device\n");
		ret = -2;

		goto fail;
	}

	ctx = g2d_init(fd);
	if (ctx == NULL) {
		fprintf(stderr, "error: failed to init G2D\n");
		ret = -3;

		goto g2d_fail;
	}

	ret = copy_userptr_userptr(ctx, bufw, bufh, iters, numbuf);
	if (ret != 0)
		goto pass_fail;

	for (i = 0; i < NUM_FLAG_COMBS; ++i) {
		ret = copy_userptr_gem(dev, ctx, bufw, bufh, iters, numbuf,
			copy_userptr_to_gem, flag_combinations[i]);

		if (ret != 0)
			goto pass_fail;

		ret = copy_userptr_gem(dev, ctx, bufw, bufh, iters, numbuf,
			copy_gem_to_userptr, flag_combinations[i]);

		if (ret != 0)
			goto pass_fail;
	}

	for (i = 0; i < NUM_FLAG_COMBS; ++i) {
		for (j = 0; j < NUM_FLAG_COMBS; ++j) {
			ret = copy_gem_gem(dev, ctx, bufw, bufh, iters, numbuf,
				flag_combinations[i], flag_combinations[j]);

			if (ret != 0)
				goto pass_fail;
		}
	}

pass_fail:
	g2d_fini(ctx);

g2d_fail:
	exynos_device_destroy(dev);

fail:
	drmClose(fd);

out:
	return ret;
}
