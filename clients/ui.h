#ifndef TW_UI_H
#define TW_UI_H

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <EGL/egl.h>
#include <GL/gl.h>
#include <wayland-egl.h>
#include <stdio.h>
#include <stdbool.h>


//doesnt support jpeg in this way, but there is a cairo-jpeg project
#include <cairo/cairo.h>
#include <wayland-client.h>
#include <sequential.h>

#include "../config.h"
#ifdef __cplusplus
extern "C" {
#endif

//////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////Application style definition/////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////


unsigned char *load_image(const char *path, const enum wl_shm_format wlformat,
	   int width, int height, unsigned char *data);

//////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////geometry definition////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
struct point2d {
	unsigned int x;
	unsigned int y;
};


struct bbox {
	unsigned int x;
	unsigned int y;
	unsigned int w;
	unsigned int h;
};

static inline bool
bbox_contain_point(const struct bbox *box, unsigned int x, unsigned int y)
{
	return ((x >= box->x) &&
		(x < box->x + box->w) &&
		(y >= box->y) &&
		(y < box->y + box->h));
}

static inline bool
bboxs_intersect(const struct bbox *ba, const struct bbox *bb)
{
	return (ba->x < bb->x+bb->w) && (ba->x+ba->w > bb->x) &&
		(ba->y < bb->y+bb->h) && (ba->y + ba->h > bb->y);
}

//////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////APPSURFACE/////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////

enum APP_SURFACE_TYPE {
	APP_BACKGROUND,
	APP_PANEL,
	APP_WIDGET,
	APP_LOCKER,
};

enum taiwins_btn_t {
	TWBTN_LEFT,
	TWBTN_RIGHT,
	TWBTN_MID,
	TWBTN_DCLICK,
};


struct app_surface;
struct egl_env;

typedef void (*keycb_t)(struct app_surface *, xkb_keysym_t, uint32_t, int);
typedef void (*pointron_t)(struct app_surface *, uint32_t, uint32_t);
typedef void (*pointrbtn_t)(struct app_surface *, enum taiwins_btn_t, bool, uint32_t, uint32_t);
typedef void (*pointraxis_t)(struct app_surface *, int, int, uint32_t, uint32_t);
/* This actually implements the wl_callback callback. */
typedef void (*frame_done_t)(struct app_surface *, uint32_t user_data);
typedef void (*frame_swap_t)(struct app_surface *);

/**
 * /brief Templated wl_surface container
 *
 * The design goal of the surface is for user to have a few frame-update
 * routines, so the user will never need to call the `wl_surface_damage`,
 * `wl_surface_attach` and `wl_surface_commit`.
 *
 * An sample routine for wl_buffers? We need to provide a wl_buffer to draw,
 * then it uses that buffer to commit. For that special case, it needs two draw
 * calls, one does the attach->damage->commit, the other one does that
 * framebuffer manipulation.
 *
 * In the nuklear's case, you need one callback for manipulating the surface and
 * the other one has the control over nuklear context.
 *
 * And there could be other cases, so here we just have a callback for the
 * swap-chain, the specific draw is implement else where.
 */
struct app_surface {
	//the structure to store wl_shell_surface, xdg_shell_surface or tw_ui
	struct wl_proxy *protocol;
	//geometry information
	unsigned int px, py; //anchor
	unsigned int w, h; //size
	unsigned int s;
	enum APP_SURFACE_TYPE type;

	struct wl_output *wl_output;
	struct wl_surface *wl_surface;
	/* buffer */
	union {
		struct {
			struct shm_pool *pool;
			struct wl_buffer  *wl_buffer[2];
			bool dirty[2];
			bool committed[2];
		};
		struct {
			/* we do not necessary need it but when it comes to
			 * surface destroy you will need it */
			EGLDisplay egldisplay;
			struct wl_egl_window *eglwin;
			EGLSurface eglsurface;
		};
		/**
		 * the parent pointer, means you will have to call the
		 * parent->frame function to do the work
		 */
		struct app_surface *parent;
		/* if we have an vulkan surface, it would be the same thing */
	};
	/* callbacks */
	struct {
		keycb_t keycb;
		pointron_t pointron;
		pointrbtn_t pointrbtn;
		pointraxis_t pointraxis;
		frame_done_t frame_done;
		frame_swap_t frame_commit;
	};
	//destructor
	void (*destroy)(struct app_surface *);
	void *user_data; // the new section should be Null, if not, then it should be freed at destruction.
};

/** TODO
 * new APIs for the appsurface
 */

/**
 * /brief clean start a new appsurface
 */
void app_surface_init(struct app_surface *surf, struct wl_surface *);

/**
 * /brief the universal release function
 */
static inline void
app_surface_release(struct app_surface *surf)
{
	//throw all the callbacks
	surf->keycb = NULL;
	surf->pointron = NULL;
	surf->pointrbtn = NULL;
	surf->pointraxis = NULL;
	surf->destroy(surf);
	if (surf->user_data)
		free(surf->user_data);
}




/**
 * /brief initialize the appsurface to a default state
 *
 * no input, no subapp, no buffer, but has a wl_surface
 */
DEPRECATED(void appsurface_init(struct app_surface *surf, struct app_surface *p,
				enum APP_SURFACE_TYPE type, struct wl_surface *wl_surface,
				struct wl_proxy *protocol));


/**
 * /brief assign wl_buffers to the appsurface, thus it initialize the double
 * buffer state and commit state
 *
 */
DEPRECATED(void appsurface_init_buffer(struct app_surface *surf, struct shm_pool *shm,
				       const struct bbox *bbox));
DEPRECATED(void appsurface_init_egl(struct app_surface *surf, struct egl_env *env));
/**
 * /brief init all the input callbacks, zero is acceptable
 */
DEPRECATED(void appsurface_init_input(struct app_surface *surf,
				      keycb_t keycb, pointron_t on, pointrbtn_t btn, pointraxis_t axis));

DEPRECATED(void appsurface_fadc(struct app_surface *surf));

DEPRECATED(void appsurface_buffer_release(void *data, struct wl_buffer *wl_buffer));


cairo_format_t
translate_wl_shm_format(enum wl_shm_format format);

size_t
stride_of_wl_shm_format(enum wl_shm_format format);


static inline struct app_surface *
app_surface_from_wl_surface(struct wl_surface *s)
{
	return (struct app_surface *)wl_surface_get_user_data(s);
}


#ifdef __cplusplus
}
#endif



#endif /* EOF */
