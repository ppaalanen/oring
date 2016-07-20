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

#include "cal.h"
#include "input.h"
#include "xalloc.h"

#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <assert.h>

#include <linux/input.h>
#include <wayland-client.h>
#include <wayland-cursor.h>

/* XXX: implement support for wl_seat up to version 5 */

static void
pointer_handle_enter(void *data, struct wl_pointer *pointer,
		     uint32_t serial, struct wl_surface *surface,
		     wl_fixed_t sx, wl_fixed_t sy)
{
	struct seat *seat = data;
	struct display *display = seat->display;
	struct window *window;
	struct wl_buffer *buffer;
	struct wl_cursor *cursor = display->default_cursor;
	struct wl_cursor_image *image;

	assert(!seat->pointer_focus || !"server bug");
	assert(seat->pointer == pointer);

	window = window_from_wl_surface(surface);
	seat->pointer_focus = window;
	if (!window)
		return;

	if (window->fullscreen)
		wl_pointer_set_cursor(pointer, serial, NULL, 0, 0);
	else if (cursor) {
		image = display->default_cursor->images[0];
		buffer = wl_cursor_image_get_buffer(image);
		if (!buffer)
			return;
		wl_pointer_set_cursor(pointer, serial,
				      display->cursor_surface,
				      image->hotspot_x,
				      image->hotspot_y);
		wl_surface_attach(display->cursor_surface, buffer, 0, 0);
		wl_surface_damage(display->cursor_surface, 0, 0,
				  image->width, image->height);
		wl_surface_commit(display->cursor_surface);
	}
}

static void
pointer_handle_leave(void *data, struct wl_pointer *pointer,
		     uint32_t serial, struct wl_surface *surface)
{
	struct seat *seat = data;
	struct window *window;

	window = window_from_wl_surface(surface);
	assert(seat->pointer_focus == window || !"server bug");

	seat->pointer_focus = NULL;
}

static void
pointer_handle_motion(void *data, struct wl_pointer *pointer,
		      uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
}

static void
pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
		      uint32_t serial, uint32_t time, uint32_t button,
		      uint32_t state)
{
	struct seat *seat = data;
	struct window *window;

	window = seat->pointer_focus;
	if (!window)
		return;

	if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED)
		wl_shell_surface_move(window->shsurf, seat->seat, serial);
}

static void
pointer_handle_axis(void *data, struct wl_pointer *wl_pointer,
		    uint32_t time, uint32_t axis, wl_fixed_t value)
{
}

static const struct wl_pointer_listener pointer_listener = {
	pointer_handle_enter,
	pointer_handle_leave,
	pointer_handle_motion,
	pointer_handle_button,
	pointer_handle_axis,
};

static void
keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
		       uint32_t format, int fd, uint32_t size)
{
	close(fd);
}

static void
keyboard_handle_enter(void *data, struct wl_keyboard *keyboard,
		      uint32_t serial, struct wl_surface *surface,
		      struct wl_array *keys)
{
	struct seat *seat = data;

	assert(!seat->keyboard_focus || !"server bug");

	seat->keyboard_focus = window_from_wl_surface(surface);
}

static void
keyboard_handle_leave(void *data, struct wl_keyboard *keyboard,
		      uint32_t serial, struct wl_surface *surface)
{
	struct seat *seat = data;
	struct window *window;

	window = window_from_wl_surface(surface);
	assert(seat->keyboard_focus == window || !"server bug");

	seat->keyboard_focus = NULL;
}

static void
keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
		    uint32_t serial, uint32_t time, uint32_t key,
		    uint32_t state)
{
	struct seat *seat = data;
	struct window *window;

	window = seat->keyboard_focus;
	if (!window)
		return;

	if (!window->display->shell)
		return;

	if (key == KEY_F11 && state) {
		window->fullscreen = !window->fullscreen;
		shell_surface_set_state(window);
	} else if (key == KEY_ESC && state)
		running = 0;
}

static void
keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard,
			  uint32_t serial, uint32_t mods_depressed,
			  uint32_t mods_latched, uint32_t mods_locked,
			  uint32_t group)
{
}

static const struct wl_keyboard_listener keyboard_listener = {
	keyboard_handle_keymap,
	keyboard_handle_enter,
	keyboard_handle_leave,
	keyboard_handle_key,
	keyboard_handle_modifiers,
};

static void
seat_handle_capabilities(void *data, struct wl_seat *seat,
			 enum wl_seat_capability caps)
{
	struct seat *s = data;

	assert(s->seat == seat);

	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !s->pointer) {
		s->pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(s->pointer, &pointer_listener, s);
	} else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && s->pointer) {
		wl_pointer_destroy(s->pointer);
		s->pointer = NULL;
		s->pointer_focus = NULL;
	}

	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !s->keyboard) {
		s->keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(s->keyboard, &keyboard_listener, s);
	} else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && s->keyboard) {
		wl_keyboard_destroy(s->keyboard);
		s->keyboard = NULL;
		s->keyboard_focus = NULL;
	}
}

static const struct wl_seat_listener seat_listener = {
	seat_handle_capabilities,
};

struct seat *
seat_create(struct display *display, struct wl_seat *proxy, uint32_t name)
{
	struct seat *seat;

	seat = xzalloc(sizeof *seat);
	seat->display = display;
	wl_list_init(&seat->link);
	seat->seat = proxy;
	seat->global_name = name;

	wl_seat_add_listener(seat->seat, &seat_listener, seat);

	return seat;
}

void
seat_destroy(struct seat *seat)
{
	wl_list_remove(&seat->link);

	if (seat->pointer)
		wl_pointer_destroy(seat->pointer);

	if (seat->keyboard)
		wl_keyboard_destroy(seat->keyboard);

	wl_seat_destroy(seat->seat);
	free(seat);
}
