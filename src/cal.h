/*
 * Copyright © 2011 Benjamin Franzke
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

#ifndef ORING_CAL_H
#define ORING_CAL_H

#include <stdbool.h>
#include <time.h>

#include <wayland-client.h>

#include "oring-clock.h"
#include "output.h"

#include "presentation-time-client-protocol.h"

#define INVALID_CLOCK_ID 9999
#define INVALID_TIME 0xffffffffffffffffULL

struct window;
struct seat;
struct renderer_display;
struct renderer_window;
struct renderer_state;

struct submission {
	struct wl_list link;
	struct window *window;

	uint64_t commit_time;
	uint64_t target_time;

	struct wl_callback *frame;
	uint64_t frame_time;

	struct wp_presentation_feedback *feedback;
	uint64_t presented_time;

	struct output *sync_output;
};

struct display {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shell *shell;

	struct wp_presentation *presentation;
	clockid_t clock_id;
	struct oring_clock gfx_clock;
	struct renderer_display *render_display;

	struct wl_shm *shm;
	struct wl_cursor_theme *cursor_theme;
	struct wl_cursor *default_cursor;
	struct wl_surface *cursor_surface;

	struct window *window;

	struct wl_list output_list; /* struct output::link */
	struct wl_list seat_list; /* struct seat::link */
};

struct geometry {
	int width, height;
};

struct window {
	struct display *display;
	struct geometry geometry, window_size;

	struct renderer_window *render_window;
	struct renderer_state *render_state;

	uint32_t benchmark_time, frames;
	struct wl_surface *surface;
	struct wl_shell_surface *shsurf;
	struct wl_callback *callback;

	bool fullscreen;
	bool opaque;

	struct wl_list submissions_list; /* struct submission::link */
	struct wl_list on_output_list; /* struct window_output::link */
};

struct window_output {
	struct output *output;
	struct wl_list link;
};

struct window *
window_from_wl_surface(struct wl_surface *surface);

struct submission *
submission_create(struct window *window, uint64_t target_time);

void
submission_set_commit_time(struct submission *subm);

extern int running;

void
shell_surface_set_state(struct window *window);

#endif /* ORING_CAL_H */
