#include <time.h>
#include <assert.h>

#ifdef _WITH_NVIDIA
#include <eglexternalplatform.h>
#endif

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>


#include <GL/gl.h>
#include <GL/glext.h>
#include <wayland-egl.h>
#include <stdbool.h>
#include <cairo/cairo.h>
#include <librsvg/rsvg.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "nk_wl_egl.h"
#include "client.h"


/*
 * ==============================================================
 *
 *                          EGL environment
 *
 * ===============================================================
 */

static const EGLint egl_context_attribs[] = {
	EGL_CONTEXT_MAJOR_VERSION, 3,
	EGL_CONTEXT_MINOR_VERSION, 3,
	EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
	EGL_NONE,
};

/* this is the required attributes we need to satisfy */
static const EGLint egl_config_attribs[] = {
	EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	EGL_RED_SIZE, 8,
	EGL_GREEN_SIZE, 8,
	EGL_BLUE_SIZE, 8,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
	EGL_NONE,
};

static void
debug_egl_config_attribs(EGLDisplay dsp, EGLConfig cfg)
{
	int size;
	bool yes;
	eglGetConfigAttrib(dsp, cfg,
			   EGL_BUFFER_SIZE, &size);
	fprintf(stderr, "\tcfg %p has buffer size %d\n", cfg, size);
	yes = eglGetConfigAttrib(dsp, cfg, EGL_BIND_TO_TEXTURE_RGBA, NULL);
	fprintf(stderr, "\tcfg %p can %s bound to the rgba buffer", cfg,
		yes ? "" : "not");
}


#ifdef _WITH_NVIDIA
//we need this entry to load the platform library
extern EGLBoolean loadEGLExternalPlatform(int major, int minor,
					  const EGLExtDriver *driver,
					  EGLExtPlatform *platform);
#endif


bool
egl_env_init(struct egl_env *env, struct wl_display *d)
{
#ifndef EGL_VERSION_1_5
	fprintf(stderr, "the feature requires EGL 1.5 and it is not supported\n");
	return false;
#endif
	env->wl_display = d;
	EGLint major, minor;
	EGLint n;
	EGLConfig egl_cfg;
//	EGLint *context_attribute = NULL;

	env->egl_display = eglGetDisplay((EGLNativeDisplayType)env->wl_display);
	assert(env->egl_display);
	assert(eglInitialize(env->egl_display, &major, &minor) == EGL_TRUE);
	fprintf(stderr, "egl display is %p\n", env->egl_display);

	const char *egl_extensions = eglQueryString(env->egl_display, EGL_EXTENSIONS);
	const char *egl_vendor = eglQueryString(env->egl_display, EGL_VENDOR);
	fprintf(stderr, "egl vendor using: %s\n", egl_vendor);
	fprintf(stderr, "egl_extensions: %s\n", egl_extensions);
	eglGetConfigs(env->egl_display, NULL, 0, &n);
	fprintf(stderr, "egl has %d configures\n", n);
	assert(EGL_TRUE == eglChooseConfig(env->egl_display, egl_config_attribs, &egl_cfg, 1, &n));
	debug_egl_config_attribs(env->egl_display, egl_cfg);
	eglBindAPI(EGL_OPENGL_API);
	env->egl_context = eglCreateContext(env->egl_display,
					    egl_cfg,
					    EGL_NO_CONTEXT,
					    egl_context_attribs);
	assert(env->egl_context != EGL_NO_CONTEXT);
	env->config = egl_cfg;
	//now we can try to create a program and see if I need
	return true;
}

void
egl_env_end(struct egl_env *env)
{
	eglDestroyContext(env->egl_display, env->egl_context);
	eglTerminate(env->egl_display);
	eglReleaseThread();
}





///////////////////////////////////////////////////////////////////////////////////
////////////////////////////////Lua C callbacks////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////

//lua uses this to render svg to the surface, so lua can use it
/* static int */
/* egl_load_svg(lua_State *L) */
/* { */
/*	struct eglapp **ptr, *app; */
/*	const char *string; */
/*	//lua call this with (eglapp, function) */
/*	int nargs = lua_gettop(L); */
/*	ptr = lua_touserdata(L, -2); */
/*	string = lua_tostring(L, -1); */

/*	app = *ptr; */
/*	RsvgHandle *handle = rsvg_handle_new_from_file(string, NULL); */
/*	rsvg_handle_render_cairo(handle, app->icon.ctxt); */
/*	rsvg_handle_close(handle, NULL); */

/*	return 0; */
/*	//then afterwords, we should have panel to use it. */
/* } */

//it can be a callback

//this function is used for lua code to actively update the icon
/* static int */
/* lua_eglapp_update_icon(lua_State *L) */
/* { */
/*	struct eglapp **ptr, *app; */
/* //	const char *string; */
/*	ptr = lua_touserdata(L, -1); */
/*	app = *ptr; */
/* } */

//create an example application, calendar
