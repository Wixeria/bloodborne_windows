/* Shared keyboard/mouse binding IDs, defaults and PS4 button metadata. */
#ifndef BB_RUNTIME_PAD_BINDINGS_H
#define BB_RUNTIME_PAD_BINDINGS_H
#include <stdint.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_mouse.h>

/* Positive values are SDL physical scancodes, negative values are mouse button
 * numbers (left/right/middle/X1/X2), zero is unbound. Store these stable IDs in
 * the ini; display names alone cannot round-trip every SDL scancode. */
#define BB_PAD_BIND_ACTIONS(X) \
    X(CROSS, "cross", "Cross", "Interact / confirm", 0x4000, SDL_SCANCODE_SPACE, 0) \
    X(TRIANGLE, "triangle", "Triangle", "Heal", 0x1000, SDL_SCANCODE_Q, 0) \
    X(SQUARE, "square", "Square", "Use item", 0x8000, SDL_SCANCODE_E, 0) \
    X(CIRCLE, "circle", "Circle", "Dodge / sprint / back", 0x2000, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_ESCAPE) \
    X(L1, "l1", "L1", "Transform weapon", 0x400, SDL_SCANCODE_1, -SDL_BUTTON_X2) \
    X(L2, "l2", "L2", "Firearm / left-hand attack", 0x100, SDL_SCANCODE_R, -SDL_BUTTON_RIGHT) \
    X(L3, "l3", "L3", "Left-stick click", 0x2, SDL_SCANCODE_Z, 0) \
    X(R1, "r1", "R1", "Attack", 0x800, SDL_SCANCODE_3, -SDL_BUTTON_LEFT) \
    X(R2, "r2", "R2", "Heavy attack", 0x200, SDL_SCANCODE_F, -SDL_BUTTON_X1) \
    X(R3, "r3", "R3", "Lock on", 0x4, SDL_SCANCODE_C, -SDL_BUTTON_MIDDLE) \
    X(UP, "dpad_up", "D-pad Up", "Up", 0x10, SDL_SCANCODE_I, 0) \
    X(DOWN, "dpad_down", "D-pad Down", "Down", 0x40, SDL_SCANCODE_K, 0) \
    X(LEFT, "dpad_left", "D-pad Left", "Left", 0x80, SDL_SCANCODE_J, 0) \
    X(RIGHT, "dpad_right", "D-pad Right", "Right", 0x20, SDL_SCANCODE_L, 0) \
    X(OPTIONS, "options", "Options", "Game menu", 0x8, SDL_SCANCODE_RETURN, 0) \
    X(TOUCH_LEFT, "touchpad_left", "Touchpad Left", "Left touchpad click", 0x100000, SDL_SCANCODE_TAB, 0) \
    X(TOUCH_RIGHT, "touchpad_right", "Touchpad Right", "Right touchpad click", 0x100000, SDL_SCANCODE_BACKSPACE, 0)

enum {
#define BB_BIND_ENUM(id, key, label, action, mask, primary, secondary) BB_BIND_##id,
    BB_PAD_BIND_ACTIONS(BB_BIND_ENUM)
#undef BB_BIND_ENUM
    BB_BIND_ACTION_COUNT,
    BB_BIND_SLOTS = 2,
    BB_BIND_COUNT = BB_BIND_ACTION_COUNT * BB_BIND_SLOTS
};
typedef struct {
    const char *key, *label, *action;
    uint32_t mask;
    int defaults[BB_BIND_SLOTS];
} BbPadBindingAction;

static const BbPadBindingAction bb_pad_binding_actions[BB_BIND_ACTION_COUNT]={
#define BB_BIND_ENTRY(id, key, label, action, mask, primary, secondary) \
    {key, label, action, mask, {primary, secondary}},
    BB_PAD_BIND_ACTIONS(BB_BIND_ENTRY)
#undef BB_BIND_ENTRY
};

static inline int bb_pad_binding_valid(int code) {
    return (code>=-5 && code<=0) || (code>SDL_SCANCODE_UNKNOWN &&
        code<SDL_SCANCODE_COUNT && code!=SDL_SCANCODE_INSERT && code!=SDL_SCANCODE_F8 &&
        code!=SDL_SCANCODE_F11);
}
#endif
