/*
 * xdg_grab.c - taiwins desktop grabs
 *
 * Copyright (c)  Xichen Zhou
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

#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <wayland-server.h>
#include <ctypes/helpers.h>
#include <taiwins/objects/seat.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/utils.h>
#include <wayland-util.h>

#include "xdg_internal.h"
#include "workspace.h"


/******************************************************************************
 * grab_interface API
 *****************************************************************************/

static void
tw_xdg_grab_interface_destroy(struct tw_xdg_grab_interface *gi)
{
	wl_list_remove(&gi->view_destroy_listener.link);
	free(gi);
}

static void
notify_grab_interface_view_destroy(struct wl_listener *listener, void *data)
{
	struct tw_xdg_grab_interface *gi =
		container_of(listener, struct tw_xdg_grab_interface,
		             view_destroy_listener);
	assert(data == gi->view);
	if (gi->pointer_grab.impl)
		tw_pointer_end_grab(&gi->pointer_grab.seat->pointer);
	else if (gi->keyboard_grab.impl)
		tw_keyboard_end_grab(&gi->keyboard_grab.seat->keyboard);
	else if (gi->touch_grab.impl)
		tw_touch_end_grab(&gi->touch_grab.seat->touch);
}

static struct tw_xdg_grab_interface *
tw_xdg_grab_interface_create(struct tw_xdg_view *view, struct tw_xdg *xdg,
                             const struct tw_pointer_grab_interface *pi,
                             const struct tw_keyboard_grab_interface *ki,
                             const struct tw_touch_grab_interface *ti)
{
	struct tw_xdg_grab_interface *gi = calloc(1, sizeof(*gi));
	if (!gi)
		return NULL;
	if (pi) {
		gi->pointer_grab.impl = pi;
		gi->pointer_grab.data = gi;
	} else if (ki) {
		gi->keyboard_grab.impl = ki;
		gi->keyboard_grab.data = gi;
	} else if (ti) {
		gi->touch_grab.impl = ti;
		gi->touch_grab.data = gi;
	}
	gi->gx = nanf("");
	gi->gy = nanf("");
	gi->view = view;
	gi->xdg = xdg;
	tw_signal_setup_listener(&view->dsurf_umapped_signal,
	                         &gi->view_destroy_listener,
	                         notify_grab_interface_view_destroy);
	return gi;
}

/******************************************************************************
 * pointer moving grab
 *****************************************************************************/

static void
handle_move_pointer_grab_motion(struct tw_seat_pointer_grab *grab,
                                uint32_t time_msec, double sx, double sy)
{
	struct tw_xdg_grab_interface *gi =
		container_of(grab, struct tw_xdg_grab_interface, pointer_grab);
	struct tw_xdg *xdg = gi->xdg;
	struct tw_workspace *ws = xdg->actived_workspace[0];
	struct tw_surface *surf = gi->view->dsurf->tw_surface;
	int32_t gx, gy;
	tw_surface_to_global_pos(surf, sx, sy, &gx, &gy);

	//TODO: when we set position for the view, here we immedidately changed
	//its position. flickering may caused from that. The cursor is fine.
	if (!isnan(gi->gx) && !isnan(gi->gy))
		tw_workspace_move_view(ws, gi->view, gx-gi->gx, gy-gi->gy);

	gi->gx = gx;
	gi->gy = gy;
}

static void
handle_move_pointer_grab_button(struct tw_seat_pointer_grab *grab,
	                   uint32_t time_msec, uint32_t button,
	                   enum wl_pointer_button_state state)
{
	struct tw_pointer *pointer = &grab->seat->pointer;
	if (state == WL_POINTER_BUTTON_STATE_RELEASED &&
	    pointer->btn_count == 0)
		tw_pointer_end_grab(pointer);
}

static void
handle_move_pointer_grab_cancel(struct tw_seat_pointer_grab *grab)
{
	struct tw_xdg_grab_interface *gi = grab->data;
	tw_xdg_grab_interface_destroy(gi);
}

static const struct tw_pointer_grab_interface move_pointer_grab_impl = {
	.motion = handle_move_pointer_grab_motion,
	.button = handle_move_pointer_grab_button,
	.cancel = handle_move_pointer_grab_cancel,
};

/******************************************************************************
 * pointer moving grab
 *****************************************************************************/

static void
handle_resize_pointer_grab_motion(struct tw_seat_pointer_grab *grab,
                                uint32_t time_msec, double sx, double sy)
{
	struct tw_xdg_grab_interface *gi =
		container_of(grab, struct tw_xdg_grab_interface, pointer_grab);
	struct tw_xdg *xdg = gi->xdg;
	struct tw_workspace *ws = xdg->actived_workspace[0];
	struct tw_surface *surf = gi->view->dsurf->tw_surface;
	int32_t gx, gy;

	tw_surface_to_global_pos(surf, sx, sy, &gx, &gy);

	if (!isnan(gi->gx) && !isnan(gi->gy))
		tw_workspace_resize_view(ws, gi->view, gx-gi->gx, gy-gi->gy,
		                         gi->edge);
	gi->gx = gx;
	gi->gy = gy;
}

static const struct tw_pointer_grab_interface resize_pointer_grab_impl = {
	.motion = handle_resize_pointer_grab_motion,
	.button = handle_move_pointer_grab_button, //same as move grab
	.cancel = handle_move_pointer_grab_cancel, //same as move grab
};


/******************************************************************************
 * exposed API
 *****************************************************************************/

bool
tw_xdg_start_moving_grab(struct tw_xdg *xdg, struct tw_xdg_view *view,
                         struct tw_seat *seat)
{
	struct tw_xdg_grab_interface *gi = NULL;
	if (seat->pointer.grab != &seat->pointer.default_grab) {
		goto err;
	}
	gi = tw_xdg_grab_interface_create(view, xdg, &move_pointer_grab_impl,
	                                  NULL, NULL);
	if (!gi)
		goto err;
	tw_pointer_start_grab(&seat->pointer, &gi->pointer_grab);
	return true;
err:
	return false;
}


bool
tw_xdg_start_resizing_grab(struct tw_xdg *xdg, struct tw_xdg_view *view,
                           enum wl_shell_surface_resize edge,
                           struct tw_seat *seat)
{
	struct tw_xdg_grab_interface *gi = NULL;
	if (seat->pointer.grab != &seat->pointer.default_grab) {
		goto err;
	}
	gi = tw_xdg_grab_interface_create(view, xdg, &resize_pointer_grab_impl,
	                                  NULL, NULL);
	if (!gi)
		goto err;
	gi->edge = edge;
	tw_pointer_start_grab(&seat->pointer, &gi->pointer_grab);
	return true;
err:
	return false;
}

bool
tw_xdg_start_task_switching_grab(struct tw_xdg *xdg, struct tw_seat *seat)
{
	//TODO implement this
	return false;
}
