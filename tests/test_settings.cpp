// SPDX-License-Identifier: GPL-2.0-or-later
// bbport_settings.cpp: the menu saves its keys into bbport.ini and keeps the launcher's (controls).
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <chrono>
#include <filesystem>
#include "gpu/shim/bbport_settings.h"

static std::string Read(const char* path) {
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

int main() {
    const auto folder = std::filesystem::temp_directory_path() /
        ("bbport-settings-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(folder);
    const std::string path_storage = (folder / "settings.ini").string();
    const char* path = path_storage.c_str();
    const char ini[] = "# launcher\nupscaler=fsr3\nkey.cross=X, Space\npad.circle=a\nshow_fps=0\n"
                       "fullscreen_hint=1\n";
    { std::ofstream file(path, std::ios::binary); file << ini; assert(file.good()); }
#ifdef _WIN32
    assert(_putenv_s("BB_CONFIG", path) == 0);
#else
    assert(setenv("BB_CONFIG", path, 1) == 0);
#endif

    BbSettings::Load();
    auto& s = BbSettings::Get();
    int vertex_fix = -1;
    for (int e = 0; e < BbSettings::EffectCount; ++e)
        if (std::string(BbSettings::Effects[e].key) == "vertex_explosion_fix") vertex_fix = e;
    assert(vertex_fix >= 0 && !s.effects[vertex_fix]);
    s.effects[vertex_fix] = true;
    assert(s.upscaler == BbSettings::UpscalerFsr3 && !s.show_fps && s.menu_x < 0.0f);
    s.show_fps = true;
    s.upscaler = BbSettings::UpscalerFsr411;
    s.menu_x = 0.625f;
    s.menu_y = 0.125f;
    s.frame_generation=true; s.fg_multiplier=4; s.reflex=2;
    s.window_mode=BbSettings::BorderlessFullscreen;
    assert(BbSettings::Save());

    const std::string saved = Read(path);
    // The launcher's controls, its other keys and comments stay; the menu's keys are replaced
    // in place, new ones appended.
    assert(saved.find("# launcher\nupscaler=fsr411\nkey.cross=X, Space\npad.circle=a\nshow_fps=1\n"
                      "fullscreen_hint=1\n") == 0);
    assert(saved.find("menu_pos=0.6250,0.1250\n") != std::string::npos);
    assert(saved.find("upscaler=fsr3") == std::string::npos);

    s.menu_x = -1.0f;
    s.menu_y = -1.0f;
    BbSettings::Load();
    assert(s.menu_x == 0.625f && s.menu_y == 0.125f && s.upscaler == BbSettings::UpscalerFsr411);
    assert(saved.find("fg_multiplier=4\n") != std::string::npos);
    assert(saved.find("reflex=boost\n") != std::string::npos);
    assert(saved.find("window_mode=borderless\n") != std::string::npos);
    assert(saved.find("vertex_explosion_fix=1\n") != std::string::npos);
    assert(s.effects[vertex_fix] && s.startup_effects[vertex_fix]);
    // A second save must replace an existing file on Windows too.
    s.show_fps=false;
    s.effects[vertex_fix] = false;
    assert(BbSettings::Save());
    assert(Read(path).find("show_fps=0\n") != std::string::npos);
    BbSettings::Load();
    assert(!s.effects[vertex_fix] && !s.startup_effects[vertex_fix]);
    assert(Read(path).find("vertex_explosion_fix=0\n") != std::string::npos);
    const std::string directory_target = (folder / "directory.ini").string();
    std::filesystem::create_directory(directory_target);
#ifdef _WIN32
    assert(_putenv_s("BB_CONFIG", directory_target.c_str()) == 0);
#else
    assert(setenv("BB_CONFIG", directory_target.c_str(), 1) == 0);
#endif
    assert(!BbSettings::Save());
    assert(std::filesystem::is_directory(directory_target));
    assert(!std::filesystem::exists(directory_target + ".tmp"));
    std::filesystem::remove(directory_target);
    std::filesystem::remove(path);
    std::filesystem::remove(folder);
    std::puts("PASS: settings save keeps other keys, menu position");
}
