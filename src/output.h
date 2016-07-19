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

#ifndef ORING_OUTPUT_H
#define ORING_OUTPUT_H

#include <stdint.h>
#include <stdbool.h>
#include <wayland-client.h>

struct vidmode {
	struct wl_list link; /* struct output::mode_list */

	uint32_t flags;
	int width;
	int height;
	int millihz;
};

struct output {
	struct wl_list link; /* struct display::output_list */
	int refcount;

	struct wl_output *proxy;
	uint32_t name;
	char *make;
	char *model;
	int mm_width;
	int mm_height;
	enum wl_output_transform transform;
	int scale;

	struct wl_list mode_list; /* struct vidmode::link */
	struct vidmode *current;

	bool done;

	struct vidmode *chosen;
};

struct output *
output_create(void *proxy, uint32_t name);

void
output_remove(struct output *o);

struct output *
output_ref(struct output *o);

int
output_unref(struct output *o);

struct output *
output_from_wl_output(struct wl_output *wo);

#endif /* ORING_OUTPUT_H */
