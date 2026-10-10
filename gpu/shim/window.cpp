// bbport: SDL3 window for the Vulkan swapchain (X11 or Wayland).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <SDL3/SDL.h>
#include "common/assert.h"
#include "common/logging/log.h"
#include "sdl_window.h"
#include "bbport_window_platform.h"
#include "bbport_window_icon.h"
#include "bbport_window_mode.h"
#include "bbport_overlay_input.h"
#include "bbport_settings.h"
#include "../bbgpu.h"
#include "../../src/runtime_pad_input.h"

namespace Frontend {

namespace {

// Issue #69: the monitor the window (and fullscreen) goes to. BB_DISPLAY: its number in SDL's
// order (1, 2, ...; bb-gpu-capabilities --displays lists them) or a part of its name; without it
// SDL's primary display. The monitors are logged so a report says which one was taken.
SDL_DisplayID ChooseDisplay() {
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    const char* wanted = std::getenv("BB_DISPLAY");
    int count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    SDL_DisplayID chosen = 0;
    if (wanted && *wanted) {
        char* end = nullptr;
        const long number = std::strtol(wanted, &end, 10);
        if (end && *end == '\0') {
            if (number >= 1 && number <= count) {
                chosen = ids[number - 1];
            }
        } else {
            for (int i = 0; i < count && !chosen; ++i) {
                const char* name = SDL_GetDisplayName(ids[i]);
                if (name && SDL_strcasestr(name, wanted)) {
                    chosen = ids[i];
                }
            }
        }
    }
    const SDL_DisplayID display = chosen ? chosen : primary;
    for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetDisplayName(ids[i]);
        const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(ids[i]);
        std::printf("Display %d: %s %dx%d%s%s\n", i + 1, name ? name : "?", mode ? mode->w : 0,
                    mode ? mode->h : 0, ids[i] == primary ? " (primary)" : "",
                    ids[i] == display ? " <- the game's (BB_DISPLAY)" : "");
    }
    if (wanted && *wanted && !chosen) {
        std::printf("Display: BB_DISPLAY=%s matches none, the primary one is used\n", wanted);
    }
    SDL_free(ids);
    return display;
}

} // namespace

WindowSDL::WindowSDL(s32 width_, s32 height_, const char* title) : width{width_}, height{height_} {
    if (const char* resolution = std::getenv("BB_WINDOW_RES")) {
        unsigned w{}, h{}; char extra{};
        if (std::sscanf(resolution, "%ux%u%c", &w, &h, &extra) == 2 &&
            w >= 320 && w <= 7680 && h >= 240 && h <= 4320 && !(w % 2) && !(h % 2)) {
            width_ = s32(w); height_ = s32(h);
        } else {
            LOG_WARNING(Frontend, "Ignoring invalid BB_WINDOW_RES {}", resolution);
        }
    }
    // Gamepads are sampled by runtime_pad.c; their events are pumped here with the window's.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        UNREACHABLE_MSG("Failed to initialize SDL video: {}", SDL_GetError());
    }
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
    const SDL_DisplayID display = ChooseDisplay();
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width_);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height_);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, true);
    fullscreen_width = width_;
    fullscreen_height = height_;
    base_title = title;
    window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    ASSERT_MSG(window, "Failed to create window: {}", SDL_GetError());
#ifdef _WIN32
    SDL_SetWindowsMessageHook([](void*, MSG* message) {
        // Click flash is sent from SDL's button event below, including raw
        // relative mouse input. Avoid sending it twice for legacy mouse messages.
        if (message->message != WM_LBUTTONDOWN) bbgpu_reflex_window_message(message->message);
        return true;
    }, nullptr);
#endif
    bb_window_set_icon(window);
    UpdateWindowMode(false);
    input_focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    UpdateGameInput();

    const char* driver = SDL_GetCurrentVideoDriver();
    switch (bb_window_native_handles(window,&window_info.display_connection,&window_info.render_surface)) {
    case BB_WINDOW_WINDOWS: window_info.type=WindowSystemType::Windows; break;
    case BB_WINDOW_X11: window_info.type=WindowSystemType::X11; break;
    case BB_WINDOW_WAYLAND: window_info.type=WindowSystemType::Wayland; break;
    default: UNREACHABLE_MSG("Unsupported SDL video driver {}",driver ? driver : "(none)");
    }
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    width = w;
    height = h;
    LOG_INFO(Frontend, "Window {}x{} on {}", w, h, driver);
}

WindowSDL::~WindowSDL() {
#ifdef _WIN32
    SDL_SetWindowsMessageHook(nullptr, nullptr);
#endif
    runtime_pad_input_configure(0, 0, 0, 1.0f, 0, 0);
    SDL_DestroyWindow(window);
}

void WindowSDL::UpdateGameInput() {
    focus_visible.store(input_focused, std::memory_order_relaxed);
    const auto& s = BbSettings::Get();
    const bool active = input_focused && is_open && !text_active && !bbgpu_overlay_captures_input();
    const bool capture = active && s.keyboard_mouse && s.mouse_capture && !mouse_released;
    // Changing relative mode flushes queued mouse motion, so only do it on a transition.
    // After a failure, F8 or a focus/menu transition can retry without a busy retry loop.
    if (capture != mouse_capture_requested) {
        mouse_capture_requested = capture;
        if (!SDL_SetWindowRelativeMouseMode(window, capture)) {
            LOG_WARNING(Frontend, "Mouse capture {} failed: {}", capture, SDL_GetError());
        }
    }
    runtime_pad_input_configure(active, capture && SDL_GetWindowRelativeMouseMode(window),
                               s.keyboard_mouse, s.mouse_sensitivity, s.mouse_invert_y, s.dpad_camera);
    std::array<int, BB_BIND_COUNT> bindings;
    for (int i = 0; i < BB_BIND_COUNT; ++i) bindings[i] = s.bindings[i].load();
    if (!bindings_initialized || bindings != last_bindings) {
        runtime_pad_input_set_bindings(bindings.data(), bindings.size());
        last_bindings = bindings;
        bindings_initialized = true;
    }
}

void WindowSDL::UpdateWindowMode(bool persist) {
    auto& settings = BbSettings::Get();
    int requested = settings.window_mode;
    if (requested == applied_window_mode) return;
    if (BbWindowMode::Apply(window, requested, fullscreen_width, fullscreen_height)) {
        applied_window_mode = requested;
        if (requested != BbSettings::Windowed) last_fullscreen_mode = requested;
        settings.window_mode_problem = nullptr;
        LOG_INFO(Frontend, "Window mode: {}", BbSettings::WindowModeName(requested));
    } else {
        const std::string error = SDL_GetError();
        // Restore the previous mode rather than leaving a half-applied exclusive mode.
        BbWindowMode::Apply(window, applied_window_mode, fullscreen_width, fullscreen_height);
        settings.window_mode.compare_exchange_strong(requested, applied_window_mode);
        settings.window_mode_problem = "Display mode change failed; previous window mode restored.";
        LOG_WARNING(Frontend, "Window mode failed: {}", error);
    }
    int w{}, h{};
    SDL_GetWindowSizeInPixels(window, &w, &h);
    width = w;
    height = h;
    if (persist && !BbSettings::Save())
        LOG_WARNING(Frontend, "Could not save the window mode to bbport.ini");
}

void WindowSDL::BeginTextInput(const std::string& initial, const std::string& prompt) {
    std::scoped_lock lock{text_mutex};
    text = initial;
    text_prompt = prompt;
    text_state = 0;
    text_requested = true;
}

int WindowSDL::PollTextInput(std::string& out) {
    std::scoped_lock lock{text_mutex};
    out = text;
    return text_state;
}

void WindowSDL::UpdateTextTitle() {
    const std::string title = text_active ? base_title + " \u2014 " + text_prompt + ": " + text + "_  (Enter = OK, Esc = cancel)"
                                          : base_title;
    SDL_SetWindowTitle(window, title.c_str());
}

bool WindowSDL::PollEvents() {
    UpdateWindowMode();
    {
        std::scoped_lock lock{text_mutex};
        if (text_requested) { // SDL text input must be toggled from the window thread
            text_requested = false;
            text_active = true;
            SDL_StartTextInput(window);
            UpdateTextTitle();
        }
    }
    if (!text_active) {
        BbOverlay::UpdateTextInput(window);
    }
    UpdateGameInput();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
#ifdef _WIN32
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT &&
            event.button.windowID == SDL_GetWindowID(window))
            bbgpu_reflex_window_message(WM_LBUTTONDOWN);
#endif
        int requested = BbSettings::Get().window_mode;
        if (BbWindowMode::Shortcut(event, SDL_GetWindowID(window), last_fullscreen_mode, requested)) {
            BbSettings::Get().window_mode = requested;
            continue;
        }
        if ((event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) &&
            event.window.windowID == SDL_GetWindowID(window)) {
            input_focused = event.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
            UpdateGameInput();
        }
        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            last_mouse_motion_ms = SDL_GetTicks();
        }
        if (text_active && (event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_KEY_DOWN)) {
            std::scoped_lock lock{text_mutex};
            if (event.type == SDL_EVENT_TEXT_INPUT) {
                text += event.text.text;
            } else if (event.key.key == SDLK_BACKSPACE && !text.empty()) {
                size_t cut = text.size() - 1; // drop one UTF-8 code point
                while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
                text.erase(cut);
            } else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER || event.key.key == SDLK_ESCAPE) {
                text_state = event.key.key == SDLK_ESCAPE ? 2 : 1;
                text_active = false;
                SDL_StopTextInput(window);
            }
            UpdateTextTitle();
            UpdateGameInput();
            continue;
        }
        if (BbOverlay::HandleEvent(event)) {
            UpdateGameInput();
            continue;
        }
        if (!text_active && input_focused && event.type == SDL_EVENT_KEY_DOWN &&
            event.key.scancode == SDL_SCANCODE_F8 && !event.key.repeat) {
            mouse_released = !mouse_released;
            UpdateGameInput();
            continue;
        }
        if (!text_active) runtime_pad_input_event(&event);
        switch (event.type) {
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED: {
            int w = 0, h = 0;
            SDL_GetWindowSizeInPixels(window, &w, &h);
            width = w;
            height = h;
            break;
        }
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            is_open = false;
            break;
        default:
            break;
        }
    }
    UpdateWindowMode();
    UpdateGameInput();
    runtime_pad_input_publish();
    UpdateCursor();
    return is_open;
}

// Issue #3: the OS cursor over the game. Hidden in fullscreen, and in a window after 3 s without
// moving the mouse; always shown while the settings menu is open.
void WindowSDL::UpdateCursor() {
    const bool fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    const bool hide = !text_active && !bbgpu_overlay_captures_input() && input_focused &&
                      (fullscreen || SDL_GetTicks() - last_mouse_motion_ms > 3000);
    if (hide != cursor_hidden) {
        cursor_hidden = hide;
        hide ? SDL_HideCursor() : SDL_ShowCursor();
    }
}

} // namespace Frontend
