// Key assignment state shared by the render UI and SDL event handler under its mutex.
#pragma once
#include <SDL3/SDL.h>
#include "../../src/runtime_pad_bindings.h"

namespace BbKeybinds {
enum class Result { Ignored, Waiting, Cancelled, Assigned };
struct Capture {
    int action = -1, slot = 0, code = 0;
    bool reserved = false;
    bool Active() const { return action >= 0; }
    void Cancel() { action = -1; reserved = false; }
    void Begin(int a, int s) { action = a; slot = s; reserved = false; }
    Result Handle(const SDL_Event& event) {
        if (!Active()) return Result::Ignored;
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            Cancel();
            return Result::Cancelled;
        }
        if (event.type != SDL_EVENT_KEY_DOWN) return Result::Ignored;
        if (event.key.repeat) return Result::Waiting;
        if (event.key.scancode == SDL_SCANCODE_INSERT) {
            Cancel();
            return Result::Ignored; // keep the overlay's close shortcut available
        }
        if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
            Cancel();
            return Result::Cancelled;
        }
        if (!bb_pad_binding_valid(event.key.scancode) || event.key.scancode == SDL_SCANCODE_UNKNOWN) {
            reserved = true;
            return Result::Waiting;
        }
        code = event.key.scancode;
        return Result::Assigned;
    }
};
} // namespace BbKeybinds
