/*
 * internal.h - taiwins server drm backend internal header
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

#ifndef TW_DRM_INTERNAL_H
#define TW_DRM_INTERNAL_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdint.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <wayland-server.h>
#include <taiwins/backend_drm.h>
#include <taiwins/render_output.h>
#include <taiwins/objects/plane.h>
#include <taiwins/objects/drm_formats.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_drm_gpu;
struct tw_drm_plane;
struct tw_drm_display;
struct tw_drm_backend;

enum tw_drm_features {
	TW_DRM_CAP_ATOMIC = 1 << 0,
	TW_DRM_CAP_PRIME = 1 << 1,
	TW_DRM_CAP_MODIFIERS = 1 << 2,
	TW_DRM_CAP_DUMPBUFFER = 1 << 3,
};

enum tw_drm_plane_type {
	TW_DRM_PLANE_MAJOR,
	TW_DRM_PLANE_OVERLAY,
	TW_DRM_PLANE_CURSOR,
};

enum tw_drm_pending_flags {
	TW_DRM_PENDING_ACTIVE = 1 << 0,
	TW_DRM_PENDING_MODE = 1 << 1,
	TW_DRM_PENDING_CRTC = 1 << 2,
};

struct tw_drm_prop_info {
	const char *name;
	uint32_t *ptr;
};

struct tw_drm_crtc_props {
	//write
	uint32_t active;
	uint32_t mode_id; /**< dpms mode? */
};

struct tw_drm_connector_props {
	//read
	uint32_t edid;
	uint32_t dpms;
	//write
	uint32_t crtc_id;
};

struct tw_drm_plane_props {
	//read
	uint32_t type;
	uint32_t in_formats;

	//write
	uint32_t src_x;
	uint32_t src_y;
	uint32_t src_w;
	uint32_t src_h;
	uint32_t crtc_x;
	uint32_t crtc_y;
	uint32_t crtc_w;
	uint32_t crtc_h;
	uint32_t crtc_id;
	uint32_t fb_id;
};

enum tw_drm_fb_type {
	TW_DRM_FB_SURFACE,
	TW_DRM_FB_WL_BUFFER,
};

// a framebuffer on a plane, it can be generated from gbm_surface or be a
// surfaceless bo imported from dma or wl_buffer or other things. I may be able
// to implement
struct tw_drm_fb {
	enum tw_drm_fb_type type;
	struct {
		struct gbm_bo *bo;
		struct gbm_surface *surf;
	} gbm;
};

void tw_drm_fb_release(struct tw_drm_fb *fb);

void tw_drm_plane_commit_fb(struct tw_drm_plane *plane);

struct tw_drm_plane {
	struct tw_plane base;
	uint32_t id; /**< drm plane id */
	uint32_t crtc_mask;
	enum tw_drm_plane_type type;

	struct tw_drm_formats formats;
	struct tw_drm_plane_props props;

	struct tw_drm_fb pending, current;
};

struct tw_drm_crtc {
	int id, idx;
	/** occupied by display */
	struct tw_drm_display *display;
	struct wl_list link; /** drm->crtc_list */

	struct tw_drm_crtc_props props;
};

struct tw_drm_mode_info {
	drmModeModeInfo info;
	struct tw_output_device_mode mode;
};

struct tw_drm_display {
	struct tw_render_output output;
	struct tw_drm_backend *drm;
	struct tw_drm_gpu *gpu;

	int conn_id, crtc_mask;
	uint32_t planes; /**< TODO possible planes used by display */
	/** output has at least one primary plane */
	struct tw_drm_plane *primary_plane;
	struct tw_drm_crtc *crtc;

	struct {
		enum tw_drm_pending_flags pending;
		bool connected, active;
		int crtc_id; /* crtc_id read from connector, may not work */
		int mode_id; /* mode_id submitting to server */
		drmModeModeInfo mode;
		struct wl_array modes;
	} status;

	/** we need a plane to create data on */
	struct {
		struct gbm_surface *gbm;
	} gbm_surface;

	struct tw_drm_connector_props props;

	struct wl_listener presentable_commit;
};

struct tw_drm_gpu {
	int gpu_fd, sysnum;
	bool boot_vga;
	bool activated; /**< valid gpu otherwise not used */
	enum tw_drm_features feats;
	struct tw_drm_backend *drm;
	struct wl_event_source *event;

	struct {
		int max_width, max_height;
		int min_width, min_height;
	} limits;

	struct tw_drm_crtc crtcs[32];
	uint32_t crtc_mask;
	struct wl_list crtc_list;

	uint32_t plane_mask;
	struct tw_drm_plane planes[32];
	struct wl_list plane_list;

	union {
		struct {
			struct gbm_device *dev;
			uint32_t visual_id;
		} gbm;
		struct {
			EGLDeviceEXT egldev;
		} eglstream;
	};
	struct tw_drm_connector_props props;
};

/**
 * @brief drm backend datasheet
 *
 * We need the backend to support multiple allocation APIs(gbm or eglstream),
 * multiple rendering API (egl or vulkan).
 * commit routine (atomic or legacy)
 *
 * The 3 event sources:
 * 1): drm fd for drmHandleEvents.
 * 2): event from login for session changes.
 * 3): event from udev for udev_device change.
 */
struct tw_drm_backend {
	struct tw_backend base;
	struct wl_display *display;
	struct tw_login *login;
	struct tw_drm_gpu *boot_gpu;
	struct wl_array gpus;

	struct wl_listener display_destroy;
	struct wl_listener login_listener;
};

void
tw_drm_print_info(int fd);

bool
tw_drm_init_gpu_gbm(struct tw_drm_gpu *gpu);

void
tw_drm_fini_gpu_gbm(struct tw_drm_gpu *gpu);

int
tw_drm_handle_drm_event(int fd, uint32_t mask, void *data);

void
tw_drm_free_gpu_resources(struct tw_drm_gpu *gpu);

bool
tw_drm_check_gpu_features(struct tw_drm_gpu *gpu);

/** the function destroys the output, which is fine, since we either call
 * this function on gpu init fail or destruction. */
bool
tw_drm_check_gpu_resources(struct tw_drm_gpu *gpu);

bool
tw_drm_plane_init(struct tw_drm_plane *plane, int fd,
                  drmModePlane *drm_plane);
void
tw_drm_plane_fini(struct tw_drm_plane *plane);

void
tw_drm_display_start(struct tw_drm_display *display);

void
tw_drm_display_atomic_pageflip(struct tw_drm_display *display);

void
tw_drm_display_legacy_pageflip(struct tw_drm_display *display);

bool
tw_drm_display_start_gbm(struct tw_drm_display *display);

void
tw_drm_display_fini_gbm(struct tw_drm_display *output);

void
tw_drm_display_remove(struct tw_drm_display *display);

struct tw_drm_display *
tw_drm_display_find_create(struct tw_drm_gpu *gpu, drmModeConnector *conn);

/**
 * scan the properties by binary search, info has to be ordered
 */
bool
tw_drm_read_properties(int fd, uint32_t obj_id, uint32_t obj_type,
                       struct tw_drm_prop_info *info, size_t prop_len);
bool
tw_drm_get_property(int fd, uint32_t obj_id, uint32_t obj_type,
                    const char *prop_name, uint64_t *value);
struct gbm_surface *
tw_drm_create_gbm_surface(struct gbm_device *dev, uint32_t w, uint32_t h,
                          uint32_t format, int n_mods, uint64_t *modifiers,
                          uint32_t flags);
uint32_t
tw_drm_gbm_get_fb(struct gbm_bo *bo);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
