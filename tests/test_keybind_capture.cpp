#include <cassert>
#include "bbport_keybind_capture.h"

int main() {
    BbKeybinds::Capture c;
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN; e.key.scancode = SDL_SCANCODE_G;
    assert(c.Handle(e) == BbKeybinds::Result::Ignored);
    c.Begin(BB_BIND_TRIANGLE, 1);
    e.key.repeat = true;
    assert(c.Handle(e) == BbKeybinds::Result::Waiting && c.Active());
    e.key.repeat = false;
    assert(c.Handle(e) == BbKeybinds::Result::Assigned && c.code == SDL_SCANCODE_G);
    assert(c.action == BB_BIND_TRIANGLE && c.slot == 1);
    c.Cancel();
    c.Begin(BB_BIND_CROSS, 0);
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN; e.button.button = SDL_BUTTON_LEFT;
    assert(c.Handle(e) == BbKeybinds::Result::Ignored && c.Active()); // mouse chosen in the UI
    e = {}; e.type = SDL_EVENT_KEY_DOWN; e.key.scancode = SDL_SCANCODE_F8;
    assert(c.Handle(e) == BbKeybinds::Result::Waiting && c.reserved && c.Active());
    e.key.scancode = SDL_SCANCODE_F11;
    assert(c.Handle(e) == BbKeybinds::Result::Waiting && c.reserved && c.Active());
    e.key.scancode = SDL_SCANCODE_ESCAPE;
    assert(c.Handle(e) == BbKeybinds::Result::Cancelled && !c.Active());
    c.Begin(BB_BIND_R2, 0);
    e.key.scancode = SDL_SCANCODE_INSERT;
    assert(c.Handle(e) == BbKeybinds::Result::Ignored && !c.Active()); // overlay may close
    c.Begin(BB_BIND_R2, 0);
    e.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    assert(c.Handle(e) == BbKeybinds::Result::Cancelled && !c.Active());
}
