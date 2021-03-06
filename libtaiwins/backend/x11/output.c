/*
 * output.c - taiwins server x11 backend output implementation
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

#include <assert.h>
#include <string.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <time.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/utils.h>

#include <taiwins/input_device.h>
#include <taiwins/output_device.h>

#include "internal.h"

#define DEFAULT_REFRESH (60 * 1000) /* 60Hz */
#define FRAME_DELAY (1000000 / DEFAULT_REFRESH)

static bool
tw_x11_output_start(struct tw_x11_output *output);

static void
tw_x11_output_resize(struct tw_x11_output *output);

static bool
check_pending_stop(struct tw_output_device *dev)
{
	struct tw_x11_output *output =
		wl_container_of(dev, output, output.device);
	bool penable = dev->pending.enabled;

	if (output->win != XCB_WINDOW_NONE && !penable) {
		tw_logl_level(TW_LOG_WARN, "changing x11_output@%s to %s is "
		              "not possible after started",
		              dev->name, penable ? "true" : "false");
		return false;
	}
	return true;
}

static bool
handle_commit_output_state(struct tw_output_device *output)
{
	struct tw_x11_output *x11_output =
		wl_container_of(output, x11_output, output.device);
	bool enabled = output->pending.enabled || output->current.enabled;
	struct tw_output_device_state pending = output->pending;
	bool resize = !tw_output_device_mode_eq(&output->current.current_mode,
	                                        &output->pending.current_mode);

	pending.current_mode.refresh = DEFAULT_REFRESH;
	pending.enabled = enabled;
	if (!check_pending_stop(output))
		return false;
	if (tw_output_device_state_eq(&output->current, &pending))
		return false;

	assert(pending.scale >= 1.0);
	assert(pending.current_mode.h > 0 && pending.current_mode.w > 0);

	//the x11 backend will simply resize the output for us so we only need
	//to update the view matrix
	memcpy(&output->current, &pending, sizeof(output->current));

	if (x11_output->win == XCB_WINDOW_NONE && enabled)
		tw_x11_output_start(x11_output);
	else if (resize)
		tw_x11_output_resize(x11_output);
	return true;
}

static const struct tw_output_device_impl x11_output_impl = {
	.commit_state = handle_commit_output_state,
};

static int
frame_handler(void *data)
{
	struct tw_x11_output *output = data;
	wl_signal_emit(&output->output.signals.need_frame,
	               &output->output);
	wl_event_source_timer_update(output->frame_timer,
	                             FRAME_DELAY);
	return 0;
}

static void
parse_output_setup(struct tw_x11_output *output, xcb_connection_t *xcb)
{
	const xcb_setup_t *xcb_setup = xcb_get_setup(xcb);

	snprintf(output->output.device.make,
	         sizeof(output->output.device.make),
	         "%.*s", xcb_setup_vendor_length(xcb_setup),
	         xcb_setup_vendor(xcb_setup));

	snprintf(output->output.device.model,
	         sizeof(output->output.device.model),
	         "%"PRIu16".%"PRIu16,
	         xcb_setup->protocol_major_version,
	         xcb_setup->protocol_minor_version);
}

void
tw_x11_remove_output(struct tw_x11_output *output)
{
	struct tw_x11_backend *x11 = output->x11;

	tw_input_device_fini(&output->pointer);
	wl_event_source_remove(output->frame_timer);

	tw_reset_wl_list(&output->output_commit_listener.link);
	tw_render_output_fini(&output->output);
	xcb_destroy_window(x11->xcb_conn, output->win);
	xcb_flush(x11->xcb_conn);
	free(output);
}

static void
notify_output_commit(struct wl_listener *listener, void *data)
{
	struct tw_x11_output *output =
		wl_container_of(listener, output, output_commit_listener);
	//TODO: the x11 backend commit logic is broke here. in weston they wait
	//for 10 ms then do the present
	tw_render_output_present(&output->output, NULL);
	tw_render_output_clean_maybe(&output->output);
}

static void
tw_x11_output_resize(struct tw_x11_output *output)
{
	uint32_t values[2];
	tw_output_device_raw_resolution(&output->output.device,
	                                &values[0], &values[1]);

	xcb_configure_window(output->x11->xcb_conn,
	                     output->win,
	                     XCB_CONFIG_WINDOW_WIDTH |
	                     XCB_CONFIG_WINDOW_HEIGHT,
	                     values);
	xcb_flush(output->x11->xcb_conn);
}

static bool
tw_x11_output_start(struct tw_x11_output *output)
{
	struct tw_x11_backend *x11 = output->x11;
	struct wl_event_loop *loop = wl_display_get_event_loop(x11->display);
	unsigned width, height;

	//for the given window
	uint32_t win_mask = XCB_CW_EVENT_MASK;
	uint32_t mask_values[] = {
		XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
	};

	tw_signal_setup_listener(&output->output.surface.commit,
	                         &output->output_commit_listener,
	                         notify_output_commit);

	tw_render_output_set_context(&output->output, x11->base.ctx);
	tw_output_device_raw_resolution(&output->output.device,
	                                &width, &height);

        //creating the window but not mapping it yet.
	output->win = xcb_generate_id(x11->xcb_conn);
	xcb_create_window(x11->xcb_conn, XCB_COPY_FROM_PARENT, output->win,
	                  x11->screen->root, 0, 0,
	                  width, height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
	                  x11->screen->root_visual,
	                  win_mask, mask_values);

        //setup the input events for the output.
	struct {
		xcb_input_event_mask_t head;
		xcb_input_xi_event_mask_t mask;
	} xinput_mask = {
		.head = {
			.deviceid = XCB_INPUT_DEVICE_ALL_MASTER,
			.mask_len = 1
		},
		.mask = _XINPUT_EVENT_MASK,
	};

	xcb_input_xi_select_events(x11->xcb_conn, output->win, 1,
	                           &xinput_mask.head);
	//creating the render surface
	if (!tw_render_presentable_init_window(&output->output.surface,
	                                       x11->base.ctx, &output->win,
	                                       x11->screen->root_visual)) {
		tw_logl_level(TW_LOG_WARN, "failed to create render surface "
		              "for X11 output");
		tw_x11_remove_output(output);
		return false;
	}

	xcb_change_property(x11->xcb_conn, XCB_PROP_MODE_REPLACE, output->win,
		x11->atoms.wm_protocols, XCB_ATOM_ATOM, 32, 1,
		&x11->atoms.wm_delete_window);

	xcb_map_window(x11->xcb_conn, output->win);
	xcb_flush(x11->xcb_conn);

	output->frame_timer = wl_event_loop_add_timer(loop, frame_handler,
	                                              output);
	wl_event_source_timer_update(output->frame_timer, FRAME_DELAY);

	//signal the pointer for the output
	wl_signal_emit(&x11->base.signals.new_input, &output->pointer);

	return true;
}

void
tw_x11_output_start_maybe(struct tw_x11_output *output)
{
	struct tw_x11_backend *x11 = output->x11;
	struct tw_output_device *dev = &output->output.device;

	wl_signal_emit(&x11->base.signals.new_output, dev);
	tw_output_device_commit_state(dev);
}


WL_EXPORT bool
tw_x11_backend_add_output(struct tw_backend *backend,
                          unsigned int width, unsigned int height)
{
	struct tw_x11_backend *x11 = wl_container_of(backend, x11, base);
	struct tw_x11_output *output = calloc(1, sizeof(*output));

        if (!output)
		return false;
        output->x11 = x11;

        tw_render_output_init(&output->output, &x11_output_impl, x11->display);
        tw_output_device_set_custom_mode(&output->output.device, width, height,
                                         0);

        sprintf(output->output.device.name, "X11-%d",
                wl_list_length(&x11->base.outputs));
        parse_output_setup(output, x11->xcb_conn);

        wl_list_insert(&x11->base.outputs, &output->output.device.link);

        tw_input_device_init(&output->pointer, TW_INPUT_TYPE_POINTER, 0, NULL);
        strncpy(output->pointer.name, "X11-pointer",
                sizeof(output->pointer.name));

        wl_list_insert(x11->base.inputs.prev, &output->pointer.link);

        if (backend->started)
	        tw_x11_output_start_maybe(output);

	return true;
}
