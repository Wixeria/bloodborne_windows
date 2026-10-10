/* The SDL game window shares its executable's embedded Windows icon. */
#ifndef BB_WINDOW_ICON_H
#define BB_WINDOW_ICON_H
#include <SDL3/SDL.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

static inline void bb_window_set_icon(SDL_Window* window) {
    HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                           SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    if (!hwnd) return;
    HINSTANCE instance = GetModuleHandleW(NULL);
    HICON large = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                  GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED);
    HICON small = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                  GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    if (large) SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)large);
    if (small) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small);
    // LR_SHARED handles belong to the module; do not destroy them with the window.
}
#else
static inline void bb_window_set_icon(SDL_Window* window) { (void)window; }
#endif
#endif
