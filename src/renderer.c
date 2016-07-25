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

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <sys/time.h>

#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "cal.h"
#include "platform.h"
#include "renderer.h"
#include "xalloc.h"

struct renderer_display {
	EGLDisplay dpy;
	EGLint egl_major;
	EGLint egl_minor;

	EGLint n_configs;
};

struct renderer_window {
	struct renderer_display *render_display;

	EGLContext ctx;
	EGLConfig conf;

	struct wl_egl_window *native;
	EGLSurface egl_surface;
};

struct renderer_state {
	GLuint rotation_uniform;
	GLuint pos;
	GLuint col;
};

struct renderer_display *
renderer_display_create(struct wl_display *wdisp)
{
	struct renderer_display *rd;
	EGLBoolean ret;

	rd = xzalloc(sizeof *rd);

	rd->dpy = weston_platform_get_egl_display(EGL_PLATFORM_WAYLAND_KHR,
						  wdisp, NULL);
	if (!rd->dpy) {
		fprintf(stderr, "Error: getting EGLDisplay failed.\n");
		exit(1);
	}

	ret = eglInitialize(rd->dpy, &rd->egl_major, &rd->egl_minor);
	if (ret != EGL_TRUE) {
		fprintf(stderr, "Error: initializing EGL failed.\n");
		exit(1);
	}

	ret = eglBindAPI(EGL_OPENGL_ES_API);
	if (ret != EGL_TRUE) {
		fprintf(stderr, "Error: binding GL ES API failed.\n");
		exit(1);
	}

	ret = eglGetConfigs(rd->dpy, NULL, 0, &rd->n_configs);
	if (ret != EGL_TRUE || rd->n_configs < 1) {
		fprintf(stderr, "Error: getting count of EGLConfigs failed.\n");
		exit(1);
	}

	printf("Initialized EGL %d.%d on Wayland platform with GL ES.\n",
	       rd->egl_major, rd->egl_minor);

	return rd;
}

void
renderer_display_destroy(struct renderer_display *rd)
{
	eglTerminate(rd->dpy);
	eglReleaseThread();
	free(rd);
}

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

static EGLConfig
egl_choose_config(struct renderer_display *rd,
		  const EGLint *config_attribs, int buffer_bits)
{
	EGLint n, i, size;
	EGLConfig *configs;
	EGLBoolean ret;
	EGLConfig chosen = 0;

	configs = calloc(rd->n_configs, sizeof *configs);
	assert(configs);

	ret = eglChooseConfig(rd->dpy, config_attribs,
			      configs, rd->n_configs, &n);
	if (!ret || n < 1) {
		fprintf(stderr, "Error: failed to find any EGLConfigs\n");
		exit(1);
	}

	for (i = 0; i < n; i++) {
		eglGetConfigAttrib(rd->dpy, configs[i], EGL_BUFFER_SIZE, &size);
		if (buffer_bits == size) {
			chosen = configs[i];
			break;
		}
	}
	free(configs);

	return chosen;
}

struct renderer_window *
renderer_window_create(struct renderer_display *rd,
		       struct wl_surface *wsurf,
		       int width,
		       int height,
		       bool has_alpha,
		       int buffer_bits,
		       int swapinterval)
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

	EGLBoolean ret;
	struct renderer_window *rw;

	rw = xzalloc(sizeof *rw);
	rw->render_display = rd;

	if (!has_alpha || buffer_bits == 16)
		config_attribs[9] = 0;

	rw->conf = egl_choose_config(rd, config_attribs, buffer_bits);
	if (rw->conf == NULL) {
		fprintf(stderr, "Error: did not find EGLConfig with buffer size %d\n",
			buffer_bits);
		exit(1);
	}

	rw->ctx = eglCreateContext(rd->dpy, rw->conf,
				   EGL_NO_CONTEXT, context_attribs);
	if (!rw->ctx) {
		fprintf(stderr, "Error: failed to create an EGL context.\n");
		exit(1);
	}

	rw->native = wl_egl_window_create(wsurf, width, height);
	rw->egl_surface = weston_platform_create_egl_surface(rd->dpy,
							     rw->conf,
							     rw->native,
							     NULL);

	ret = eglMakeCurrent(rd->dpy, rw->egl_surface, rw->egl_surface, rw->ctx);
	assert(ret == EGL_TRUE);

	ret = eglSwapInterval(rd->dpy, swapinterval);
	if (ret != EGL_TRUE) {
		fprintf(stderr,
			"Error: setting EGL swap interval to %d failed.\n",
			swapinterval);
		exit(1);
	}

	return rw;
}

void
renderer_window_destroy(struct renderer_window *rw)
{
	EGLDisplay dpy = rw->render_display->dpy;

	/* Required, otherwise segfault in egl_dri2.c: dri2_make_current()
	 * on eglReleaseThread(). */
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(dpy, rw->ctx);

	eglDestroySurface(dpy, rw->egl_surface);
	wl_egl_window_destroy(rw->native);

	free(rw);
}

void
renderer_window_resize(struct renderer_window *rw, int width, int height)
{
	wl_egl_window_resize(rw->native, width, height, 0, 0);
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

void
init_gl(struct window *window)
{
	struct renderer_state *gl;
	GLuint frag, vert;
	GLuint program;
	GLint status;

	gl = xzalloc(sizeof *gl);

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

	gl->pos = 0;
	gl->col = 1;

	glBindAttribLocation(program, gl->pos, "pos");
	glBindAttribLocation(program, gl->col, "color");
	glLinkProgram(program);

	gl->rotation_uniform = glGetUniformLocation(program, "rotation");

	window->render_state = gl;
}

struct submission *
redraw(struct window *window, uint64_t target_time)
{
	struct renderer_window *rw = window->render_window;
	struct renderer_state *gl = window->render_state;
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
	struct submission *subm;
	uint32_t time;

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

	glUniformMatrix4fv(gl->rotation_uniform, 1, GL_FALSE,
			   (GLfloat *) rotation);

	glClearColor(0.0, 0.0, 0.0, 0.5);
	glClear(GL_COLOR_BUFFER_BIT);

	glVertexAttribPointer(gl->pos, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(gl->col, 3, GL_FLOAT, GL_FALSE, 0, colors);
	glEnableVertexAttribArray(gl->pos);
	glEnableVertexAttribArray(gl->col);

	glDrawArrays(GL_TRIANGLES, 0, 3);

	glDisableVertexAttribArray(gl->pos);
	glDisableVertexAttribArray(gl->col);

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

	subm = submission_create(window, target_time);
	eglSwapBuffers(rw->render_display->dpy, rw->egl_surface);
	submission_set_commit_time(subm);
	window->frames++;

	return subm;
}
