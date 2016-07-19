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

#include <linux/input.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-cursor.h>

#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <sys/types.h>
#include <unistd.h>

#include "platform.h"
#include "helpers.h"
#include "xalloc.h"
#include "oring-clock.h"
#include "timespec-util.h"
#include "output.h"

#include "presentation-time-client-protocol.h"

#define TITLE PACKAGE_STRING " cal"

#define INVALID_CLOCK_ID 9999

struct window;
struct seat;

struct submission {
	struct wl_list link;
	struct window *window;

	struct wl_callback *frame;
	bool frame_done;
	struct wp_presentation_feedback *feedback;

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

	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;

	struct wl_shm *shm;
	struct wl_cursor_theme *cursor_theme;
	struct wl_cursor *default_cursor;
	struct wl_surface *cursor_surface;

	struct {
		EGLDisplay dpy;
		EGLContext ctx;
		EGLConfig conf;
	} egl;
	struct window *window;

	struct wl_list output_list; /* struct output::link */
};

struct geometry {
	int width, height;
};

struct window {
	struct display *display;
	struct geometry geometry, window_size;
	struct {
		GLuint rotation_uniform;
		GLuint pos;
		GLuint col;
	} gl;

	uint32_t benchmark_time, frames;
	struct wl_egl_window *native;
	struct wl_surface *surface;
	struct wl_shell_surface *shsurf;
	EGLSurface egl_surface;
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

static const char *vert_shader_text =
	"uniform mat4 rotation;\n"
	"attribute vec4 pos;\n"
	"attribute vec4 color;\n"
	"varying vec4 v_color;\n"
	"void main() {\n"
	"  gl_Position = rotation * pos;\n"
	"  v_color = color;\n"
	"}\n";

static const char *frag_shader_text =
	"precision mediump float;\n"
	"varying vec4 v_color;\n"
	"void main() {\n"
	"  gl_FragColor = v_color;\n"
	"}\n";

static int running = 1;

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

static int
create_submission(struct window *window)
{
	struct submission *subm;
	struct display *d = window->display;

	subm = zalloc(sizeof *subm);
	if (!subm)
		return -1;

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

	return 0;
}

static void
init_egl(struct display *display, bool has_alpha, int buffer_size)
{
	static const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};

	EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, 1,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};

	EGLint major, minor, n, count, i, size;
	EGLConfig *configs;
	EGLBoolean ret;

	if (!has_alpha || buffer_size == 16)
		config_attribs[9] = 0;

	display->egl.dpy =
		weston_platform_get_egl_display(EGL_PLATFORM_WAYLAND_KHR,
						display->display, NULL);
	assert(display->egl.dpy);

	ret = eglInitialize(display->egl.dpy, &major, &minor);
	assert(ret == EGL_TRUE);
	ret = eglBindAPI(EGL_OPENGL_ES_API);
	assert(ret == EGL_TRUE);

	if (!eglGetConfigs(display->egl.dpy, NULL, 0, &count) || count < 1)
		assert(0);

	configs = calloc(count, sizeof *configs);
	assert(configs);

	ret = eglChooseConfig(display->egl.dpy, config_attribs,
			      configs, count, &n);
	assert(ret && n >= 1);

	for (i = 0; i < n; i++) {
		eglGetConfigAttrib(display->egl.dpy,
				   configs[i], EGL_BUFFER_SIZE, &size);
		if (buffer_size == size) {
			display->egl.conf = configs[i];
			break;
		}
	}
	free(configs);
	if (display->egl.conf == NULL) {
		fprintf(stderr, "did not find config with buffer size %d\n",
			buffer_size);
		exit(EXIT_FAILURE);
	}

	display->egl.ctx = eglCreateContext(display->egl.dpy,
					    display->egl.conf,
					    EGL_NO_CONTEXT, context_attribs);
	assert(display->egl.ctx);
}

static void
fini_egl(struct display *display)
{
	eglTerminate(display->egl.dpy);
	eglReleaseThread();
}

static GLuint
create_shader(struct window *window, const char *source, GLenum shader_type)
{
	GLuint shader;
	GLint status;

	shader = glCreateShader(shader_type);
	assert(shader != 0);

	glShaderSource(shader, 1, (const char **) &source, NULL);
	glCompileShader(shader);

	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (!status) {
		char log[1000];
		GLsizei len;
		glGetShaderInfoLog(shader, 1000, &len, log);
		fprintf(stderr, "Error: compiling %s: %*s\n",
			shader_type == GL_VERTEX_SHADER ? "vertex" : "fragment",
			len, log);
		exit(1);
	}

	return shader;
}

static void
init_gl(struct window *window)
{
	GLuint frag, vert;
	GLuint program;
	GLint status;

	frag = create_shader(window, frag_shader_text, GL_FRAGMENT_SHADER);
	vert = create_shader(window, vert_shader_text, GL_VERTEX_SHADER);

	program = glCreateProgram();
	glAttachShader(program, frag);
	glAttachShader(program, vert);
	glLinkProgram(program);

	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (!status) {
		char log[1000];
		GLsizei len;
		glGetProgramInfoLog(program, 1000, &len, log);
		fprintf(stderr, "Error: linking:\n%*s\n", len, log);
		exit(1);
	}

	glUseProgram(program);

	window->gl.pos = 0;
	window->gl.col = 1;

	glBindAttribLocation(program, window->gl.pos, "pos");
	glBindAttribLocation(program, window->gl.col, "color");
	glLinkProgram(program);

	window->gl.rotation_uniform =
		glGetUniformLocation(program, "rotation");
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

static void
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
create_window(struct display *display, const struct geometry *size,
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

static void
redraw(void *data, struct wl_callback *callback, uint32_t time)
{
	struct window *window = data;
	struct display *display = window->display;
	static const GLfloat verts[3][2] = {
		{ -0.5, -0.5 },
		{  0.5, -0.5 },
		{  0,    0.5 }
	};
	static const GLfloat colors[3][3] = {
		{ 1, 0, 0 },
		{ 0, 1, 0 },
		{ 0, 0, 1 }
	};
	GLfloat angle;
	GLfloat rotation[4][4] = {
		{ 1, 0, 0, 0 },
		{ 0, 1, 0, 0 },
		{ 0, 0, 1, 0 },
		{ 0, 0, 0, 1 }
	};
	static const uint32_t speed_div = 5, benchmark_interval = 5;
	struct wl_region *region;
	struct timeval tv;

	assert(window->callback == callback);
	window->callback = NULL;

	if (callback)
		wl_callback_destroy(callback);

	gettimeofday(&tv, NULL);
	time = tv.tv_sec * 1000 + tv.tv_usec / 1000;
	if (window->frames == 0)
		window->benchmark_time = time;
	if (time - window->benchmark_time > (benchmark_interval * 1000)) {
		printf("%d frames in %d seconds: %f fps\n",
		       window->frames,
		       benchmark_interval,
		       (float) window->frames / benchmark_interval);
		window->benchmark_time = time;
		window->frames = 0;
	}

	angle = (time / speed_div) % 360 * M_PI / 180.0;
	rotation[0][0] =  cos(angle);
	rotation[0][2] =  sin(angle);
	rotation[2][0] = -sin(angle);
	rotation[2][2] =  cos(angle);

	glViewport(0, 0, window->geometry.width, window->geometry.height);

	glUniformMatrix4fv(window->gl.rotation_uniform, 1, GL_FALSE,
			   (GLfloat *) rotation);

	glClearColor(0.0, 0.0, 0.0, 0.5);
	glClear(GL_COLOR_BUFFER_BIT);

	glVertexAttribPointer(window->gl.pos, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(window->gl.col, 3, GL_FLOAT, GL_FALSE, 0, colors);
	glEnableVertexAttribArray(window->gl.pos);
	glEnableVertexAttribArray(window->gl.col);

	glDrawArrays(GL_TRIANGLES, 0, 3);

	glDisableVertexAttribArray(window->gl.pos);
	glDisableVertexAttribArray(window->gl.col);

	if (window->opaque || window->fullscreen) {
		region = wl_compositor_create_region(window->display->compositor);
		wl_region_add(region, 0, 0,
			      window->geometry.width,
			      window->geometry.height);
		wl_surface_set_opaque_region(window->surface, region);
		wl_region_destroy(region);
	} else {
		wl_surface_set_opaque_region(window->surface, NULL);
	}

	create_submission(window);
	eglSwapBuffers(display->egl.dpy, window->egl_surface);
	window->frames++;
}

static void
pointer_handle_enter(void *data, struct wl_pointer *pointer,
		     uint32_t serial, struct wl_surface *surface,
		     wl_fixed_t sx, wl_fixed_t sy)
{
	struct display *display = data;
	struct wl_buffer *buffer;
	struct wl_cursor *cursor = display->default_cursor;
	struct wl_cursor_image *image;

	if (display->window->fullscreen)
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
	struct display *display = data;

	if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED)
		wl_shell_surface_move(display->window->shsurf,
						 display->seat, serial);
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
}

static void
keyboard_handle_leave(void *data, struct wl_keyboard *keyboard,
		      uint32_t serial, struct wl_surface *surface)
{
}

static void
keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
		    uint32_t serial, uint32_t time, uint32_t key,
		    uint32_t state)
{
	struct display *d = data;

	if (!d->shell)
		return;

	if (key == KEY_F11 && state) {
		d->window->fullscreen = !d->window->fullscreen;
		shell_surface_set_state(d->window);
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
	struct display *d = data;

	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !d->pointer) {
		d->pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(d->pointer, &pointer_listener, d);
	} else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && d->pointer) {
		wl_pointer_destroy(d->pointer);
		d->pointer = NULL;
	}

	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !d->keyboard) {
		d->keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(d->keyboard, &keyboard_listener, d);
	} else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && d->keyboard) {
		wl_keyboard_destroy(d->keyboard);
		d->keyboard = NULL;
	}
}

static const struct wl_seat_listener seat_listener = {
	seat_handle_capabilities,
};

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
	assert(wl_proxy_get_version(proxy) == 1);
	assert(!d->seat);

	d->seat = proxy;
	wl_seat_add_listener(d->seat, &seat_listener, d);

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

	wl_surface_destroy(d->cursor_surface);
	if (d->cursor_theme)
		wl_cursor_theme_destroy(d->cursor_theme);

	if (d->shell)
		wl_shell_destroy(d->shell);

	if (d->compositor)
		wl_compositor_destroy(d->compositor);

	wl_registry_destroy(d->registry);
	wl_display_roundtrip(d->display);
	wl_display_disconnect(d->display);

	wl_list_for_each_safe(o, otmp, &d->output_list, link) {
		if (output_unref(o) != 0)
			fprintf(stderr, "Warning: output leaked.\n");
	}

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

	window = create_window(display, &winsize, opaque, fullscreen);
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
