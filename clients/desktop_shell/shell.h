/*
 * shell.h - taiwins client shell header
 *
 * Copyright (c) 2019-2021 Xichen Zhou
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

#ifndef TW_SHELL_CLIENT_H
#define TW_SHELL_CLIENT_H

#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/input.h>
#include <cairo/cairo.h>
#include <poll.h>
#include <wayland-client.h>

#include <wayland-taiwins-shell-client-protocol.h>
#include <wayland-taiwins-theme-client-protocol.h>
#include <ctypes/sequential.h>
#include <ctypes/os/file.h>
#include <twclient/client.h>
#include <twclient/shmpool.h>
#include <twidgets/nk_backends.h>
#include <twclient/theme.h>
#include <tdbus.h>

#include <shared_config.h>
#include <widget/widget.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TAIWINS_MAX_MENU_CMD_LEN 15
#define TAIWINS_MAX_MENU_ITEM_NAME 15
#define TAIWINS_MAX_MENU_LEN 16

struct tw_menu_item {

	struct {
		char title[TAIWINS_MAX_MENU_ITEM_NAME+1];
		/* short commands. long commands please use console */
		char cmd[TAIWINS_MAX_MENU_CMD_LEN+1];
	} endnode;
	/* submenu settings */
	bool has_submenu; /* has submenu */
	size_t len; /* submenu size */
};

struct shell_notif {
	char *msg;
	struct wl_list link;
};

struct shell_config {
	void *(*run_config)(struct shell_config *, const char *);
	void (*fini_config)(struct shell_config *);
	char *(*request_wallpaper)(struct shell_config *);
	void *config_data;
};

struct shell_output {
	struct desktop_shell *shell;
	struct taiwins_output *output;
	//options
	struct {
		struct tw_bbox bbox;
		int index;
	};
	struct taiwins_ui *bg_ui;
	struct taiwins_ui *pn_ui;
	struct tw_appsurf background;
	struct tw_appsurf panel;
	struct tw_app_event_filter background_events;
	//a temporary struct
	double widgets_span;
};

//state of current widget and widget to launch
struct widget_launch_info {
	uint32_t x;
	uint32_t y;
	struct shell_widget *widget;
	struct shell_widget *current;
	/**> which output to launch */
	struct shell_output *output;
};

struct desktop_shell {
	struct tw_globals globals;
	struct taiwins_shell *interface;
	struct taiwins_theme *theme_interface;
	enum taiwins_shell_panel_pos panel_pos;
	struct tdbus *system_bus;
	struct tdbus *session_bus;
	struct shell_config config;
	/**< panel bg configurations */
	struct {
		struct nk_wl_backend *panel_backend;
		struct nk_user_font *icon_font;
		char wallpaper_path[128];
		//TODO calculated from font size
		size_t panel_height;
	};
	/**< widget configurations */
	struct {
		struct nk_wl_backend *widget_backend;
		struct wl_list shell_widgets;
		struct widget_launch_info widget_launch;
		//surface like locker, on-screen-keyboard, menu, notification
		struct taiwins_ui *transient_ui;
		struct tw_appsurf transient;
	};

	/**< notifications for shell to show */
	struct {
		/* supporting functions should be push notificaiton; delete
		 * notification; since notification does not necessarily create, delete
		 * in order, we would need to use a list, there should also be a signal,
		 * notifying the any new notifications coming */
		struct wl_list msgs;
		struct tw_signal msg_recv_signal;
		struct tw_signal msg_del_signal;
	} notifs;

	vector_t menu;
	//outputs
	struct shell_output *main_output;
	struct shell_output shell_outputs[16];

	struct tw_theme theme;
};

static inline int
desktop_shell_n_outputs(struct desktop_shell *shell)
{
	for (int i = 0; i < 16; i++)
		if (shell->shell_outputs[i].shell == NULL)
			return i;
	return 16;
}

static inline void
shell_end_transient_surface(struct desktop_shell *shell)
{
	taiwins_ui_destroy(shell->transient_ui);
	shell->transient_ui = NULL;
	tw_appsurf_release(&shell->transient);
}

void
shell_load_wallpaper(struct desktop_shell *shell, const char *path);

void
shell_init_bg_for_output(struct shell_output *output);

void
shell_resize_bg_for_output(struct shell_output *output);

void
shell_init_panel_for_output(struct shell_output *output);

void
shell_resize_panel_for_output(struct shell_output *output);

void
shell_locker_init(struct desktop_shell *shell);

void
shell_process_msg(struct desktop_shell *shell, uint32_t type,
                  const struct wl_array *data);
static inline void
shell_notif_msg(struct desktop_shell *shell, int maxlen,
                const char *format, ...)
{
	struct wl_array data;
	char err_msg[maxlen+1];
	va_list ap;

        data.alloc = 0;
        data.data = err_msg;
        data.size = maxlen+1;

        va_start(ap, format);
        vsnprintf(err_msg, maxlen, format, ap);
        va_end(ap);

	shell_process_msg(shell, TAIWINS_SHELL_MSG_TYPE_NOTIFICATION,
	                  &data);
}

void
shell_tdbus_init(struct desktop_shell *shell);

void
shell_tdbus_end(struct desktop_shell *shell);

void
shell_cleanup_notifications(struct desktop_shell *shell);

void
shell_launch_menu(struct desktop_shell *shell, struct shell_output *o,
                  uint32_t x, uint32_t y);
void
shell_launch_notif(struct desktop_shell *shell, struct shell_notif *n);

void
shell_launch_widget(struct desktop_shell *shell);

void
shell_close_widget(struct desktop_shell *shell);

void *
shell_config_run_lua(struct shell_config *config, const char *path);

#ifdef __cplusplus
}
#endif


#endif /* EOF */
