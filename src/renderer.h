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

#ifndef ORING_RENDERER_H
#define ORING_RENDERER_H

#include "cal.h"

struct renderer_display *
renderer_display_create(struct wl_display *wdisp);

void
renderer_display_destroy(struct renderer_display *rd);

struct renderer_window *
renderer_window_create(struct renderer_display *rd,
		       struct wl_surface *wsurf,
		       int width,
		       int height,
		       bool has_alpha,
		       int buffer_bits,
		       int swapinterval);

void
renderer_window_resize(struct renderer_window *rw, int width, int height);

void
renderer_window_destroy(struct renderer_window *rw);

void
init_gl(struct window *window);

void
redraw(void *data, struct wl_callback *callback, uint32_t time);

#endif /* ORING_RENDERER_H */
