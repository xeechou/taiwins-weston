/*
 * seat_pointer.c - taiwins server wl_keyboard implemetation
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
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>

#include <taiwins/objects/utils.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/seat.h>
#include <ctypes/os/os-compatibility.h>

static void
clear_focus_no_signal(struct tw_keyboard *keyboard)
{
	struct tw_seat_client *client;
	struct wl_resource *res;
	uint32_t serial;
	struct tw_seat *seat = wl_container_of(keyboard, seat, keyboard);

        if (keyboard->focused_surface && keyboard->focused_client) {
		client = keyboard->focused_client;
		serial = wl_display_next_serial(seat->display);
		wl_resource_for_each(res, &client->keyboards)
			wl_keyboard_send_leave(res, serial,
			                       keyboard->focused_surface);
		tw_reset_wl_list(&keyboard->focused_destroy.link);
	}
	keyboard->focused_client = NULL;
	keyboard->focused_surface = NULL;
}

static void
tw_keyboard_set_focus(struct tw_keyboard *keyboard,
                      struct wl_resource *wl_surface,
                      struct wl_array *focus_keys)
{
	struct wl_array zero_keys = {0};
	struct tw_seat_client *client;
	struct wl_resource *res;
	uint32_t serial;
	struct tw_seat *seat = wl_container_of(keyboard, seat, keyboard);

        if (wl_surface == keyboard->focused_surface)
		return;

	focus_keys = focus_keys ? focus_keys : &zero_keys;
	client = tw_seat_client_find(seat, wl_resource_get_client(wl_surface));
	if (client && !wl_list_empty(&client->keyboards)) {
		//clear the focus without signal
		clear_focus_no_signal(keyboard);

		serial = wl_display_next_serial(seat->display);
		wl_resource_for_each(res, &client->keyboards)
			wl_keyboard_send_enter(res, serial, wl_surface,
			                       focus_keys);
		keyboard->focused_client = client;
		keyboard->focused_surface = wl_surface;
		//set focus
		tw_reset_wl_list(&keyboard->focused_destroy.link);
		wl_resource_add_destroy_listener(wl_surface,
		                                 &keyboard->focused_destroy);
		wl_signal_emit(&seat->signals.focus, keyboard);
	}
}

static void
tw_keyboard_clear_focus(struct tw_keyboard *keyboard)
{
	struct tw_seat *seat = wl_container_of(keyboard, seat, keyboard);

        clear_focus_no_signal(keyboard);
        wl_signal_emit(&seat->signals.unfocus, keyboard);
}

WL_EXPORT void
tw_keyboard_default_enter(struct tw_seat_keyboard_grab *grab,
                          struct wl_resource *surface, uint32_t pressed[],
                          size_t n_pressed)
{
	struct tw_keyboard *keyboard = &grab->seat->keyboard;
	struct wl_array key_array;

	wl_array_init(&key_array);
	key_array.data = pressed;
	key_array.alloc = 0;
	key_array.size = sizeof(uint32_t) * n_pressed;

	if (surface)
		tw_keyboard_set_focus(keyboard, surface, &key_array);
	else
		tw_keyboard_clear_focus(keyboard);
}

WL_EXPORT void
tw_keyboard_default_key(struct tw_seat_keyboard_grab *grab, uint32_t time_msec,
                        uint32_t key, uint32_t state)
{
	struct tw_seat *seat = grab->seat;
	struct wl_resource *keyboard;
	struct tw_seat_client *client = seat->keyboard.focused_client;
	uint32_t serial;

	if (client) {
		serial = wl_display_next_serial(seat->display);
		wl_resource_for_each(keyboard, &client->keyboards)
			wl_keyboard_send_key(keyboard, serial, time_msec,
			                     key, state);
		seat->last_keyboard_serial = serial;
	}
}

WL_EXPORT void
tw_keyboard_default_modifiers(struct tw_seat_keyboard_grab *grab,
                              uint32_t mods_depressed, uint32_t mods_latched,
                              uint32_t mods_locked, uint32_t group)
{
	struct tw_seat *seat = grab->seat;
	struct tw_seat_client *client = seat->keyboard.focused_client;
	struct wl_resource *keyboard;
	uint32_t serial;

	if (client) {
		serial = wl_display_next_serial(seat->display);
		wl_resource_for_each(keyboard, &client->keyboards)
			wl_keyboard_send_modifiers(keyboard, serial,
			                           mods_depressed,
			                           mods_latched,
			                           mods_locked,
			                           group);
	}
}

WL_EXPORT void
tw_keyboard_default_cancel(struct tw_seat_keyboard_grab *grab)
{
}

static const struct tw_keyboard_grab_interface default_grab_impl = {
	.enter = tw_keyboard_default_enter,
	.key = tw_keyboard_default_key,
	.modifiers = tw_keyboard_default_modifiers,
	.cancel = tw_keyboard_default_cancel,
};

static void
notify_focused_disappear(struct wl_listener *listener, void *data)
{
	struct tw_keyboard *keyboard =
		wl_container_of(listener, keyboard, focused_destroy);

        keyboard->focused_client = NULL;
	keyboard->focused_surface = NULL;
	tw_reset_wl_list(&listener->link);
	tw_keyboard_clear_focus(keyboard); //just for emitting signals
}

WL_EXPORT struct tw_keyboard *
tw_seat_new_keyboard(struct tw_seat *seat)
{
	struct tw_keyboard *keyboard = &seat->keyboard;
	if (seat->capabilities & WL_SEAT_CAPABILITY_KEYBOARD)
		return keyboard;

	seat->keyboard.focused_client = NULL;
	seat->keyboard.focused_surface = NULL;
	seat->keyboard.keymap_size = 0;
	seat->keyboard.keymap_string = NULL;

	seat->keyboard.default_grab.data = NULL;
	seat->keyboard.default_grab.seat = seat;
	seat->keyboard.default_grab.impl = &default_grab_impl;
	seat->keyboard.grab = &keyboard->default_grab;

	wl_list_init(&keyboard->grabs);
	wl_list_init(&keyboard->focused_destroy.link);
	keyboard->focused_destroy.notify = notify_focused_disappear;

	seat->capabilities |= WL_SEAT_CAPABILITY_KEYBOARD;
	tw_seat_send_capabilities(seat);
	return keyboard;
}

WL_EXPORT void
tw_seat_remove_keyboard(struct tw_seat *seat)
{
	struct tw_seat_client *client;
	struct wl_resource *resource, *next;
	struct tw_keyboard *keyboard = &seat->keyboard;

	seat->capabilities &= ~WL_SEAT_CAPABILITY_KEYBOARD;
	tw_seat_send_capabilities(seat);

	//now we remove the link of the resources, the resource itself would get
	//destroyed in release request.
	wl_list_for_each(client, &seat->clients, link)
		wl_resource_for_each_safe(resource, next, &client->keyboards)
			tw_reset_wl_list(wl_resource_get_link(resource));

	if (keyboard->keymap_string)
		free(keyboard->keymap_string);
	keyboard->keymap_string = NULL;
	keyboard->keymap_size = 0;
	keyboard->focused_client = NULL;
	keyboard->focused_surface = NULL;
	tw_reset_wl_list(&keyboard->focused_destroy.link);
}

WL_EXPORT void
tw_keyboard_start_grab(struct tw_keyboard *keyboard,
                       struct tw_seat_keyboard_grab *grab, uint32_t priority)
{
	struct tw_seat_keyboard_grab *old = keyboard->grab;
	struct tw_seat *seat = wl_container_of(keyboard, seat, keyboard);

	if (keyboard->grab != grab &&
	    !tw_find_list_elem(&keyboard->grabs, &grab->node.link)) {
		struct wl_list *pos =
			tw_seat_grab_node_find_pos(&keyboard->grabs,
			                           priority);
		grab->seat = seat;
		grab->node.priority = priority;
		//swap grab and notify the old grab its replacement
		if (pos == &keyboard->grabs) {
			if (old != grab && old->impl->grab_action)
				old->impl->grab_action(old, TW_SEAT_GRAB_PUSH);
			keyboard->grab = grab;
		}
		wl_list_insert(pos, &grab->node.link);
	}
}

WL_EXPORT void
tw_keyboard_end_grab(struct tw_keyboard *keyboard,
                     struct tw_seat_keyboard_grab *grab)
{
	struct tw_seat_keyboard_grab *old = keyboard->grab;
	if (tw_find_list_elem(&keyboard->grabs, &grab->node.link)) {
		if (grab->impl->cancel)
			grab->impl->cancel(grab);
		tw_reset_wl_list(&grab->node.link);
	}
	//finding previous grab from list or default if stack is empty
	if (!wl_list_empty(&keyboard->grabs))
		grab = wl_container_of(keyboard->grabs.next, grab, node.link);
	else
		grab = &keyboard->default_grab;
	keyboard->grab = grab;
	keyboard->grab->seat =
		wl_container_of(keyboard, grab->seat, keyboard);
	//notify previous grab of its return
	if (grab != old && grab->impl->grab_action)
		grab->impl->grab_action(grab, TW_SEAT_GRAB_POP);
}

WL_EXPORT void
tw_keyboard_set_keymap(struct tw_keyboard *keyboard,
                       struct xkb_keymap *keymap)
{
	struct wl_resource *resource;
	struct tw_seat_client *client;
	struct tw_seat *seat = wl_container_of(keyboard, seat, keyboard);

	if (keyboard->keymap_string)
		free(keyboard->keymap_string);
	keyboard->keymap_string =
		xkb_keymap_get_as_string(keymap,
		                         XKB_KEYMAP_FORMAT_TEXT_V1);
	keyboard->keymap_size = strlen(keyboard->keymap_string);

	//send the keymap to all clients.
	wl_list_for_each(client, &seat->clients, link) {
		wl_resource_for_each(resource, &client->keyboards)
			tw_keyboard_send_keymap(keyboard, resource);
	}
}

WL_EXPORT void
tw_keyboard_send_keymap(struct tw_keyboard *keyboard,
                        struct wl_resource *resource)
{
	int keymap_fd;
	void *ptr;
	if (!keyboard->keymap_string)
		return;

	keymap_fd = os_create_anonymous_file(keyboard->keymap_size);
	if (keymap_fd < 0) {
		tw_logl("error creating kaymap file for %u bytes\n",
		        keyboard->keymap_size);
		return;
	}
	ptr = mmap(NULL, keyboard->keymap_size, PROT_READ | PROT_WRITE,
	           MAP_SHARED, keymap_fd, 0);
	if (ptr == MAP_FAILED) {
		tw_logl("error in mmap() for %u bytes\n",
		        keyboard->keymap_size);
		close(keymap_fd);
		return;
	}
	strcpy(ptr, keyboard->keymap_string);
	munmap(ptr, keyboard->keymap_size);
	wl_keyboard_send_keymap(resource, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
	                        keymap_fd, keyboard->keymap_size);
	close(keymap_fd);
}
