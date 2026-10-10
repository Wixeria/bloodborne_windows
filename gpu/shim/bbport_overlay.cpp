// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_overlay.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL.h>
#include "bbport_settings.h"
#include "bbport_keybind_capture.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_dlssg.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

// DejaVu Sans (Cyrillic), embedded (third_party/fonts, Bitstream Vera license).
#ifdef _WIN32
asm(".section .rdata,\"dr\"\n"
    ".balign 16\n"
    ".global bb_font_ttf\n"
    "bb_font_ttf:\n"
    ".incbin \"" BB_FONT_PATH "\"\n"
    ".global bb_font_ttf_end\n"
    "bb_font_ttf_end:\n"
    ".text\n");
#else
asm(".section .rodata\n"
    ".balign 16\n"
    ".hidden bb_font_ttf\n"
    ".global bb_font_ttf\n"
    "bb_font_ttf:\n"
    ".incbin \"" BB_FONT_PATH "\"\n"
    ".hidden bb_font_ttf_end\n"
    ".global bb_font_ttf_end\n"
    "bb_font_ttf_end:\n"
    ".previous\n");
#endif
extern "C" const unsigned char bb_font_ttf[];
extern "C" const unsigned char bb_font_ttf_end[];

extern "C" void runtime_restart(void); // bb-probe (probe.c)

namespace BbOverlay {

namespace {

std::mutex imgui_mutex; // the ImGui context: window thread (input) and present thread
bool initialized = false;
std::atomic<bool> menu_open{false};
bool l3_down = false, r3_down = false;
bool dirty = false; // settings changed while open: saved on close
bool save_failed = false;
BbKeybinds::Capture binding_capture;
float base_scale = 1.0f;

// Present rate for the FPS counter.
std::chrono::steady_clock::time_point last_present{};
float frame_ms_avg = 0.0f;

void SetOpen(bool value) {
    if (menu_open.exchange(value) == value) {
        return;
    }
    ImGui::GetIO().MouseDrawCursor = value;
    if (!value) binding_capture.Cancel();
    if (!value && dirty) {
        save_failed = !BbSettings::Save();
        dirty = save_failed;
    }
}

ImGuiKey KeyFromSdl(SDL_Keycode key) {
    switch (key) {
    case SDLK_TAB: return ImGuiKey_Tab;
    case SDLK_LEFT: return ImGuiKey_LeftArrow;
    case SDLK_RIGHT: return ImGuiKey_RightArrow;
    case SDLK_UP: return ImGuiKey_UpArrow;
    case SDLK_DOWN: return ImGuiKey_DownArrow;
    case SDLK_PAGEUP: return ImGuiKey_PageUp;
    case SDLK_PAGEDOWN: return ImGuiKey_PageDown;
    case SDLK_HOME: return ImGuiKey_Home;
    case SDLK_END: return ImGuiKey_End;
    case SDLK_DELETE: return ImGuiKey_Delete;
    case SDLK_BACKSPACE: return ImGuiKey_Backspace;
    case SDLK_SPACE: return ImGuiKey_Space;
    case SDLK_RETURN: return ImGuiKey_Enter;
    case SDLK_KP_ENTER: return ImGuiKey_KeypadEnter;
    case SDLK_ESCAPE: return ImGuiKey_Escape;
    case SDLK_LCTRL: return ImGuiKey_LeftCtrl;
    case SDLK_RCTRL: return ImGuiKey_RightCtrl;
    case SDLK_LSHIFT: return ImGuiKey_LeftShift;
    case SDLK_RSHIFT: return ImGuiKey_RightShift;
    case SDLK_LALT: return ImGuiKey_LeftAlt;
    case SDLK_RALT: return ImGuiKey_RightAlt;
    default: return ImGuiKey_None;
    }
}

ImGuiKey KeyFromGamepad(u8 button) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return ImGuiKey_GamepadFaceDown;
    case SDL_GAMEPAD_BUTTON_EAST: return ImGuiKey_GamepadFaceRight;
    case SDL_GAMEPAD_BUTTON_WEST: return ImGuiKey_GamepadFaceLeft;
    case SDL_GAMEPAD_BUTTON_NORTH: return ImGuiKey_GamepadFaceUp;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return ImGuiKey_GamepadDpadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return ImGuiKey_GamepadDpadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return ImGuiKey_GamepadDpadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return ImGuiKey_GamepadDpadRight;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return ImGuiKey_GamepadL1;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return ImGuiKey_GamepadR1;
    case SDL_GAMEPAD_BUTTON_START: return ImGuiKey_GamepadStart;
    case SDL_GAMEPAD_BUTTON_BACK: return ImGuiKey_GamepadBack;
    default: return ImGuiKey_None;
    }
}

float PixelDensity(SDL_WindowID id) {
    SDL_Window* window = SDL_GetWindowFromID(id);
    const float density = window ? SDL_GetWindowPixelDensity(window) : 1.0f;
    return density > 0.0f ? density : 1.0f;
}

// Marks the settings dirty when a widget changed them.
template <typename T>
void Store(std::atomic<T>& target, T value, bool changed) {
    if (changed) {
        target = value;
        dirty = true;
    }
}

void Checkbox(const char* label, std::atomic<bool>& value) {
    bool v = value;
    // Evaluate the widget before copying v into Store (PR #6).
    const bool changed = ImGui::Checkbox(label, &v);
    Store(value, v, changed);
}

void Slider(const char* label, std::atomic<float>& value, float lo, float hi) {
    float v = value;
    const bool changed = ImGui::SliderFloat(label, &v, lo, hi, "%.2f");
    Store(value, v, changed);
}

void Hint(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

std::string BindingName(int code) {
    switch (code) {
    case 0: return "Unbound";
    case -SDL_BUTTON_LEFT: return "Mouse Left";
    case -SDL_BUTTON_RIGHT: return "Mouse Right";
    case -SDL_BUTTON_MIDDLE: return "Mouse Middle";
    case -SDL_BUTTON_X1: return "Mouse Side 1";
    case -SDL_BUTTON_X2: return "Mouse Side 2";
    default: {
        const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(code));
        return name && name[0] ? name : "Key #" + std::to_string(code);
    }
    }
}

void AssignBinding(int code) {
    if (binding_capture.Active() &&
        BbSettings::Rebind(binding_capture.action, binding_capture.slot, code)) {
        dirty = true;
        binding_capture.Cancel();
    }
}

void KeyBindings() {
    auto& s = BbSettings::Get();
    if (ImGui::CollapsingHeader("Key Bindings")) {
        ImGui::TextWrapped("Click an assignment to change it. Either slot can use a keyboard key or mouse button. "
                           "Changes apply immediately and are saved when you close Settings.");
        ImGui::TextWrapped("Assigning an already used key moves it to this control.");
        if (ImGui::BeginTable("##bindings", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                                              ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Controller Button", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn("Primary", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Secondary", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableHeadersRow();
            for (int a = 0; a < BB_BIND_ACTION_COUNT; ++a) {
                ImGui::PushID(a);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(bb_pad_binding_actions[a].label);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", bb_pad_binding_actions[a].action);
                for (int slot = 0; slot < BB_BIND_SLOTS; ++slot) {
                    ImGui::TableSetColumnIndex(slot + 1);
                    ImGui::PushID(slot);
                    const int code = s.bindings[a * BB_BIND_SLOTS + slot];
                    const auto name = BindingName(code);
                    if (ImGui::Button(name.c_str(), ImVec2(-1, 0))) {
                        binding_capture.Begin(a, slot);
                    }
                    if (code && ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::Text("%s: %s", bb_pad_binding_actions[a].label, name.c_str());
                        for (int other = 0; other < BB_BIND_ACTION_COUNT; ++other) {
                            if (other == a) continue;
                            for (int os = 0; os < BB_BIND_SLOTS; ++os) {
                                if (s.bindings[other * BB_BIND_SLOTS + os] == code) {
                                    ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "Also bound to %s",
                                                       bb_pad_binding_actions[other].label);
                                    break;
                                }
                            }
                        }
                        ImGui::EndTooltip();
                    }
                    ImGui::PopID();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (ImGui::Button("Reset All Bindings to Defaults")) {
            BbSettings::ResetBindings();
            dirty = true;
        }
        ImGui::TextDisabled("WASD: move   Ctrl: walk   Mouse / unassigned arrows: camera");
        ImGui::TextDisabled("WASD / Ctrl keep their movement roles if assigned to a button.");
        ImGui::TextDisabled("Insert, F8 and F11 are reserved for the port.");
    }
    if (binding_capture.Active() && !ImGui::IsPopupOpen("Assign Binding"))
        ImGui::OpenPopup("Assign Binding");
    if (ImGui::BeginPopupModal("Assign Binding", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!binding_capture.Active()) {
            ImGui::CloseCurrentPopup();
        } else {
            const int action = binding_capture.action, slot = binding_capture.slot;
            ImGui::Text("%s - %s", bb_pad_binding_actions[action].label, slot ? "Secondary" : "Primary");
            ImGui::TextUnformatted("Press a keyboard key, or choose a mouse button below.\nEsc cancels; use the Escape button below to bind Escape.");
            if (binding_capture.reserved)
                ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "Insert, F8 and F11 are reserved. Choose another key.");
            const int mouse[] = {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE, SDL_BUTTON_X1, SDL_BUTTON_X2};
            for (int i = 0; i < 5; ++i) {
                if (i) ImGui::SameLine();
                if (ImGui::Button(BindingName(-mouse[i]).c_str())) AssignBinding(-mouse[i]);
            }
            if (ImGui::Button("Escape")) AssignBinding(SDL_SCANCODE_ESCAPE);
            ImGui::SameLine();
            if (ImGui::Button("Unbound")) AssignBinding(0);
            ImGui::SameLine();
            if (ImGui::Button("Default")) AssignBinding(bb_pad_binding_actions[action].defaults[slot]);
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) binding_capture.Cancel();
            if (!binding_capture.Active()) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Menu() {
    auto& s = BbSettings::Get();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + (s.menu_x >= 0 ? s.menu_x.load() * viewport->WorkSize.x : 40.0f * base_scale),
                                   viewport->WorkPos.y + (s.menu_y >= 0 ? s.menu_y.load() * viewport->WorkSize.y : 40.0f * base_scale)),
                            ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(620.0f * base_scale, 0.0f), ImGuiCond_Appearing);
    bool keep_open = true;
    if (!ImGui::Begin("Bloodborne — Settings  (Insert / L3+R3)", &keep_open,
                      ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    ImGui::Text("%.0f FPS  (%.1f ms)", frame_ms_avg > 0.0f ? 1000.0f / frame_ms_avg : 0.0f,
                frame_ms_avg);
    // Persist a moved menu in screen-relative coordinates, without writing
    // the settings file on every frame of a title-bar drag.
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && ImGui::IsMouseDragging(0)) {
        const ImVec2 pos = ImGui::GetWindowPos();
        if (viewport->WorkSize.x > 0 && viewport->WorkSize.y > 0) {
            s.menu_x = std::clamp((pos.x - viewport->WorkPos.x) / viewport->WorkSize.x, 0.f, 1.f);
            s.menu_y = std::clamp((pos.y - viewport->WorkPos.y) / viewport->WorkSize.y, 0.f, 1.f);
            dirty = true;
        }
    }

    ImGui::SeparatorText("Temporal Upscaler");
    static const char* upscalers[] = {"Off", "FSR 3.1", "FSR 4 (INT8)", "FSR 4.1.1 (INT8)",
                                     "TAA (Native Antialiasing)", "DLSS (NVIDIA)"};
    static const char* later[] = {"XeSS"};
    int upscaler = s.upscaler;
    if (ImGui::BeginCombo("Upscaler", upscalers[upscaler])) {
        for (int i = 0; i < BbSettings::UpscalerCount; ++i) {
            const bool supported = i == BbSettings::UpscalerFsr4 ? s.fsr4_supported.load()
                : i == BbSettings::UpscalerFsr411 ? s.fsr411_supported.load()
                : i == BbSettings::UpscalerDlss ? s.dlss_supported.load() : true;
            ImGui::BeginDisabled(!supported);
            if (ImGui::Selectable(upscalers[i], i == upscaler)) {
                Store(s.upscaler, i, true);
            }
            ImGui::EndDisabled();
            if (!supported) {
                ImGui::SameLine();
                ImGui::TextDisabled("— not supported by this GPU");
            }
        }
        for (const char* name : later) {
            ImGui::BeginDisabled();
            ImGui::Selectable(name, false);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("— in development");
        }
        ImGui::EndCombo();
    }
    if (s.upscaler == BbSettings::UpscalerDlss) {
        Hint("NVIDIA DLSS Super Resolution requires an RTX GPU and the NVIDIA DLSS library. "
             "Native AA uses DLAA. If initialization fails, FSR 3.1 is used.");
    }
    const auto problem_text = [](const char* text) {
        if (!text) return;
        ImGui::PushTextWrapPos();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", text);
        ImGui::PopTextWrapPos();
    };
    problem_text(s.fsr4_problem.load());
    if (s.upscaler == BbSettings::UpscalerDlss) problem_text(s.dlss_problem.load());
    if (BbSettings::IsFsr4(s.upscaler)) {
        if (s.upscaler == BbSettings::UpscalerFsr411) {
            Hint("FSR 4.1.1 uses AMD's INT8 model from the 4.1.1 DLL, reproduced in Vulkan (matching the DLL output). "
                 "One model covers Native through Performance; Ultra Performance uses a separate model. "
                 "Assets: tools/fsr4cap/build_assets.sh (requires the DLLs and Proton).");
        } else {
            Hint("FSR 4 INT8 uses model v07 from the AMD FidelityFX SDK sources. It can look better "
                 "than FSR 3.1 but takes more GPU time. Changing the preset rebuilds the model and may "
                 "cause a brief pause. Assets: tools/fetch_fsr4_assets.sh.");
        }
        Checkbox("FSR 4: Auto Exposure", s.fsr4_auto_exposure);
        Checkbox("FSR 4: Invert Jitter Sign", s.fsr4_invert_jitter);
        Hint("These options can help with ghosting. FSR 4 uses exposure-normalized color to decide "
             "when to discard previous frames. Changes apply immediately; no restart is needed.");
    }
    const bool upscaler_on = s.upscaler != BbSettings::UpscalerOff;
    const bool taa = s.upscaler == BbSettings::UpscalerTaa;
    ImGui::BeginDisabled(!upscaler_on);
    ImGui::BeginDisabled(taa);
    int preset = taa ? BbSettings::NativeAA : s.preset.load();
    char preset_label[64];
    std::snprintf(preset_label, sizeof(preset_label), "%s (x%.1f)", BbSettings::PresetName(preset),
                  BbSettings::PresetScale(preset));
    if (ImGui::BeginCombo("Preset", preset_label)) {
        for (int i = 0; i < BbSettings::PresetCount; ++i) {
            char label[64];
            const float scale = BbSettings::PresetScale(i);
            const int output = s.output_res;
            std::snprintf(label, sizeof(label), "%s (x%.1f, render %dx%d)",
                          BbSettings::PresetName(i), scale,
                          int(std::lround(BbSettings::OutputWidths[output] / scale / 2) * 2),
                          int(std::lround(BbSettings::OutputHeights[output] / scale / 2) * 2));
            if (ImGui::Selectable(label, i == preset)) {
                Store(s.preset, i, true);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (taa) {
        ImGui::TextWrapped("TAA antialiases the scene at output resolution without the FSR model or upscaling. "
                           "Your saved FSR preset is restored when you select FSR again.");
    }
    ImGui::Text("Active Scene Resolution: %d x %d", s.active_render_width.load(),
                s.active_render_height.load());
    if (BbSettings::FixedRenderSession()) {
        ImGui::Text("Startup Preset: %s", BbSettings::PresetName(s.startup_preset));
        if (const char* automatic = std::getenv("BB_AUTO_RENDER_RES");
            automatic && automatic[0] == '1') {
            Hint("At output resolutions other than 1080p, the startup patch renders the whole game at the preset resolution. "
                 "This is usually fastest on Steam Deck and slower GPUs. Preset and output resolution changes need a restart. "
                 "Enable Live Resolution Changes below to switch without restarting; post-processing then stays at 1080p and is slower.");
        } else {
            Hint("BB_RENDER_RES locks the scene resolution at startup. Remove this environment variable "
                 "to change resolution and presets without restarting the game.");
        }
    } else {
        Hint("Native AA uses FSR for antialiasing. Other presets render the scene below output resolution, "
             "while the interface stays at output resolution. Preset changes apply next frame without restarting.");
    }
    Checkbox("Sharpening (RCAS)", s.sharpen);
    ImGui::BeginDisabled(!s.sharpen);
    Slider("Sharpening Strength", s.sharpness, 0.0f, 2.0f);
    Hint("Up to 1 controls the upscaler's own RCAS sharpening. Values above 1 add another RCAS pass. "
         "Ctrl-click the slider to enter an exact value.");
    ImGui::EndDisabled();
    Checkbox("Subpixel Jitter", s.jitter);
    Hint("The scene shifts by a fraction of a pixel each frame, allowing the upscaler to combine "
         "details from multiple frames. Without jitter, it can only smooth using frame history.");

    ImGui::SeparatorText("Reactive Mask");
    ImGui::BeginDisabled(taa);
    Checkbox("Enable Reactive Mask", s.reactive);
    Hint("Marks transparent effects such as particles and haze so the upscaler relies less on "
         "previous frames. This reduces trails behind effects, but may bring back flicker beneath them.");
    ImGui::BeginDisabled(!s.reactive);
    Slider("Scale", s.reactive_scale, 0.0f, 4.0f);
    Slider("Threshold", s.reactive_threshold, 0.0f, 1.0f);
    Slider("Maximum", s.reactive_max, 0.0f, 1.0f);
    bool show_mask = s.debug_view == BbSettings::DebugReactive;
    if (ImGui::Checkbox("Show Mask (Debug)", &show_mask)) {
        s.debug_view = show_mask ? BbSettings::DebugReactive : BbSettings::DebugNone;
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    Checkbox("Character Motion Vectors", s.object_motion);
    Hint("Adds accurate motion vectors for animated objects, reducing breakup on clothing and weapons "
         "in motion. Static scenes need no extra pass. This change takes effect after restarting the game.");
    bool show_motion = s.debug_view == BbSettings::DebugMotion;
    if (ImGui::Checkbox("Show Motion Vectors (Debug)", &show_motion)) {
        s.debug_view = show_motion ? BbSettings::DebugMotion : BbSettings::DebugNone;
    }
    Hint("Red/green show horizontal/vertical motion (8 pixels = full intensity). Blue means the pixel "
         "has an object motion vector, not just camera motion. If a moving object has neither blue nor "
         "red/green vectors, the upscaler treats it as static, which can cause trails.");
    ImGui::EndDisabled(); // upscaler off

    ImGui::SeparatorText("NVIDIA Reflex");
    const auto reflex = Vulkan::Dlssg::GetReflexControls();
    static const char* reflex_modes[] = {"Off", "On", "On + Boost"};
    int reflex_mode = std::clamp(s.reflex.load(), 0, 2);
    ImGui::BeginDisabled(!reflex.ready || !reflex.supported || reflex.failed);
    if (ImGui::BeginCombo("Low Latency", reflex_modes[reflex_mode])) {
        for (int i = 0; i < 3; ++i)
            if (ImGui::Selectable(reflex_modes[i], i == reflex_mode)) Store(s.reflex, i, true);
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    Hint("Paces the CPU before the game reads input. Works with FG off. On + Boost also requests "
         "higher GPU clocks and can increase power use. FG uses at least Reflex On.");
    if (!reflex.loaded) ImGui::TextWrapped("Reflex requires the prepared Streamline runtime.");
    else if (reflex.failed) ImGui::TextWrapped("Reflex failed; check game/Streamline logs and restart.");
    else if (!reflex.ready || !reflex.supported) ImGui::TextWrapped("Reflex low latency is unavailable on this runtime/device.");
    else if (reflex.applied >= 0) {
        ImGui::Text("Applied: %s", reflex_modes[std::clamp(reflex.applied, 0, 2)]);
        if (reflex_mode == 0 && reflex.applied == 1 && s.frame_generation.load())
            ImGui::TextWrapped("Saved Off; FG currently requires On. Off applies when FG is disabled.");
    }

    ImGui::SeparatorText("Frame Generation (Experimental)");
    const auto fg_controls = Vulkan::Dlssg::GetControls();
    ImGui::BeginDisabled(!fg_controls.ready || fg_controls.failed || fg_controls.control_run);
    Checkbox("Enable DLSS Frame Generation", s.frame_generation);
    const int fg_multiplier = s.fg_multiplier.load();
    char fg_label[16];
    std::snprintf(fg_label, sizeof(fg_label), "%dX", fg_multiplier);
    if (ImGui::BeginCombo("FG Multiplier", fg_label)) {
        for (int multiplier = 2; multiplier <= 6; ++multiplier) {
            std::snprintf(fg_label, sizeof(fg_label), "%dX", multiplier);
            ImGui::BeginDisabled(u32(multiplier) > fg_controls.max_multiplier);
            if (ImGui::Selectable(fg_label, fg_multiplier == multiplier)) {
                Store(s.fg_multiplier, multiplier, true);
            }
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    Hint("2X = one rendered frame plus one generated frame; 6X = one plus five. "
         "Changes apply when you close this menu. Generation pauses while the menu is open. "
         "Higher factors add GPU work; displayed FPS is separate from real game FPS.");
    if (!fg_controls.loaded) {
        ImGui::TextWrapped("Launch the prepared FG build with -FrameGeneration menu-sm86 "
                           "(experimental RTX 30 bridge) or menu (official runtime).");
    } else if (fg_controls.failed) {
        ImGui::TextWrapped("FG initialization or runtime failed. See game/Streamline logs; "
                           "restart the game before retrying.");
    } else if (fg_controls.control_run) {
        ImGui::TextWrapped("FG is disabled by this benchmark control run.");
    } else if (!fg_controls.ready) {
        ImGui::TextWrapped("Waiting for the runtime capability check, or FG is unsupported.");
    } else {
        ImGui::Text("Runtime maximum: %uX", fg_controls.max_multiplier);
        if (fg_controls.output_width && fg_controls.output_height)
            ImGui::Text("FG resolution: %u x %u", fg_controls.output_width, fg_controls.output_height);
        Hint("FG processes the final window resolution. A 720p scene in a 1080p borderless window "
             "still uses 1080p FG. Lower both output and window resolution for lower FG cost; "
             "borderless uses the desktop resolution. Higher multipliers do not speed up input.");
        if (u32(fg_multiplier) > fg_controls.max_multiplier)
            ImGui::TextWrapped("Saved factor is above the runtime limit; the supported maximum is used.");
        ImGui::TextWrapped("Requires DLSS/FSR4 scene inputs, SDR, Immediate presentation "
                           "and a window at least 720 pixels high. Keep the game focused.");
        ImGui::TextWrapped("Short camera tests cover 2X and 4X. Image quality and latency still need testing.");
    }

    ImGui::SeparatorText("Window Mode");
    static const char* window_modes[] = {"Windowed", "Borderless Fullscreen", "Fullscreen"};
    int window_mode = s.window_mode;
    if (ImGui::BeginCombo("Window Mode", window_modes[window_mode])) {
        for (int i = 0; i < BbSettings::WindowModeCount; ++i)
            if (ImGui::Selectable(window_modes[i], i == window_mode)) Store(s.window_mode, i, true);
        ImGui::EndCombo();
    }
    Hint("Changes apply live. Borderless uses the desktop resolution. Fullscreen selects a supported "
         "display mode near the startup window resolution. F11 toggles windowed and your last fullscreen mode.");
    if (const char* problem = s.window_mode_problem.load()) ImGui::TextWrapped("%s", problem);
    ImGui::SeparatorText("Output Resolution");
    static const char* outputs[] = {"1280 x 720", "1920 x 1080", "2560 x 1440", "3840 x 2160"};
    int output = s.output_res;
    if (ImGui::BeginCombo("Output Resolution", outputs[output])) {
        for (int i = 0; i < BbSettings::OutputCount; ++i) {
            if (ImGui::Selectable(outputs[i], i == output)) {
                Store(s.output_res, i, true);
            }
        }
        ImGui::EndCombo();
    }
    if (BbSettings::FixedRenderSession()) {
        Hint("Sets the final image and interface resolution. The preset sets scene resolution relative "
             "to output: 4K Performance = 1920x1080. Takes effect after restarting the game.");
    } else {
        Hint("The final image and interface resolution changes at the next frame boundary. The preset "
             "sets scene resolution relative to output: 4K Performance = 1920x1080. Changing size resets "
             "FSR history and may cause a brief pause.");
    }
    static const char* live_modes[] = {"Auto (startup on Windows)", "Disabled (faster)", "Enabled"};
    int live = s.live_resolution + 1;
    if (ImGui::BeginCombo("Live Resolution Changes", live_modes[live])) {
        for (int i = 0; i < 3; ++i) {
            if (ImGui::Selectable(live_modes[i], i == live)) {
                Store(s.live_resolution, i - 1, true);
            }
        }
        ImGui::EndCombo();
    }
    Hint("Enabled: output resolution and preset can change without restarting, but game post-processing "
         "stays at 1080p, which is notably slower on Steam Deck and older GPUs. Disabled: everything is "
         "rendered at the preset resolution; changes require a restart. Auto enables live changes on "
         "powerful discrete GPUs with a capability probe; the Windows runner currently uses startup "
         "patches for Auto. This setting takes effect after restarting the game.");
    ImGui::SeparatorText("Game Effects (Restart Required)");
    static const char* lods[] = {"Maximum (-2)", "Default", "Lower (1)", "Minimum (2)"};
    static constexpr int lod_values[] = {-2, 0, 1, 2};
    int lod_index = 1;
    for (int i = 0; i < 4; ++i) {
        if (lod_values[i] == s.model_lod) lod_index = i;
    }
    if (ImGui::BeginCombo("Model Detail", lods[lod_index])) {
        for (int i = 0; i < 4; ++i) {
            if (ImGui::Selectable(lods[i], i == lod_index)) {
                Store(s.model_lod, lod_values[i], true);
            }
        }
        ImGui::EndCombo();
    }
    for (int e = 0; e < BbSettings::EffectCount; ++e) {
        Checkbox(BbSettings::Effects[e].label, s.effects[e]);
        if (std::strcmp(BbSettings::Effects[e].key, "vertex_explosion_fix") == 0) {
            Hint("Prevents FaceGen-related vertex explosions by disabling custom face shapes for "
                 "the player and affected NPCs. Does not cover vertex corruption from other causes. "
                 "Requires a game restart; uncheck and restart to restore FaceGen.");
        }
    }
    Hint("These effects are toggled with game patches at startup (patches/Bloodborne.xml). Motion blur "
         "and dynamic-source shadows can significantly load the GPU.");
    Hint("Free camera: hold Cross and press L3 (keyboard: Space + Z). Debug menu: left touchpad / Tab. "
         "Requires DbgFont14h.ccm and DbgFont14h.tpf in dvdroot_ps4/font from Nexus mod #253. "
         "Right touchpad: Backspace.");

    bool restart = s.object_motion != s.startup_object_motion ||
                   s.model_lod != s.startup_model_lod ||
                   s.live_resolution != s.startup_live_resolution ||
                   BbSettings::ResolutionNeedsRestart();
    for (int e = 0; e < BbSettings::EffectCount; ++e) {
        restart |= s.effects[e] != s.startup_effects[e];
    }
    if (restart) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                           "Changes take effect after restarting the game");
        if (ImGui::Button("Apply and Restart Game")) {
            save_failed = !BbSettings::Save();
            if (!save_failed) {
                dirty = false;
                runtime_restart();
            }
        }
    }
    if (save_failed) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                           "Could not save settings. Check the settings file is writable.");
    }

    ImGui::SeparatorText("Keyboard & Mouse (Live)");
    Checkbox("Enable Keyboard & Mouse", s.keyboard_mouse);
    Checkbox("Capture Mouse for Camera", s.mouse_capture);
    Slider("Mouse Sensitivity", s.mouse_sensitivity, 0.1f, 5.0f);
    Checkbox("Invert Mouse Y", s.mouse_invert_y);
    Hint("Mouse motion controls the right stick, with the original game's maximum camera speed. "
         "F8 releases/captures the cursor. Insert opens these settings and releases it automatically. "
         "Alt-Tab and system text entry also release the cursor. Changes are live and saved on close.");
    Checkbox("D-pad Controls Camera", s.dpad_camera);
    Hint("Use D-pad directions as the right stick. Applies to controller D-pads and their keyboard/mouse "
         "bindings. While enabled, these directions do not switch items or navigate game menus. "
         "Disable this option to restore normal D-pad actions. Changes are live and saved on close.");
    KeyBindings();

    ImGui::SeparatorText("Other");
    Checkbox("Show FPS Counter in Corner", s.show_fps);

    ImGui::Spacing();
    if (ImGui::Button("Close")) {
        keep_open = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Settings are saved in bbport.ini");
    ImGui::End();
    if (!keep_open) {
        SetOpen(false);
    }
}

void FpsCounter() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float pad = 12.0f * base_scale;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - pad,
                                   viewport->WorkPos.y + pad),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::Begin("##fps", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    const auto& s = BbSettings::Get();
    ImGui::Text("%.0f FPS  %.1f ms  %s", frame_ms_avg > 0.0f ? 1000.0f / frame_ms_avg : 0.0f,
                frame_ms_avg,
                s.upscaler == BbSettings::UpscalerFsr3   ? "FSR 3.1"
                : s.upscaler == BbSettings::UpscalerFsr4 ? "FSR 4"
                : s.upscaler == BbSettings::UpscalerFsr411 ? "FSR 4.1.1"
                : s.upscaler == BbSettings::UpscalerTaa ? "TAA"
                : s.upscaler == BbSettings::UpscalerDlss ? "DLSS"
                                                         : "");
    const auto fg = Vulkan::Dlssg::GetFps();
    if (fg.active) {
        if (fg.focused && fg.presented > 0.0f)
            ImGui::Text("FG: %.0f presented FPS (%.0f rendered)", fg.presented, fg.rendered);
        else
            ImGui::TextUnformatted(fg.focused ? "FG: measuring..." : "FG: paused (window unfocused)");
    }
    ImGui::End();
}

/// BB_MENU_KEYS_FILE=<file> (scripted tests): tokens toggle up down left right enter back l1 r1,
/// consumed when the file appears (it is removed), one key press per frame.
void ScriptedKeys() {
    static const char* path = std::getenv("BB_MENU_KEYS_FILE");
    static std::chrono::steady_clock::time_point last_check{};
    static std::vector<std::string> queue;
    static bool release = false;
    static ImGuiKey held = ImGuiKey_None;
    if (!path) {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (release) {
        io.AddKeyEvent(held, false);
        release = false;
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (queue.empty() && now - last_check > std::chrono::milliseconds(100)) {
        last_check = now;
        if (FILE* f = std::fopen(path, "r")) {
            char token[32];
            while (std::fscanf(f, "%31s", token) == 1) {
                queue.emplace_back(token);
            }
            std::fclose(f);
            std::remove(path);
        }
    }
    if (queue.empty()) {
        return;
    }
    const std::string token = queue.front();
    queue.erase(queue.begin());
    if (token == "toggle") {
        SetOpen(!menu_open);
        return;
    }
    held = token == "up" ? ImGuiKey_GamepadDpadUp : token == "down" ? ImGuiKey_GamepadDpadDown
         : token == "left" ? ImGuiKey_GamepadDpadLeft : token == "right" ? ImGuiKey_GamepadDpadRight
         : token == "enter" ? ImGuiKey_GamepadFaceDown : token == "l1" ? ImGuiKey_GamepadL1
         : token == "r1" ? ImGuiKey_GamepadR1 : token == "kdown" ? ImGuiKey_DownArrow
         : token == "kup" ? ImGuiKey_UpArrow : ImGuiKey_None;
    if (token == "back") {
        SetOpen(false);
        return;
    }
    if (held != ImGuiKey_None) {
        io.AddKeyEvent(held, true);
        release = true;
    }
}

} // namespace

void Init(const Vulkan::Instance& instance, vk::Format format, u32 image_count) {
    std::scoped_lock lock{imgui_mutex};
    if (initialized) {
        return;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // window positions are not kept
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.BackendPlatformName = "bbport";

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg].w = 0.92f;

    ImFontConfig font_config;
    font_config.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(bb_font_ttf),
                                   int(bb_font_ttf_end - bb_font_ttf), 18.0f, &font_config);

    const vk::Instance vk_instance = instance.GetInstance();
    ImGui_ImplVulkan_LoadFunctions(
        instance.ApiVersion(),
        [](const char* name, void* user) {
            return VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr(
                *static_cast<const vk::Instance*>(user), name);
        },
        const_cast<vk::Instance*>(&vk_instance));

    const VkFormat color_format = static_cast<VkFormat>(format);
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = instance.ApiVersion();
    info.Instance = vk_instance;
    info.PhysicalDevice = instance.GetPhysicalDevice();
    info.Device = instance.GetDevice();
    info.QueueFamily = instance.GetGraphicsQueueFamilyIndex();
    info.Queue = instance.GetGraphicsQueue();
    info.DescriptorPoolSize = 16;
    info.MinImageCount = std::max(image_count, 2u);
    info.ImageCount = std::max(image_count, 2u);
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_format,
    };
    if (!ImGui_ImplVulkan_Init(&info)) {
        std::printf("Overlay: ImGui Vulkan backend init failed\n");
        ImGui::DestroyContext();
        return;
    }
    initialized = true;
    std::printf("Overlay: menu ready (Insert or L3+R3)\n");
}

void UpdateTextInput(SDL_Window* window) {
    bool want = false;
    {
        std::scoped_lock lock{imgui_mutex};
        want = initialized && menu_open && ImGui::GetIO().WantTextInput;
    }
    if (want != SDL_TextInputActive(window)) {
        if (want) {
            SDL_StartTextInput(window);
        } else {
            SDL_StopTextInput(window);
        }
    }
}

bool HandleEvent(const SDL_Event& event) {
    std::scoped_lock lock{imgui_mutex};
    if (!initialized) {
        return false;
    }
    ImGuiIO& io = ImGui::GetIO();
    const bool is_open = menu_open;
    if (is_open) {
        const auto result = binding_capture.Handle(event);
        if (result == BbKeybinds::Result::Assigned) {
            AssignBinding(binding_capture.code);
            return true;
        }
        if ((result == BbKeybinds::Result::Waiting || result == BbKeybinds::Result::Cancelled) &&
            event.type == SDL_EVENT_KEY_DOWN) return true;
    }
    switch (event.type) {
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        io.AddFocusEvent(event.type == SDL_EVENT_WINDOW_FOCUS_GAINED);
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) l3_down = r3_down = false;
        return false;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const bool down = event.type == SDL_EVENT_KEY_DOWN;
        if (down && !event.key.repeat &&
            (event.key.key == SDLK_INSERT || (is_open && event.key.key == SDLK_ESCAPE))) {
            SetOpen(event.key.key == SDLK_INSERT ? !is_open : false);
            return true;
        }
        if (!is_open) {
            return false;
        }
        io.AddKeyEvent(ImGuiMod_Ctrl, (event.key.mod & SDL_KMOD_CTRL) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (event.key.mod & SDL_KMOD_SHIFT) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (event.key.mod & SDL_KMOD_ALT) != 0);
        if (const ImGuiKey key = KeyFromSdl(event.key.key); key != ImGuiKey_None) {
            io.AddKeyEvent(key, down);
        }
        return true;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        const bool down = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        const u8 button = event.gbutton.button;
        if (button == SDL_GAMEPAD_BUTTON_LEFT_STICK) {
            l3_down = down;
        } else if (button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) {
            r3_down = down;
        }
        if (down && l3_down && r3_down) {
            SetOpen(!is_open);
            return true;
        }
        if (!is_open) {
            return false;
        }
        if (const ImGuiKey key = KeyFromGamepad(button); key != ImGuiKey_None) {
            io.AddKeyEvent(key, down);
        }
        return true;
    }
    case SDL_EVENT_TEXT_INPUT: {
        // Typed characters (Ctrl+click on a slider, a text field): key events alone erase but
        // do not type. SDL sends them while text input is on (UpdateTextInput).
        if (!is_open) {
            return false;
        }
        io.AddInputCharactersUTF8(event.text.text);
        return true;
    }
    case SDL_EVENT_MOUSE_MOTION: {
        if (!is_open) {
            return false;
        }
        const float density = PixelDensity(event.motion.windowID);
        io.AddMousePosEvent(event.motion.x * density, event.motion.y * density);
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (!is_open) {
            return false;
        }
        const int button = event.button.button == SDL_BUTTON_LEFT    ? 0
                           : event.button.button == SDL_BUTTON_RIGHT  ? 1
                           : event.button.button == SDL_BUTTON_MIDDLE ? 2
                                                                      : -1;
        if (button >= 0) {
            io.AddMouseButtonEvent(button, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        }
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        if (!is_open) {
            return false;
        }
        io.AddMouseWheelEvent(event.wheel.x, event.wheel.y);
        return true;
    default:
        return false;
    }
}

bool Visible() {
    return initialized && (menu_open || BbSettings::Get().show_fps);
}

bool CapturesInput() {
    return menu_open;
}

void Render(vk::CommandBuffer cmdbuf, vk::ImageView view, vk::Extent2D extent) {

    // Present interval for the FPS readout (measured also while nothing is drawn).
    const auto now = std::chrono::steady_clock::now();
    const float ms = std::chrono::duration<float, std::milli>(now - last_present).count();
    last_present = now;
    if (ms > 0.0f && ms < 1000.0f) {
        frame_ms_avg = frame_ms_avg == 0.0f ? ms : frame_ms_avg * 0.95f + ms * 0.05f;
    }
    if (std::getenv("BB_MENU_KEYS_FILE") && initialized) {
        std::scoped_lock lock{imgui_mutex};
        ScriptedKeys();
    }
    if (!Visible()) {
        return;
    }
    std::scoped_lock lock{imgui_mutex};
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(extent.width), float(extent.height));
    io.DeltaTime = ms > 0.0f && ms < 1000.0f ? ms / 1000.0f : 1.0f / 60.0f;
    // UI scale follows the display height (1080p = 1).
    const float scale = std::max(float(extent.height) / 1080.0f, 0.75f);
    if (std::abs(scale - base_scale) > 0.01f) {
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScaleAllSizes(scale / base_scale);
        style.FontScaleMain = scale;
        base_scale = scale;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    if (menu_open) {
        Menu();
    }
    if (BbSettings::Get().show_fps && !menu_open) {
        FpsCounter();
    }
    ImGui::Render();

    const vk::RenderingAttachmentInfo attachment{
        .imageView = view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    cmdbuf.beginRendering(vk::RenderingInfo{
        .renderArea = {{0, 0}, extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachment,
    });
    {
        // Font atlas uploads submit to the graphics queue themselves.
        std::scoped_lock submit_lock{Vulkan::Scheduler::submit_mutex};
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdbuf);
    }
    cmdbuf.endRendering();
}

} // namespace BbOverlay
