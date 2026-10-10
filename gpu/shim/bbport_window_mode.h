// Window-thread-only mode changes. Rendering observes SDL's resulting pixel size.
#pragma once
#include <SDL3/SDL.h>
#include "bbport_settings.h"

namespace BbWindowMode {
inline bool Apply(SDL_Window* window, int mode, int width, int height) {
    if (mode < BbSettings::Windowed || mode >= BbSettings::WindowModeCount)
        return SDL_SetError("Invalid window mode");
    SDL_DisplayMode exclusive{};
    const SDL_DisplayMode* display_mode = nullptr;
    if (mode == BbSettings::Fullscreen) {
        // Use this window's monitor, preserving its preferred refresh rate.
        const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
        if (!SDL_GetClosestFullscreenDisplayMode(display, width, height, 0.f, true, &exclusive))
            return false;
        display_mode = &exclusive;
    }
    return SDL_SetWindowFullscreenMode(window, display_mode) &&
           SDL_SetWindowFullscreen(window, mode != BbSettings::Windowed) &&
           SDL_SyncWindow(window);
}

inline bool Shortcut(const SDL_Event& event, SDL_WindowID window, int last_fullscreen, int& requested) {
    if ((event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) ||
        event.key.windowID != window || event.key.scancode != SDL_SCANCODE_F11) return false;
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        requested = requested == BbSettings::Windowed ? last_fullscreen : BbSettings::Windowed;
    return true; // never pass F11 or its key-up/repeat through to bindings or ImGui
}
} // namespace BbWindowMode
