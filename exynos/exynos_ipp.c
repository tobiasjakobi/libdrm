/*
 * Copyright (C) 2017 - Tobias Jakobi
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xf86drm.h>
#include "exynos_drm.h"
#include "exynos_drmif.h"

#include "exynos_ipp.h"


#define MSG_PREFIX "exynos/ipp: "

struct ipp_context {
	/* File descriptor of the DRM device. */
	int fd;

	/* Number of IPP modules. */
	unsigned num_modules;

	/* The available IPP modules. */
	struct ipp_module *modules;

	/* Buffer to commit tasks to the IPP kernel driver. */
	void *commit_buf;
	unsigned buf_size;
};

struct ipp_task {
	/* Index of the IPP module provided by the context. */
	unsigned module_idx;

	/* Source and destination buffer. */
	struct drm_exynos_ipp_task_buffer *buffers[2];

	/* Source and destination rectangle. */
	struct drm_exynos_ipp_task_rect *rects[2];

	/* Transform data. */
	struct drm_exynos_ipp_task_transform *transform;

	/* Alpha data. */
	struct drm_exynos_ipp_task_alpha* alpha;
};

/* Light wrapper for the IPP_GET_RESOURCES ioctl(). */
static int
do_get_resources(int fd, unsigned *num_ids, uint32_t **ids)
{
	int ret;
	struct drm_exynos_ioctl_ipp_get_res req = { 0 };

	ret = drmIoctl(fd, DRM_IOCTL_EXYNOS_IPP_GET_RESOURCES, &req);
	if (ret < 0)
		return ret;

	*ids = malloc(req.count_ipps * sizeof(uint32_t));
	if (!*ids)
		return -ENOMEM;

	req.ipp_id_ptr = (uint64_t)(uintptr_t)(*ids);

	ret = drmIoctl(fd, DRM_IOCTL_EXYNOS_IPP_GET_RESOURCES, &req);
	if (ret < 0) {
		free(*ids);
		*ids = NULL;
	}

	*num_ids = req.count_ipps;

	return ret;
}

/* Light wrapper for the IPP_GET_CAPS ioctl(). */
static int
do_get_caps(int fd, unsigned id, unsigned *caps, unsigned *num_fmts,
			struct drm_exynos_ipp_format **fmts)
{
	int ret;
	struct drm_exynos_ioctl_ipp_get_caps req = { 0 };

	req.ipp_id = id;

	ret = drmIoctl(fd, DRM_IOCTL_EXYNOS_IPP_GET_CAPS, &req);
	if (ret < 0)
		return ret;

	if (!req.formats_count)
		return -EFAULT;

	*fmts = malloc(req.formats_count * sizeof(struct drm_exynos_ipp_format));
	if (!*fmts)
		return -ENOMEM;

	req.formats_ptr = (uint64_t)(uintptr_t)(*fmts);

	ret = drmIoctl(fd, DRM_IOCTL_EXYNOS_IPP_GET_CAPS, &req);
	if (ret < 0) {
		free(*fmts);
		*fmts = NULL;
	} else {
		*caps = req.capabilities;
		*num_fmts = req.formats_count;
	}

	return ret;
}

/* Light wrapper for the IPP_GET_LIMITS ioctl(). */
static int
do_get_limits(int fd, unsigned id, uint32_t fmt, uint64_t mod,
			  uint32_t type, unsigned *num_lmts,
			  struct drm_exynos_ipp_limit **lmts)
{
	int ret;
	struct drm_exynos_ioctl_ipp_get_limits req = { 0 };

	req.ipp_id = id;
	req.fourcc = fmt;
	req.modifier = mod;
	req.type = type;

	ret = drmIoctl(fd, DRM_IOCTL_EXYNOS_IPP_GET_LIMITS, &req);
	if (ret < 0)
		return ret;

	if (!req.limits_count) {
		*num_lmts = 0;
		return 0;
	}

	*lmts = malloc(req.limits_count * sizeof(struct drm_exynos_ipp_limit));
	if (!*lmts)
		return -ENOMEM;

	req.limits_ptr = (uint64_t)(uintptr_t)(*lmts);

	ret = drmIoctl(fd, DRM_IOCTL_EXYNOS_IPP_GET_LIMITS, &req);
	if (ret < 0) {
		free(*lmts);
		*lmts = NULL;
	} else {
		*num_lmts = req.limits_count;
	}

	return ret;
}

static void
serialize_task(struct ipp_task *task, uint8_t *buf)
{
	unsigned i;

	for (i = 0; i < 2; ++i) {
		memcpy(buf, task->buffers[i], sizeof(struct drm_exynos_ipp_task_buffer));
		buf += sizeof(struct drm_exynos_ipp_task_buffer);

		memcpy(buf, task->rects[i], sizeof(struct drm_exynos_ipp_task_rect));
		buf += sizeof(struct drm_exynos_ipp_task_rect);
	}

	if (task->transform) {
		memcpy(buf, task->transform, sizeof(struct drm_exynos_ipp_task_transform));
		buf += sizeof(struct drm_exynos_ipp_task_transform);
	}

	if (task->alpha) {
		memcpy(buf, task->alpha, sizeof(struct drm_exynos_ipp_task_alpha));
		buf += sizeof(struct drm_exynos_ipp_task_alpha);
	}
}

static int
setup_buffer(struct ipp_task *task, struct ipp_buffer *buf, bool is_source)
{
	unsigned buf_idx;
	unsigned task_type;
	struct drm_exynos_ipp_task_buffer *tbuf;

	buf_idx = is_source ? 0 : 1;
	task_type = is_source ? DRM_EXYNOS_IPP_TASK_TYPE_SOURCE :
		DRM_EXYNOS_IPP_TASK_TYPE_DESTINATION;

	if (!task->buffers[buf_idx]) {
		task->buffers[buf_idx] = malloc(sizeof(struct drm_exynos_ipp_task_buffer));

		if (!task->buffers[buf_idx])
			return -ENOMEM;

		task->buffers[buf_idx]->id = DRM_EXYNOS_IPP_TASK_BUFFER | task_type;
	}

	tbuf = task->buffers[buf_idx];

	tbuf->fourcc = buf->fourcc;
	tbuf->width = buf->width;
	tbuf->height = buf->height;
	memcpy(tbuf->gem_id, buf->gem_id, sizeof(uint32_t) * 4);
	memcpy(tbuf->offset, buf->offset, sizeof(uint32_t) * 4);
	memcpy(tbuf->pitch, buf->pitch, sizeof(uint32_t) * 4);
	tbuf->modifier = buf->modifier;

	return 0;
}

static int
setup_rect(struct ipp_task *task, struct ipp_rect *rect, bool is_source)
{
	unsigned rect_idx;
	unsigned task_type;
	struct drm_exynos_ipp_task_rect *trect;

	rect_idx = is_source ? 0 : 1;
	task_type = is_source ? DRM_EXYNOS_IPP_TASK_TYPE_SOURCE : DRM_EXYNOS_IPP_TASK_TYPE_DESTINATION;

	if (!task->rects[rect_idx]) {
		task->rects[rect_idx] = malloc(sizeof(struct drm_exynos_ipp_task_rect));

		if (!task->rects[rect_idx])
			return -ENOMEM;

		task->rects[rect_idx]->id = DRM_EXYNOS_IPP_TASK_RECTANGLE | task_type;
	}

	trect = task->rects[rect_idx];

	trect->x = rect->x;
	trect->y = rect->y;
	trect->w = rect->w;
	trect->h = rect->h;

	return 0;
}

static bool
is_task_ready(struct ipp_task *task)
{
	if (!task->buffers[0] || !task->buffers[1])
		return false;

	if (!task->rects[0] || !task->rects[1])
		return false;

	return true;
}

static unsigned
get_task_size(struct ipp_task *task)
{
	unsigned ret = 0;

	if (task->buffers[0])
		ret += sizeof(struct drm_exynos_ipp_task_buffer);
	if (task->buffers[1])
		ret += sizeof(struct drm_exynos_ipp_task_buffer);

	if (task->rects[0])
		ret += sizeof(struct drm_exynos_ipp_task_rect);
	if (task->rects[1])
		ret += sizeof(struct drm_exynos_ipp_task_rect);

	if (task->transform)
		ret += sizeof(struct drm_exynos_ipp_task_transform);

	if (task->alpha)
		ret += sizeof(struct drm_exynos_ipp_task_alpha);

	return ret;
}

static void
copy_limits(struct ipp_limit *lmt, const struct drm_exynos_ipp_limit *ipp_lmt)
{
	switch (ipp_lmt->type & DRM_EXYNOS_IPP_LIMIT_TYPE_MASK) {
	case DRM_EXYNOS_IPP_LIMIT_TYPE_SIZE:
		lmt->type = ipp_limit_size;
		break;
	case DRM_EXYNOS_IPP_LIMIT_TYPE_SCALE:
		lmt->type = ipp_limit_size;
		break;
	default:
		fprintf(stderr, MSG_PREFIX "warning: unknown limit type.\n");
		lmt->type = 0;
		break;
	}

	switch (ipp_lmt->type & DRM_EXYNOS_IPP_LIMIT_SIZE_MASK) {
	case DRM_EXYNOS_IPP_LIMIT_SIZE_BUFFER:
		lmt->size = ipp_limit_buffer;
		break;
	case DRM_EXYNOS_IPP_LIMIT_SIZE_AREA:
		lmt->size = ipp_limit_area;
		break;
	case DRM_EXYNOS_IPP_LIMIT_SIZE_ROTATED:
		lmt->size = ipp_limit_rotated;
		break;
	default:
		fprintf(stderr, MSG_PREFIX "warning: unknown limit size.\n");
		lmt->type = 0;
		break;
	}

	lmt->h_min = ipp_lmt->h.min;
	lmt->h_max = ipp_lmt->h.max;
	lmt->h_align = ipp_lmt->h.align;

	lmt->v_min = ipp_lmt->v.min;
	lmt->v_max = ipp_lmt->v.max;
	lmt->v_align = ipp_lmt->v.align;
}

static int
setup_format(int fd, uint32_t ipp_id, struct ipp_format *fmt,
			 const struct drm_exynos_ipp_format *ipp_fmt)
{
	int ret;
	unsigned i, numlmts;
	struct drm_exynos_ipp_limit *lmts;

	fmt->fourcc = ipp_fmt->fourcc;
	fmt->modifier = ipp_fmt->modifier;

	fmt->type_src = !!(ipp_fmt->type & DRM_EXYNOS_IPP_FORMAT_SOURCE);
	fmt->type_dst = !!(ipp_fmt->type & DRM_EXYNOS_IPP_FORMAT_DESTINATION);

	if (fmt->type_src) {
		lmts = NULL;

		ret = do_get_limits(fd, ipp_id, ipp_fmt->fourcc, ipp_fmt->modifier,
			DRM_EXYNOS_IPP_FORMAT_SOURCE, &numlmts, &lmts);

		if (ret < 0)
			return ret;

		if (!numlmts) {
			fmt->limits[0] = NULL;
			fmt->num_limits[0] = 0;
		} else {
			fmt->limits[0] = malloc(numlmts * sizeof(struct ipp_limit));

			for (i = 0; i < numlmts; ++i)
				copy_limits(&fmt->limits[0][i], &lmts[i]);

			fmt->num_limits[0] = numlmts;
		}

		free(lmts);
	}

	if (fmt->type_dst) {
		lmts = NULL;

		ret = do_get_limits(fd, ipp_id, ipp_fmt->fourcc, ipp_fmt->modifier,
			DRM_EXYNOS_IPP_FORMAT_DESTINATION, &numlmts, &lmts);

		if (ret < 0)
			return ret;

		if (!numlmts) {
			fmt->limits[1] = NULL;
			fmt->num_limits[1] = 0;
		} else {
			fmt->limits[1] = malloc(numlmts * sizeof(struct ipp_limit));

			for (i = 0; i < numlmts; ++i)
				copy_limits(&fmt->limits[1][i], &lmts[i]);

			fmt->num_limits[1] = numlmts;
		}

		free(lmts);
	}

	return 0;
}


/* Start of the public IPP API. */

/**
 * ipp_init - create a new IPP context and query available IPP modules.
 *
 * @fd: a file descriptor to an opened DRM device.
 */
struct ipp_context *ipp_init(int fd)
{
	int ret;
	unsigned i;
	struct ipp_context *ctx;
	uint32_t *ids;

	ctx = calloc(1, sizeof(struct ipp_context));
	if (!ctx) {
		fprintf(stderr, MSG_PREFIX "failed to allocate context.\n");
		return NULL;
	}

	ret = do_get_resources(fd, &ctx->num_modules, &ids);
	if (ret < 0) {
		fprintf(stderr, MSG_PREFIX "failed to get IPP resources.\n");
		free(ctx);
		return NULL;
	}

	printf(MSG_PREFIX "%u IPP modules found.\n", ctx->num_modules);

	ctx->modules = calloc(ctx->num_modules, sizeof(struct ipp_module));
	for (i = 0; i < ctx->num_modules; ++i) {
		unsigned j, numfmts;
		struct drm_exynos_ipp_format *fmts;
		uint32_t caps;

		ret = do_get_caps(fd, ids[i], &caps, &numfmts, &fmts);

		if (ret < 0) {
			fprintf(stderr, MSG_PREFIX "failed to get caps for IPP "
					"module %u (%d).\n", i, ret);

			break;
		}

		ctx->modules[i].cap_crop = !!(caps & DRM_EXYNOS_IPP_CAP_CROP);
		ctx->modules[i].cap_rotate = !!(caps & DRM_EXYNOS_IPP_CAP_ROTATE);
		ctx->modules[i].cap_scale = !!(caps & DRM_EXYNOS_IPP_CAP_SCALE);
		ctx->modules[i].cap_convert = !!(caps & DRM_EXYNOS_IPP_CAP_CONVERT);

		ctx->modules[i].id = ids[i];
		ctx->modules[i].formats = calloc(numfmts, sizeof(struct ipp_format));

		for (j = 0; j < numfmts; ++j) {
			ret = setup_format(fd, ids[i], &ctx->modules[i].formats[j], &fmts[j]);

			if (ret < 0) {
				fprintf(stderr, MSG_PREFIX "failed to get limits for IPP "
						"module %u, format %u (%d).\n", i, j, ret);

				break;
			}
		}

		if (j != numfmts) {
			while (j-- > 0) {
				free(ctx->modules[i].formats[j].limits[0]);
				free(ctx->modules[i].formats[j].limits[1]);
			}
		}

		ctx->modules[i].num_formats = numfmts;

		free(fmts);
	}

	if (i != ctx->num_modules) {
		while (i-- > 0)
			free(ctx->modules[i].formats);
	}

	ctx->fd = fd;
	free(ids);

	return ctx;
}

/**
 * ipp_fini - destroy a IPP context.
 *
 * @ctx: a pointer to a IPP context structure.
 *
 * Passing a null pointer as context results in a NOP.
 */
void ipp_fini(struct ipp_context *ctx)
{
	if (ctx) {
		unsigned i, j;

		for (i = 0; i < ctx->num_modules; ++i) {
			for (j = 0; j < ctx->modules[i].num_formats; ++j) {
				free(ctx->modules[i].formats[j].limits[0]);
				free(ctx->modules[i].formats[j].limits[1]);
			}

			free(ctx->modules[i].formats);
		}

		free(ctx->modules);
	}

	free(ctx);
}

/**
 * ipp_num_modules - get number of available IPP modules.
 *
 * @ctx: a pointer to a IPP context structure.
 */
unsigned ipp_num_modules(struct ipp_context *ctx)
{
	if (!ctx)
		return 0;

	return ctx->num_modules;
}

/**
 * ipp_get_module - get pointer to IPP module via index.
 *
 * @ctx: a pointer to a valid IPP context structure.
 * @index: index of the IPP module.
 */
const struct ipp_module*
ipp_get_module(struct ipp_context *ctx, unsigned index)
{
	if (index >= ctx->num_modules)
		return NULL;
	else
		return &ctx->modules[index];
}

/**
 * ipp_task_create - create a new IPP task.
 *
 * @module_idx: index of the IPP index this task should run on.
 */
struct ipp_task *ipp_task_create(unsigned module_idx)
{
	struct ipp_task *task;

	task = calloc(1, sizeof(struct ipp_task));
	if (!task)
		return NULL;

	task->module_idx = module_idx;

	return task;
}

/**
 * ipp_task_destroy - destroy an IPP task.
 *
 * @task: a pointer to a IPP task structure.
 *
 * Passing a null pointer as context results in a NOP.
 */
void ipp_task_destroy(struct ipp_task *task)
{
	unsigned i;

	if (!task)
		return;

	for (i = 0; i < 2; ++i) {
		free(task->buffers[i]);
		free(task->rects[i]);
	}

	free(task->transform);
	free(task->alpha);

	free(task);
}

/**
 * ipp_task_config_src - configurate the source of an IPP task.
 *
 * @task: a pointer to a IPP task structure.
 * @buf: a pointer to a IPP buffer structure.
 * @rect: a pointer to a IPP rectangle structure.
 *
 * Both buf and rect are allowed to be null.
 */
int ipp_task_config_src(struct ipp_task *task, struct ipp_buffer *buf, struct ipp_rect *rect)
{
	int ret;

	if (!task)
		return -EINVAL;

	if (buf) {
		ret = setup_buffer(task, buf, true);

		if (ret < 0)
			return ret;
	}

	if (rect) {
		ret = setup_rect(task, rect, true);

		if (ret < 0)
			return ret;
	}

	return 0;
}

/**
 * ipp_task_config_dst - configurate the destination of an IPP task.
 *
 * @task: a pointer to a IPP task structure.
 * @buf: a pointer to a IPP buffer structure.
 * @rect: a pointer to a IPP rectangle structure.
 *
 * Both buf and rect are allowed to be null.
 */
int ipp_task_config_dst(struct ipp_task *task, struct ipp_buffer *buf, struct ipp_rect *rect)
{
	int ret;

	if (!task)
		return -EINVAL;

	if (buf) {
		ret = setup_buffer(task, buf, false);

		if (ret < 0)
			return ret;
	}

	if (rect) {
		ret = setup_rect(task, rect, false);

		if (ret < 0)
			return ret;
	}

	return 0;
}

/**
 * ipp_task_config_transf - configurate the transform operation of an IPP task.
 *
 * @task: a pointer to a IPP task structure.
 * @rotation: the transform's rotation value.
 */
int ipp_task_config_transf(struct ipp_task *task, uint32_t rotation)
{
	if (!task)
		return -EINVAL;

	if (!task->transform) {
		task->transform = malloc(sizeof(struct drm_exynos_ipp_task_transform));

		if (!task->transform)
			return -ENOMEM;

		task->transform->id = DRM_EXYNOS_IPP_TASK_TRANSFORM;
	}

	task->transform->rotation = rotation;

	return 0;
}

/**
 * ipp_task_config_alpha - configurate the alpha operation of an IPP task.
 *
 * @task: a pointer to a IPP task structure.
 * @alpha: the alpha value to apply.
 */
int ipp_task_config_alpha(struct ipp_task *task, uint32_t alpha)
{
	if (!task)
		return -EINVAL;

	if (!task->alpha) {
		task->alpha = malloc(sizeof(struct drm_exynos_ipp_task_alpha));

		if (!task->alpha)
			return -ENOMEM;

		task->alpha->id = DRM_EXYNOS_IPP_TASK_ALPHA;
	}

	task->alpha->value = alpha;

	return 0;
}

/**
 * ipp_check - perform a check on a IPP task.
 *
 * @ctx: a pointer to a IPP context structure.
 * @task: a pointer to a IPP task structure.
 */
int ipp_check(struct ipp_context *ctx, struct ipp_task *task)
{
	struct drm_exynos_ioctl_ipp_commit req = { 0 };
	unsigned task_size;

	if (!ctx || !task)
		return -EINVAL;

	if (task->module_idx >= ctx->num_modules)
		return -EINVAL;

	if (!is_task_ready(task))
		return -EFAULT;

	task_size = get_task_size(task);

	if (task_size > ctx->buf_size) {
		free(ctx->commit_buf);
		ctx->buf_size = 0;

		ctx->commit_buf = malloc(task_size);

		if (!ctx->commit_buf)
			return -ENOMEM;

		ctx->buf_size = task_size;
	}

	serialize_task(task, ctx->commit_buf);

	req.flags = DRM_EXYNOS_IPP_FLAG_TEST_ONLY;
	req.ipp_id = ctx->modules[task->module_idx].id;
	req.params_size = task_size;
	req.params_ptr = (uint64_t)(uintptr_t)(ctx->commit_buf);

	return drmIoctl(ctx->fd, DRM_IOCTL_EXYNOS_IPP_COMMIT, &req);
}

/**
 * ipp_check - commits an IPP task to the IPP kernel driver.
 *
 * @ctx: a pointer to a IPP context structure.
 * @task: a pointer to a IPP task structure.
 * @event_data: opaque pointer to be used for event processing
 *
 * If event_data is not null, the IPP commit is done in a non-blocking
 * fashion. The completion of the task is signaled through an event on
 * the DRM fd.
 */
int ipp_commit(struct ipp_context *ctx, struct ipp_task *task, void *event_data)
{
	struct drm_exynos_ioctl_ipp_commit req = { 0 };
	unsigned task_size;

	if (!ctx || !task)
		return -EINVAL;

	if (task->module_idx >= ctx->num_modules)
		return -EINVAL;

	if (!is_task_ready(task))
		return -EFAULT;

	task_size = get_task_size(task);

	/* Increase commit buffer size if necessary. */
	if (task_size > ctx->buf_size) {
		free(ctx->commit_buf);
		ctx->buf_size = 0;

		ctx->commit_buf = malloc(task_size);

		if (!ctx->commit_buf)
			return -ENOMEM;

		ctx->buf_size = task_size;
	}

	serialize_task(task, ctx->commit_buf);

	if (event_data)
		req.flags = DRM_EXYNOS_IPP_FLAG_NONBLOCK | DRM_EXYNOS_IPP_FLAG_EVENT;

	req.ipp_id = ctx->modules[task->module_idx].id;
	req.params_size = task_size;
	req.params_ptr = (uint64_t)(uintptr_t)(ctx->commit_buf);
	req.user_data = (uint64_t)(uintptr_t)(event_data);

	return drmIoctl(ctx->fd, DRM_IOCTL_EXYNOS_IPP_COMMIT, &req);
}
