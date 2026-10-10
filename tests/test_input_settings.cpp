// Persistent input settings and validation, without a GPU or game files.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "bbport_settings.h"

static void environment(const char* name, const char* value) {
#ifdef _WIN32
    assert(_putenv_s(name, value ? value : "") == 0);
#else
    assert((value ? setenv(name, value, 1) : unsetenv(name)) == 0);
#endif
}

int main() {
    const char* path = std::getenv("BB_CONFIG");
    assert(path && path[0]);
    std::remove(path);
    BbSettings::Load();
    auto& s = BbSettings::Get();
    assert(s.keyboard_mouse && s.mouse_capture && !s.mouse_invert_y && s.mouse_sensitivity == 1.0f);
    assert(!s.dpad_camera);
    assert(!s.frame_generation && s.fg_multiplier == 2);
    assert(s.reflex == 1);
    s.keyboard_mouse = false;
    s.mouse_capture = false;
    s.mouse_invert_y = true;
    s.mouse_sensitivity = 2.25f;
    s.dpad_camera = true;
    s.upscaler = BbSettings::UpscalerDlss;
    s.frame_generation = true;
    s.fg_multiplier = 6;
    s.reflex = 2;
    s.effects[0] = false;
    assert(BbSettings::SetBinding(BB_BIND_CROSS, 0, SDL_SCANCODE_G));
    assert(BbSettings::Rebind(BB_BIND_CROSS, 1, -SDL_BUTTON_RIGHT));
    assert(s.bindings[BB_BIND_L2 * 2 + 1] == 0);
    assert(BbSettings::SetBinding(BB_BIND_CIRCLE, 0, 0));
    assert(BbSettings::SetBinding(BB_BIND_CIRCLE, 1, SDL_SCANCODE_RETURN2));
    assert(!BbSettings::SetBinding(-1, 0, SDL_SCANCODE_SPACE));
    assert(!BbSettings::SetBinding(BB_BIND_CROSS, 2, SDL_SCANCODE_SPACE));
    assert(!BbSettings::SetBinding(BB_BIND_CROSS, 0, SDL_SCANCODE_INSERT));
    assert(!BbSettings::SetBinding(BB_BIND_CROSS, 0, SDL_SCANCODE_F8));
    assert(!BbSettings::SetBinding(BB_BIND_CROSS, 0, SDL_SCANCODE_F11));
    assert(!BbSettings::SetBinding(BB_BIND_CROSS, 0, -99));
    assert(BbSettings::Save());
    s.keyboard_mouse = s.mouse_capture = true;
    s.mouse_invert_y = false;
    s.mouse_sensitivity = 1.0f;
    s.dpad_camera = false;
    s.upscaler = BbSettings::UpscalerOff;
    s.frame_generation = false;
    s.fg_multiplier = 2;
    BbSettings::ResetBindings();
    BbSettings::Load();
    assert(!s.keyboard_mouse && !s.mouse_capture && s.mouse_invert_y && s.mouse_sensitivity == 2.25f);
    assert(s.dpad_camera);
    assert(s.upscaler == BbSettings::UpscalerDlss && !s.effects[0]);
    assert(s.frame_generation && s.fg_multiplier == 6);
    assert(s.reflex == 2);
    assert(s.bindings[BB_BIND_CROSS * 2] == SDL_SCANCODE_G);
    assert(s.bindings[BB_BIND_CROSS * 2 + 1] == -SDL_BUTTON_RIGHT);
    assert(s.bindings[BB_BIND_CIRCLE * 2] == 0);
    assert(s.bindings[BB_BIND_CIRCLE * 2 + 1] == SDL_SCANCODE_RETURN2); // same display name as Return
    const char* values[] = {"999", "-5", "nan", "inf"};
    const float expected[] = {5.0f, 0.1f, 1.0f, 1.0f};
    for (int i = 0; i < 4; ++i) {
        FILE* f = std::fopen(path, "w");
        assert(f);
        std::fprintf(f, "mouse_sensitivity=%s\n", values[i]);
        assert(std::fclose(f) == 0);
        BbSettings::Load();
        assert(s.mouse_sensitivity == expected[i]);
    }
    const char* factors[] = {"1", "99", "4", "oops", "6junk", "9999999999999999999999"};
    const int expected_factors[] = {2, 6, 4, 2, 2, 2};
    for (int i = 0; i < 6; ++i) {
        FILE* f = std::fopen(path, "w");
        assert(f);
        std::fprintf(f, "frame_generation=1\nfg_multiplier=%s\n", factors[i]);
        assert(std::fclose(f) == 0);
        BbSettings::Load();
        assert(s.frame_generation && s.fg_multiplier == expected_factors[i]);
    }
    FILE* f = std::fopen(path, "w");
    assert(f);
    std::fprintf(f, "bind_cross=oops\nbind_cross_alt=-99\nbind_l1=%d\nbind_l2=99999999999999999999\n"
                    "bind_r1=12junk\nbind_dpad_down=0\n", int(SDL_SCANCODE_INSERT));
    assert(std::fclose(f) == 0);
    BbSettings::Load();
    assert(s.bindings[BB_BIND_CROSS * 2] == SDL_SCANCODE_SPACE);
    assert(s.bindings[BB_BIND_CROSS * 2 + 1] == 0);
    assert(s.bindings[BB_BIND_L1 * 2] == SDL_SCANCODE_1);
    assert(s.bindings[BB_BIND_L2 * 2] == SDL_SCANCODE_R);
    assert(s.bindings[BB_BIND_R1 * 2] == SDL_SCANCODE_3);
    assert(s.bindings[BB_BIND_DOWN * 2] == 0);
    BbSettings::ResetBindings();
    assert(s.bindings[BB_BIND_DOWN * 2] == SDL_SCANCODE_K);
    s.frame_generation = false;
    s.fg_multiplier = 6;
    assert(BbSettings::Save());
    environment("BB_FRAME_GENERATION", "menu");
    BbSettings::Load();
    assert(!s.frame_generation && s.fg_multiplier == 6);
    environment("BB_FRAME_GENERATION", "on");
    BbSettings::Load();
    assert(s.frame_generation && s.fg_multiplier == 6);
    environment("BB_FRAME_GENERATION", "control");
    BbSettings::Load();
    assert(!s.frame_generation);
    environment("BB_FRAME_GENERATION", "menu");
    s.frame_generation = true;
    assert(BbSettings::Save());
    BbSettings::Load();
    assert(s.frame_generation);
    environment("BB_FRAME_GENERATION", "off");
    BbSettings::Load();
    assert(!s.frame_generation);
    environment("BB_FRAME_GENERATION", nullptr);
    environment("BB_FG_MULTIPLIER", "4");
    BbSettings::Load();
    assert(s.frame_generation && s.fg_multiplier == 4);
    environment("BB_FG_MULTIPLIER", nullptr);
    BbSettings::Load();
    assert(s.frame_generation && s.fg_multiplier == 6);
    environment("BB_REFLEX", "off");
    BbSettings::Load();
    assert(s.reflex == 0 && s.frame_generation); // saved preference stays independent
    assert(BbSettings::Save());
    environment("BB_REFLEX", "boost");
    BbSettings::Load();
    assert(s.reflex == 2);
    environment("BB_REFLEX", nullptr);
    BbSettings::Load();
    assert(s.reflex == 0);
    environment("BB_REFLEX", "invalid");
    BbSettings::Load();
    assert(s.reflex == 0); // invalid override must not change the saved preference
    environment("BB_REFLEX", nullptr);
    assert(std::remove(path) == 0);
    std::puts("PASS: input/binding persistence, reset, reserved shortcuts, invalid values and graphics coexistence");
}
