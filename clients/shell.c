#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <wayland-taiwins-shell-client-protocol.h>
#include <wayland-client.h>
#include <sequential.h>
#include "client.h"
#include "ui.h"

struct output_widgets;
struct desktop_shell;

struct output_widgets {
	struct desktop_shell *shell;
	struct wl_output *output;
	struct app_surface background;
	struct shell_panel panel;
	struct shm_pool pool;

	list_t link;
	bool inited;
};

struct desktop_shell {
	struct wl_globals globals;
	struct taiwins_shell *shell;
	//right now we only have one output, but we still keep the info
	list_t outputs;
};

static void
shell_panel_init(struct shell_panel *panel, struct output_widgets *w)
{
	struct app_surface *s = &panel->panelsurf;
	panel->panelsurf = (struct app_surface){0};
	s->wl_surface = wl_compositor_create_surface(w->shell->globals.compositor);
	s->keycb = NULL;
	wl_surface_set_user_data(s->wl_surface, s);
	s->wl_output = w->output;
	s->type = APP_PANEL;
	panel->widgets = (vector_t){0};
}

static void
output_init(struct output_widgets *w)
{
	struct taiwins_shell *shell = w->shell->shell;
	//arriere-plan
	w->background = (struct app_surface){0};
	w->background.wl_surface = wl_compositor_create_surface(w->shell->globals.compositor);
	w->background.wl_output = w->output;
	w->background.keycb = NULL;
	wl_surface_set_user_data(w->background.wl_surface, &w->background);
	w->background.type = APP_BACKGROUND;
	taiwins_shell_set_background(shell, w->output, w->background.wl_surface);
	//panel
	shell_panel_init(&w->panel, w);
	taiwins_shell_set_panel(shell, w->output, w->panel.panelsurf.wl_surface);
	w->inited = true;
}

static void
output_create(struct output_widgets *w, struct wl_output *wl_output, struct desktop_shell *twshell)
{
	w->shell = twshell;
	shm_pool_create(&w->pool, twshell->globals.shm, 4096);
	w->inited = false;
	w->output = wl_output;
	wl_output_set_user_data(wl_output, w);
	if (w->shell->shell)
		output_init(w);
}


static void
output_distroy(struct output_widgets *o)
{
	wl_output_release(o->output);
}


static void
desktop_shell_init(struct desktop_shell *shell, struct wl_display *display)
{
	wl_globals_init(&shell->globals, display);
	list_init(&shell->outputs);
	shell->shell = NULL;
}

static void
desktop_shell_release(struct desktop_shell *shell)
{
	taiwins_shell_destroy(shell->shell);
	struct output_widgets *w, *next;
	list_for_each_safe(w, next, &shell->outputs, link) {
		list_remove(&w->link);
		output_distroy(w);
	}
	wl_globals_release(&shell->globals);
}



static void
shell_configure_surface(void *data,
			struct taiwins_shell *taiwins_shell,
			struct wl_surface *surface,
			uint32_t scale,
			uint32_t edges,
			int32_t width,
			int32_t height)
{
	struct output_widgets *output;
	//damn it, we need a hack
	struct app_surface *appsurf = (struct app_surface *)wl_surface_get_user_data(surface);
	if (appsurf->type == APP_BACKGROUND)
		output = container_of(appsurf, struct output_widgets, background);
	else if (appsurf->type == APP_PANEL)
		output = container_of(appsurf, struct output_widgets, panel);

	int32_t w = scale *(width - edges);
	int32_t h = scale *(height - edges);

	void *buffer_addr = NULL;
	struct wl_buffer *new_buffer = shm_pool_alloc_buffer(&output->pool, w, h);
	buffer_addr = shm_pool_buffer_access(new_buffer);
	appsurf->w = w;
	appsurf->h = h;
	appsurf->px = 0;
	appsurf->py = 0;

	if (appsurf->type == APP_BACKGROUND) {
		printf("background surface buffer %p, wl_buffer: %p\n", buffer_addr, new_buffer);
		char imgpath[100];
		sprintf(imgpath, "%s/.wallpaper/wallpaper.png", getenv("HOME"));
		if (load_image(imgpath, WL_SHM_FORMAT_ARGB8888, w, h,
			       (unsigned char *)buffer_addr) != buffer_addr) {
			fprintf(stderr, "failed to load image somehow\n");
		}
		wl_surface_attach(output->background.wl_surface, new_buffer, 0, 0);
		wl_surface_damage(output->background.wl_surface, 0, 0, w, h);
		wl_surface_commit(output->background.wl_surface);
		//TODO maybe using the double buffer?
		if (output->background.wl_buffer)
			shm_pool_buffer_release(output->background.wl_buffer);
		output->background.wl_buffer = new_buffer;

	} else if (appsurf->type == APP_PANEL) {
		struct shell_panel *panel = container_of(appsurf, struct shell_panel, panelsurf);
		panel->format = WL_SHM_FORMAT_ABGR8888;
		printf("panel surface buffer %p, wl_buffer: %p\n", buffer_addr, new_buffer);
		memset(buffer_addr, 255, w*h*4);
		wl_surface_attach(output->panel.panelsurf.wl_surface, new_buffer, 0, 0);
		wl_surface_damage(output->panel.panelsurf.wl_surface, 0, 0, w, h);
		wl_surface_commit(output->panel.panelsurf.wl_surface);
		if (output->panel.panelsurf.wl_buffer)
			shm_pool_buffer_release(output->panel.panelsurf.wl_buffer);
		output->panel.panelsurf.wl_buffer = new_buffer;
		struct eglapp *app;
		app = eglapp_addtolist(&output->panel);
		eglapp_init_with_funcs(app, calendar_icon, NULL);
	}

}
/* if (output->background.wl_buffer[0]) */
/*	shm_pool_buffer_release(output->background.wl_buffer[0]); */
/* swap(output->background.wl_buffer[0], */
/*      output->background.wl_buffer[1]); */
/* if (!output->background.wl_buffer[0]) */
/*	output->background.wl_buffer[0] = shm_pool_alloc_buffer(&output->pool, w, h); */
/* else if (shm_pool_buffer_size(output->background.wl_buffer[0]) != w * h * 4) { */
/*	shm_pool_buffer_release(output->background.wl_buffer[0]); */
/*	output->background.wl_buffer[0] = shm_pool_alloc_buffer(&output->pool, w, h); */
/* } */
/* buffer_addr = shm_pool_buffer_access(output->background.wl_buffer[0]); */



static struct taiwins_shell_listener taiwins_listener = {
	.configure = shell_configure_surface
};


static
void announce_globals(void *data,
		      struct wl_registry *wl_registry,
		      uint32_t name,
		      const char *interface,
		      uint32_t version)
{
	struct desktop_shell *twshell = (struct desktop_shell *)data;

	if (strcmp(interface, taiwins_shell_interface.name) == 0) {
		fprintf(stderr, "shell registé\n");
		twshell->shell = wl_registry_bind(wl_registry, name, &taiwins_shell_interface, version);
		taiwins_shell_add_listener(twshell->shell, &taiwins_listener, twshell);
	} else if (!strcmp(interface, wl_output_interface.name)) {
		struct output_widgets *output = malloc(sizeof(*output));
		struct wl_output *wl_output = wl_registry_bind(wl_registry, name, &wl_output_interface, version);
		output_create(output, wl_output, twshell);
		list_append(&twshell->outputs, &output->link);

	} else
		wl_globals_announce(&twshell->globals, wl_registry, name, interface, version);
}

static void
announce_global_remove(void *data, struct wl_registry *wl_registry, uint32_t name)
{

}

static struct wl_registry_listener registry_listener = {
	.global = announce_globals,
	.global_remove = announce_global_remove
};


int main(int argc, char **argv)
{
	struct desktop_shell oneshell;
	//TODO change to wl_display_connect_to_fd
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "couldn't connect to wayland display\n");
		return -1;
	}
	desktop_shell_init(&oneshell, display);

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, &oneshell);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);
	{	//initialize the output
		struct output_widgets *w, *next;
		list_for_each_safe(w, next, &oneshell.outputs, link) {
			if (!w->inited)
				output_init(w);
		}
	}

	//also, you need to create some kind of event_queue for the shell, and
	//launch a thread to read this event queue, a event should have a
	//listener, call the listener
	while(wl_display_dispatch(display) != -1);
	desktop_shell_release(&oneshell);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
	return 0;
}
