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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <signal.h>
#include <time.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-cursor.h>

#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <sys/types.h>
#include <unistd.h>

#include "cal.h"
#include "platform.h"
#include "helpers.h"
#include "xalloc.h"
#include "oring-clock.h"
#include "timespec-util.h"
#include "input.h"
#include "output.h"
#include "renderer.h"

#include "presentation-time-client-protocol.h"

#define TITLE PACKAGE_STRING " cal"

int running = 1;

static void
submission_destroy(struct submission *subm)
{
	wl_list_remove(&subm->link);

	if (subm->frame)
		wl_callback_destroy(subm->frame);
	if (subm->feedback)
		wp_presentation_feedback_destroy(subm->feedback);
	if (subm->sync_output)
		output_unref(subm->sync_output);

	free(subm);
}

static void
submission_finish(struct submission *subm)
{
	struct window *window = subm->window;

	submission_destroy(subm);

	printf("Trigger %p!\n", window);
}

static void
submission_feedback_destroy(struct submission *subm,
			    struct wp_presentation_feedback *feedback)
{
	assert(feedback == subm->feedback);

	wp_presentation_feedback_destroy(subm->feedback);
	subm->feedback = NULL;
}

static void
feedback_handle_sync_output(void *data,
			    struct wp_presentation_feedback *feedback,
			    struct wl_output *wo)
{
	struct submission *subm = data;
	struct output *output;

	if (subm->sync_output)
		return;

	output = output_from_wl_output(wo);
	subm->sync_output = output_ref(output);
}

static void
feedback_handle_presented(void *data,
			  struct wp_presentation_feedback *feedback,
			  uint32_t tv_sec_hi,
			  uint32_t tv_sec_lo,
			  uint32_t tv_nsec,
			  uint32_t refresh,
			  uint32_t seq_hi,
			  uint32_t seq_lo,
			  uint32_t flags)
{
	struct submission *subm = data;
	struct display *d = subm->window->display;
	struct timespec tm;
	uint64_t nsec;

	assert(subm->frame_done);

	timespec_from_proto(&tm, tv_sec_hi, tv_sec_lo, tv_nsec);
	nsec = oring_clock_get_nsec(&d->gfx_clock, &tm);

	printf("presented at %.3f ms on output-%d\n", (double)nsec * 1e-6,
	       subm->sync_output ? subm->sync_output->name : 9999);

	submission_feedback_destroy(subm, feedback);

	submission_finish(subm);
}

static void
feedback_handle_discarded(void *data,
			  struct wp_presentation_feedback *feedback)
{
	struct submission *subm = data;

	fprintf(stderr, "Warning: frame discarded unexpectedly.\n");

	submission_feedback_destroy(subm, feedback);

	submission_finish(subm);
}

static const struct wp_presentation_feedback_listener
				presentation_feedback_listener = {
	feedback_handle_sync_output,
	feedback_handle_presented,
	feedback_handle_discarded,
};

static void
frame_callback_handle_done(void *data, struct wl_callback *cb, uint32_t arg)
{
	struct submission *subm = data;

	assert(cb == subm->frame);

	wl_callback_destroy(subm->frame);
	subm->frame = NULL;
	subm->frame_done = true;

	if (!subm->window->display->presentation)
		submission_finish(subm);

	printf("Frame\n");
}

static const struct wl_callback_listener frame_callback_listener = {
	frame_callback_handle_done,
};

struct submission *
submission_create(struct window *window)
{
	struct submission *subm;
	struct display *d = window->display;

	subm = xzalloc(sizeof *subm);
	subm->window = window;

	subm->frame = wl_surface_frame(window->surface);
	wl_callback_add_listener(subm->frame, &frame_callback_listener, subm);
	subm->frame_done = false;

	if (d->presentation) {
		subm->feedback = wp_presentation_feedback(d->presentation,
							  window->surface);
		wp_presentation_feedback_add_listener(subm->feedback,
						&presentation_feedback_listener,
						subm);
	}

	wl_list_insert(&window->submissions_list, &subm->link);

	return subm;
}

static void
handle_surface_ping(void *data, struct wl_shell_surface *shsurf,
		    uint32_t serial)
{
	wl_shell_surface_pong(shsurf, serial);
}

static void
handle_surface_configure(void *data, struct wl_shell_surface *shsurf,
			 uint32_t edges,
			 int32_t width, int32_t height)
{
	struct window *window = data;

	if (width > 0 && height > 0) {
		if (!window->fullscreen) {
			window->window_size.width = width;
			window->window_size.height = height;
		}
		window->geometry.width = width;
		window->geometry.height = height;
	} else if (!window->fullscreen) {
		window->geometry = window->window_size;
	}

	wl_egl_window_resize(window->native,
			     window->geometry.width,
			     window->geometry.height, 0, 0);
}

static void
handle_surface_popup_done(void *data, struct wl_shell_surface *shsurf)
{
	fprintf(stderr, "UNEXPECTED: %s\n", __func__);
}

static const struct wl_shell_surface_listener shell_surface_listener = {
	handle_surface_ping,
	handle_surface_configure,
	handle_surface_popup_done,
};

static void
create_shell_surface(struct window *window, struct display *display)
{
	window->shsurf = wl_shell_get_shell_surface(display->shell,
						    window->surface);
	wl_shell_surface_add_listener(window->shsurf,
				      &shell_surface_listener, window);

	wl_shell_surface_set_title(window->shsurf, TITLE);
	wl_shell_surface_set_class(window->shsurf, PACKAGE_NAME);
}

void
shell_surface_set_state(struct window *window)
{
	if (window->fullscreen) {
		wl_shell_surface_set_fullscreen(window->shsurf,
				WL_SHELL_SURFACE_FULLSCREEN_METHOD_DEFAULT,
				0, NULL);
	} else {
		window->geometry = window->window_size;
		wl_shell_surface_set_toplevel(window->shsurf);
		wl_egl_window_resize(window->native,
				window->geometry.width,
				window->geometry.height, 0, 0);
	}
}

static void
window_output_destroy(struct window_output *wino)
{
	wl_list_remove(&wino->link);
	output_unref(wino->output);
	free(wino);
}

static struct window_output *
window_output_create(struct output *output)
{
	struct window_output *wino;

	wino = xzalloc(sizeof *wino);
	wl_list_init(&wino->link);
	wino->output = output_ref(output);

	return wino;
}

static struct window_output *
window_find_window_output(struct window *window, struct output *output)
{
	struct window_output *wino;

	wl_list_for_each(wino, &window->on_output_list, link) {
		if (wino->output == output)
			return wino;
	}

	return NULL;
}

static void
surface_handle_enter(void *data, struct wl_surface *wl_surface,
		     struct wl_output *wo)
{
	struct window *window = data;
	struct output *output;
	struct window_output *wino;

	assert(window->surface == wl_surface);

	output = output_from_wl_output(wo);
	if (!output)
		return;

	assert(window_find_window_output(window, output) == NULL);

	wino = window_output_create(output);
	wl_list_insert(&window->on_output_list, &wino->link);
}

static void
surface_handle_leave(void *data, struct wl_surface *wl_surface,
		     struct wl_output *wo)
{
	struct window *window = data;
	struct output *output;
	struct window_output *wino;

	assert(window->surface == wl_surface);

	output = output_from_wl_output(wo);
	if (!output)
		return;

	wino = window_find_window_output(window, output);
	assert(wino);

	window_output_destroy(wino);
}

static const struct wl_surface_listener surface_listener = {
	surface_handle_enter,
	surface_handle_leave,
};

static struct window *
window_create(struct display *display, const struct geometry *size,
	      bool opaque, bool fullscreen)
{
	struct window *window;
	EGLBoolean ret;

	window = xzalloc(sizeof *window);

	window->display = display;
	window->geometry = *size;
	window->window_size = window->geometry;
	window->opaque = opaque;
	window->fullscreen = fullscreen;

	wl_list_init(&window->submissions_list);
	wl_list_init(&window->on_output_list);

	window->surface = wl_compositor_create_surface(display->compositor);
	wl_surface_add_listener(window->surface, &surface_listener, window);

	window->native = wl_egl_window_create(window->surface,
					      size->width, size->height);
	window->egl_surface =
		weston_platform_create_egl_surface(display->egl.dpy,
						   display->egl.conf,
						   window->native, NULL);

	create_shell_surface(window, display);

	ret = eglMakeCurrent(window->display->egl.dpy, window->egl_surface,
			     window->egl_surface, window->display->egl.ctx);
	assert(ret == EGL_TRUE);

	shell_surface_set_state(window);

	return window;
}

/** Safe cast from wl_surface
 *
 * \param surface The wl_surface to cast from.
 * \return The corresponding struct window, or NULL.
 *
 * This is safe to call with NULL. The returned pointer is non-NULL only
 * if the wl_surface really has a struct window associated.
 */
struct window *
window_from_wl_surface(struct wl_surface *surface)
{
	const void *impl;

	if (!surface)
		return NULL;

	impl = wl_proxy_get_listener((struct wl_proxy *)surface);
	if (impl != &surface_listener)
		return NULL;

	return wl_surface_get_user_data(surface);
}

static void
window_destroy(struct window *window)
{
	struct submission *subm, *tmp;
	struct window_output *wino, *winotmp;

	/* Required, otherwise segfault in egl_dri2.c: dri2_make_current()
	 * on eglReleaseThread(). */
	eglMakeCurrent(window->display->egl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE,
		       EGL_NO_CONTEXT);

	eglDestroySurface(window->display->egl.dpy, window->egl_surface);
	wl_egl_window_destroy(window->native);

	wl_shell_surface_destroy(window->shsurf);
	wl_surface_destroy(window->surface);

	if (window->callback)
		wl_callback_destroy(window->callback);

	wl_list_for_each_safe(subm, tmp, &window->submissions_list, link)
		submission_destroy(subm);

	wl_list_for_each_safe(wino, winotmp, &window->on_output_list, link)
		window_output_destroy(wino);

	free(window);
}

static int
register_wl_compositor(struct display *d, void *proxy, uint32_t name)
{
	assert(wl_proxy_get_version(proxy) == 1);
	assert(!d->compositor);

	d->compositor = proxy;

	return 0;
}

static int
register_wl_shell(struct display *d, void *proxy, uint32_t name)
{
	assert(wl_proxy_get_version(proxy) == 1);
	assert(!d->shell);

	d->shell = proxy;

	return 0;
}

static int
register_wl_seat(struct display *d, void *proxy, uint32_t name)
{
	struct seat *seat;

	/* XXX: implement support for wl_seat up to version 5 */
	assert(wl_proxy_get_version(proxy) == 1);

	seat = seat_create(d, proxy, name);
	wl_list_insert(&d->seat_list, &seat->link);

	return 0;
}

static int
register_wl_shm(struct display *d, void *proxy, uint32_t name)
{
	assert(wl_proxy_get_version(proxy) == 1);
	assert(!d->shm);

	d->shm = proxy;

	d->cursor_theme = wl_cursor_theme_load(NULL, 32, d->shm);
	if (!d->cursor_theme) {
		fprintf(stderr, "unable to load default theme\n");
		return -1;
	}

	d->default_cursor =
		wl_cursor_theme_get_cursor(d->cursor_theme, "left_ptr");
	if (!d->default_cursor) {
		fprintf(stderr, "unable to load default left pointer\n");
		// TODO: abort ?
	}

	return 0;
}

static int
register_wl_output(struct display *d, void *proxy, uint32_t name)
{
	struct output *o;

	o = output_create(proxy, name);
	if (!o)
		return -1;

	wl_list_insert(d->output_list.prev, &o->link);

	return 0;
}

static void
presentation_clock_id(void *data, struct wp_presentation *wp_presentation,
		      uint32_t clk_id)
{
	struct display *d = data;

	d->clock_id = clk_id;
}

static const struct wp_presentation_listener presentation_listener = {
	presentation_clock_id,
};

static int
register_wp_presentation(struct display *d, void *proxy, uint32_t name)
{
	assert(wl_proxy_get_version(proxy) == 1);
	assert(!d->presentation);

	d->presentation = proxy;
	wp_presentation_add_listener(d->presentation,
				     &presentation_listener, d);

	return 0;
}

static const struct global_binder {
	const struct wl_interface *interface;
	int (*register_)(struct display *d, void *proxy, uint32_t name);
	uint32_t supported_version;
} global_binders[] = {
	{ &wl_compositor_interface, register_wl_compositor, 1 },
	{ &wl_shell_interface, register_wl_shell, 1 },
	{ &wl_seat_interface, register_wl_seat, 1 },
	{ &wl_shm_interface, register_wl_shm, 1 },
	{ &wl_output_interface, register_wl_output, 2 },
	{ &wp_presentation_interface, register_wp_presentation, 1 },
};

static void
registry_handle_global(void *data, struct wl_registry *registry,
		       uint32_t name, const char *interface, uint32_t version)
{
	struct display *d = data;
	unsigned i;
	void *proxy;
	const struct global_binder *gi;
	uint32_t bind_ver;

	for (i = 0; i < ARRAY_LENGTH(global_binders); i++) {
		gi = &global_binders[i];

		if (strcmp(interface, gi->interface->name) != 0)
			continue;

		bind_ver = MIN(version, gi->supported_version);
		proxy = wl_registry_bind(registry, name, gi->interface,
					 bind_ver);
		if (!proxy)
			break;

		if (gi->register_(d, proxy, name) < 0)
			break;

		return;
	}

	if (i == ARRAY_LENGTH(global_binders))
		return;

	fprintf(stderr, "failed to bind '%s' (name %d)\n", interface, name);
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry,
			      uint32_t name)
{
	struct display *d = data;
	struct output *output = NULL;
	struct output *o;
	struct seat *seat = NULL;
	struct seat *s;

	wl_list_for_each(o, &d->output_list, link) {
		if (o->name == name) {
			output = o;
			break;
		}
	}

	if (output) {
		printf("output-%d removed by the compositor.\n",
		       output->name);
		output_remove(output);

		return;
	}

	wl_list_for_each(s, &d->seat_list, link) {
		if (s->global_name == name) {
			seat = s;
			break;
		}
	}

	if (seat) {
		printf("seat-%d removed by the compositor.\n",
		       seat->global_name);
		seat_destroy(seat);

		return;
	}
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
	registry_handle_global_remove
};

static struct display *
display_connect(void)
{
	struct display *d;
	const char *clockname;

	d = xzalloc(sizeof(*d));

	wl_list_init(&d->output_list);
	wl_list_init(&d->seat_list);
	d->clock_id = INVALID_CLOCK_ID;

	d->display = wl_display_connect(NULL);
	if (!d->display) {
		perror("Error connecting");
		exit(1);
	}

	d->registry = wl_display_get_registry(d->display);
	wl_registry_add_listener(d->registry, &registry_listener, d);

	/* Get globals */
	wl_display_roundtrip(d->display);

	/* Ensure initial events for bound globals */
	wl_display_roundtrip(d->display);

	d->cursor_surface = wl_compositor_create_surface(d->compositor);

	if (!d->presentation) {
		fprintf(stderr, "Warning: wp_presentation unavailable, "
			"timings will suffer.\n");

		d->clock_id = CLOCK_MONOTONIC;
		clockname = "frame callback";
	} else {
		if (d->clock_id == INVALID_CLOCK_ID) {
			fprintf(stderr,
				"Error: wp_presentation clock not received\n");
			exit(1);
		}

		clockname = "Presentation extension";
	}

	oring_clock_init_now(&d->gfx_clock, d->clock_id);

	printf("Using %s, clock id %d (%s)\n", clockname, d->clock_id,
	       clock_get_name(d->clock_id));

	return d;
}

static void
display_destroy(struct display *d)
{
	struct output *o, *otmp;
	struct seat *s, *stmp;

	wl_surface_destroy(d->cursor_surface);
	if (d->cursor_theme)
		wl_cursor_theme_destroy(d->cursor_theme);

	if (d->shell)
		wl_shell_destroy(d->shell);

	if (d->compositor)
		wl_compositor_destroy(d->compositor);

	wl_registry_destroy(d->registry);

	wl_list_for_each_safe(s, stmp, &d->seat_list, link)
		seat_destroy(s);

	wl_list_for_each_safe(o, otmp, &d->output_list, link) {
		if (output_unref(o) != 0)
			fprintf(stderr, "Warning: output leaked.\n");
	}

	wl_display_roundtrip(d->display);
	wl_display_disconnect(d->display);
	free(d);
}

static const char * const output_transform_string[] = {
	[WL_OUTPUT_TRANSFORM_NORMAL] = "normal",
	[WL_OUTPUT_TRANSFORM_90] = "90",
	[WL_OUTPUT_TRANSFORM_180] = "180",
	[WL_OUTPUT_TRANSFORM_270] = "270",
	[WL_OUTPUT_TRANSFORM_FLIPPED] = "flipped",
	[WL_OUTPUT_TRANSFORM_FLIPPED_90] = "flipped-90",
	[WL_OUTPUT_TRANSFORM_FLIPPED_180] = "flipped-180",
	[WL_OUTPUT_TRANSFORM_FLIPPED_270] = "flipped-270"
};

static struct output *
display_choose_output(struct display *d)
{
	struct output *output;
	int len;

	len = wl_list_length(&d->output_list);
	printf("found %d outputs:\n", len);
	if (len == 0)
		return NULL;

	wl_list_for_each(output, &d->output_list, link) {
		printf("\toutput-%d: ", output->name);
		if (!output->done) {
			printf("error getting output info\n");
			continue;
		}

		if (output->current)
			printf("%dx%d ", output->current->width,
			       output->current->height);
		else
			printf("(no mode) ");

		printf("%s, scale=%d, ",
		       output_transform_string[output->transform],
		       output->scale);
		printf("%s, %s\n", output->make, output->model);
	}

	return wl_container_of(d->output_list.prev, output, link);
}

static void
signal_int(int signum)
{
	running = 0;
}

static void
usage(int error_code)
{
	fprintf(stderr, "Usage: oring-cal [OPTIONS]\n\n"
		"  -f\tRun in fullscreen mode\n"
		"  -o\tCreate an opaque surface\n"
		"  -s\tUse a 16 bpp EGL config\n"
		"  -b\tset eglSwapInterval to 0 (default 1)\n"
		"  -h\tThis help text\n\n");

	exit(error_code);
}

int
main(int argc, char **argv)
{
	struct sigaction sigint;
	struct display *display;
	struct window *window;
	struct output *output;
	bool fullscreen = false;
	bool opaque = false;
	bool egl_frame_sync = true;
	int buffer_bits = 32;
	struct geometry winsize = { 250, 250 };
	int i, ret = 0;

	printf(TITLE "\n");

	for (i = 1; i < argc; i++) {
		if (strcmp("-f", argv[i]) == 0)
			fullscreen = true;
		else if (strcmp("-o", argv[i]) == 0)
			opaque = true;
		else if (strcmp("-s", argv[i]) == 0)
			buffer_bits = 16;
		else if (strcmp("-b", argv[i]) == 0)
			egl_frame_sync = false;
		else if (strcmp("-h", argv[i]) == 0)
			usage(EXIT_SUCCESS);
		else
			usage(EXIT_FAILURE);
	}

	display = display_connect();

	output = display_choose_output(display);
	if (!output) {
		fprintf(stderr, "Error: Could not choose output.\n");
		exit(1);
	}
	printf("chose output-%d\n", output->name);

	init_egl(display, !opaque, buffer_bits);
	if (!egl_frame_sync)
		eglSwapInterval(display->egl.dpy, 0);

	window = window_create(display, &winsize, opaque, fullscreen);
	display->window = window;

	init_gl(window);

	sigint.sa_handler = signal_int;
	sigemptyset(&sigint.sa_mask);
	sigint.sa_flags = SA_RESETHAND;
	sigaction(SIGINT, &sigint, NULL);

	/* The mainloop here is a little subtle.  Redrawing will cause
	 * EGL to read events so we can just call
	 * wl_display_dispatch_pending() to handle any events that got
	 * queued up as a side effect. */
	while (running && ret != -1) {
		wl_display_dispatch_pending(display->display);
		redraw(window, NULL, 0);
	}

	fprintf(stderr, TITLE " exiting\n");

	window_destroy(window);
	fini_egl(display);

	display_destroy(display);

	return 0;
}
