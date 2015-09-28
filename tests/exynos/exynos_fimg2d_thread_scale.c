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

#include <unistd.h>
#include <poll.h>

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <getopt.h>

#include <pthread.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include "exynos_drm.h"
#include "exynos_drmif.h"
#include "exynos_fimg2d.h"

#define NUM_PAGES 3

struct threaddata;

enum page_flags {
	/* The page is free/unused. */
	page_free  = (1 << 0),
	/* The page is ready to be displayed. */
	page_ready = (1 << 1)
};

struct g2d_page {
	struct exynos_bo *bo[2];
	struct g2d_image img[2];

	uint32_t buf_id;

	uint64_t index;
	unsigned int flags;

	struct threaddata *base;
};

struct exynos_evhandler {
	struct pollfd fds;
	struct exynos_event_context evctx;
};

struct threaddata {
	unsigned int stop;
	unsigned int pageflip_pending;
	
	int drm_fd;
	unsigned width, height;
	
	drmModeRes *resources;
	drmModeConnector *connector;
	drmModeEncoder *encoder;
	drmModeModeInfo *mode;
	drmModeCrtc *crtc, *orig_crtc;

	struct exynos_device *dev;
	struct g2d_context *ctx;
	struct exynos_evhandler evhandler;
};

#if 0
static void pageflip_event_handler(int fd, unsigned int cmdlist_no, unsigned int tv_sec,
							unsigned int tv_usec, void *user_data)
{
	struct g2d_page *page = user_data;
	
	page->flags |= page_free;

	if (page->base) {
		// TODO: use mutex here
		page->base->pageflip_pending--;
	}
	
	
	
	/* TODO */
	
}

static void g2d_event_handler(int fd, unsigned int cmdlist_no, unsigned int tv_sec,
							unsigned int tv_usec, void *user_data)
{
	struct g2d_page *page = user_data;

	page->flags |= page_ready;
}

/*
 * Fill a userspace buffer with random content.
 */
static void fill_random(void *buf, unsigned buf_size)
{
	unsigned int i;
	uint32_t *data;

	assert(buf_size % 4 == 0);
	data = buf;

	for (i = 0; i < buf_size / 4; ++i)
		data[i] = rand();
}

static void setup_event_handler(struct exynos_evhandler *evhandler, int fd)
{
	evhandler->fds.fd = fd;
	evhandler->fds.events = POLLIN;
	evhandler->evctx.base.version = DRM_EVENT_CONTEXT_VERSION;
	evhandler->evctx.base.page_flip_handler = pageflip_event_handler;
	evhandler->evctx.version = EXYNOS_EVENT_CONTEXT_VERSION;
	evhandler->evctx.g2d_event_handler = g2d_event_handler;
}

static void* threadfunc(void *arg) {
	const int timeout = 0;
	struct threaddata *data;

	data = arg;

	while (1) {
		if (data->stop) break;

		usleep(500);

		data->evhandler.fds.revents = 0;

		if (poll(&data->evhandler.fds, 1, timeout) < 0)
			continue;

		if (data->evhandler.fds.revents & (POLLHUP | POLLERR))
			continue;

		if (data->evhandler.fds.revents & POLLIN)
			exynos_handle_event(data->dev, &data->evhandler.evctx);
	}

	pthread_exit(0);
}

static int init_exynos(uint32_t connector_id, int crtc_id, struct threaddata *data)
{
	int i, j, fd;

	drmModeRes *resources;
	drmModeConnector *connector;
	drmModeEncoder *encoder;
	drmModeModeInfo *mode;
	drmModeCrtc *crtc, *orig_crtc;

	fd = drmOpen("exynos", NULL);
	if (fd < 0) {
		fprintf(stderr, "Failed to open Exynos DRM.\n");
		return -1;
	}

	resources = drmModeGetResources(fd);
	if (resources == NULL) {
		fprintf(stderr, "Failed to get DRM resources.\n");
		goto fail;
	}

	for (i = 0; i < resources->count_connectors; ++i) {
		connector = drmModeGetConnector(fd, resources->connectors[i]);
		if (connector == NULL)
			continue;

		if (connector->connector_id != connector_id)
			continue;

		if (connector->connection == DRM_MODE_CONNECTED &&
			connector->count_modes > 0)
			break;

		drmModeFreeConnector(connector);
		connector = NULL;
	}

	if (i == resources->count_connectors) {
		fprintf(stderr, "No currently active connector found.\n");
		goto fail;
	}

	for (i = 0; i < connector->count_encoders; i++) {
		encoder = drmModeGetEncoder(fd, connector->encoders[i]);
		if (encoder == NULL)
			continue;

		/* Find a CRTC that is compatible with the encoder. */
		for (j = 0; j < resources->count_crtcs; ++j) {
			if (encoder->possible_crtcs & (1 << j))
				break;
		}

		/* Select this encoder if compatible CRTC was found. */
		if (j != resources->count_crtcs)
			break;

		drmModeFreeEncoder(encoder);
		encoder = NULL;
	}

	if (i == connector->count_encoders) {
		fprintf(stderr, "No compatible encoder found.\n");
		goto fail;
	}

	crtc = drmModeGetCrtc(fd, resources->crtcs[j]);
	if (crtc == NULL) {
		fprintf(stderr, "Failed to get crtc from encoder\n");
		goto fail;
	}
	
	    drm->mode = &drm->connector->modes[0];
  }

  if (drm->mode->hdisplay == 0 || drm->mode->vdisplay == 0) {
    RARCH_ERR("video_exynos: failed to select sane resolution\n");
    goto fail;
  }

  drm->crtc_id = drm->crtc->crtc_id;
  drm->connector_id = drm->connector->connector_id;
  drm->orig_crtc = drmModeGetCrtc(fd, drm->crtc_id);
  if (!drm->orig_crtc)
    RARCH_WARN("video_exynos: cannot find original crtc\n");

  pdata->width = drm->mode->hdisplay;
  pdata->height = drm->mode->vdisplay;

	return 0;

fail:
	
	return -1;
}

/*
 * We need to wait until all G2D jobs are finished, otherwise we
 * potentially remove a BO which the engine still operates on.
 * This results in the following kernel message:
 * [drm:exynos_drm_gem_put_dma_addr] *ERROR* failed to lookup gem object.
 * Also any subsequent BO allocations fail then with:
 * [drm:exynos_drm_alloc_buf] *ERROR* failed to allocate buffer.
 */
static void wait_all_pages(struct g2d_job *pages, unsigned num_pages)
{
	unsigned i;

	for (i = 0; i < num_jobs; ++i) {
		while (jobs[i].busy)
			usleep(500);
	}

}

static struct g2d_page* free_page(struct g2d_page *pages, unsigned num_pages)
{
	unsigned i;

	for (i = 0; i < num_pages; ++i) {
		if (pages[i].flags & page_free)
			return &pages[i];
	}

	return NULL;
}

static struct g2d_page* ready_page(struct g2d_page *pages, uint64_t index, unsigned num_pages)
{
	unsigned i;

	// TODO
	for (i = 0; i < num_pages; ++i) {
		if (pages[i].flags & page_ready)
			return &pages[i];
	}

	return NULL;
	
}

static int scale_work(struct g2d_context *ctx,
					unsigned num_jobs, unsigned iterations)
{
	struct g2d_page pages[NUM_PAGES] = {0};
	struct g2d_page *page;
	int ret;
	unsigned i;

	const unsigned src_bufsize = (width / 4) * (height / 4) * 4;
	const unsigned dst_bufsize = width * height * 4;
	
	/* setup pages */
	for (i = 0; i < NUM_PAGES; ++i) {
		
		pages[i].bo[0] = exynos_bo_create(dev, src_bufsize, 0);
		pages[i].bo[1] = exynos_bo_create(dev, dst_bufsize, 0);
		
		// todo: add dst bos as framebuffer
		
		pages[i].img[0] = (struct g2d_image) {
			.width = width / 4,
			.height = height / 4,
			.stride = 0, /* TODO */
			.buf_type = G2D_IMGBUF_GEM,
			.color_mode = G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_AXRGB,
			.bo[0] = pages[i].bo[0]->handle
		};

		pages[i].img[1] = (struct g2d_image) {
			.width = width,
			.height = height,
			.stride = 0, /* TODO */
			.buf_type = G2D_IMGBUF_GEM,
			.color_mode = G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_AXRGB,
			.bo[0] = pages[i].bo[1]->handle
		};
		
		//pages[i].
		
	}

	while (1) {
		usleep(500);

		page = free_page(pages, NUM_PAGES);
		
		if (page) {
			page->flags &= ~page_free;

			fill_random(page->bo[0]->vaddr, size);

			g2d_config_event(ctx, page);
			ret = g2d_copy_with_scale(ctx, &page->img[0],
				&page->img[1], 0, 0, width / 4, height / 4,
				0, 0, width, height, 0);

			if (ret == 0)
				ret = g2d_exec2(ctx, G2D_EXEC_FLAG_ASYNC);

			if (ret != 0) {
				fprintf(stderr, "error: iteration %u (x = %u, x = %u, x = %u, x = %u) failed\n",
					i, x, y, w, h);
				break;
			}
			
		}
		
		// continue if pageflip pending
		
		
		// find ready page (with correct index)
		page = ready_page(pages, idx, NUM_PAGES);
		
		if (page) {
			page->flags &= ~page_ready;
			
			/* Issue a page flip at the next vblank interval. */
			if (drmModePageFlip(pdata->fd, pdata->drm->crtc_id, page->buf_id,
						DRM_MODE_PAGE_FLIP_EVENT, page) != 0) {
				RARCH_ERR("video_exynos: failed to issue page flip\n");
				return -1;
			} else {
				pdata->pageflip_pending++;
			}
			
			
			// if ready page avilable:
			// remove ready flag
			// queue page for pageflip
		}
		
		
		
		
	}
	
	
	


	wait_all_jobs(jobs, num_jobs);
	free(jobs);

	return 0;
}

static void usage(const char *name)
{
	fprintf(stderr, "usage: %s [-ijwh]\n\n", name);

	fprintf(stderr, "\t-i <number of iterations>\n");
	fprintf(stderr, "\t-j <number of G2D jobs> (default = 4)\n\n");

	fprintf(stderr, "\t-w <buffer width> (default = 4096)\n");
	fprintf(stderr, "\t-h <buffer height> (default = 4096)\n");

	exit(0);
}

int main(int argc, char **argv)
{
	int fd, ret, c, parsefail;

	pthread_t event_thread;
	struct threaddata event_data = {0};

	

	uint32_t connector_id;
	int crtc = -1;

	ret = 0;
	parsefail = 0;

	while ((c = getopt(argc, argv, "i:j:w:h:")) != -1) {
		switch (c) {
		case 'i':
			if (sscanf(optarg, "%u", &iters) != 1)
				parsefail = 1;
			break;
		case 'j':
			if (sscanf(optarg, "%u", &njobs) != 1)
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




	event_data.dev = dev;
	setup_g2d_event_handler(&event_data.evhandler, fd);

	pthread_create(&event_thread, NULL, threadfunc, &event_data);

	ret = g2d_work(ctx, &img, njobs, iters);
	if (ret != 0)
		fprintf(stderr, "error: g2d_work failed\n");

	event_data.stop = 1;
	pthread_join(event_thread, NULL);

	exynos_bo_destroy(bo);

bo_fail:
	g2d_fini(ctx);

g2d_fail:
	exynos_device_destroy(dev);

fail:
	drmClose(fd);

out:
	return ret;
}
#endif

/* DUMMY */
int main(int argc, char **argv)
{

}
