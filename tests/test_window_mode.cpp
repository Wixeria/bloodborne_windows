// Real SDL desktop/exclusive transitions, restoration, F11 and ini persistence.
#include "bbport_window_mode.h"
#include <cassert>
#include <initializer_list>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main() {
    assert(SDL_Init(SDL_INIT_VIDEO));
    const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    assert(desktop);
    const int width = desktop->w, height = desktop->h;
    SDL_Window* window = SDL_CreateWindow("bbport window mode validation", 640, 480, SDL_WINDOW_RESIZABLE);
    assert(window);
    SDL_SyncWindow(window);
    int original_width{}, original_height{}, original_x{}, original_y{};
    SDL_GetWindowSize(window, &original_width, &original_height);
    SDL_GetWindowPosition(window, &original_x, &original_y);
    auto& settings = BbSettings::Get();
    for (int mode : {BbSettings::BorderlessFullscreen, BbSettings::Fullscreen, BbSettings::Windowed}) {
        assert(BbWindowMode::Apply(window, mode, width, height));
        const bool fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
        assert(fullscreen == (mode != BbSettings::Windowed));
        const auto* exclusive = SDL_GetWindowFullscreenMode(window);
        assert((exclusive != nullptr) == (mode == BbSettings::Fullscreen));
        if (exclusive) assert(exclusive->w == width && exclusive->h == height);
        settings.window_mode = mode;
        assert(BbSettings::Save());
        settings.window_mode = -1;
        BbSettings::Load();
        assert(settings.window_mode == mode);
        std::printf("PASS: %s SDL flags/display mode and ini reload\n", BbSettings::WindowModeName(mode));
    }
    int restored_width{}, restored_height{}, restored_x{}, restored_y{};
    SDL_GetWindowSize(window, &restored_width, &restored_height);
    SDL_GetWindowPosition(window, &restored_x, &restored_y);
    assert(restored_width == original_width && restored_height == original_height);
    assert(restored_x == original_x && restored_y == original_y);
    assert(!BbWindowMode::Apply(window, -1, width, height));
    assert(!(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = SDL_GetWindowID(window);
    event.key.scancode = SDL_SCANCODE_F11;
    int requested = BbSettings::Windowed;
    assert(BbWindowMode::Shortcut(event, event.key.windowID, BbSettings::BorderlessFullscreen, requested));
    assert(requested == BbSettings::BorderlessFullscreen);
    assert(BbWindowMode::Apply(window, requested, width, height));
    event.key.repeat = true;
    assert(BbWindowMode::Shortcut(event, event.key.windowID, BbSettings::BorderlessFullscreen, requested));
    assert(requested == BbSettings::BorderlessFullscreen);
    event.type = SDL_EVENT_KEY_UP;
    assert(BbWindowMode::Shortcut(event, event.key.windowID, BbSettings::BorderlessFullscreen, requested));
    assert(requested == BbSettings::BorderlessFullscreen);
    event.type = SDL_EVENT_KEY_DOWN; event.key.repeat = false;
    assert(BbWindowMode::Shortcut(event, event.key.windowID, BbSettings::BorderlessFullscreen, requested));
    assert(requested == BbSettings::Windowed);
    assert(BbWindowMode::Apply(window, requested, width, height));
    event.key.windowID++;
    assert(!BbWindowMode::Shortcut(event, SDL_GetWindowID(window), BbSettings::Fullscreen, requested));
    // Explicit startup override wins over legacy fullscreen; invalid strings are ignored.
    assert(_putenv_s("BB_FULLSCREEN", "1") == 0);
    assert(_putenv_s("BB_WINDOW_MODE", "fullscreen") == 0);
    BbSettings::Load(); assert(settings.window_mode == BbSettings::Fullscreen);
    assert(_putenv_s("BB_WINDOW_MODE", "invalid") == 0);
    BbSettings::Load(); assert(settings.window_mode == BbSettings::BorderlessFullscreen);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::remove(std::getenv("BB_CONFIG"));
    std::puts("PASS: window restoration, F11 repeat/key-up filtering, window routing and startup precedence");
}
