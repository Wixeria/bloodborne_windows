// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: user settings changed at run time from the in-game menu (bbport_overlay.h) and kept
// in bbport.ini (BB_CONFIG overrides the path). Environment variables override the file at
// start. Readers load the atomics every frame; writers are the menu and Load().

#pragma once

#include <atomic>
#include "../../src/runtime_pad_bindings.h"

namespace BbSettings {

enum Upscaler : int { UpscalerOff = 0, UpscalerFsr3 = 1, UpscalerFsr4 = 2, UpscalerFsr411 = 3,
                      UpscalerTaa = 4, UpscalerDlss = 5, UpscalerCount };
/// FSR 4 v07 or FSR 4.1.1: the same inputs, settings and placement in the frame.
inline bool IsFsr4(int upscaler) {
    return upscaler == UpscalerFsr4 || upscaler == UpscalerFsr411;
}
/// Providers that consume the shared color/depth/motion/jitter frame.
inline bool IsFrameUpscaler(int upscaler) {
    return IsFsr4(upscaler) || upscaler == UpscalerDlss;
}
enum Preset : int { NativeAA = 0, Quality, Balanced, Performance, UltraPerformance, PresetCount };
enum DebugView : int { DebugNone = 0, DebugReactive = 1, DebugMotion = 2, DebugViewCount };
enum WindowMode : int { Windowed = 0, BorderlessFullscreen, Fullscreen, WindowModeCount };
const char* WindowModeName(int mode);

/// Game effects switched by the community patches at start (patches.py EFFECTS): ini key,
/// menu label, default (the game's own behaviour).
struct Effect {
    const char* key;
    const char* label;
    bool default_on;
};
inline constexpr Effect Effects[] = {
    {"effect_chromatic_aberration", "Chromatic Aberration", true},
    {"effect_dof", "Depth of Field (DoF)", true},
    {"effect_motion_blur", "Motion Blur", true},
    {"effect_ssao", "SSAO", true},
    {"effect_game_aa", "In-Game Antialiasing", true},
    {"effect_dynamic_shadows", "Dynamic Shadows", true},
    {"effect_ssr", "SSR Reflections (not in original game)", false},
    {"vertex_explosion_fix", "Vertex Explosion Fix (Disable FaceGen)", false},
    {"skip_intro", "Skip Intro Videos", false},
    {"debug_camera", "Free Camera (Cross + L3)", false},
    {"debug_menu", "Debug menu (font files required)", false},
};
inline constexpr int EffectCount = int(sizeof(Effects) / sizeof(Effects[0]));
/// Live output resolutions: the upscaler's output and the UI host targets.
inline constexpr int OutputWidths[] = {1280, 1920, 2560, 3840};
inline constexpr int OutputHeights[] = {720, 1080, 1440, 2160};
inline constexpr int OutputCount = 4;
inline constexpr int OutputDefault = 1; ///< 1920x1080, the game's own size

struct Values {
    Values() {
        for (int a = 0; a < BB_BIND_ACTION_COUNT; ++a)
            for (int slot = 0; slot < BB_BIND_SLOTS; ++slot)
                bindings[a * BB_BIND_SLOTS + slot] = bb_pad_binding_actions[a].defaults[slot];
    }
    std::atomic<int> upscaler{UpscalerFsr3};
    std::atomic<int> preset{NativeAA};
    std::atomic<bool> sharpen{true};
    std::atomic<float> sharpness{0.3f};
    std::atomic<bool> jitter{true};
    std::atomic<bool> reactive{false};
    std::atomic<bool> object_motion{true};
    std::atomic<float> reactive_scale{1.0f};
    std::atomic<float> reactive_threshold{0.2f};
    std::atomic<float> reactive_max{0.9f};
    std::atomic<int> debug_view{DebugNone};
    std::atomic<bool> show_fps{false};
    std::atomic<float> menu_x{-1.0f}, menu_y{-1.0f};
    std::atomic<int> window_mode{Windowed};
    std::atomic<const char*> window_mode_problem{nullptr};
    std::atomic<bool> frame_generation{false};
    std::atomic<int> fg_multiplier{2}; ///< total presentations: 1 rendered + N-1 generated
    std::atomic<int> reflex{1}; ///< 0 Off, 1 On, 2 On + Boost; independent of FG
    /// Live input settings. Mouse motion is translated to the game's right stick.
    std::atomic<bool> keyboard_mouse{true}, mouse_capture{true}, mouse_invert_y{false};
    std::atomic<float> mouse_sensitivity{1.0f};
    std::atomic<bool> dpad_camera{false};
    std::atomic<int> bindings[BB_BIND_COUNT]{};
    // FSR 4 checks (menu): the provider's auto exposure, the jitter sign it is given.
    std::atomic<bool> fsr4_auto_exposure{true};
    std::atomic<bool> fsr4_invert_jitter{false};
    std::atomic<int> active_render_width{1920}, active_render_height{1080};
    /// Applied at start (patches.py); the menu shows when a restart is needed.
    std::atomic<bool> effects[EffectCount]{};
    std::atomic<int> model_lod{0}; ///< -2 highest .. 2 lowest, 0 the game's
    std::atomic<int> output_res{OutputDefault}; ///< index into OutputWidths
    /// Live resolution and preset changes (run.sh): 0 off by default (startup patch, fastest
    /// on the Steam Deck and older GPUs), -1 auto (strong discrete GPUs), 1 on. On restart.
    std::atomic<int> live_resolution{0};
    /// Why FSR 4 cannot run (assets, device features), or null. Set by the renderer.
    std::atomic<const char*> fsr4_problem{nullptr};
    std::atomic<bool> fsr4_supported{false}, fsr411_supported{false}, dlss_supported{false};
    std::atomic<const char*> dlss_problem{nullptr};

    /// Startup settings for the explicit BB_RENDER_RES compatibility patch only.
    int startup_preset = NativeAA;
    int startup_upscaler = UpscalerFsr3;
    bool startup_object_motion = true;
    bool startup_effects[EffectCount]{};
    int startup_model_lod = 0;
    int startup_output_res = OutputDefault;
    int startup_live_resolution = 0;
};

Values& Get();

/// Reads the file, then the environment overrides. Called once at start.
void Load();
/// Checks the loaded choice before the first frame; unsupported FSR 4 uses FSR 3.1.
void ConfigureUpscalerSupport(bool fsr4, bool fsr411, bool dlss = false);
void ConfigureDlssSupport(bool available, const char* problem);
/// Startup-patched scene dimensions cannot change until run.sh prepares a new image.
bool FixedRenderSession();
int RenderPreset();
bool ResolutionNeedsRestart();
/// Writes the file (menu changes).
bool Save();
/// Keyboard/mouse assignments; reject host shortcuts and invalid input IDs.
bool SetBinding(int action, int slot, int code);
/// Interactive assignment moves an already used key/button to this slot.
bool Rebind(int action, int slot, int code);
void ResetBindings();

/// Render resolution divisor of a preset (1.0 native, 1.5 quality, ...).
float PresetScale(int preset);
const char* PresetName(int preset);
const char* UpscalerName(int upscaler);

} // namespace BbSettings
