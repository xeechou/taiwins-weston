/*
 * output_device.h - taiwins server output device header
 *
 * Copyright (c) 2020 Xichen Zhou
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

#ifndef TW_OUTPUT_DEVICE_H
#define TW_OUTPUT_DEVICE_H

#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <pixman.h>

#include <taiwins/objects/matrix.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_output_device;

struct tw_output_device_mode {
	int32_t w, h; /** indicate the pixel size of the output */
	int32_t refresh; /** -1 means unavailable */
	bool preferred;
};

struct tw_output_device_state {
	bool enabled;
	float scale;
	int32_t gx, gy; /**< x,y position in global space */
	struct tw_mat3 view_2d;
	enum wl_output_subpixel subpixel;
	enum wl_output_transform transform;
	/* current mode indicates the actual window size, the effective size
	 * is actual_size / scale */
	struct tw_output_device_mode current_mode;
};

struct tw_output_device_impl {
	void (*commit_state) (struct tw_output_device *device);
};

/**
 * @brief abstraction of an output device
 *
 * Backends will pass a tw_output_device in the new_output signal.

 * At the moment, tw_backend_output has quite a few methods that actually does
 * not belong to it, currently the user is the configs. We want
 * tw_backend_output to include this and drive direct on output devices's
 * methods.
 *
 * We would probably need to include
 */
struct tw_output_device {
	char name[32], make[32], model[32];
	char serial[16];
	int32_t phys_width, phys_height;

	/** a native window for different backend, could be none */
	void *native_window;
	const struct tw_output_device_impl *impl;
	struct wl_list link; /** backend: list */
	struct wl_array available_modes;

	struct tw_output_device_state state, pending;

	struct {
		struct wl_signal destroy;
		struct wl_signal present;
		struct wl_signal new_frame;
		/** emit when general wl_output information is available, or
		 * when output state changed */
                struct wl_signal info;
		/** emit right after applying pending state, backend could
		 * listen on this for applying the states to the hardware */
		struct wl_signal commit_state;
	} events;
};

void
tw_output_device_init(struct tw_output_device *device,
                      const struct tw_output_device_impl *impl);
void
tw_output_device_fini(struct tw_output_device *device);

void
tw_output_device_set_scale(struct tw_output_device *device, float scale);

void
tw_output_device_commit_state(struct tw_output_device *device);

pixman_rectangle32_t
tw_output_device_geometry(const struct tw_output_device *device);

/**
 * @brief mapping (0...1) in the output to global position
 */
void
tw_output_device_loc_to_global(const struct tw_output_device *device,
                               float x, float y, float *gx, float *gy);

void
tw_output_device_state_rebuild_view_mat(struct tw_output_device_state *state);

#ifdef  __cplusplus
}
#endif

#endif
