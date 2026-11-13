// The handheld screen's shadows. The console's own screen, the OLED's above all, shows the game's dark scenes darker
// than a TV does: corners and night levels went black. An exponent at scan-out, on the game's gamma ramp
// (vulkan.cpp), lifts the shadows in handheld; black stays black and white stays white. Docked it is left alone. Set
// in the options panel (src/common/menu.cpp) and kept in screen.toml beside gyro.toml.
#include <switch.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>

#include <toml++/toml.hpp>

#include "app/app.h"
#include "core/log.h"
#include "host.h"

namespace screen {

namespace {

std::mutex g_lock;
float g_handheld = 0.8f;  // the exponent in handheld, 1 none
bool g_loaded = false;

std::filesystem::path File() { return app::ExeDir() / "screen.toml"; }

void Load() {
    g_loaded = true;
    try {
        toml::table t = toml::parse_file(File().string());
        g_handheld = std::clamp(float(t["handheld_gamma"].value_or(double(g_handheld))), 0.5f, 1.0f);
    } catch (...) {
    }
    LOGI("screen: handheld shadows at %.2f", g_handheld);
}

void Save() {
    toml::table t{{"handheld_gamma", double(g_handheld)}};
    std::ofstream f(File());
    f << "# the handheld screen's shadow lift: 1 none, 0.8 the default, lower lifts more\n" << t << "\n";
}

}  // namespace

float HandheldGamma() {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    return g_handheld;
}

void SetHandheldGamma(float g) {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    g_handheld = std::clamp(g, 0.5f, 1.0f);
    Save();
}

float Gamma() { return appletGetOperationMode() == AppletOperationMode_Handheld ? HandheldGamma() : 1.0f; }

}  // namespace screen
