// Aiming by turning the controller, as the Android host does with the phone: the six-axis sensor of the handheld
// console, the right Joy-Con or a Pro Controller turns the view through the game's mouse look (src/game/mouse.cpp,
// input::TakeMouse). Off, always on, or only while ZR is held; the mode, the sensitivity and the inversion are set in
// the game's options menu (src/common/menu.cpp) and kept in gyro.toml beside the game's settings.
#include <switch.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <mutex>

#include <toml++/toml.hpp>

#include "app/app.h"
#include "common/menu.h"
#include "common/mouse_keyboard.h"
#include "core/log.h"
#include "host.h"

namespace gyro {

namespace {

constexpr float kDegreesPerCount = 0.05f;  // src/game/mouse.cpp's kDegrees
constexpr float kDeadZone = 0.9f;          // degrees a second: a hand held still
constexpr const char* kNames[kModes] = {"OFF", "ON", "FIRE"};

std::mutex g_lock;
std::atomic<uint32_t> g_mode{kOff};
float g_sensitivity = 1.5f;  // view degrees per degree the controller turns
bool g_invert_x = false;
bool g_invert_y = false;
bool g_loaded = false;

// the sensors: handheld, Pro Controller (or one Joy-Con held sideways), the Joy-Con pair (left, right)
HidSixAxisSensorHandle g_handles[4];
bool g_started = false;
uint64_t g_last_sample = 0;
std::chrono::steady_clock::time_point g_last_take;
float g_up[3] = {0, 0, 1};  // the direction gravity holds the controller from, smoothed
float g_carry_x = 0, g_carry_y = 0;
int g_logged = 0;

std::filesystem::path File() { return app::ExeDir() / "gyro.toml"; }

void Load() {
    g_loaded = true;
    try {
        toml::table t = toml::parse_file(File().string());
        g_mode = std::min<uint32_t>(t["gyro"].value_or(uint32_t(kOff)), kModes - 1);
        g_sensitivity = float(t["gyro_sensitivity"].value_or(double(g_sensitivity)));
        g_invert_x = t["gyro_invert_x"].value_or(g_invert_x);
        g_invert_y = t["gyro_invert_y"].value_or(g_invert_y);
    } catch (...) {
    }
}

void Save() {
    toml::table t{{"gyro", int64_t(g_mode.load())},
                  {"gyro_sensitivity", double(g_sensitivity)},
                  {"gyro_invert_x", g_invert_x},
                  {"gyro_invert_y", g_invert_y}};
    std::ofstream(File()) << t << "\n";
}

void Start() {
    if (g_started) return;
    hidGetSixAxisSensorHandles(&g_handles[0], 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);
    hidGetSixAxisSensorHandles(&g_handles[1], 1, HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey);
    hidGetSixAxisSensorHandles(&g_handles[2], 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual);
    for (const HidSixAxisSensorHandle& h : g_handles) hidStartSixAxisSensor(h);
    g_started = true;
    g_last_sample = 0;
    LOGI("gyro: sensors on");
}

void Stop() {
    if (!g_started) return;
    for (const HidSixAxisSensorHandle& h : g_handles) hidStopSixAxisSensor(h);
    g_started = false;
    LOGI("gyro: sensors off");
}

// the sensor of what player 1 plays with: the console in handheld, else the Pro Controller or the right Joy-Con
const HidSixAxisSensorHandle* Current() {
    if (hidGetNpadStyleSet(HidNpadIdType_Handheld) & HidNpadStyleTag_NpadHandheld) return &g_handles[0];
    u32 style = hidGetNpadStyleSet(HidNpadIdType_No1);
    if (style & HidNpadStyleTag_NpadFullKey) return &g_handles[1];
    if (style & HidNpadStyleTag_NpadJoyDual) return &g_handles[3];
    return nullptr;
}

// view counts for the time since the last call. The sensor reads in its own axes, turns a second (1 = 360 degrees):
// x to the right, y away from the player, z out of the face. The turn about the world's up axis, wherever the
// controller is tilted (lying flat or the console held upright), is the turn about the direction the accelerometer
// says is up; the look up and down is the turn about x, level in either grip.
bool Take(float& dx, float& dy) {
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - g_last_take).count();
    g_last_take = now;
    const HidSixAxisSensorHandle* h = Current();
    if (!h) return false;
    HidSixAxisSensorState states[16];
    size_t n = hidGetSixAxisSensorStates(*h, states, 16);
    if (!n) return false;
    // the samples since the last call, averaged: the game asks less often than the sensor reads
    float w[3] = {}, a[3] = {};
    int fresh = 0;
    uint64_t newest = g_last_sample;
    for (size_t i = 0; i < n; i++) {
        const HidSixAxisSensorState& s = states[i];
        if (s.sampling_number <= g_last_sample) continue;
        newest = std::max(newest, s.sampling_number);
        w[0] += s.angular_velocity.x;
        w[1] += s.angular_velocity.y;
        w[2] += s.angular_velocity.z;
        a[0] += s.acceleration.x;
        a[1] += s.acceleration.y;
        a[2] += s.acceleration.z;
        fresh++;
    }
    bool first = !g_last_sample;
    g_last_sample = newest;
    if (!fresh) return false;
    for (int i = 0; i < 3; i++) {
        w[i] = w[i] / float(fresh) * 360.0f;  // degrees a second
        a[i] /= float(fresh);
    }
    if (g_logged < 3) {
        g_logged++;
        LOGI("gyro: turn %.1f %.1f %.1f deg/s, gravity %.2f %.2f %.2f g", w[0], w[1], w[2], a[0], a[1], a[2]);
    }
    float len = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (len > 0.5f && len < 1.5f) {
        float k = first ? 1.0f : 0.05f;
        for (int i = 0; i < 3; i++) g_up[i] += (a[i] / len - g_up[i]) * k;
        float ul = std::sqrt(g_up[0] * g_up[0] + g_up[1] * g_up[1] + g_up[2] * g_up[2]);
        for (float& u : g_up) u /= ul;
    }
    // a pause (the game not asking, a menu) or the first reading: no turn piled up
    if (first || dt <= 0 || dt > 0.25f) return false;
    float yaw = w[0] * g_up[0] + w[1] * g_up[1] + w[2] * g_up[2];  // positive: left
    float pitch = w[0];                                               // positive: looking up
    if (std::fabs(yaw) < kDeadZone) yaw = 0;
    if (std::fabs(pitch) < kDeadZone) pitch = 0;
    if (!yaw && !pitch) return false;
    float k = g_sensitivity * dt / kDegreesPerCount;
    dx = (g_invert_x ? -yaw : yaw) * k;  // the console's up is the accelerometer's minus: a turn left reads negative
    dy = (g_invert_y ? pitch : -pitch) * k;
    return true;
}

}  // namespace

uint32_t CurrentMode() {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    return g_mode;
}

// the game's options menu (src/common/menu.cpp) sets these; saved at once
void SetMode(uint32_t mode) {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    g_mode = mode % kModes;
    LOGI("gyro: %s", kNames[g_mode]);
    Save();
}

float Sensitivity() {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    return g_sensitivity;
}

void SetSensitivity(float s) {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    g_sensitivity = std::clamp(s, 0.1f, 5.0f);
    Save();
}

bool InvertX() {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    return g_invert_x;
}

bool InvertY() {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    return g_invert_y;
}

void SetInvert(bool x, bool y) {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    g_invert_x = x;
    g_invert_y = y;
    Save();
}

const char* ModeName() { return kNames[CurrentMode() % kModes]; }

void NextMode() { SetMode(g_mode + 1); }

}  // namespace gyro

namespace input {

// the game's aim assist is off while this holds (src/game/mouse.cpp): always on, or ZR held in the firing mode
bool GyroAiming() {
    uint32_t mode = gyro::CurrentMode();
    return mode == gyro::kAlways || (mode == gyro::kFiring && (NavButtons() & HidNpadButton_ZR));
}

// the game's mouse look, each tick in play: the gyro's turn as mouse counts, whole ones, the rest carried to the next
bool TakeMouse(float& dx, float& dy) {
    using namespace gyro;
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    if (g_mode == kOff) {
        Stop();
        return false;
    }
    Start();
    float x = 0, y = 0;
    if (!Take(x, y) || app::MenuOpen()) return false;
    if (g_mode == kFiring && !(NavButtons() & HidNpadButton_ZR)) return false;
    g_carry_x += x;
    g_carry_y += y;
    float ix = std::trunc(g_carry_x), iy = std::trunc(g_carry_y);
    g_carry_x -= ix;
    g_carry_y -= iy;
    if (!ix && !iy) return false;
    dx = ix;
    dy = iy;
    return true;
}

}  // namespace input
