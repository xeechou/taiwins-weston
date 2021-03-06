/*
 * render_context.h - taiwins render context
 *
 * Copyright (c) 2020-2021 Xichen Zhou
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef TW_RENDER_CONTEXT_H
#define TW_RENDER_CONTEXT_H

#include <stdbool.h>
#include <stdint.h>
#include <pixman.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <taiwins/objects/dmabuf.h>
#include <taiwins/objects/compositor.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/layers.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_egl_options;
struct tw_render_context;
struct tw_render_surface;
struct tw_render_presentable;
struct tw_render_texture;

enum tw_renderer_type {
	TW_RENDERER_EGL,
	TW_RENDERER_VK,
};

struct tw_render_presentable_impl {
	void (*destroy)(struct tw_render_presentable *surface,
	                struct tw_render_context *ctx);
	bool (*commit)(struct tw_render_presentable *surf,
	               struct tw_render_context *ctx);
        int (*make_current)(struct tw_render_presentable *surf,
	                    struct tw_render_context *ctx);
};

struct tw_render_presentable {
	intptr_t handle;
	struct wl_signal commit;
	const struct tw_render_presentable_impl *impl;
};

struct tw_render_texture {
	uint32_t width, height;
	int fmt;
	bool has_alpha, inverted_y;
	enum wl_shm_format wl_format;
	struct tw_render_context *ctx;

	void (*destroy)(struct tw_render_texture *tex,
	                struct tw_render_context *ctx);
};


struct tw_render_context_impl {
	//TODO, change this as well.
	bool (*new_offscreen_surface)(struct tw_render_presentable *surf,
	                              struct tw_render_context *ctx,
	                              unsigned int width, unsigned int height);
	/** creating a render surface target for the given format, the format
	 * is platform specific */
	bool (*new_window_surface)(struct tw_render_presentable *surf,
	                           struct tw_render_context *ctx,
	                           void *native_window, uint32_t format);
};

/* we create this render context from scratch so we don't break everything, the
 * backends are still hooking to wlroots for now, they are created with a
 * tw_renderer, we will move backend on to this when we have our first backend.
 *
 */
struct tw_render_context {
	enum tw_renderer_type type;

	const struct tw_render_context_impl *impl;
	struct wl_display *display;
	struct wl_listener display_destroy;

	struct wl_list outputs;

	struct {
		struct wl_signal destroy;
		//this is plain damn weird.
		struct wl_signal output_lost;
		struct wl_signal wl_surface_dirty;
		struct wl_signal wl_surface_destroy;
	} signals;

	//globals
	struct tw_linux_dmabuf dma_manager;
	struct tw_compositor compositor_manager;

	struct wl_list pipelines;
};

struct tw_render_context *
tw_render_context_create_egl(struct wl_display *display,
                             const struct tw_egl_options *opts);
void
tw_render_context_destroy(struct tw_render_context *ctx);

void
tw_render_context_build_view_list(struct tw_render_context *ctx,
                                  struct tw_layers_manager *manager);
#ifdef  __cplusplus
}
#endif


#endif /* EOF */
