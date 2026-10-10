// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_settings.h"

#include <algorithm>
#include <cmath>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <mutex>
#include <utility>
#include <vector>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace BbSettings {

namespace {

const char* Path() {
    const char* env = std::getenv("BB_CONFIG");
    return env && env[0] ? env : "bbport.ini";
}
std::filesystem::path Utf8Path(std::string_view value) {
    return std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

float Clamp(float v, float lo, float hi) {
    return std::clamp(v, lo, hi);
}

void Set(Values& v, const std::string& key, const std::string& value) {
    const float f = float(std::atof(value.c_str()));
    const int i = std::atoi(value.c_str());
    if (key == "upscaler") {
        for (int u = 0; u < UpscalerCount; ++u) {
            if (value == UpscalerName(u)) {
                v.upscaler = u;
            }
        }
    } else if (key == "preset") {
        v.preset = std::clamp(i, 0, PresetCount - 1);
    } else if (key == "sharpen") {
        v.sharpen = i != 0;
    } else if (key == "sharpness") {
        v.sharpness = Clamp(f, 0.0f, 2.0f);
    } else if (key == "jitter") {
        v.jitter = i != 0;
    } else if (key == "reactive") {
        v.reactive = i != 0;
    } else if (key == "object_motion") {
        v.object_motion = i != 0;
    } else if (key == "reactive_scale") {
        v.reactive_scale = Clamp(f, 0.0f, 16.0f);
    } else if (key == "reactive_threshold") {
        v.reactive_threshold = Clamp(f, 0.0f, 1.0f);
    } else if (key == "reactive_max") {
        v.reactive_max = Clamp(f, 0.0f, 1.0f);
    } else if (key == "debug_view") {
        v.debug_view = std::clamp(i, 0, DebugViewCount - 1);
    } else if (key == "show_fps") {
        v.show_fps = i != 0;
    } else if (key == "menu_pos") {
        float x=-1.0f, y=-1.0f;
        if (std::sscanf(value.c_str(), "%f,%f", &x, &y) == 2 &&
            std::isfinite(x) && std::isfinite(y) && x >= 0 && x <= 1 && y >= 0 && y <= 1) {
            v.menu_x = x; v.menu_y = y;
        }
    } else if (key == "window_mode") {
        for (int mode = 0; mode < WindowModeCount; ++mode)
            if (value == WindowModeName(mode)) v.window_mode = mode;
    } else if (key == "frame_generation") {
        v.frame_generation = value == "1" || value == "on";
    } else if (key == "reflex") {
        if (value == "off" || value == "0") v.reflex = 0;
        else if (value == "on" || value == "1") v.reflex = 1;
        else if (value == "boost" || value == "2") v.reflex = 2;
    } else if (key == "fg_multiplier") {
        int multiplier{};
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), multiplier);
        if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size())
            v.fg_multiplier = std::clamp(multiplier, 2, 6);
    } else if (key == "keyboard_mouse") {
        v.keyboard_mouse = i != 0;
    } else if (key == "mouse_capture") {
        v.mouse_capture = i != 0;
    } else if (key == "mouse_invert_y") {
        v.mouse_invert_y = i != 0;
    } else if (key == "dpad_camera") {
        v.dpad_camera = i != 0;
    } else if (key == "mouse_sensitivity") {
        v.mouse_sensitivity = std::isfinite(f) ? Clamp(f, 0.1f, 5.0f) : 1.0f;
    } else if (key == "fsr4_auto_exposure") {
        v.fsr4_auto_exposure = i != 0;
    } else if (key == "fsr4_invert_jitter") {
        v.fsr4_invert_jitter = i != 0;
    } else if (key == "model_lod") {
        v.model_lod = std::clamp(i, -2, 2);
    } else if (key == "live_resolution") {
        v.live_resolution = value == "auto" ? -1 : std::clamp(i, 0, 1);
    } else if (key == "output_res") {
        for (int r = 0; r < OutputCount; ++r) {
            if (value == std::to_string(OutputWidths[r]) + "x" + std::to_string(OutputHeights[r])) {
                v.output_res = r;
            }
        }
    } else if (key.starts_with("bind_")) {
        int code = 0;
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), code);
        if (error != std::errc{} || end != value.data() + value.size()) return;
        for (int a = 0; a < BB_BIND_ACTION_COUNT; ++a) {
            const std::string name = std::string("bind_") + bb_pad_binding_actions[a].key;
            if (key == name) SetBinding(a, 0, code);
            else if (key == name + "_alt") SetBinding(a, 1, code);
        }
    } else {
        for (int e = 0; e < EffectCount; ++e) {
            if (key == Effects[e].key) {
                v.effects[e] = i != 0;
            }
        }
    }
}

} // namespace

Values& Get() {
    static Values values;
    return values;
}

bool SetBinding(int action, int slot, int code) {
    if (action < 0 || action >= BB_BIND_ACTION_COUNT || slot < 0 || slot >= BB_BIND_SLOTS ||
        !bb_pad_binding_valid(code)) return false;
    Get().bindings[action * BB_BIND_SLOTS + slot] = code;
    return true;
}

bool Rebind(int action, int slot, int code) {
    if (!SetBinding(action, slot, code)) return false;
    const int target = action * BB_BIND_SLOTS + slot;
    if (code) {
        auto& v = Get();
        for (int i = 0; i < BB_BIND_COUNT; ++i)
            if (i != target && v.bindings[i] == code) v.bindings[i] = 0;
    }
    return true;
}

void ResetBindings() {
    for (int a = 0; a < BB_BIND_ACTION_COUNT; ++a)
        for (int slot = 0; slot < BB_BIND_SLOTS; ++slot)
            SetBinding(a, slot, bb_pad_binding_actions[a].defaults[slot]);
}

void Load() {
    auto& v = Get();
    v.frame_generation = false;
    v.fg_multiplier = 2;
    v.reflex = 1;
    v.window_mode = Windowed;
    v.window_mode_problem = nullptr;
    ResetBindings();
    for (int e = 0; e < EffectCount; ++e) {
        v.effects[e] = Effects[e].default_on;
    }
    if (FILE* file = std::fopen(Path(), "r")) {
        char line[256];
        while (std::fgets(line, sizeof(line), file)) {
            std::string text{line};
            text.erase(text.find_last_not_of(" \t\r\n") + 1);
            const auto eq = text.find('=');
            if (text.empty() || text[0] == '#' || eq == std::string::npos) {
                continue;
            }
            Set(v, text.substr(0, eq), text.substr(eq + 1));
        }
        std::fclose(file);
        std::printf("Settings: %s\n", Path());
    }
    // Environment overrides (scripts, A/B tests).
    // Compatibility with old launchers: SDL's old fullscreen flag was borderless.
    if (const char* legacy = std::getenv("BB_FULLSCREEN"))
        v.window_mode = legacy[0] == '1' ? BorderlessFullscreen : Windowed;
    if (const char* env = std::getenv("BB_UPSCALER")) {
        v.upscaler = UpscalerOff;
        for (int u = 0; u < UpscalerCount; ++u) {
            if (std::strcmp(env, UpscalerName(u)) == 0) v.upscaler = u;
        }
    }
    const std::pair<const char*, const char*> env_keys[] = {
        {"BB_FSR_SHARPNESS", "sharpness"},        {"BB_JITTER", "jitter"},
        {"BB_REACTIVE", "reactive"},              {"BB_REACTIVE_SCALE", "reactive_scale"},
        {"BB_REACTIVE_THRESHOLD", "reactive_threshold"}, {"BB_REACTIVE_MAX", "reactive_max"},
        {"BB_UPSCALE_PRESET", "preset"},            {"BB_OBJECT_MOTION", "object_motion"},
        {"BB_SHOW_FPS", "show_fps"},
        {"BB_FG_MULTIPLIER", "fg_multiplier"},
        {"BB_REFLEX", "reflex"},
        {"BB_KBM", "keyboard_mouse"},
        {"BB_MOUSE_CAPTURE", "mouse_capture"},
        {"BB_MOUSE_SENSITIVITY", "mouse_sensitivity"},
        {"BB_MOUSE_INVERT_Y", "mouse_invert_y"},
        {"BB_DPAD_CAMERA", "dpad_camera"},
        {"BB_OUTPUT_RES", "output_res"},
        {"BB_LIVE_RES", "live_resolution"},
        {"BB_WINDOW_MODE", "window_mode"},
    };
    for (const auto& [env, key] : env_keys) {
        if (const char* value = std::getenv(env)) {
            Set(v, key, value);
        }
    }
    if (const char* mode = std::getenv("BB_FRAME_GENERATION")) {
        if (!std::strcmp(mode, "on")) v.frame_generation = true;
        else if (!std::strcmp(mode, "off") || !std::strcmp(mode, "control"))
            v.frame_generation = false;
        // "menu" initializes the runtime while preserving the saved on/off choice.
    }
    v.startup_preset = v.preset;
    v.startup_upscaler = v.upscaler;
    v.startup_object_motion = v.object_motion;
    for (int e = 0; e < EffectCount; ++e) {
        v.startup_effects[e] = v.effects[e];
    }
    v.startup_model_lod = v.model_lod;
    v.startup_output_res = v.output_res;
    v.startup_live_resolution = v.live_resolution;
}

void ConfigureUpscalerSupport(bool fsr4, bool fsr411, bool dlss) {
    auto& v = Get();
    v.fsr4_supported = fsr4;
    v.fsr411_supported = fsr4 && fsr411;
    v.dlss_supported = dlss;
    const int requested = v.upscaler;
    if (requested == UpscalerDlss && !dlss) {
        v.fsr4_problem = "DLSS needs NVIDIA NGX and nvngx_dlss; using FSR 3.1";
        std::printf("Upscaler: dlss unavailable; falling back to FSR 3.1 before the first frame\n");
        v.upscaler = UpscalerFsr3;
    } else if ((requested == UpscalerFsr4 && !v.fsr4_supported) ||
        (requested == UpscalerFsr411 && !v.fsr411_supported)) {
        v.fsr4_problem = "GPU does not support the selected FSR 4 shaders; using FSR 3.1";
        std::printf("Upscaler: %s unsupported on this GPU; falling back to FSR 3.1 before the first frame\n",
                    UpscalerName(requested));
        v.upscaler = UpscalerFsr3;
    }
}

void ConfigureDlssSupport(bool available, const char* problem) {
    auto& v = Get();
    v.dlss_supported = available;
    static std::string kept;
    kept = problem ? problem : "";
    v.dlss_problem = available || kept.empty() ? nullptr : kept.c_str();
    if (v.upscaler == UpscalerDlss && !available) {
        std::printf("Upscaler: DLSS unavailable (%s); falling back to FSR 3.1\n",
                    kept.empty() ? "the DLSS bridge or NVIDIA's DLSS library is missing" : kept.c_str());
        v.upscaler = UpscalerFsr3;
    }
}

bool FixedRenderSession() {
    const char* size = std::getenv("BB_RENDER_RES");
    return size && size[0];
}

int RenderPreset() {
    const auto& v = Get();
    return FixedRenderSession() ? v.startup_preset :
        v.upscaler == UpscalerTaa ? NativeAA : v.preset.load();
}

bool ResolutionNeedsRestart() {
    const auto& v = Get();
    // TAA needs the live path (native guest targets): run.sh selects it on restart.
    return FixedRenderSession() &&
        (v.preset != v.startup_preset || v.output_res != v.startup_output_res ||
         (v.upscaler == UpscalerOff) != (v.startup_upscaler == UpscalerOff) ||
         (v.upscaler == UpscalerTaa) != (v.startup_upscaler == UpscalerTaa));
}

bool Save() {
    static std::mutex save_mutex;
    const std::scoped_lock lock{save_mutex};
    const auto& v = Get();
    // The keys the menu writes, in this order; the rest of the file stays as it is: the
    // launcher's controls (key.* / pad.*) and its other keys, comments.
    std::vector<std::pair<std::string, std::string>> keys;
    const auto put = [&](const char* key, std::string value) { keys.emplace_back(key, std::move(value)); };
    const auto fixed = [](float value, int digits) {
        char text[32];
        std::snprintf(text, sizeof(text), "%.*f", digits, value);
        return std::string(text);
    };
    const auto flag = [](bool value) { return std::string(value ? "1" : "0"); };

    put("keyboard_mouse", flag(v.keyboard_mouse));
    put("mouse_capture", flag(v.mouse_capture));
    put("mouse_invert_y", flag(v.mouse_invert_y));
    put("mouse_sensitivity", fixed(v.mouse_sensitivity, 2));
    put("dpad_camera", flag(v.dpad_camera));
    put("window_mode", WindowModeName(v.window_mode));
    put("frame_generation", flag(v.frame_generation));
    put("fg_multiplier", std::to_string(v.fg_multiplier.load()));
    static const char* reflex_modes[]{"off", "on", "boost"};
    put("reflex", reflex_modes[std::clamp(v.reflex.load(), 0, 2)]);
    for (int a = 0; a < BB_BIND_ACTION_COUNT; ++a) {
        const std::string name = std::string("bind_") + bb_pad_binding_actions[a].key;
        put(name.c_str(), std::to_string(v.bindings[a * BB_BIND_SLOTS].load()));
        put((name + "_alt").c_str(), std::to_string(v.bindings[a * BB_BIND_SLOTS+1].load()));
    }
    put("upscaler", UpscalerName(v.upscaler));
    put("preset", std::to_string(v.preset.load()));
    put("sharpen", flag(v.sharpen));
    put("sharpness", fixed(v.sharpness, 2));
    put("jitter", flag(v.jitter));
    put("reactive", flag(v.reactive));
    put("object_motion", flag(v.object_motion));
    put("reactive_scale", fixed(v.reactive_scale, 2));
    put("reactive_threshold", fixed(v.reactive_threshold, 2));
    put("reactive_max", fixed(v.reactive_max, 2));
    put("debug_view", std::to_string(v.debug_view.load()));
    put("show_fps", flag(v.show_fps));
    put("fsr4_auto_exposure", flag(v.fsr4_auto_exposure));
    put("fsr4_invert_jitter", flag(v.fsr4_invert_jitter));
    // Read by patches.py at start.
    for (int e = 0; e < EffectCount; ++e) {
        put(Effects[e].key, flag(v.effects[e]));
    }
    put("model_lod", std::to_string(v.model_lod.load()));
    put("output_res", std::to_string(OutputWidths[v.output_res]) + "x" +
                          std::to_string(OutputHeights[v.output_res]));
    // Read by run.sh at start.
    put("live_resolution", v.live_resolution < 0 ? "auto" : flag(v.live_resolution != 0));
    if (v.menu_x >= 0.0f && v.menu_y >= 0.0f) {
        put("menu_pos", fixed(v.menu_x, 4) + "," + fixed(v.menu_y, 4));
    }

    std::string out;
    bool had_lines = false;
    std::vector<bool> written(keys.size());
    {
        std::ifstream file(Utf8Path(Path()), std::ios::binary);
        std::string text;
        while (std::getline(file, text)) {
            had_lines = true;
            text.erase(text.find_last_not_of("\r\n") + 1);
            const auto eq = text.find('=');
            if (!text.empty() && text[0] != '#' && eq != std::string::npos) {
                std::string key = text.substr(0, eq);
                key.erase(key.find_last_not_of(" \t") + 1);
                key.erase(0, key.find_first_not_of(" \t"));
                const auto it = std::find_if(keys.begin(), keys.end(),
                                             [&](const auto& k) { return k.first == key; });
                if (it != keys.end()) {
                    out += it->first + "=" + it->second + "\n";
                    written[size_t(it - keys.begin())] = true;
                    continue;
                }
            }
            out += text + "\n";
        }
        if (file.bad()) {
            std::printf("Settings: cannot read %s\n", Path());
            return false;
        }
    }
    if (!had_lines) {
        out = "# bbport settings (in-game menu: Insert / L3+R3)\n";
    }
    for (size_t k = 0; k < keys.size(); ++k) {
        if (!written[k]) {
            out += keys[k].first + "=" + keys[k].second + "\n";
        }
    }
    // Replace only after the complete file has been written and closed.
    const auto destination = Utf8Path(Path());
    const auto temporary = Utf8Path(std::string(Path()) + ".tmp");
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file.write(out.data(), std::streamsize(out.size()));
    file.flush();
    const bool complete = bool(file);
    file.close();
    bool ok = complete && !file.fail();
    if (ok) {
#ifdef _WIN32
        ok = MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        std::error_code error;
        std::filesystem::rename(temporary, destination, error);
        ok = !error;
#endif
    }
    if (!ok) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        std::printf("Settings: cannot write %s\n", Path());
        return false;
    }
    std::printf("Settings: saved %s\n", Path());
    return true;
}

float PresetScale(int preset) {
    static constexpr float scales[PresetCount] = {1.0f, 1.5f, 1.7f, 2.0f, 3.0f};
    return scales[std::clamp(preset, 0, PresetCount - 1)];
}

const char* WindowModeName(int mode) {
    static constexpr const char* names[] = {"windowed", "borderless", "fullscreen"};
    return names[std::clamp(mode, 0, WindowModeCount - 1)];
}

const char* PresetName(int preset) {
    static constexpr const char* names[PresetCount] = {"Native AA", "Quality", "Balanced",
                                                       "Performance", "Ultra Performance"};
    return names[std::clamp(preset, 0, PresetCount - 1)];
}

const char* UpscalerName(int upscaler) {
    static constexpr const char* names[UpscalerCount] = {"off", "fsr3", "fsr4", "fsr411", "taa", "dlss"};
    return names[std::clamp(upscaler, 0, UpscalerCount - 1)];
}

} // namespace BbSettings
