/*
 * Copyright © 2016 Pekka Paalanen <pq@iki.fi>
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#include "output.h"
#include "xalloc.h"

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>

#include <wayland-client.h>

static void
output_destroy(struct output *o)
{
	struct vidmode *v;

	wl_list_remove(&o->link);
	free(o->make);
	free(o->model);

	while (!wl_list_empty(&o->mode_list)) {
		v = wl_container_of(o->mode_list.next, v, link);
		wl_list_remove(&v->link);
		free(v);
	}

	free(o);
}

struct output *
output_ref(struct output *o)
{
	assert(o->refcount > 0);

	o->refcount++;

	return o;
}

int
output_unref(struct output *o)
{
	assert(o->refcount > 0);

	o->refcount--;

	if (o->refcount == 0) {
		output_destroy(o);

		return 0;
	}

	return o->refcount;
}

static void
output_handle_geometry(void *data,
		       struct wl_output *output,
		       int32_t x,
		       int32_t y,
		       int32_t physical_width,
		       int32_t physical_height,
		       int32_t subpixel,
		       const char *make,
		       const char *model,
		       int32_t transform)
{
	struct output *o = data;

	assert(o->proxy == output);

	o->mm_width = physical_width;
	o->mm_height = physical_height;
	o->make = strdup(make);
	o->model = strdup(model);
	o->transform = transform;
}

static void
output_handle_mode(void *data,
		   struct wl_output *output,
		   uint32_t flags,
		   int32_t width,
		   int32_t height,
		   int32_t refresh)
{
	struct output *o = data;
	struct vidmode *mode;

	assert(o->proxy == output);

	mode = xzalloc(sizeof(*mode));
	mode->flags = flags;
	mode->width = width;
	mode->height = height;
	mode->millihz = refresh;
	wl_list_insert(o->mode_list.prev, &mode->link);

	if (flags & WL_OUTPUT_MODE_CURRENT)
		o->current = mode;
}

static void
output_handle_done(void *data, struct wl_output *output)
{
	struct output *o = data;

	assert(o->proxy == output);

	o->done = true;
}

static void
output_handle_scale(void *data, struct wl_output *output, int32_t factor)
{
	struct output *o = data;

	assert(o->proxy == output);

	o->scale = factor;
}

static const struct wl_output_listener output_listener = {
	output_handle_geometry,
	output_handle_mode,
	output_handle_done,
	output_handle_scale,
};

struct output *
output_create(void *proxy, uint32_t name)
{
	struct output *o;

	if (wl_proxy_get_version(proxy) < 2) {
		fprintf(stderr,
			"unsupported: wl_output version is below 2\n");
		return NULL;
	}

	o = xzalloc(sizeof(*o));
	wl_list_init(&o->mode_list);

	o->proxy = proxy;
	o->name = name;
	wl_list_init(&o->link);
	o->refcount = 1;

	wl_output_add_listener(o->proxy, &output_listener, o);

	return o;
}
