/**
 * @file test-xkbcommon.c
 *
 * common pratice of using xkbcommon as a wayland client
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <wayland-client.h>


struct seat {
	struct wl_seat *s;
	uint32_t id;
	struct wl_keyboard *keyboard;
	struct wl_pointer *pointer;
	struct wl_touch *touch;
	const char *name;
	//keyboard informations
	struct xkb_context *kctxt;
	struct xkb_keymap *kmap;
	struct xkb_state  *kstate;
} seat0;

//struct wl_seat *seat0;

//keyboard listener, you will definitly announce the
static void handle_keymap(void *data, struct wl_keyboard *wl_keyboard,
		   uint32_t format,
		   int32_t fd,
		   uint32_t size)
{
	struct seat *seat0 = (struct seat *)data;
	//now it is the time to creat a context
	seat0->kctxt = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	void *addr = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
//	printf("%s\n", addr);
	seat0->kmap = xkb_keymap_new_from_string(seat0->kctxt, addr, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(addr, size);
	seat0->kstate = xkb_state_new(seat0->kmap);
}
static
void handle_key(void *data,
		struct wl_keyboard *wl_keyboard,
		uint32_t serial,
		uint32_t time,
		uint32_t key,
		uint32_t state)
{
	//keycode of C and c is the same
	char keysym_name[64];
	struct seat *seat0 = (struct seat *)data;
	xkb_keysym_t keysym = xkb_state_key_get_one_sym(seat0->kstate, key+8);
	xkb_keysym_get_name(keysym, keysym_name, sizeof(keysym_name));
	//a and ctrl-a is the same
	fprintf(stderr, "got a key %d for the code %d with name %s\n", keysym, key+8, keysym_name);
	//now it is time to decode
}
static
void handle_modifiers(void *data,
		      struct wl_keyboard *wl_keyboard,
		      uint32_t serial,
		      uint32_t mods_depressed, //which key
		      uint32_t mods_latched,
		      uint32_t mods_locked,
		      uint32_t group)
{
	fprintf(stderr, "We pressed a modifier\n");
	//I guess this serial number is different for each event
	struct seat *seat0 = (struct seat *)data;
	//wayland uses layout group. you need to know what xkb_matched_layout is
	xkb_state_update_mask(seat0->kstate, mods_depressed, mods_latched, mods_locked, 0, 0, group);
}
static
void handle_keyboard_enter(void *data,
			   struct wl_keyboard *wl_keyboard,
			   uint32_t serial,
			   struct wl_surface *surface,
			   struct wl_array *keys)
{
	fprintf(stderr, "keyboard got focus\n");
}
static
void handle_keyboard_leave(void *data,
		    struct wl_keyboard *wl_keyboard,
		    uint32_t serial,
		    struct wl_surface *surface)
{
	fprintf(stderr, "keyboard lost focus\n");
}
static
void handle_repeat_info(void *data,
			    struct wl_keyboard *wl_keyboard,
			    int32_t rate,
			    int32_t delay)
{

}



static
struct wl_keyboard_listener keyboard_listener = {
	.key = handle_key,
	.modifiers = handle_modifiers,
	.enter = handle_keyboard_enter,
	.leave = handle_keyboard_leave,
	.keymap = handle_keymap,
	.repeat_info = handle_repeat_info,
};


static
void seat_capabilities(void *data,
		       struct wl_seat *wl_seat,
		       uint32_t capabilities)
{
	struct seat *seat0 = (struct seat *)data;
	if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
		seat0->keyboard = wl_seat_get_keyboard(wl_seat);
		fprintf(stderr, "got a keyboard\n");
		wl_keyboard_add_listener(seat0->keyboard, &keyboard_listener, seat0);
	}
	if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
		seat0->pointer = wl_seat_get_pointer(wl_seat);
		fprintf(stderr, "got a mouse\n");
	}
	if (capabilities & WL_SEAT_CAPABILITY_TOUCH) {
		seat0->touch = wl_seat_get_touch(wl_seat);
		fprintf(stderr, "got a touchpad\n");
	}
}

static void
seat_name(void *data, struct wl_seat *wl_seat, const char *name)
{
	struct seat *seat0 = (struct seat *)data;
	fprintf(stderr, "we have this seat with a name called %s\n", name);
	seat0->name = name;
}

static
struct wl_seat_listener seat_listener = {
	.capabilities = seat_capabilities,
	.name = seat_name,
};



////shell with the input
static struct wl_shell *gshell;
static struct wl_compositor *gcompositor;
struct wl_shm *shm;



static void
shm_format(void *data, struct wl_shm *wl_shm, uint32_t format)
{
    //struct display *d = data;

    //	d->formats |= (1 << format);
    fprintf(stderr, "Format %d\n", format);
}

static struct wl_shm_listener shm_listener = {
	shm_format
};

static
void announce_globals(void *data,
		       struct wl_registry *wl_registry,
		       uint32_t name,
		       const char *interface,
		       uint32_t version)
{
	(void)data;
	if (strcmp(interface, wl_seat_interface.name) == 0) {
		seat0.id = name;
		seat0.s = wl_registry_bind(wl_registry, name, &wl_seat_interface, version);
		wl_seat_add_listener(seat0.s, &seat_listener, &seat0);
	} else if (strcmp(interface, wl_shell_interface.name) == 0) {
		fprintf(stderr, "announcing the shell\n");
		gshell = wl_registry_bind(wl_registry, name, &wl_shell_interface, version);
	} else if (strcmp(interface, wl_compositor_interface.name) == 0) {
		fprintf(stderr, "announcing the compositor\n");
		gcompositor = wl_registry_bind(wl_registry, name, &wl_compositor_interface, version);
	} else if (strcmp(interface, wl_shm_interface.name) == 0)  {
		fprintf(stderr, "got a shm handler\n");
		shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, version);
		wl_shm_add_listener(shm, &shm_listener, NULL);
	}


}
static
void announce_global_remove(void *data,
		      struct wl_registry *wl_registry,
		      uint32_t name)
{
	if (name == seat0.id) {
		fprintf(stderr, "wl_seat removed");
	}

}


static struct wl_registry_listener registry_listener = {
	.global = announce_globals,
	.global_remove = announce_global_remove
};

static
void
handle_ping(void *data,
		     struct wl_shell_surface *wl_shell_surface,
		     uint32_t serial)
{
	fprintf(stderr, "ping!!!\n");
}

static void
handle_configure(void *data, struct wl_shell_surface *shell_surface,
		 uint32_t edges, int32_t width, int32_t height)
{
}

static void
handle_popup_done(void *data, struct wl_shell_surface *shell_surface)
{
}

struct wl_shell_surface_listener pingpong = {
	.ping = handle_ping,
	.configure = handle_configure,
	.popup_done = handle_popup_done
};





#define WIDTH 400
#define HEIGHT 200
void *shm_data;

//struct wl_buffer *buffer;

extern int os_create_anonymous_file(off_t size);

static struct wl_buffer *
create_buffer() {
    struct wl_shm_pool *pool;
    int stride = WIDTH * 4; // 4 bytes per pixel
    int size = stride * HEIGHT;
    int fd;
    struct wl_buffer *buff;

    fd = os_create_anonymous_file(size);
    if (fd < 0) {
	fprintf(stderr, "creating a buffer file for %d B failed: %m\n",
		size);
	exit(1);
    }

    shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm_data == MAP_FAILED) {
	fprintf(stderr, "mmap failed: %m\n");
	close(fd);
	exit(1);
    }

    pool = wl_shm_create_pool(shm, fd, size);
    buff = wl_shm_pool_create_buffer(pool, 0,
					  WIDTH, HEIGHT,
					  stride,
					  WL_SHM_FORMAT_XRGB8888);
    //wl_buffer_add_listener(buffer, &buffer_listener, buffer);
    wl_shm_pool_destroy(pool);
    return buff;
}

int main(int argc, char *argv[])
{
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {

		return -1;
	}

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);

	//okay, now we need to create surface
	struct wl_surface *surface = wl_compositor_create_surface(gcompositor);
	if (!surface) {
		fprintf(stderr, "cant creat a surface\n");
		return -1;
	}
	struct wl_shell_surface *shell_surface = wl_shell_get_shell_surface(gshell, surface);
	wl_shell_surface_add_listener(shell_surface, &pingpong, NULL);
	wl_shell_surface_set_toplevel(shell_surface);
	struct wl_buffer *buffer = create_buffer();
	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_commit(surface);
	//create a buffer and attach to it, so we can see something
//	wl_surface_attach(surface, struct wl_buffer *buffer, int32_t x, int32_t y)


	while(wl_display_dispatch(display) != -1);

	wl_display_disconnect(display);
	return 0;
}
