/* SDL native handles shared by the renderer and the native presentation test. */
#ifndef BB_WINDOW_PLATFORM_H
#define BB_WINDOW_PLATFORM_H
#include <SDL3/SDL.h>
#include <string.h>
enum BbWindowSystem { BB_WINDOW_NONE, BB_WINDOW_WINDOWS, BB_WINDOW_X11, BB_WINDOW_WAYLAND };
static inline enum BbWindowSystem bb_window_native_handles(SDL_Window *window,void **display,void **surface) {
    const char *driver=SDL_GetCurrentVideoDriver();
    SDL_PropertiesID p=SDL_GetWindowProperties(window);
    *display=NULL; *surface=NULL;
    if (!driver) return BB_WINDOW_NONE;
    if (!strcmp(driver,"windows")) {
        *display=SDL_GetPointerProperty(p,SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER,NULL);
        *surface=SDL_GetPointerProperty(p,SDL_PROP_WINDOW_WIN32_HWND_POINTER,NULL);
        return *display && *surface ? BB_WINDOW_WINDOWS : BB_WINDOW_NONE;
    }
    if (!strcmp(driver,"x11")) {
        *display=SDL_GetPointerProperty(p,SDL_PROP_WINDOW_X11_DISPLAY_POINTER,NULL);
        *surface=(void *)(uintptr_t)SDL_GetNumberProperty(p,SDL_PROP_WINDOW_X11_WINDOW_NUMBER,0);
        return *display && *surface ? BB_WINDOW_X11 : BB_WINDOW_NONE;
    }
    if (!strcmp(driver,"wayland")) {
        *display=SDL_GetPointerProperty(p,SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER,NULL);
        *surface=SDL_GetPointerProperty(p,SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER,NULL);
        return *display && *surface ? BB_WINDOW_WAYLAND : BB_WINDOW_NONE;
    }
    return BB_WINDOW_NONE;
}
#endif
