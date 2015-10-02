/*
 * Copyright (C) 2017 - Tobias Jakobi
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
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include <time.h>
#include <getopt.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>

#include <inttypes.h>

#include <xf86drm.h>
#include <drm_fourcc.h>

#include "exynos_drm.h"
#include "exynos_drmif.h"
#include "exynos_ipp.h"

static unsigned int active_jobs = 0;
static bool print_limits = true;

struct ipp_job {
	struct ipp_task *task;
	struct exynos_bo *src_bo;
	struct exynos_bo *dst_bo;

	bool busy;
};

static const char*
fmt_type_to_str(const struct ipp_format *fmt)
{
	if (fmt->type_src && fmt->type_dst)
		return "source+destination";
	else if (fmt->type_src)
		return "source";
	else if (fmt->type_dst)
		return "destination";
	else
		return "unknown";
}

static const char*
lmt_type_to_str(const struct ipp_limit *lmt)
{
	switch (lmt->type) {
	case ipp_limit_size:
		return "size (horizontal/vertical) in pixels";
	case ipp_limit_scale:
		return "scale ratio (horizontal/vertical) in 16.16 fp";
	default:
		return "unknown";
	}
}

static const char*
lmt_size_to_str(const struct ipp_limit *lmt)
{
	switch (lmt->size) {
	case ipp_limit_buffer:
		return "image buffer area";
	case ipp_limit_area:
		return "rectangle area";
	case ipp_limit_rotated:
		return "rectangle area (when rotated)";
	default:
		return "unknown";
	}
}

static const char*
fourcc_to_str(uint32_t fourcc)
{
	static char buf[5];
	buf[4] = '\0';

	buf[0] = (fourcc >>  0) & 0xff;
	buf[1] = (fourcc >>  8) & 0xff;
	buf[2] = (fourcc >> 16) & 0xff;
	buf[3] = (fourcc >> 24) & 0xff;

	return buf;
}

static void
print_fmt_limits(const struct ipp_format *fmt, bool is_source)
{
	unsigned i;
	unsigned lmt_idx;
	const char* lmt_str;

	lmt_idx = is_source ? 0 : 1;
	lmt_str = is_source ? "source" : "destination";

	if (!fmt->num_limits[lmt_idx]) {
		fprintf(stderr, "\t\tformat has no %s limits\n", lmt_str);
		return;
	}

	for (i = 0; i < fmt->num_limits[lmt_idx]; ++i) {
		const struct ipp_limit *lmt = &fmt->limits[lmt_idx][i];

		fprintf(stderr, "\t\t%s limit: type = %s, size = %s\n", lmt_str,
			lmt_type_to_str(lmt), lmt_size_to_str(lmt));
		fprintf(stderr, "\t\th_min = %u, h_max = %u, h_align = %u\n",
			lmt->h_min, lmt->h_max, lmt->h_align);
		fprintf(stderr, "\t\tv_min = %u, v_max = %u, v_align = %u\n",
			lmt->v_min, lmt->v_max, lmt->v_align);
	}
}

static void
print_fmt_info(const struct ipp_format *fmt)
{
	fprintf(stderr, "\tfourcc: %s, type: %s, modifier: 0x%" PRIx64 "\n",
			fourcc_to_str(fmt->fourcc), fmt_type_to_str(fmt),
			fmt->modifier);

	if (print_limits) {
		print_fmt_limits(fmt, true);
		print_fmt_limits(fmt, false);
	}
}

/*
 * Print information about the IPP module to stderr.
 */
static void
print_ipp_info(const struct ipp_module *ipp)
{
	unsigned i;

	fprintf(stderr, "\tid = %u\n", ipp->id);

	if (ipp->cap_crop)
		fprintf(stderr, "\tsupports crop\n");

	if (ipp->cap_rotate)
		fprintf(stderr, "\tsupports rotate\n");

	if (ipp->cap_scale)
		fprintf(stderr, "\tsupports scale\n");

	if (ipp->cap_convert)
		fprintf(stderr, "\tsupports convert\n");

	fprintf(stderr, "a total of %u formats are supported:\n",
		ipp->num_formats);

	for (i = 0; i < ipp->num_formats; ++i)
		print_fmt_info(&ipp->formats[i]);
}

/*
 * Fill a GEM buffer with random content.
 */
static int
fill_gem_random(struct exynos_bo *bo)
{
	unsigned i;
	uint32_t *buf;

	buf = exynos_bo_map(bo);
	if (buf == NULL)
		return -1;

	assert(bo->size % 4 == 0);

	for (i = 0; i < bo->size / 4; ++i)
		buf[i] = rand();

	return 0;
}

/*
 * Compare two GEM buffers.
 * The size of the buffers has to match.
 */
static int
memcmp_gem_gem(struct exynos_bo *bo1, struct exynos_bo *bo2)
{
	void *buf1, *buf2;
	int ret;

	assert(bo1->size == bo2->size);

	buf1 = exynos_bo_map(bo1);
	buf2 = exynos_bo_map(bo2);
	if (buf1 == NULL || buf2 == NULL)
		return -1;

	ret = memcmp(buf1, buf2, bo1->size);

	return ret;
}

static void
ipp_handler(int fd, unsigned ipp_id, unsigned sequence, unsigned tv_sec,
	unsigned tv_usec, void *user_data)
{
	struct ipp_job *job;

	if (user_data) {
		job = user_data;

		assert(job->busy);

		job->busy = false;
	}

	--active_jobs;

	fprintf(stderr, "IPP handler:\n");

	fprintf(stderr, "\tfd = %d, ipp_id = %u, seq = %u, tv_sec = %u, tv_usec = %u\n",
		fd, ipp_id, sequence, tv_sec, tv_usec);
	fprintf(stderr, "\tuser_data = %p\n", user_data);
}

static void
print_rgb565_diff(uint16_t x, uint16_t y)
{
	const uint16_t blue_mask = 0x1f;
	const uint16_t green_mask = 0x7e0;
	const uint16_t red_mask = 0xf800;

	fprintf(stderr, "(r:%d|g:%d,|b:%d) ",
		(int32_t)((x & red_mask)  >> 11) - (int32_t)((y & red_mask)  >> 11),
		(int32_t)((x & green_mask) >> 5) - (int32_t)((y & green_mask) >> 5),
		(int32_t)((x & blue_mask)  >> 0) - (int32_t)((y & blue_mask)  >> 0));
}

static void
print_xrgb8888_diff(uint32_t x, uint32_t y)
{
	fprintf(stderr, "(x:%d|r:%d,|g:%d|b:%d) ",
		(int32_t)((x & 0xff000000) >> 24) - (int32_t)((y & 0xff000000) >> 24),
		(int32_t)((x & 0x00ff0000) >> 16) - (int32_t)((y & 0x00ff0000) >> 16),
		(int32_t)((x & 0x0000ff00) >>  8) - (int32_t)((y & 0x0000ff00) >>  8),
		(int32_t)((x & 0x000000ff) >>  0) - (int32_t)((y & 0x000000ff) >>  0));
}

static unsigned
rgb565_diff(uint16_t x, uint16_t y)
{
	const uint16_t blue_mask = 0x1f;
	const uint16_t green_mask = 0x7e0;
	const uint16_t red_mask = 0xf800;

	return
		abs((int32_t)((x & red_mask)  >> 11) - (int32_t)((y & red_mask)  >> 11)) +
		abs((int32_t)((x & green_mask) >> 5) - (int32_t)((y & green_mask) >> 5)) +
		abs((int32_t)((x & blue_mask)  >> 0) - (int32_t)((y & blue_mask)  >> 0));
}

static unsigned
xrgb8888_diff(uint32_t x, uint32_t y)
{
	/* We ignore the X-component here. */
	return
		abs((int32_t)((x & 0x00ff0000) >> 16) - (int32_t)((y & 0x00ff0000) >> 16)) +
		abs((int32_t)((x & 0x0000ff00) >>  8) - (int32_t)((y & 0x0000ff00) >>  8)) +
		abs((int32_t)((x & 0x000000ff) >>  0) - (int32_t)((y & 0x000000ff) >>  0));
}

static int
fuzzy_memcmp_rgb565(struct exynos_bo *bo1, struct exynos_bo *bo2)
{
	uint16_t *buf1, *buf2;
	unsigned i;

	assert(bo1->size == bo2->size);
	assert(bo1->size % 2 == 0);

	buf1 = exynos_bo_map(bo1);
	buf2 = exynos_bo_map(bo2);
	if (buf1 == NULL || buf2 == NULL)
		return -1;

	for (i = 0; i < bo1->size / 2; ++i) {
		if (rgb565_diff(buf1[i], buf2[i]) > 3)
			return -2;
	}

	return 0;
}

static int
fuzzy_memcmp_xrgb8888(struct exynos_bo *bo1, struct exynos_bo *bo2)
{
	uint32_t *buf1, *buf2;
	unsigned i;

	assert(bo1->size == bo2->size);
	assert(bo1->size % 4 == 0);

	buf1 = exynos_bo_map(bo1);
	buf2 = exynos_bo_map(bo2);
	if (buf1 == NULL || buf2 == NULL)
		return -1;

	for (i = 0; i < bo1->size / 4; ++i) {
		if (xrgb8888_diff(buf1[i], buf2[i]) > 5)
			return -2;
	}

	return 0;
}

static void
viscmp_xrgb8888(struct exynos_bo *bo1, struct exynos_bo *bo2,
				unsigned w, unsigned h)
{
	uint32_t *buf1, *buf2;
	unsigned unperfect;
	unsigned i, j;

	assert(bo1->size == bo2->size);
	assert(bo1->size % 4 == 0);

	buf1 = exynos_bo_map(bo1);
	buf2 = exynos_bo_map(bo2);
	if (buf1 == NULL || buf2 == NULL)
		return;

	unperfect = 0;

	for (j = 0; j < h; ++j) {
		for (i = 0; i < w; ++i) {
			const unsigned idx = j * w + i;

			if ((buf1[idx] & 0xffffff) != (buf2[idx] & 0xffffff)) {
				fprintf(stderr, "%u ", xrgb8888_diff(buf1[idx], buf2[idx]));
				unperfect++;
			} else {
				fprintf(stderr, "0 ");
			}
		}
		fprintf(stderr, "\n");
	}

	fprintf(stderr, "\n");
	fprintf(stderr, "%u unperfect pixels \n", unperfect);
}

static bool
is_csc_supported(const struct ipp_module *mod, uint32_t src_fourcc,
				 uint32_t dst_fourcc)
{
	unsigned i;
	bool src_found = false, dst_found = false;

	for (i = 0; i < mod->num_formats; ++i) {
		const struct ipp_format *fmt = &mod->formats[i];

		if (fmt->fourcc == src_fourcc && fmt->type_src)
			src_found = true;

		if (fmt->fourcc == dst_fourcc && fmt->type_dst)
			dst_found = true;

		if (src_found && dst_found)
			break;
	}

	return src_found && dst_found;
}

static void
usage(const char *name)
{
	fprintf(stderr, "usage: %s [-itnwh]\n\n", name);

	fprintf(stderr, "\t-i <number of iterations>\n");
	fprintf(stderr, "\t-t <number of tasks> (default = 3)\n\n");
	fprintf(stderr, "\t-n <IPP module index> (default = 0)\n\n");

	fprintf(stderr, "\t-w <buffer width> (default = 64)\n");
	fprintf(stderr, "\t-h <buffer height> (default = 64)\n\n");

	exit(0);
}

int main(int argc, char **argv)
{
	int fd, ret, c, parsefail;
	unsigned i, jobidx;

	struct exynos_device *dev;
	struct ipp_context *ctx;
	struct ipp_task **tasks;
	unsigned num_modules;

	struct ipp_job *jobs;

	struct pollfd fds;
	struct exynos_event_context evctx = { .base = {0}, 0 };

	unsigned iters = 0, numtasks = 3, ipp_idx = 0;
	unsigned bufw = 64, bufh = 64;

	ret = 0;
	parsefail = 0;

	while ((c = getopt(argc, argv, "i:t:n:w:h:")) != -1) {
		switch (c) {
		case 'i':
			if (sscanf(optarg, "%u", &iters) != 1)
				parsefail = 1;
			break;
		case 't':
			if (sscanf(optarg, "%u", &numtasks) != 1)
				parsefail = 1;
			break;
		case 'n':
			if (sscanf(optarg, "%u", &ipp_idx) != 1)
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

	if (parsefail || (argc == 1) || (iters == 0) || (numtasks == 0))
		usage(argv[0]);

	if (bufw < 2 || bufw > 2048 || bufh < 2 || bufh > 2048) {
		fprintf(stderr, "error: buffer width/height should be in the range 2 to 2048.\n");
		ret = -1;

		goto out;
	}

	fd = drmOpen("exynos", NULL);
	if (fd < 0) {
		fprintf(stderr, "error: failed to open DRM.\n");
		ret = -2;

		goto out;
	}

	dev = exynos_device_create(fd);
	if (!dev) {
		fprintf(stderr, "error: failed to create device.\n");
		ret = -3;

		goto fail;
	}

	ctx = ipp_init(fd);
	if (!ctx) {
		fprintf(stderr, "error: failed to init IPP.\n");
		ret = -4;

		goto ipp_fail;
	}

	/* Setup DRM event handling structures. */
	fds.fd = fd;
	fds.events = POLLIN;
	evctx.base.version = DRM_EVENT_CONTEXT_VERSION;
	evctx.version = EXYNOS_EVENT_CONTEXT_VERSION;
	evctx.ipp_event_handler = ipp_handler;

	num_modules = ipp_num_modules(ctx);

	if (ipp_idx >= num_modules) {
		fprintf(stderr, "error: IPP index is out-of-bounds.\n");
		ret = -EINVAL;

		goto ipp_idx_fail;
	}

	/* Show detailed information about each IPP module. */
	for (i = 0; i < num_modules; ++i) {
		const struct ipp_module *ipp = ipp_get_module(ctx, i);

		fprintf(stderr, "IPP module number %u:\n", i);
		print_ipp_info(ipp);
	}

	if (!is_csc_supported(ipp_get_module(ctx, ipp_idx),
						  DRM_FORMAT_XRGB8888, DRM_FORMAT_XRGB8888)) {
		fprintf(stderr, "requested CSC not supported.\n");
		ret = -EINVAL;

		goto tasks_array_fail;
	}

	tasks = malloc(numtasks * sizeof(struct ipp_task*));
	if (!tasks) {
		fprintf(stderr, "error: failed to allocate tasks array.\n");
		ret = -ENOMEM;

		goto tasks_array_fail;
	}

	jobs = calloc(numtasks, sizeof(struct ipp_job));
	if (!jobs) {
		fprintf(stderr, "error: failed to allocate jobs array.\n");
		ret = -ENOMEM;

		goto jobs_array_fail;
	}

	for (i = 0; i < numtasks; ++i) {
		jobs[i].src_bo = exynos_bo_create(dev, bufw * bufh * sizeof(uint32_t), 0);
		if (!jobs[i].src_bo)
			break;

		jobs[i].dst_bo = exynos_bo_create(dev, bufw * bufh * sizeof(uint32_t), 0);
		if (!jobs[i].dst_bo)
			break;

		jobs[i].busy = false;
	}

	if (i != numtasks) {
		fprintf(stderr, "error: failed to allocate src/dst BOs.\n");

		while (i-- > 0) {
			exynos_bo_destroy(jobs[i].src_bo);
			exynos_bo_destroy(jobs[i].dst_bo);
		}

		ret = -ENOMEM;

		goto bo_fail;
	}

	for (i = 0; i < numtasks; ++i) {
		struct ipp_buffer buffer = { 0 };
		struct ipp_rect rect;

		tasks[i] = ipp_task_create(ipp_idx);

		if (!tasks[i]) {
			fprintf(stderr, "error: failed to create task %u.\n", i);
			break;
		}

		/* Task source: */
		buffer.fourcc = DRM_FORMAT_XRGB8888;
		buffer.width = bufw;
		buffer.height = bufh;
		buffer.gem_id[0] = exynos_bo_handle(jobs[i].src_bo);
		buffer.pitch[0] = sizeof(uint32_t) * bufw;

		rect.x = 0;
		rect.y = 0;
		rect.w = bufw;
		rect.h = bufh;

		ret = ipp_task_config_src(tasks[i], &buffer, &rect);

		if (ret < 0) {
			fprintf(stderr, "error: failed to config source for task %u (%d).\n",
				i, ret);
			break;
		}

		/* Task destination: */
		buffer.gem_id[0] = exynos_bo_handle(jobs[i].dst_bo);

		ret = ipp_task_config_dst(tasks[i], &buffer, &rect);

		if (ret < 0) {
			fprintf(stderr, "error: failed to config destination for task %u (%d).\n",
				i, ret);
			break;
		}

		jobs[i].task = tasks[i];
	}

	if (i != numtasks) {
		while (i-- > 0)
			ipp_task_destroy(tasks[i]);

		ret = -EFAULT;

		goto task_fail;
	}


	ret = fill_gem_random(jobs[0].src_bo);
	if (ret < 0) {
		fprintf(stderr, "error: failed to fill first BO with random data (%d).\n", ret);
		ret = -EFAULT;

		goto rnd_fail;
	}

	ret = ipp_commit(ctx, jobs[0].task, &jobs[0]);
	if (ret < 0) {
		fprintf(stderr, "error: failed to commit first task (%d).\n", ret);
		ret = -EFAULT;

		goto first_fail;
	}

	jobs[0].busy = true;

	++active_jobs;
	jobidx = 0;

	while (iters--) {
		const int timeout = 300;

		fds.revents = 0;

		if (poll(&fds, 1, timeout) < 0)
			continue;

		if (fds.revents & (POLLHUP | POLLERR))
			continue;

		if (fds.revents & POLLIN)
			exynos_handle_event(dev, &evctx);

		if (!active_jobs) {
			ret = fuzzy_memcmp_xrgb8888(jobs[jobidx].src_bo, jobs[jobidx].dst_bo);
			if (ret < 0) {
				fprintf(stderr, "error: fuzzy src/dst buffer compare failed (idx = %u).\n",
					jobidx);
				viscmp_xrgb8888(jobs[i].src_bo, jobs[i].dst_bo, bufw, bufh);

				goto iter_fail;
			}

			jobidx = (jobidx + 1) % numtasks;

			ret = fill_gem_random(jobs[jobidx].src_bo);
			if (ret < 0) {
				fprintf(stderr, "error: failed to fill BO with random data (idx = %u).\n",
					jobidx);

				goto iter_fail;
			}

			assert(!jobs[jobidx].busy);

			ret = ipp_commit(ctx, jobs[jobidx].task, &jobs[jobidx]);
			if (ret < 0) {
				fprintf(stderr, "error: failed to commit IPP task (idx = %u).\n",
					jobidx);

				goto iter_fail;
			}

			jobs[jobidx].busy = true;
			++active_jobs;
		}
	}

iter_fail:
first_fail:
rnd_fail:
	for (i = 0; i < numtasks; ++i)
		ipp_task_destroy(tasks[i]);

task_fail:
	for (i = 0; i < numtasks; ++i) {
		exynos_bo_destroy(jobs[i].src_bo);
		exynos_bo_destroy(jobs[i].dst_bo);
	}

bo_fail:
	free(jobs);

jobs_array_fail:
	free(tasks);

tasks_array_fail:
ipp_idx_fail:
	ipp_fini(ctx);

ipp_fail:
	exynos_device_destroy(dev);

fail:
	drmClose(fd);

out:
	return ret;
}
