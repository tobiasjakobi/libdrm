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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

/*
 * Get the ID of an object's property using the property name.
 */
static bool get_propid_by_name(int fd, uint32_t object_id, uint32_t object_type,
							   const char *name, uint32_t *prop_id) {
	drmModeObjectProperties *properties;
	unsigned i;
	bool found = false;

	properties = drmModeObjectGetProperties(fd, object_id, object_type);

	if (!properties)
		goto out;

	for (i = 0; i < properties->count_props; ++i) {
		drmModePropertyRes *prop;

		prop = drmModeGetProperty(fd, properties->props[i]);
		if (!prop)
			continue;

		if (!strcmp(prop->name, name)) {
			*prop_id = prop->prop_id;
			found = true;
		}

		drmModeFreeProperty(prop);

		if (found)
			break;
	}

	drmModeFreeObjectProperties(properties);

out:
	return found;
}

/*
 * Get the value of an object's property using the ID.
 */
static bool get_propval_by_id(int fd, uint32_t object_id, uint32_t object_type,
							  uint32_t id, uint64_t *prop_value) {
	drmModeObjectProperties *properties;
	unsigned i;
	bool found = false;

	properties = drmModeObjectGetProperties(fd, object_id, object_type);

	if (!properties)
		goto out;

	for (i = 0; i < properties->count_props; ++i) {
		drmModePropertyRes *prop;

		prop = drmModeGetProperty(fd, properties->props[i]);
		if (!prop)
			continue;

		if (prop->prop_id == id) {
			*prop_value = properties->prop_values[i];
			found = true;
		}

		drmModeFreeProperty(prop);

		if (found)
			break;
	}

	drmModeFreeObjectProperties(properties);

out:
	return found;
}

extern char *optarg;
static const char optstr[] = "r:";

static void usage(char *name)
{
	fprintf(stderr, "usage: %s\n", name);
	fprintf(stderr, "-r <RGB range>\n");
	exit(0);
}

int main(int argc, char **argv)
{
	int parsefail = 0;

	int c, range, ret, fd, i, j;

	drmModeRes *resources;
	drmModeConnector *connector;

	uint32_t crtc_id, prop_id;
	uint64_t value;

	drmModeAtomicReq *req;

	range = -1;

	while ((c = getopt(argc, argv, optstr)) != -1) {
		switch (c) {
		case 'r':
			if (sscanf(optarg, "%u", &range) != 1)
				parsefail = 1;
			break;

		default:
			parsefail = 1;
			break;
		}
	}

	if (parsefail || range < 0)
		usage(argv[0]);

	fd = drmOpen("exynos", NULL);
	if (fd < 0) {
		fprintf(stderr, "failed to open.\n");
		return fd;
	}

	ret = drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);
	if (ret < 0) {
		fprintf(stderr, "failed to enable atomic support\n");
		goto fail;
	}

	resources = drmModeGetResources(fd);
	if (!resources) {
		fprintf(stderr, "drmModeGetResources failed: %s\n",
				strerror(errno));
		ret = -EFAULT;
		goto fail;
	}

	for (i = 0; i < resources->count_connectors; ++i) {
		uint32_t ctype;

		connector = drmModeGetConnector(fd, resources->connectors[i]);
		if (connector == NULL)
			continue;

		ctype = connector->connector_type;

		if ((ctype == DRM_MODE_CONNECTOR_HDMIA ||
			(ctype == DRM_MODE_CONNECTOR_HDMIB)) &&
			(connector->connection == DRM_MODE_CONNECTED &&
			connector->count_modes > 0))
		break;

		drmModeFreeConnector(connector);
		connector = NULL;
	}

	if (i == resources->count_connectors) {
		fprintf(stderr, "no currently active connector found\n");
		ret = -EFAULT;
		goto fail_resources;
	}

	for (i = 0; i < connector->count_encoders; i++) {
		drmModeEncoder *encoder = drmModeGetEncoder(fd, connector->encoders[i]);

		if (!encoder)
			continue;

		/* Find a CRTC that is compatible with the encoder. */
		for (j = 0; j < resources->count_crtcs; ++j) {
			if (encoder->possible_crtcs & (1 << j))
				break;
		}

		drmModeFreeEncoder(encoder);

		/* Stop when a suitable CRTC was found. */
		if (j != resources->count_crtcs)
			break;
	}

	if (i == connector->count_encoders) {
		fprintf(stderr, "no compatible encoder found\n");
		ret = -EFAULT;
		goto fail_connector;
	}

	crtc_id = resources->crtcs[j];

	if (!get_propid_by_name(fd, crtc_id, DRM_MODE_OBJECT_CRTC,
							"RGB Range", &prop_id)) {
		fprintf(stderr, "failed to get range property\n");
		ret = -EFAULT;
		goto fail_connector;
	}

	if (!get_propval_by_id(fd, crtc_id, DRM_MODE_OBJECT_CRTC,
						   prop_id, &value)) {
		fprintf(stderr, "failed to get current range value\n");
		ret = -EFAULT;
		goto fail_connector;
	}

	fprintf(stdout, "info: current range = %llu\n", value);

	req = drmModeAtomicAlloc();
	if (!req) {
		fprintf(stderr, "failed to allocate atomic request\n");
		ret = -EFAULT;
		goto fail_connector;
	}

	if (drmModeAtomicAddProperty(req, crtc_id, prop_id, range) < 0) {
		fprintf(stderr, "failed to add range property to atomic request\n");
		ret = -EFAULT;
		goto fail_atomic;
	}

	if (drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL)) {
		fprintf(stderr, "failed to commit atomic request\n");
		ret = -EFAULT;
		goto fail_atomic;
	}

	ret = 0;

fail_atomic:
	drmModeAtomicFree(req);

fail_connector:
	drmModeFreeConnector(connector);

fail_resources:
	drmModeFreeResources(resources);

fail:
	drmClose(fd);

	return ret;
}
